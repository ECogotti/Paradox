# Gameplay Actions Core

## Purpose

This document defines the architectural foundation of the `GameplayActions` Unreal Engine plugin.

The plugin must provide a generic, data-driven and Blueprint-extensible execution layer for actions requested by player controllers, AI systems, goal-oriented agents, replay systems or other gameplay code.

The plugin is responsible for:

- describing an action through data;
- creating immutable execution requests;
- validating and scheduling requests;
- executing actions through runtime instances;
- managing action state, priority and interruption;
- preventing incompatible actions from running at the same time;
- exposing authoritative lifecycle events and structured results;
- providing a stable integration contract for a future `IntentReplay` plugin.

The plugin must not contain project-specific gameplay rules.

---

# 1. Architectural boundaries

`GameplayActions` is an execution system. It must not decide which high-level objective an agent should pursue.

External systems decide **what** should be attempted. `GameplayActions` decides whether the corresponding request can be accepted, when it can start, how it executes and how it ends.

Conceptual flow:

```text
External requester
    ↓
FGameplayActionRequest
    ↓
UGameplayActionComponent
    ↓
UGameplayActionInstance
    ↓
FGameplayActionResult
```

Examples of valid external requesters include:

- player input code;
- AI logic;
- a future goal-oriented agent plugin;
- the future `IntentReplay` plugin;
- project integration code.

The core plugin must not depend on any of these systems.

## 1.1 Required dependency direction

The future dependency direction must remain:

```text
IntentReplay → GameplayActions
```

Never introduce:

```text
GameplayActions → IntentReplay
```

`GameplayActions` must expose a generic journaling/integration contract that `IntentReplay` can implement later without requiring invasive changes to the core action types.

## 1.2 Runtime and optional editor modules

The first implementation should create one runtime module:

```text
GameplayActions
```

An optional editor module may be added later:

```text
GameplayActionsEditor
```

The editor module is not required for the first core milestone. It may later provide custom Blueprint nodes, Property Bag validation and improved editor tooling. Runtime code must never depend on the editor module.

---

# 2. Core concepts

The architecture must keep the following concepts separate:

1. **Action Definition** — shared, immutable authored configuration.
2. **Action Request** — one concrete request with parameter overrides.
3. **Action Instance** — transient runtime state for one execution.
4. **Action Component** — authoritative scheduler and owner for one actor.
5. **Action Result** — structured terminal outcome.
6. **Execution Locks** — exclusive capabilities required by an action.
7. **Priority** — arbitration value used when actions contend for the same locks.
8. **Journal Event** — immutable event payload intended for observers and future replay recording.

Do not merge these responsibilities into one UObject.

---

# 3. Action Definition

Create a Blueprint-visible primary data asset conceptually named:

```cpp
UGameplayActionDefinition
```

It represents an authored action type and its default configuration.

A Definition must be shareable by multiple actors and requests. It must never contain runtime execution state.

## 3.1 Required Definition data

The Definition should contain at least:

```text
ActionClass
ActionTag
DefaultPriority
DefaultParameters
ExecutionLocks
bCanBeInterrupted
DefaultBlockedPolicy
OptionalTimeout
Debug metadata
```

Recommended semantics:

### `ActionClass`

A `TSubclassOf<UGameplayActionInstance>` used to create the runtime instance.

### `ActionTag`

A semantic identifier for the action, for example:

```text
GameplayAction.MoveTo
GameplayAction.Interact
GameplayAction.Wait
```

`ActionTag` identifies what the action represents. It must not be overloaded as the sole concurrency mechanism.

When a design requirement says that two actions with “tag X” must exclude or preempt each other, `X` must be represented as an `Execution Lock` (or a future dedicated execution channel), not merely as the semantic `ActionTag`. This keeps identity and scheduling policy separate.

### `DefaultPriority`

The priority used when a request does not provide an explicit override.

Use a signed integer type. Higher values mean higher priority.

Default priority:

```text
0
```

Negative priorities are allowed unless the project later decides otherwise.

### `DefaultParameters`

An `FInstancedPropertyBag` containing:

- the parameter schema;
- parameter types;
- default values.

### `ExecutionLocks`

A `FGameplayTagContainer` describing the exclusive capabilities required while the action is active.

Examples:

