# Environmental Puzzle System — Codex Architecture

This file defines the local architecture for the environmental puzzle system.

It supplements the root `AGENTS.md`. Do not duplicate global coding, workflow, logging, documentation, profiling, or Unreal Engine rules here.

---

# PURPOSE

Build a generic, modular, Blueprint-extensible system for environmental puzzles based on exactly three fundamental world roles:

1. **Emitters** — `UPuzzleEmitterComponent` instances that generate stateful signals.
2. **Controllers** — `APuzzleController` Actors that observe signals and decide whether receivers should be active.
3. **Receivers** — `UPuzzleReceiverComponent` instances that own requested/effective puzzle activation state.

Emitter and Receiver are composable capabilities, not mutually exclusive Actor classes. The same gameplay Actor may own both components.

The system must support reusable puzzle logic such as:

- one pressure plate opens one door;
- two switches must both be active;
- either of two emitters can activate a receiver;
- one input must be active while another is inactive;
- at least N inputs must be active;
- a condition depends on custom signal data;
- one controller activates multiple receivers;
- multiple independent controllers can safely request the same receiver to remain active.

The architecture must remain usable by designers primarily through Blueprint without requiring new C++ code for every new puzzle rule.

---

# FIXED ARCHITECTURAL DECISIONS

These decisions are intentional and must not be redesigned casually.

## 1. The world-level flow is always

```text
UPuzzleEmitterComponent -> APuzzleController -> UPuzzleReceiverComponent
```

Do not bypass the Controller for ordinary puzzle activation.

- Emitters do not activate Receivers directly.
- Receivers do not inspect Emitters.
- Emitters do not know which Controllers observe them.
- Controllers own puzzle wiring and activation logic.

A simple one-switch/one-door puzzle still uses a Controller with a single input condition.

---

## 2. Keep exactly three fundamental world roles

Emitter, Controller, and Receiver are architectural roles, not three mutually exclusive inheritance roots.

The required default representation is:

```text
Emitter  = UPuzzleEmitterComponent
Controller = APuzzleController
Receiver = UPuzzleReceiverComponent
```

The same Actor may own both `UPuzzleEmitterComponent` and `UPuzzleReceiverComponent`. This is the standard way to build puzzle chains.

Do not introduce a fourth mandatory Actor role such as:

- Puzzle Manager;
- Puzzle Action Actor;
- Puzzle Effect Actor;
- Signal Router Actor.

Internal helper types are allowed, especially UObjects used as condition strategies and signal payloads, but the world architecture remains based on Emitters, Controllers, and Receivers.

The Receiver component is the puzzle-state endpoint. The owning gameplay Actor is normally the gameplay-effect endpoint.

## 2.1. Emitter and Receiver capabilities are composable

An Actor may own:

```text
Emitter only
Receiver only
Emitter + Receiver
```

Do not encode these capabilities through mutually exclusive Actor inheritance.

The same Actor owning both capabilities must keep the responsibilities separate:

```text
Receiver component = requested/effective puzzle state
Gameplay Actor = concrete gameplay reaction
Emitter component = observed gameplay state publication
```

---

## 3. The system is event-driven

Normal puzzle evaluation must not require Tick.

The expected flow is:

1. an `UPuzzleEmitterComponent` changes one of its signal states;
2. the Emitter component broadcasts the change;
3. subscribed Controllers update their cached input state;
4. affected Controllers reevaluate their root condition;
5. only Controllers whose result changed update their Receiver components;
6. Receiver components recompute effective state;
7. only effective state transitions emit activation/deactivation notifications;
8. owning gameplay Actors or reusable specialized Receiver components perform gameplay actions.

Do not add periodic polling as the default architecture.

---

## 4. Signal identity uses `FGameplayTag`

Signals are identified by a semantic `FGameplayTag`.

Examples:

```text
Puzzle.Signal.Pressed
Puzzle.Signal.Powered
Puzzle.Signal.Blocked
Puzzle.Signal.Completed
```

Do not encode signal meaning in hard-coded Actor classes or arbitrary string comparisons.