```text
GameplayAction.Lock.Primary
GameplayAction.Lock.Movement
GameplayAction.Lock.Facing
GameplayAction.Lock.Interaction
GameplayAction.Lock.Inventory
```

### `bCanBeInterrupted`

Controls whether a conflicting higher-priority action may preempt this action.

This setting does not prevent explicit cancellation or lifecycle aborts.

### `DefaultBlockedPolicy`

Determines what normally happens when the request cannot start immediately because a required lock is occupied.

The initial implementation should support at least:

```text
Queue
Reject
```

Priority-based preemption is evaluated before the blocked policy.

### `OptionalTimeout`

Optional action timeout expressed in the plugin's chosen gameplay-time domain. The first implementation may omit automatic timeout execution if it would make the core milestone unnecessarily large, but the Definition must leave a clean extension point.

## 3.2 Data validation

The Definition must participate in Unreal data validation where appropriate.

Validate at least:

- `ActionClass` is valid;
- `ActionClass` derives from the required base class;
- `ActionTag` is valid when required by the plugin policy;
- every Execution Lock is under the expected lock tag hierarchy;
- the Property Bag exists and has a valid layout;
- values that are incompatible with the action class are reported when validation can determine this safely.

Do not hardcode parameter names in the core Definition class.

---

# 4. Dynamic action parameters

Action parameters must be data-driven and editable without modifying C++.

Use:

```cpp
FInstancedPropertyBag
```

The Definition's Property Bag is the authoritative schema and provides default values.

## 4.1 Required workflow

```text
Definition DefaultParameters
        ↓ deep copy
Mutable Action Request Parameters
        ↓ deep copy when accepted
Immutable Runtime Parameter Snapshot
```

The system must preserve value isolation at every step.

Changing:

- the Definition;
- a Blueprint request variable;
- another request using the same Definition;

must never mutate the parameters of an already accepted action.

## 4.2 Editor-authored schema

A designer must be able to create different schemas for different Definitions.

Example A:

```text
Duration       Float
bLoop          Bool
Iterations     Int32
```

Example B:

```text
Destination    FVector
Rotation       FRotator
```

Adding `Iterations` to Example A in the asset must not require a C++ change or plugin recompilation.

The Blueprint action class may then read the new parameter and use it.

## 4.3 Runtime schema rules

Requests may override values already declared by the Definition schema.

The initial implementation must not silently create new Property Bag fields at runtime when a parameter name is misspelled or absent.

Example:

```text
Set Float Parameter("Duraton", 2.0)
```

must fail if the schema only contains:

```text
Duration
```

This avoids accidental schema divergence and replay incompatibility.

## 4.4 Blueprint parameter API

Provide safe Blueprint-callable helpers for common types, including at least:

- bool;
- integer;
- float/double according to the actual Property Bag API used by the engine version;
- name;
- string;
- Gameplay Tag;
- vector;
- rotator;
- transform;
- enum;
- struct;
- object reference;
- soft object reference;
- class reference;
- soft class reference.

Verify the exact `FInstancedPropertyBag` API and supported types against the Unreal Engine version used by the project. Do not invent wrapper calls from memory.

Every setter and getter must return observable success or a structured error.

Do not return a default value without also indicating whether the parameter was actually found and had the expected type.

Suggested parameter access result states:

```text
Success
ParameterNotFound
WrongType
InvalidRequest
InvalidInstance
UnsupportedType
```

## 4.5 Object parameters

The Property Bag may contain UObject-related values, but the core plugin must not assume all object references have the same persistence semantics.

General guidance:

- hard object references are appropriate when intentionally retaining an object or asset;
- soft object references are appropriate for path-addressable assets and world-authored objects that should not be kept alive by the request;
- runtime-generated objects may require an external stable identity system in the future;
- the core plugin must copy the Property Bag value exactly and must not attempt to reinterpret object identity.

The future `IntentReplay` plugin will be responsible for deciding whether every parameter value is recordable and replay-safe.

---

# 5. Action Request

Create a Blueprint-visible value type conceptually named:

```cpp
FGameplayActionRequest
```

A request represents one requested execution.

It is not the runtime action instance.

## 5.1 Required request data

The request should contain at least:

```text
Definition
ParameterOverrides or resolved Parameters
OptionalPriorityOverride
OptionalBlockedPolicyOverride
OriginTag
Requester context
Correlation data reserved for future integrations
```

Recommended semantics:

### `Definition`

The action Definition used to build and validate the request.

### `Parameters`

A mutable copy of the Definition's default Property Bag.

Blueprint code modifies this copy before submission.

### `OptionalPriorityOverride`

When unset, use `Definition.DefaultPriority`.

When set, this becomes the request's effective priority.

Avoid ambiguous sentinel values. Use an explicit optional representation or an accompanying boolean.

### `OptionalBlockedPolicyOverride`

Allows a caller to choose queue or reject behavior for that submission without mutating the Definition.

### `OriginTag`

A generic extensible tag describing where the request came from.

Possible future values include:

```text
GameplayAction.Origin.Player
GameplayAction.Origin.AI
GameplayAction.Origin.Replay
GameplayAction.Origin.GoalDerived
GameplayAction.Origin.System
```

The core plugin must preserve this value but must not attach project-specific behavior to those example tags.

### Correlation data

Reserve a stable, generic correlation field for future integrations such as `IntentReplay`.

Do not make a runtime action handle serve as a persistent replay record identifier. Runtime handles and persistent record IDs have different lifecycles.

## 5.2 Request creation

Provide one authoritative request creation path:

```text
Create Request From Definition
```

It must:

1. validate the Definition reference;
2. deep-copy the Property Bag schema and values;
3. copy default priority and policy information as appropriate;
4. return a structured creation result.

Avoid exposing partially initialized request structs that callers must assemble manually.

---

# 6. Action Handle

Create a lightweight runtime handle conceptually named:

```cpp
FGameplayActionHandle
```

The handle identifies one accepted action within the owning component's runtime lifetime.

Requirements:

- invalid by default;
- comparable and hashable;
- Blueprint-visible;
- generated only by the authoritative Action Component;
- never reused while an older action with the same value may still be observed;
- not treated as a persistent save or replay identifier.

A monotonic component-local or world-local numeric ID is sufficient if implemented safely.

---

# 7. Action Instance

Create a transient, Blueprintable UObject base class conceptually named:

```cpp
UGameplayActionInstance
```

Every accepted request creates one new runtime instance, including requests that initially enter the queue.

The instance contains execution state only. It must not mutate its Definition.

## 7.1 Ownership

The owning `UGameplayActionComponent` must:

- create the instance;
- retain it through reflected ownership;
- remain the sole authority allowed to transition its state;
- release it after terminal notifications and required cleanup.

The instance must be `Transient` and must not be saved as authored content.

## 7.2 Runtime data

The instance should contain at least:

```text
Handle
OwningActionComponent
Definition
ImmutableParameterSnapshot
EffectivePriority
EffectiveBlockedPolicy
ExecutionLocks snapshot
CurrentState
SubmissionSequence
TerminalResult
```

Copy execution locks and effective configuration when the request is accepted. Runtime behavior must not change because the Definition asset is edited while the action is queued or running.

## 7.3 Public API versus overridable hooks

The component must own the public state transition API.

The instance should expose protected Blueprint/C++ extension hooks such as:

```text
CanStartAction
OnActionStarted
OnActionPaused
OnActionResumed
OnActionCancelled
OnActionInterrupted
OnActionAborted
OnActionCleanup
```

Verify which hooks should be `BlueprintNativeEvent`, protected virtual C++ functions or both according to the existing project conventions.

Do not allow external callers to invoke lifecycle hooks directly.

Provide controlled completion operations for the action implementation:

```text
SucceedAction
FailAction
```

These operations must route back through the component's single authoritative terminal transition.

## 7.4 Terminal guard

An action may terminate exactly once.

Every terminal path must pass through one internal component-owned function equivalent to:

```text
FinishAction(Handle, Result)
```

Late timer callbacks, delegate callbacks or async completions after terminal state must be ignored safely and must not emit a second result.

---

# 8. Action Component

Create an Actor Component conceptually named:

```cpp
UGameplayActionComponent
```

Each actor capable of executing actions owns one component.

The component is the authoritative scheduler and state owner for that actor.

## 8.1 Responsibilities

The component must:

- accept submission requests;
- validate requests;
- assign handles and submission sequence numbers;
- create runtime instances;
- calculate effective priority and policy;
- detect Execution Lock conflicts;
- perform priority preemption;
- queue or reject blocked requests;
- start eligible actions;
- manage pause/resume;
- cancel, interrupt or abort actions;
- release locks;
- start newly eligible queued actions;
- emit structured lifecycle events;
- clean up all actions during EndPlay or owner destruction.

## 8.2 Game Thread authority

The initial implementation must run state transitions on the Game Thread.

Do not add background-thread UObject access or speculative async scheduling to the core milestone.

## 8.3 No uncontrolled external mutation

External callers may:

- submit a request;
- cancel an action they are authorized to cancel;
- query state;
- observe events.

External callers must not:

- mutate the active action collection;
- force arbitrary state transitions;
- directly release locks;
- modify accepted parameter snapshots;
- invoke terminal lifecycle hooks.

---

# 9. Action states

Use an explicit state machine.

Recommended non-terminal states:

```text
Created
Queued
Starting
Running
Paused
Ending
```

Recommended terminal states:

```text
Succeeded
Failed
Cancelled
Interrupted
Aborted
```

A rejected submission does not need to create a lasting runtime instance and should be represented by the submission result rather than an action terminal state.

## 9.1 State semantics

### `Created`

The runtime instance has been constructed but not yet admitted to the queue or started.

### `Queued`

The request was accepted but cannot currently acquire all required Execution Locks.

### `Starting`

The component is performing final validation and invoking start hooks.

### `Running`

The action owns all required Execution Locks and is executing.

### `Paused`

The action remains accepted and retains its scheduling identity. The exact policy for retaining locks while paused must be consistent and documented. For the first implementation, paused actions should retain their locks unless a future explicit suspension policy is added.

### `Ending`

Internal guard state used while terminal cleanup and notifications are being processed.

### Terminal states

Terminal states are immutable.

## 9.2 Allowed transition examples

```text
Created → Queued
Created → Starting
Queued → Starting
Starting → Running
Starting → Failed
Running → Paused
Paused → Running
Queued → Cancelled
Running → Succeeded
Running → Failed
Running → Cancelled
Running → Interrupted
Any non-terminal state → Aborted
```

Reject invalid transitions and report them observably.

---

# 10. Structured results

Create a Blueprint-visible result type conceptually named:

```cpp
FGameplayActionResult
```

It should contain at least:

```text
TerminalState
ReasonTag
OptionalContext
```

Use a fixed enum for the broad terminal state and Gameplay Tags for extensible reasons.

Example reason tags:

```text
GameplayAction.Result.Success
GameplayAction.Failure.InvalidRequest
GameplayAction.Failure.CannotStart
GameplayAction.Failure.Timeout
GameplayAction.Cancelled.ByRequester
GameplayAction.Interrupted.HigherPriority
GameplayAction.Aborted.OwnerEndPlay
GameplayAction.Aborted.SystemReset
```

The core must not use display strings as authoritative result data.

---

# 11. Execution Locks

Execution Locks model exclusive capabilities of the owning actor.

They are not consumable resources. They prevent incompatible actions from executing concurrently.

An action acquires all of its required locks atomically when it starts and releases all of them when it ends.

## 11.1 Conflict rule

Two active actions conflict when their Execution Lock containers overlap.

Conceptually:

```text
Conflict = ActiveLocks ∩ IncomingLocks is not empty
```

Examples:

```text
MoveTo
Locks: Movement

FollowTarget
Locks: Movement

Result: conflict
```

```text
MoveTo
Locks: Movement

PlayVoiceLine
Locks: Voice

Result: no conflict
```

## 11.2 Atomic lock acquisition

An action requiring multiple locks must acquire all of them or none of them.

Example:

```text
Incoming action requires: Movement + Facing
```

It must never start while owning only one of those locks.

Do not interrupt some conflicting actions unless the incoming action will be able to preempt every conflict and acquire the complete lock set.

## 11.3 Default sequential behavior

To support simple sequential action execution, Definitions may include:

```text
GameplayAction.Lock.Primary
```

Actions using the `Primary` lock execute one at a time unless a higher-priority action preempts the current owner.

Actions intended to run in parallel may omit `Primary` and use narrower locks.

---

# 12. Priority and preemption

Priority resolves contention between actions requiring overlapping Execution Locks.

Higher numeric values mean higher priority.

Priority has no effect between actions that do not conflict.