Multiple Emitters may publish the same signal tag.

An input is therefore not identified only by its signal tag. The source Emitter also matters.

---

## 5. Controller-local input aliases use `FName`

A Controller maps external Emitter signals to local input IDs.

Conceptual binding:

```text
InputId: LeftPlate
Emitter: BP_LeftPressurePlate
SignalTag: Puzzle.Signal.Pressed
```

Conditions reference `InputId`, not Actor references.

This is required because it:

- keeps condition objects independent from level Actors;
- allows the same condition structure to be rewired to different Emitters;
- makes Controller wiring the single source of truth;
- prevents Actor references from spreading through nested condition objects.

`InputId` only needs to be unique inside one Controller.

Duplicate input IDs are invalid configuration.

---

## 6. Arbitrary signal data uses Blueprintable `UObject` payloads

Do not return to a fixed signal struct containing generic fields such as:

```text
BoolValue
FloatValue
IntValue
ObjectValue
```

Do not use a primitive union as the extensibility mechanism.

The signal protocol may contain the fundamental active/inactive state, but arbitrary signal-specific data belongs in a polymorphic payload object.

Target base type:

```text
UPuzzleSignalPayload
```

The base payload type must be Blueprintable and designed for subclassing.

Examples of future payload subclasses:

```text
UPuzzleActorSignalPayload
UPuzzleWeightSignalPayload
UPuzzleDirectionSignalPayload
UPuzzleSequenceSignalPayload
```

Do not add these subclasses until a real use case requires them.

The base payload class must not accumulate unrelated convenience fields.

---

## 7. Condition logic uses Blueprintable polymorphic `UObject` strategies

Do not use `FInstancedStruct` as the primary condition extensibility mechanism.

The required model is a polymorphic UObject hierarchy because the system must be highly extensible from Blueprint.

Target base type:

```text
UPuzzleCondition
```

The base condition must support native default implementations where useful and Blueprint specialization.

Conditions are owned by the Controller as instanced subobjects.

A Controller has one root condition.

Composite conditions own child condition objects.

---

## 8. Data Assets are static shared presets only

Do not use Data Assets for mutable runtime signal state.

Do not use Data Assets as live payload instances.

Do not store level Actor references inside reusable Data Assets.

Data Assets may be introduced later for genuinely shared, immutable presets or definitions, but they are not the core runtime data model.

Do not add a preset asset layer before a real reuse requirement exists.

---

## 9. Do not add a global routing subsystem by default

Ordinary level puzzles use explicit configured references and direct delegate subscriptions between Controller Actors and the relevant puzzle components.

Do not introduce a World Subsystem or global manager merely to route normal signals.

A subsystem may only be considered later for a concrete requirement such as:

- global persistence;
- cross-level puzzle state;
- streaming-safe indirection;
- large-scale runtime registration;
- global save/load orchestration.

Those concerns are outside the initial core system.

---

# CORE DATA MODEL

## `FPuzzleInputBinding`

A Controller-owned binding between one external signal and one local input ID.

Required conceptual fields:

```text
FName InputId
TObjectPtr<UPuzzleEmitterComponent> Emitter
FGameplayTag SignalTag
```

The serialized designer-facing form may reference an owning Actor and resolve its first `UPuzzleEmitterComponent` during controlled initialization if direct external component references are impractical in the editor. An opt-in binding flag may instead select a specific component by name. Do not use runtime world searches.

Optional editor-only validation data may be added when it provides real value.

Do not store condition logic in the binding.

---

## Runtime observed input state

The Controller caches the latest state for each valid `InputId`.

Conceptually, an observed input state needs:

```text
bool bIsValid
bool bIsActive
TObjectPtr<UPuzzleSignalPayload> Payload
uint64 Revision
```

The exact internal representation may differ, but these semantics must exist.

`bIsActive` is part of the puzzle signal protocol. It is not the generic arbitrary payload value that was previously rejected.

Invalid and inactive are different states.

A missing or broken binding must not accidentally satisfy a condition that expects an inactive input.

Conditions must fail closed when required input data is invalid.

---

## Signal revision

Each published signal update should have a monotonically increasing revision or equivalent change identity.

The goal is to distinguish:

- no meaningful change;
- active state transition;
- payload replacement;
- payload data update that requires reevaluation.

If payload data changes in place and conditions depend on that data, the Emitter must explicitly republish the signal through the public signal API.

Silent payload mutation that bypasses signal notification is not allowed.

---

# EMITTERS

Target base component:

```text
UPuzzleEmitterComponent
```

An Emitter component owns one or more output signal channels for its owning Actor.

The owning Actor decides what gameplay observation causes a signal to be published. The component owns the reusable signal protocol and runtime signal state.

## Responsibilities

An Emitter must:

- own the current state of the signals it publishes;
- expose the current state so late subscribers can initialize correctly;
- publish active/inactive changes;
- optionally attach a `UPuzzleSignalPayload`;
- notify observers when a meaningful signal update occurs.

## Non-responsibilities

An Emitter must not:

- evaluate puzzle conditions;
- know which Receivers exist;
- activate Receivers;
- contain puzzle-specific multi-input logic;
- search the world for Controllers.

## Required signal semantics

The base Emitter should conceptually support an operation equivalent to:

```text
SetSignalState(SignalTag, bIsActive, Payload)
```

Exact API naming may adapt to an existing implementation, but the semantics must remain.

Publishing must update the Emitter's cached state before notifying Controllers.

A Controller that subscribes late must be able to query the latest state.

## Stateful signals, not transient events

The core system is based on current state.

Examples:

```text
Pressed = true
Powered = false
Blocked = true
```

Do not model the core system as fire-and-forget event messages.

A one-frame or timed pulse may be implemented by a specialized Emitter that explicitly transitions its signal active and inactive.

Do not add a separate event bus for pulses to the core architecture.

## Blueprint extension model

Specific gameplay Actors become Emitters by owning `UPuzzleEmitterComponent`.

The Actor decides when to publish through the component. The component must not contain game-specific detection logic merely because one Actor needs it.

Examples of gameplay Actors that may own an Emitter component:

```text
Pressure plate
Lever
Breakable object
Weight sensor
Trigger volume
Rotating mechanism
Goblin corpse detector
```

The owning gameplay Actor decides when to call the component signal publication API.

The same Actor may also own `UPuzzleReceiverComponent`.

---

# CONTROLLERS

Target base Actor:

```text
APuzzleController
```

The Controller is the only world element responsible for deciding whether its Receivers should be active.

## Required owned data

A Controller owns:

```text
InputBindings
RootCondition
Receivers
LastEvaluationResult
RuntimeInputCache
```

`RootCondition` is one instanced `UPuzzleCondition`.

`Receivers` may contain one or more `UPuzzleReceiverComponent` targets. The serialized designer-facing form may use owning Actor references and resolve the first Receiver component during controlled initialization, with an opt-in binding flag for selecting a specific component by name.

One Controller produces one boolean output state.

If different Receivers require different logic, use different Controllers rather than adding a per-Receiver condition graph inside one Controller.

## Responsibilities

A Controller must:

- validate its input bindings;
- subscribe to the relevant Emitters;
- initialize its cache from each Emitter's current state;
- update only the affected local inputs when a signal changes;
- evaluate the root condition;
- cache the previous result;
- update Receivers only when the result changes;
- release its Receiver requests when it ends play.

## Non-responsibilities

A Controller must not:

- implement door animations;
- move platforms;
- play trap VFX;
- own the physical effect of activation;
- mutate Emitter state during condition evaluation;
- expose its internal runtime cache as freely mutable data.

## Evaluation API

Conditions must read Controller state through controlled query functions.

Conceptual queries:

```text
IsInputValid(InputId)
IsInputActive(InputId)
GetInputPayload(InputId)
GetInputRevision(InputId)
```

Prefer a `TryGet...` form when the result would otherwise be ambiguous.

Do not give conditions direct mutable access to the runtime input map.

## Reentrant updates

Puzzle chains may cause synchronous signal changes while another Controller is already evaluating.

Do not recursively re-enter the same Controller evaluation stack.