## 12.1 Required preemption behavior

The following behavior is mandatory:

```text
Active action:
    Lock X
    Priority 0

Incoming action:
    Lock X
    Priority 1
```

If the active action is interruptible, the component must:

1. accept the incoming request;
2. mark the incoming action as pending start;
3. interrupt the active action with reason `HigherPriority`;
4. complete the interrupted action's cleanup;
5. release Lock X;
6. start the incoming priority 1 action;
7. emit events in deterministic order.

Expected event order:

```text
Incoming Accepted
Active Interrupted/Ended
Incoming Started
```

Do not start the incoming action before the previous lock owner has completed terminal cleanup and released its locks.

## 12.2 Equal or lower priority

Default rule:

```text
Incoming priority <= conflicting active priority
```

The incoming request must not interrupt the active action.

It must then follow its effective blocked policy:

- `Queue` → remain queued;
- `Reject` → reject the submission.

For equal priority, the already active action wins. This prevents unstable mutual interruption.

## 12.3 Non-interruptible conflicts

A higher-priority action cannot preempt an active conflicting action whose snapshot has:

```text
bCanBeInterrupted = false
```

The incoming action must queue or be rejected according to its blocked policy.

Explicit lifecycle aborts must still be able to terminate non-interruptible actions.

## 12.4 Multiple conflicting active actions

Because actions may run concurrently when their locks do not overlap, an incoming action can conflict with more than one active action.

Example:

```text
Active A:
    Movement
    Priority 0

Active B:
    Facing
    Priority 2

Incoming C:
    Movement + Facing
    Priority 1
```

Incoming C must not interrupt only Active A and wait for Facing.

It cannot atomically acquire its complete lock set because Active B has higher priority.

Therefore:

- interrupt none of the active actions;
- queue or reject Incoming C.

An incoming action may preempt only when all conflicting active actions:

1. have lower priority than the incoming action;
2. are interruptible.

If both conditions are true for every conflict, interrupt all conflicting actions in deterministic order, release all locks, then start the incoming action.

## 12.5 Deterministic ordering

All scheduling decisions must be deterministic.

Use at least:

1. effective priority, descending;
2. submission sequence, ascending.

Do not rely on:

- pointer address;
- UObject iteration order;
- unordered container iteration;
- frame timing;
- delegate binding order.

## 12.6 Queued action selection

Whenever locks are released, the component reevaluates queued actions.

Select the highest-priority queued action that can atomically acquire all its locks.

For equal priority, select the oldest submission sequence.

After starting one action, reevaluate the queue again because other non-conflicting actions may also be able to start concurrently.

The scheduler must stop when no queued action can currently start.

## 12.7 Reentrancy protection

Priority interruption and terminal callbacks can cause external code to submit or cancel additional actions.

Do not mutate scheduler collections recursively while iterating them.

Use a controlled transition phase or deferred command queue so that:

- callbacks may request operations safely;
- collection mutation occurs at defined points;
- an action cannot be started twice;
- locks cannot be released twice;
- event ordering remains deterministic.

This is a core correctness requirement, not an optional optimization.

---

# 13. Interruption, cancellation and abort

These outcomes must remain distinct.

## 13.1 Cancellation

Cancellation means the requester or another authorized caller no longer wants the action.

Example reason:

```text
GameplayAction.Cancelled.ByRequester
```

## 13.2 Interruption

Interruption means another gameplay execution displaced the action, normally through priority preemption.

Example reason:

```text
GameplayAction.Interrupted.HigherPriority
```

## 13.3 Abort

Abort is a forced lifecycle/system termination.

Examples:

```text
Owner EndPlay
World teardown
System reset
Component deactivation
```

Abort must ignore normal interruptibility rules.

## 13.4 Synchronous ownership release

From the scheduler's point of view, cancellation, interruption and abort must release the action's Execution Locks synchronously during the controlled terminal transition.

Action implementations must cancel timers, unbind delegates and detach from external async operations during cleanup.

If an external operation cannot be cancelled synchronously, the action must stop observing it and ignore late callbacks after reaching terminal state.

Do not keep locks occupied while waiting for a callback from an operation that has already been logically cancelled.

---

# 14. Submission result

Submitting a request must return a structured result conceptually named:

```cpp
FGameplayActionSubmissionResult
```

It should distinguish at least:

```text
AcceptedAndStarted
AcceptedAndQueued
RejectedInvalidRequest
RejectedBlocked
RejectedMissingRequiredJournal
RejectedByActionValidation
```

When accepted, return the runtime handle.

Do not communicate submission failure through logs alone.

---

# 15. Lifecycle events

Expose a small set of authoritative structured events.

Recommended events:

```text
OnActionAccepted
OnActionRejected
OnActionStarted
OnActionPaused
OnActionResumed
OnActionEnded
```

`OnActionEnded` carries the complete terminal result and replaces separate internal completion paths.

External convenience delegates may be added later, but internally all terminal outcomes must converge through one event and one state transition.

## 15.1 Immutable event payload

Create a snapshot type conceptually named:

```cpp
FGameplayActionEvent
```

It should contain enough immutable data for:

- debugging;
- UI observers;
- analytics;
- the future `IntentReplay` plugin.

Include at least:

```text
EventType
RuntimeHandle
Definition identifier
ActionTag
Parameter snapshot when appropriate
EffectivePriority
Execution Locks snapshot
OriginTag
Submission sequence
Owning actor reference or stable owner context
Terminal result when applicable
Correlation data
```

Avoid exposing a mutable runtime instance as the only source of event data.

---

# 16. Future IntentReplay integration

Do not implement `IntentReplay` in this plugin.

However, implement the action core so that the future plugin can record and replay actions without changing the fundamental request, instance or component architecture.

## 16.1 Generic journal sink contract

Define a generic integration contract in `GameplayActions`, conceptually named:

```text
Gameplay Action Journal Sink
```

It must not mention clones, timelines or project-specific replay concepts.

The future `IntentReplay` plugin will implement this contract.

The contract should receive immutable structured action events or snapshots.

Prefer one structured event entry point over several unrelated callbacks, unless verified Unreal reflection constraints make separate methods safer.

## 16.2 Journal requirement

Support a component or project-level journal requirement policy:

```text
Disabled
Optional
Required
```

Initial generic default:

```text
Optional
```

Future project integration may configure:

```text
Required
```

When journaling is `Required`, an action must not execute unless the registered sink successfully accepts the initial accepted-action snapshot.

This validation must occur before the action starts or interrupts another action.

A failed required-journal write must not preempt an existing action.

## 16.3 Replay-safe snapshots

The core must guarantee that journal events use copied snapshots rather than mutable references to request state.

The future replay plugin must be able to preserve:

- Definition identity;
- Property Bag schema and values;
- origin;
- priority;
- order;
- correlation data;
- result.

The core does not decide which accepted actions become part of a replay track. That policy belongs to `IntentReplay`.

## 16.4 Avoid recursive recording assumptions

Preserve `OriginTag` and correlation data so the future plugin can distinguish:

- original requests;
- replay-generated requests;
- derived requests;
- system requests.

Do not hardcode filtering rules into `GameplayActions`.

---

# 17. Blueprint API

The Blueprint API must make the safe workflow easy and the unsafe workflow difficult.

Minimum workflow:

```text
Create Action Request From Definition
    ↓
Set typed parameter overrides
    ↓
Set optional priority/policy/origin
    ↓
Submit Action
    ↓
Receive structured submission result and handle
```

Provide query functions such as:

```text
Is Action Active
Is Action Queued
Get Action State
Get Action Result
Get Active Actions
Get Queued Actions
Can Submit Action
```

Avoid Blueprint APIs that expose mutable internal arrays or runtime instances directly.

## 17.1 Blueprint action implementation

Blueprint-derived Action Instances must be able to:

- read their immutable parameters;
- observe their owner and component safely;
- complete with success;
- complete with structured failure;
- react to pause/resume;
- clean up when cancelled, interrupted or aborted.

They must not be able to:

- change their own priority after acceptance;
- change their lock set after acceptance;
- directly start another queued instance;
- release locks manually;
- transition themselves to arbitrary states.

---

# 18. Pause and component shutdown

## 18.1 Pause

The component must support explicit pause/resume behavior independent from assumptions about global world pause.

For the first implementation:

- running actions transition to `Paused` through controlled hooks;
- paused actions retain their Execution Locks;
- queued actions remain queued;
- no queued action starts while the component is paused;
- resuming reevaluates the queue after active actions resume.