The Controller should use a small reentrancy guard with semantics equivalent to:

```text
if already evaluating:
    mark reevaluation requested
    return

repeat:
    clear reevaluation requested
    evaluate current cached state
until no reevaluation was requested
```

The implementation must collapse nested updates onto the latest cached state rather than building recursive call depth.

Do not add a global Tick-based evaluation queue for this.

State-transition guards in Emitter components, Controllers, and Receiver components should prevent stable cycles from repeatedly propagating identical values.

Oscillating cyclic puzzle graphs remain a content/configuration error and should be diagnosable.

---

# CONDITIONS

Target base UObject:

```text
UPuzzleCondition
```

The condition hierarchy is the primary logic extension point.

## Required characteristics

Conditions must be designed for:

- Blueprint subclassing;
- inline instancing inside Controllers or composite conditions;
- read-only evaluation against Controller input state;
- nesting through composite conditions.

The base class should be suitable for Unreal patterns equivalent to:

```text
Blueprintable
Abstract
EditInlineNew
DefaultToInstanced
```

Use the exact Unreal declarations that are correct for the engine version and existing module architecture.

## Evaluation contract

A condition returns a boolean result.

Conceptually:

```text
EvaluateCondition(Controller) -> bool
```

The evaluation API may use a dedicated read-only context object or struct if that improves the implementation, but it must remain fully practical from Blueprint.

Do not expose a C++-only extensibility path as the primary workflow.

## Conditions must be side-effect free

Condition evaluation must not:

- activate Receivers;
- modify Emitters;
- publish signals;
- spawn Actors;
- change world state.

Conditions answer one question only:

```text
Should this Controller currently be active?
```

## Minimum built-in condition set

Implement only the small set needed to compose common logic:

### Input state condition

Reads one local `InputId` and checks whether it is active or inactive.

Invalid input must fail the condition.

### All condition

Returns true only when every child condition is true.

An empty child list must not silently become a surprising always-true puzzle. Choose and document one explicit behavior; prefer invalid/false unless there is a strong existing project convention.

### Any condition

Returns true when at least one child condition is true.

An empty child list should evaluate false.

### Not condition

Inverts one child condition.

Missing child condition is invalid and must fail closed.

### Threshold condition

Returns true when at least N child conditions are true.

This supports patterns such as:

```text
2 of 3 pressure plates
3 of 5 generators
at least one route completed
```

Do not create many specialized boolean subclasses when they can be composed from these primitives.

## Payload-dependent conditions

Custom conditions may read the payload attached to an input.

They should:

1. query the payload by `InputId`;
2. validate the payload class;
3. cast to the expected payload subclass;
4. evaluate typed data owned by that subclass.

Do not add generic `GetFloatValue`, `GetIntValue`, or similar fields to the core signal type.

If a new reusable data concept appears, create a dedicated payload subclass.

---

# RECEIVERS

Target base component:

```text
UPuzzleReceiverComponent
```

A Receiver component is the destination of Controller decisions. It owns puzzle activation requests and the resulting effective active/inactive state.

The Receiver component does not normally own the concrete gameplay action. The owning Actor reacts to Receiver state transitions.

Examples of gameplay Actors that may own a Receiver component:

```text
Door
Moving platform
Trap
Bridge
Elevator
Rotating obstacle
Spawner
Checkpoint mechanism
```

## Responsibilities

A Receiver component must:

- receive active/inactive requests from Controllers;
- calculate its effective active state;
- store that state as the authoritative puzzle activation state for the capability;
- notify observers only on effective state transitions;
- provide Blueprint-assignable transition delegates;
- provide protected C++ virtual transition hooks for reusable component specialization.

## Non-responsibilities

A Receiver component must not:

- subscribe to Emitters;
- evaluate conditions;
- know which signals caused activation;
- search for puzzle Actors;
- duplicate Controller logic;
- contain door-, platform-, trap-, or project-specific gameplay behavior in the generic base component.

## Source-aware activation requests

A Receiver component may be targeted by multiple independent Controllers.

Do not allow direct `SetActive(bool)` calls from Controllers to overwrite each other.

The Receiver component must track activation requests by source Controller.

Conceptual API:

```text
SetControllerRequest(SourceController, bRequestedActive)
RemoveControllerRequest(SourceController)
```

Default effective state:

```text
Receiver component is active when at least one valid Controller currently requests active.
```

This OR aggregation exists only to prevent request ownership races.

Complex puzzle logic does not belong in the Receiver.

For AND logic across multiple signals or requirements, use one Controller with the appropriate condition tree.

## State transition notifications

The base Receiver component should expose Blueprint-assignable notifications equivalent to:

```text
OnReceiverActivated
OnReceiverDeactivated
OnReceiverStateChanged
```

Use the smallest useful set based on the existing implementation.

Notifications must run only when the effective state actually changes.

Repeated identical requests must not replay activation behavior.

The normal Blueprint workflow is:

```text
UPuzzleReceiverComponent changes state
    -> delegate/event
Owning gameplay Actor reacts
    -> OpenDoor / MovePlatform / DisableTrap / custom action
```

The generic Receiver component decides **when the puzzle capability is active**. The owning Actor decides **what active means in gameplay**.

## C++ virtual extension hooks

The base Receiver component should also provide protected virtual hooks equivalent to:

```text
HandleReceiverActivated()
HandleReceiverDeactivated()
HandleReceiverStateChanged(bool bIsActive)
```

These hooks exist for reusable Receiver-component specializations. They are not the required workflow for every concrete puzzle Actor.

A state transition should conceptually follow this order:

```text
Effective state changes
    -> protected virtual hook(s)
    -> Blueprint-assignable delegate(s)
```

Adapt exact ordering only when an existing module convention provides a strong reason. Keep it deterministic and documented.

## Gameplay action ownership

Do not create a Receiver component subclass for every door, trap, platform, bridge, or scripted set piece.

For game-specific behavior, prefer:

```text
Gameplay Actor
├── UPuzzleReceiverComponent
└── Actor-specific gameplay logic

Receiver Activated
    -> Actor.OpenDoor()
```

Create a Receiver component subclass only when the entire reaction is genuinely reusable across unrelated Actor classes.

Good examples:

```text
UMovePuzzleReceiverComponent
URotatePuzzleReceiverComponent
UVisibilityPuzzleReceiverComponent
```

Bad examples:

```text
UGoblinCrusherDoorReceiverComponent
UAncientTempleBossGateReceiverComponent
```

The generic base component must remain free of specific gameplay semantics.

## Actors that are both Receiver and Emitter

The same gameplay Actor may own both components:

```text
AActor
├── UPuzzleReceiverComponent
└── UPuzzleEmitterComponent
```

This is the standard pattern for puzzle chains.

Example:

```text
Controller A
    -> Door Receiver becomes active
    -> Door Actor starts opening
    -> Door reaches fully open state
    -> Door Emitter publishes Puzzle.Signal.FullyOpened = true
    -> Controller B reevaluates
```

Do not automatically mirror Receiver state into an Emitter signal.

A Receiver state is a requested puzzle state. An Emitter signal should represent a meaningful observed gameplay fact.

Correct:

```text
Receiver active -> door starts opening -> movement completes -> FullyOpened signal becomes true
```

Incorrect by default:

```text
Receiver active -> immediately republish the same active state as an Emitter signal
```

---

# INITIALIZATION AND LIFECYCLE

The system must be independent of Actor `BeginPlay` ordering.

## Controller startup sequence

For every unique Emitter component referenced by `InputBindings`:

1. bind to the Emitter component's signal-changed notification;
2. query the Emitter component's current state for each bound signal;
3. populate the Controller cache;
4. evaluate the root condition;
5. apply the initial Controller request to Receivers.

This ordering guarantees:

- signals emitted before Controller startup are not lost because current state is queryable;
- signals emitted after binding are observed;
- initial Receiver state does not depend on Actor BeginPlay order.

## Emitter destruction

If a bound Emitter component or its owning Actor becomes invalid or is destroyed:

- mark every affected input binding invalid;
- reevaluate the Controller;
- do not treat the missing signal as merely inactive.

## Controller shutdown

When a Controller ends play:

- unbind from Emitter components;
- remove its activation request from every valid Receiver component.

A Receiver component must not remain active because a dead Controller left behind stale ownership state.

## Receiver request cleanup

Receiver-component request tracking should tolerate destroyed Controllers.

Invalid source requests must be pruned when the effective state is recomputed.

No Tick-based garbage cleanup is required.

---

# BLUEPRINT EXTENSIBILITY MODEL

The system must provide useful native defaults while allowing designers to create new puzzle content without editing C++.

## Designers should be able to create

### New Emitters

By adding `UPuzzleEmitterComponent` to a gameplay Actor, implementing gameplay detection in the Actor or another appropriate gameplay system, and publishing one or more signals through the component.

Examples:

```text
A plate activated by a goblin corpse
A lever with three physical positions
A detector that publishes weight data
A rotating rune that publishes direction data
```

### New Conditions

By subclassing `UPuzzleCondition` in Blueprint.

Examples:

```text
Payload weight >= threshold
Sequence index equals expected step
Actor payload implements an interface
Two numeric payloads differ by less than tolerance
```

### New Receivers

By adding `UPuzzleReceiverComponent` to a gameplay Actor and reacting to its activation/deactivation notifications.

Examples:

```text
Open a door
Disable a trap
Rotate a bridge
Start an elevator
```

For truly reusable generic reactions, designers/programmers may use a specialized Receiver component subclass. Do not require subclassing for ordinary Actor-specific gameplay.

## Designers should not need to create

- a new Controller subclass for each boolean combination;
- a new signal struct for every payload type;
- C++ code just to combine AND, OR, NOT, or threshold logic;
- global registrations for ordinary level puzzles.

---

# DATA ASSET POLICY FOR THIS SYSTEM

Data Assets are optional and secondary.

Use them only when there is concrete shared static data worth reusing across many instances.

Good future candidates:

```text
Reusable immutable signal metadata
Shared tuning presets
Reusable puzzle archetype definitions without level Actor references
```

Bad uses:

```text
Current active state
Current payload instance
Controller runtime cache
Direct references to level Emitters or Receivers
Mutable per-puzzle execution data
```

Do not introduce Data Assets merely because a type is configurable.

---

# CONCEPTUAL API SHAPE

The following is architectural guidance, not permission to invent Unreal APIs without verification.

Exact signatures may adapt to the existing module, but responsibilities must remain equivalent.

## Emitter

```text
UPuzzleEmitterComponent

Publish/SetSignalState(
    FGameplayTag SignalTag,
    bool bIsActive,
    UPuzzleSignalPayload* Payload
)

TryGetSignalState(
    FGameplayTag SignalTag,
    OutState
)

OnSignalChanged
```

## Controller

```text
APuzzleController

InputBindings
RootCondition
Receivers

TryGetInputState(FName InputId, OutState)
IsInputActive(FName InputId)
GetInputPayload(FName InputId)

EvaluateController()
ApplyResultToReceivers()
```

## Condition

```text
UPuzzleCondition

EvaluateCondition(APuzzleController* Controller) -> bool
```

## Receiver

```text
UPuzzleReceiverComponent

SetControllerRequest(
    APuzzleController* SourceController,
    bool bRequestedActive
)

RemoveControllerRequest(APuzzleController* SourceController)

IsReceiverActive()

OnReceiverActivated
OnReceiverDeactivated
OnReceiverStateChanged

protected virtual HandleReceiverActivated()
protected virtual HandleReceiverDeactivated()
protected virtual HandleReceiverStateChanged(bool bIsActive)
```

---

# SUGGESTED MODULE STRUCTURE

Use the existing module structure if the module already exists.

For a new dedicated puzzle module, prefer domain folders equivalent to:

```text
Public/
├── Conditions/
│   ├── PuzzleCondition.h
│   ├── PuzzleInputStateCondition.h
│   ├── PuzzleAllCondition.h
│   ├── PuzzleAnyCondition.h
│   ├── PuzzleNotCondition.h
│   └── PuzzleThresholdCondition.h
├── Controllers/
│   └── PuzzleController.h
├── Emitters/
│   └── PuzzleEmitterComponent.h
├── Receivers/
│   └── PuzzleReceiverComponent.h
└── Signals/
    ├── PuzzleSignalPayload.h
    └── PuzzleSignalTypes.h

Private/
├── Conditions/
├── Controllers/
├── Emitters/
├── Receivers/
└── Signals/
```

Do not create empty files or folders only to match this diagram.

Do not create separate folders for every tiny payload subclass unless real complexity requires it.

---

# PUZZLE-SPECIFIC VALIDATION

Configuration errors should be detectable before they become mysterious runtime behavior.

Validate at least:

- duplicate `InputId` values inside one Controller;
- missing Emitter components, or an explicitly requested Emitter component name that does not resolve;
- invalid signal tags;
- null root condition;
- missing Receiver components, or an explicitly requested Receiver component name that does not resolve;
- condition references to unknown `InputId` values;
- invalid child conditions inside composites;
- impossible threshold values;
- duplicate Receiver references where duplicates have no semantic value.

Invalid configuration must fail closed.

A broken puzzle should prefer an inactive Receiver over accidental activation.

---

# PUZZLE-SPECIFIC DEBUG INFORMATION

When debug support is enabled according to the module's normal debug rules, make the following state inspectable.

## Emitter debug

Show or report:

```text
Emitter component and owning Actor name
Published signal tags
Current active/inactive state
Payload class
Revision
```

## Controller debug

Show or report:

```text
Controller name
Input bindings
Input validity
Current input states
Current payload classes
Root condition result
Previous result
Connected Receivers
```

When practical, the most useful Controller visualization is a compact world-space label showing local `InputId` values and the final condition result.

## Receiver debug

Show or report:

```text
Receiver component and owning Actor name
Effective active state
Controllers currently requesting active
```

This information must make it possible to answer:

```text
Why is this Receiver active?
Why is this Receiver inactive?
Which signal or invalid binding caused this result?
```

---

# FORBIDDEN ARCHITECTURAL SHORTCUTS

Do not implement the system using any of the following as the default design:

## Direct Emitter-to-Receiver references

This makes logic difficult to compose and duplicates condition handling.

## Receiver-owned conditions

Receiver components are puzzle-state endpoints, not logic evaluators.

## Fixed generic primitive payload fields

Do not recreate a signal container with generic bool/float/int slots.

## `FInstancedStruct` as the primary Blueprint condition system

The required primary extension path is Blueprintable UObject polymorphism.

## Data Assets as runtime values

Shared immutable definitions and mutable runtime state are different responsibilities.

## One giant Puzzle Manager

Explicit local dependencies are preferred for ordinary puzzle wiring.

## World searches during signal propagation

Controllers know their configured Emitter and Receiver components, directly or through explicitly configured owning Actor references resolved during initialization.

## Tick-based reevaluation

Signals trigger reevaluation.

## Controller subclasses for every boolean formula

Use condition composition.

## Receiver subclasses for logic combinations

Use Controllers and conditions.

## Receiver component subclasses for every concrete gameplay Actor

Do not create a dedicated Receiver component subclass merely because one specific Actor opens, moves, plays VFX, or runs custom scripted behavior. Bind the generic component's transition notifications to the owning Actor's gameplay logic.

Subclass the Receiver component only for a genuinely reusable reaction policy.

## Automatically mirroring Receiver state into an Emitter signal

An Actor that owns both components must publish gameplay facts intentionally. Requested Receiver state and observed Emitter state are separate concepts.

## Silent payload mutation

A payload change that affects conditions must produce a signal update/revision.

---

# REQUIRED BEHAVIOR SCENARIOS

Before treating the core architecture as stable, validate these scenarios.

## 1. Single input

```text
Emitter active -> Controller true -> Receiver active
Emitter inactive -> Controller false -> Receiver inactive
```

## 2. ALL condition

Two inputs must both be active.

## 3. ANY condition