Timers owned by actions must follow a clearly documented gameplay-time policy.

## 18.2 EndPlay and teardown

During component or owner teardown:

1. stop accepting new requests;
2. abort all non-terminal actions;
3. remove timers and delegate bindings;
4. release all locks;
5. emit terminal events only while it is safe to do so;
6. clear runtime collections.

Cleanup must tolerate world teardown and partially destroyed owners.

---

# 19. Suggested folder structure

Follow the project-wide folder rules without duplicating them in implementation documentation.

Suggested runtime module organization:

```text
Plugins/GameplayActions/
├── GameplayActions.uplugin
├── CODEX/
│   └── GameplayActionsCore.md
├── Docs/
└── Source/
    └── GameplayActions/
        ├── GameplayActions.Build.cs
        ├── Public/
        │   ├── Actions/
        │   │   ├── GameplayActionDefinition.h
        │   │   └── GameplayActionInstance.h
        │   ├── Components/
        │   │   └── GameplayActionComponent.h
        │   ├── Interfaces/
        │   │   └── GameplayActionJournalSink.h
        │   ├── Types/
        │   │   ├── GameplayActionEvent.h
        │   │   ├── GameplayActionHandle.h
        │   │   ├── GameplayActionRequest.h
        │   │   ├── GameplayActionResult.h
        │   │   └── GameplayActionState.h
        │   └── Blueprint/
        │       └── GameplayActionBlueprintLibrary.h
        ├── Private/
        │   ├── Actions/
        │   ├── Components/
        │   ├── Types/
        │   ├── Blueprint/
        │   └── Tests/
        ├── CODEX/
        └── Docs/
```

Adjust the exact structure only when real implementation needs justify it. Do not create empty folders merely to match this diagram.

---

# 20. Module dependencies

Keep dependencies minimal.

The runtime module will likely require modules providing:

- core Unreal types;
- UObject and Actor Component support;
- Gameplay Tags;
- `FInstancedPropertyBag` / Struct Utils support.

Verify the exact module names and public/private placement against the Unreal Engine version and the actual public headers used.

Do not add dependencies on:

- GridWorld;
- IntentReplay;
- GoalAgents;
- puzzle systems;
- Gameplay Ability System;
- editor-only modules.

Optional integration modules may depend on `GameplayActions` later.

---

# 21. Initial implementation scope

The first core milestone should implement:

1. runtime plugin and module;
2. log category and plugin-specific logging macros;
3. `UGameplayActionDefinition`;
4. dynamic default parameters through `FInstancedPropertyBag`;
5. safe request creation and typed Blueprint overrides;
6. runtime handles;
7. `UGameplayActionInstance` base lifecycle;
8. `UGameplayActionComponent` scheduler;
9. explicit states and structured results;
10. atomic Execution Locks;
11. integer priority and required preemption rules;
12. deterministic queue ordering;
13. cancel, interrupt and abort paths;
14. explicit pause/resume;
15. immutable lifecycle event snapshots;
16. generic journal sink contract for future `IntentReplay`;
17. journal requirement policy;
18. debug state inspection;
19. automated tests;
20. user-facing documentation in the plugin `Docs` folder.

## 21.1 Minimal reference action

Implement one simple asynchronous reference action, preferably a wait/delay action, to validate the complete lifecycle.

Its parameters should be defined through the Definition Property Bag rather than a dedicated action-specific C++ struct.

The reference action must demonstrate:

- parameter reading;
- start;
- pause/resume behavior;
- successful completion;
- cancellation;
- interruption by a higher-priority conflicting action;
- abort cleanup;
- late callback protection.

Do not turn the reference action into a project-specific feature.

---

# 22. Required automated tests

Add focused automation tests for the architecture rather than only testing the reference action.

At minimum verify:

## Parameters

1. A request receives a deep copy of Definition defaults.
2. Two requests from the same Definition do not share mutable values.
3. An accepted instance is unaffected by later request modifications.
4. A missing parameter returns an observable failure.
5. A wrong parameter type returns an observable failure.
6. Runtime setters cannot silently add undeclared parameters.

## Lifecycle

7. An accepted action starts at most once.
8. An accepted action reaches one terminal state at most once.
9. Late callbacks after termination do not emit additional results.
10. Queued cancellation releases all references and emits the correct result.
11. EndPlay aborts active and queued actions safely.