Either input can activate the Receiver.

## 4. NOT condition

The Receiver activates only while one input is inactive.

## 5. Threshold condition

At least 2 of 3 inputs must be active.

## 6. Initial state before Controller startup

An Emitter is already active before the Controller initializes.

The Controller must still initialize correctly from current Emitter state.

## 7. Payload-dependent condition

A custom Blueprint condition reads a custom Blueprint payload subclass and reevaluates after the Emitter republishes updated payload data.

## 8. Multiple Controllers targeting one Receiver

Controller A and Controller B both request active.

When A becomes inactive, the Receiver must remain active because B still owns an active request.

## 9. Controller destruction while active

Destroying or ending play on an active Controller must remove its Receiver request.

## 10. Emitter destruction

The affected Controller input becomes invalid and the condition fails closed.

## 11. Reentrant puzzle chain

A Receiver activation causes another Emitter to change during Controller evaluation.

The system must settle without recursive evaluation growth or duplicate state application.

## 12. Actor is both Receiver and Emitter

One Actor owns both components.

```text
Controller A -> Receiver active -> Actor performs gameplay action -> observed gameplay state changes -> Emitter publishes -> Controller B
```

The chain must work without direct Receiver-to-Receiver calls or automatic state mirroring.

## 13. Actor-specific gameplay reaction

A generic Receiver component on a Door Actor becomes active.

The Door reacts through a delegate/event and opens without requiring a Door-specific Receiver component subclass.

## 14. Reusable specialized Receiver component

A reusable movement Receiver component can be attached to multiple unrelated Actor classes and perform the same generic movement policy through protected virtual/base behavior.

## 15. Invalid binding

A condition referencing a missing input must not accidentally activate the Receiver, including when it expects the input to be inactive.

---

# IMPLEMENTATION ORDER

When building the system from scratch, use this order:

1. signal payload base class and signal runtime types;
2. `UPuzzleEmitterComponent` state storage, query, and change notification;
3. Controller input bindings and runtime cache;
4. basic Controller evaluation lifecycle;
5. `UPuzzleReceiverComponent` source-aware activation requests and effective state transitions;
6. base condition UObject;
7. input state condition;
8. composite conditions: All, Any, Not;
9. threshold condition;
10. Receiver transition delegates and protected virtual hooks;
11. Blueprint extension hooks;
12. puzzle-specific validation;
13. puzzle-specific debug inspection;
14. payload-dependent Blueprint condition test;
15. multi-Controller Receiver test;
16. same-Actor Receiver + Emitter chain test;
17. generic Actor-specific reaction test without component subclassing;
18. lifecycle and reentrancy tests.

Do not begin by creating many concrete puzzle Actors or one Receiver component subclass per concrete Actor type.

First make the generic signal -> condition -> receiver path stable.

---

# ARCHITECTURAL COMPLETION CRITERIA

The puzzle-specific architecture is only considered established when:

- an `UPuzzleEmitterComponent` can publish and expose persistent signal state;
- a Controller can bind external signals to local input IDs;
- a Controller initializes correctly regardless of BeginPlay order;
- conditions are instanced Blueprintable UObjects;
- conditions reference local input IDs rather than level Actor references;
- arbitrary signal data uses typed UObject payload subclasses;
- no fixed generic bool/float/int payload union exists;
- a Controller evaluates only when relevant state changes;
- Receiver components are updated only when Controller output changes;
- a Receiver component safely handles active requests from multiple Controllers;
- Receiver components expose effective-state transitions through reusable delegates/events;
- ordinary Actor-specific gameplay actions do not require Receiver component subclasses;
- protected virtual Receiver hooks exist for genuinely reusable component specializations;
- the same Actor can own both Emitter and Receiver components;
- requested Receiver state is not automatically mirrored into emitted gameplay state;
- invalid inputs fail closed;
- Controller shutdown releases Receiver requests;
- signal payload changes that affect logic trigger reevaluation;
- common boolean puzzle logic can be built without new Controller subclasses;
- a custom Blueprint payload and custom Blueprint condition can work without modifying the core C++ signal type.