## Locks and concurrency

12. Non-overlapping locks allow concurrent execution.
13. Overlapping locks prevent concurrent execution.
14. Multiple locks are acquired atomically.
15. Releasing one action's locks causes eligible queued actions to be reevaluated.

## Priority

16. Priority 1 with Lock X interrupts interruptible Priority 0 with Lock X.
17. Equal priority does not interrupt the active action.
18. Lower priority does not interrupt the active action.
19. Higher priority cannot interrupt a non-interruptible action.
20. An action requiring X + Y interrupts nothing when any conflict cannot be preempted.
21. An action requiring X + Y interrupts all lower-priority interruptible conflicts and then starts.
22. Queued selection is priority-descending and FIFO for ties.
23. Priority does not affect non-conflicting active actions.

## Events and journaling

24. Event order for preemption is Accepted → old Ended → new Started.
25. Event payloads preserve immutable parameter snapshots.
26. Required journaling rejects a request when no sink is available.
27. A failed required-journal acceptance does not interrupt an active action.
28. Optional journaling permits normal execution without a sink.

## Reentrancy

29. Submitting a new action from an `OnActionEnded` observer does not corrupt scheduler state.
30. Cancelling another action from a callback does not invalidate scheduler iteration.

---

# 23. Debugging requirements

Provide inspectable runtime information for each component.

At minimum expose or draw, when debug is enabled:

```text
Owner
Component paused state
Active actions
Queued actions
Handle
ActionTag
State
Effective priority
Execution Locks
Submission sequence
Elapsed execution time where available
Last terminal result
```

For scheduling decisions, make it possible to understand:

- which locks caused a conflict;
- why an incoming action preempted another action;
- why a request queued;
- why a request was rejected;
- which queued action was selected next.

Avoid per-frame log spam. Prefer state-transition logs and opt-in debug output.

---

# 24. Explicit non-goals for the first milestone

Do not implement the following as part of this core task:

- `IntentReplay` recording sessions or replay tracks;
- project-specific replay rules;
- grid navigation or movement actions;
- puzzle-specific targets or interactions;
- high-level goals or replanning;
- save-game serialization;
- network replication or prediction;
- Gameplay Ability System integration;
- custom Blueprint `UK2Node` generation from Property Bag fields;
- arbitrary runtime Property Bag schema mutation;
- multithreaded action execution;
- complex starvation prevention or priority aging;
- deep inheritance hierarchies of concrete actions.

Leave clean extension points, but do not add speculative abstractions that are not required by the defined core.

---

# 25. Core invariants

The implementation is incorrect if any of these invariants can be violated:

1. The Action Component is the single authority for runtime state transitions.
2. A Definition never stores per-execution mutable state.
3. Every accepted request owns an isolated parameter snapshot.
4. An action starts at most once.
5. An action ends at most once.
6. Terminal state is immutable.
7. Execution Locks are acquired and released atomically.
8. Two active actions never own the same exclusive lock.
9. Priority only arbitrates actions with conflicting locks.
10. Equal or lower priority never preempts an active conflict by default.
11. Higher priority preempts only when every conflicting action is lower priority and interruptible.
12. Failed preemption interrupts none of the conflicting actions.
13. Scheduler ordering is deterministic.
14. Required journal acceptance occurs before preemption and execution.
15. Late async callbacks cannot resurrect or re-finish a terminal action.
16. External code cannot mutate accepted configuration or scheduler collections.
17. Component teardown leaves no timers, delegate bindings, locks or live runtime instances behind.

---

# 26. Definition of done for the core milestone

The core milestone is complete only when:

- the plugin compiles for the project's actual Unreal Engine version;
- all required types and lifecycle paths are implemented;
- Blueprint request creation and dynamic parameter overrides work;
- the reference action demonstrates the full lifecycle;
- Execution Lock concurrency behaves correctly;
- higher-priority conflicting actions preempt lower-priority interruptible actions;
- equal/lower priority behavior is deterministic;
- atomic multi-lock preemption is validated;
- required and optional journal policies work;
- automated tests cover the listed critical cases;
- runtime debug information explains scheduling decisions;
- the plugin contains updated human-facing documentation;
- no dependency on the future `IntentReplay` plugin or project-specific gameplay code has been introduced.
