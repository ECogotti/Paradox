# Paradox Clone Perception Integration — Milestone 3

## Task purpose

Integrate the completed `PerceptionKnowledge` plugin and the completed `IntentReplayPerception` extension inside the **Paradox project runtime module** that owns clone AI behavior.

This milestone must implement the first project-specific clone behavior loop:

```text
Replay
    ↓ unexpected relevant observation
Investigating
    ↓ investigation completed and replay can resume
Replay
```

The project must also be prepared for a future third mode:

```text
GOAP
```

For this milestone:

- the Behavior Tree must expose three authorable high-level branches: `Replay`, `Investigating`, and `GOAP`;
- `GOAP` is only a placeholder and performs no gameplay behavior;
- no runtime condition must switch a clone to `GOAP` yet;
- the future transition to `GOAP` must stop the Behavior Tree and hand control to an external GOAP system;
- the future GOAP transition will be irreversible for the remainder of the current run;
- the GOAP planner, goals, actions, exploration, and execution system are explicitly out of scope.

This milestone must also provide:

- project-level clone integration components;
- Behavior Tree tasks, decorators, and Blackboard contracts that make the three-state asset easy to author;
- interruption, suspension, investigation, and replay-resume behavior;
- two placeable testing Actors:
  - a cube whose observable visual state changes when overlapped;
  - a sphere that emits semantic hearing noise when overlapped;
- a generic hearing-range mesh renderer implemented in `PerceptionKnowledge`, not in the Paradox project module;
- detailed integration debugging and acceptance tests.

The project module may depend on the generic plugins. The generic plugins must never depend on the Paradox project module.

---

# Mandatory preliminary rules

Before modifying code or content, Codex must:

1. read the root `AGENTS.md`;
2. identify the actual Paradox runtime module that owns clone AI behavior;
3. not assume that the module is named `ParadoxGameplay` if the repository uses another name;
4. find and read all relevant `CODEX` folders for:
   - the Paradox project module;
   - `PerceptionKnowledge`;
   - `IntentReplay`;
   - `IntentReplayPerception`;
   - `GameplayActions` when its APIs are used;
5. read the corresponding user-facing `Docs`;
6. inspect the actual implementation produced by Milestones 1 and 2 rather than relying only on their architecture documents;
7. inspect the existing clone Character, Controller, IntentReplay setup, GameplayActions integration, navigation component, and Behavior Tree conventions;
8. inspect the actual Unreal Engine headers for the project version before using Behavior Tree, Blackboard, Perception, navigation, mesh, or brain-control APIs;
9. preserve existing Blueprint and serialized asset compatibility;
10. make the smallest correct changes;
11. compile the appropriate editor target after every meaningful change;
12. validate runtime behavior in PIE;
13. update user-facing documentation in every modified module or plugin;
14. review the final diff and remove unrelated changes;
15. not consider the task complete until the affected target compiles successfully.

Do not fabricate Blueprint assets, material assets, Engine APIs, delegate signatures, or module dependencies.

If binary asset creation is not safely supported by the available workflow, Codex must still provide fully placeable native test Actors, Behavior Tree node classes, exact asset-authoring instructions, and validation utilities so that the required assets can be created in the editor without further C++ work.

---

# Source milestone contracts

This task depends on the implemented public APIs of:

```text
PerceptionKnowledge — Milestone 1
IntentReplay Perception Timeline — Milestone 2
```

The following conceptual ownership boundaries are binding.

## `PerceptionKnowledge` owns

- AI Perception listener configuration;
- Sight and Hearing semantic adaptation;
- observable sources;
- semantic state observations;
- semantic event observations;
- the observer's Current Knowledge Store;
- recent perceived events;
- current perception and knowledge debug;
- immutable knowledge snapshots for future consumers.

It does not own:

- Observation Tracks;
- Observation Journals;
- timeline comparison;
- Replay, Investigating, or GOAP behavior;
- paradox rules.

## `IntentReplayPerception` owns

- recording the original-run Observation Track;
- comparing current observations against the source track;
- the runtime Observation Journal;
- match, mismatch, ambiguity, duplicate, and pending classifications;
- replay-relative timing and time-window matching;
- comparative timeline debug;
- public immutable comparison notifications.

It does not own:

- clone behavior mode;
- Behavior Tree control;
- investigation movement;
- project-specific discrepancy relevance;
- GOAP activation;
- paradox generation.

## The Paradox project module owns

- clone behavior mode;
- project-specific response policy;
- the Replay-to-Investigating transition;
- investigation context and execution;
- IntentReplay suspension and resume coordination;
- Behavior Tree and Blackboard integration;
- future irreversible GOAP handoff;
- test Actors and test scenarios specific to Paradox;
- the rule that AI Perception Sight does not generate a paradox.

---

# Binding architectural decisions

## 1. One authoritative behavior coordinator

Create a project-specific component conceptually named:

```cpp
UParadoxCloneBehaviorCoordinatorComponent
```

Use the actual project naming conventions.

This component is the single authoritative owner of the clone's high-level behavior mode.

Conceptual enum:

```cpp
UENUM(BlueprintType)
enum class EParadoxCloneBehaviorMode : uint8
{
    Replay,
    Investigating,
    Goap
};
```

Required invariant:

```text
Only the coordinator changes the authoritative behavior mode.
```

The following systems must not independently change mode:

- `PerceptionKnowledge`;
- `IntentReplayPerception`;
- the Behavior Tree task itself;
- the investigation component;
- GameplayActions;
- the test Actors.

Those systems may publish results or requests. The coordinator decides the transition.

The coordinator must expose controlled operations equivalent to:

```text
EnterReplayMode(...)
EnterInvestigatingMode(...)
CompleteInvestigation(...)
RequestFutureGoapMode(...)
GetCurrentMode()
```

The exact API must preserve invariants and return useful failure results.

---

## 2. Behavior Tree is the active state orchestrator for Replay and Investigating

A Behavior Tree must be used for the current clone behavior loop.

The Behavior Tree represents three high-level authorable branches:

```text
Replay
Investigating
GOAP
```

However:

- Replay and Investigating are active Behavior Tree behaviors;
- GOAP is a placeholder branch only;
- the future real GOAP system will not run through a Behavior Tree or StateTree;
- entering real GOAP will stop the Behavior Tree first;
- after real GOAP begins, the Behavior Tree must not restart during the same run.

Do not use StateTree for this milestone.

Do not implement GOAP logic as Behavior Tree tasks.

---

## 3. One execution system

Replay and Investigation must execute movement and other gameplay operations through the existing `GameplayActions` infrastructure when compatible APIs already exist.

Required flow:

```text
IntentReplay
    ↓
GameplayAction request
    ↓
GameplayActions executor

Investigation
    ↓
GameplayAction request
    ↓
GameplayActions executor
```

Do not create a second movement or interaction execution framework solely for investigation.

Reuse:

- existing movement actions;
- existing GridWorld movement/path-following integration;
- action priorities;
- Execution Locks;
- interruption reasons;
- completion and failure reporting.

If the current project lacks one small adapter required for investigation movement, add the smallest project-level adapter rather than redesigning `GameplayActions`.

---

## 4. Sight and paradox detection remain completely separate

AI Perception Sight is used for:

- semantic environmental scanning;
- perceived state acquisition;
- Current Knowledge Store updates;
- Observation Track recording;
- Observation Track comparison;
- detecting a changed visible world state that may trigger Investigating.

AI Perception Sight must never:

- generate a paradox;
- call the paradox manager;
- inspect Temporal Index for paradox generation;
- replace the dynamic visual-cone mesh;
- use the visible debug cone as gameplay collision;
- classify clone visibility as a paradox.

Paradox generation remains exclusively based on:

```text
overlap with the dynamic visual-cone mesh
+
project-specific Temporal Index comparison
```

This invariant must be documented and protected by module boundaries.

A changed state seen through AI Perception, such as a computer being powered when it was expected to be off, may trigger `Investigating`. That is not paradox generation.

---

## 5. Project behavior responds to comparison results, not raw stimuli

The coordinator must not transition to `Investigating` directly from raw `UAIPerceptionComponent` callbacks.

Required chain:

```text
Unreal AI Perception
    ↓
PerceptionKnowledge semantic observation
    ↓
IntentReplayPerception comparison
    ↓
OnObservationUnexpected or equivalent immutable event
    ↓
Paradox project response policy
    ↓
Behavior coordinator
    ↓
Replay → Investigating
```

This guarantees that a heard sound or seen state causes investigation only when it is classified against the source timeline.

Do not bypass the Observation Track comparator.

---

## 6. Unexpected events and unexpected states may trigger Investigating

The default project policy for this milestone must support at least:

```text
Unexpected Hearing Event
    → candidate for Investigating

Unexpected Sight State
    → candidate for Investigating
```

Examples:

```text
A new impact noise was not heard during the original run.
    → Investigating

A computer is seen powered on, while the recorded observation expected it powered off.
    → Investigating
```

The response must be policy-driven rather than hardcoded to concrete Actor classes.

Create a project-level policy or settings object conceptually named:

```cpp
UParadoxObservationResponsePolicy
```

or an equivalent data-driven structure.

It should be able to filter by at least:

- comparison result;
- mismatch reason;
- sense tag;
- state or event semantic tag;
- source category when available;
- causal or instigator justification;
- current clone mode;
- minimum confidence or relevance;
- ignored tags.

The default policy must not investigate:

- matched observations;
- duplicates;
- irrelevant observations;
- observations excluded by policy;
- reliably self-caused or replay-justified changes when the comparison context establishes that justification;
- Sight acquisition without an actual unexpected state comparison.

---

# Project module structure

Adapt the actual paths to the existing repository.

Conceptual layout:

```text
Source/<ParadoxRuntimeModule>/
├── Public/
│   └── AI/
│       ├── Behavior/
│       ├── Investigation/
│       ├── Perception/
│       ├── Testing/
│       └── Types/
├── Private/
│   └── AI/
│       ├── Behavior/
│       ├── Investigation/
│       ├── Perception/
│       ├── Testing/
│       └── Tests/
├── CODEX/
└── Docs/
```

Do not create empty folders solely to match this example.

This milestone must not create a new plugin for Paradox-specific integration.

The integration belongs in the existing project runtime module.

Editor-only validation or asset helpers may live in the existing project editor module if one exists. Runtime code must not depend on that editor module.

---

# Module dependencies

The Paradox project runtime module may require dependencies equivalent to:

```text
Core
CoreUObject
Engine
AIModule
GameplayTags
PerceptionKnowledge
IntentReplay
IntentReplayPerception
GameplayActions
GridWorld — only when already required for investigation movement
NavigationSystem — only when actually used by existing movement infrastructure
UMG — only if runtime UI feedback is implemented in this milestone
```

Determine whether each dependency belongs in public or private dependencies.

Default to private unless public headers expose those types.

Do not add dependencies on:

```text
StateTree modules
GoalAgents
future GOAP modules
SmartObjectsModule unless already required by an existing action target contract
editor-only Behavior Tree modules in runtime code
```

The `PerceptionKnowledge` hearing renderer extension may require only generic Engine rendering dependencies. Keep that change inside the plugin module.

---

# Required clone composition

Codex must inspect the existing clone class and reuse current components.

The resulting conceptual composition should support:

```text
Clone Character or Pawn
├── GameplayAction executor
├── IntentReplay runtime component
├── IntentReplay Observation component
├── PerceptionKnowledge listener or binding
├── Paradox clone behavior coordinator
├── Paradox investigation component
└── optional hearing-range renderer

Clone AI Controller
├── BehaviorTreeComponent / BrainComponent
├── BlackboardComponent
├── AI Perception component or listener binding
└── explicit references to the controlled clone components
```

Do not duplicate a component that already exists on the clone or controller.

The actual Perception listener may live on the AI Controller, Pawn, or another supported owner according to the Milestone 1 implementation. Preserve the verified Body Actor/viewpoint behavior.

All cross-owner references must be resolved during controlled initialization and cached safely.

Do not repeatedly search the world.

---

# Behavior Tree authoring contract

Codex does not have to create the final `.uasset` Behavior Tree and Blackboard assets if binary asset authoring is unsafe or unsupported.

Codex must nevertheless implement every native node, enum, key contract, component API, and validation rule necessary to create the assets easily in the editor.

## Blackboard contract

Provide a stable Blackboard key contract.

At minimum:

```text
BehaviorMode
InvestigationLocation
InvestigationSourceActor
InvestigationSourceEntityId or equivalent serializable data
InvestigationObservationType
InvestigationSemanticTag
HasValidInvestigation
```

Additional useful keys may include:

```text
InvestigationConfidence
InvestigationSense
InvestigationJournalEntryId
ReplayResumeAvailable
LastModeTransitionReason
```

Do not store mutable pointers where a stable ID or controlled object reference is safer.

`BehaviorMode` should use the project enum when Blackboard enum support is compatible with the actual Engine version and project conventions.

If Unreal's Blackboard enum workflow creates fragile Blueprint coupling, provide a documented safe alternative while keeping the public project enum authoritative.

## Recommended Behavior Tree graph

Document and support this structure:

```text
Root
└── Selector
    ├── Sequence: Investigating
    │   ├── Decorator: BehaviorMode == Investigating
    │   └── Task: Run Clone Investigation
    │
    ├── Sequence: Replay
    │   ├── Decorator: BehaviorMode == Replay
    │   └── Task: Run Intent Replay
    │
    └── Sequence: GOAP Placeholder
        ├── Decorator: BehaviorMode == Goap
        └── Task: Wait For External GOAP Handoff / Placeholder
```

The recommended branch order is:

```text
Investigating
Replay
GOAP Placeholder
```

Each mode decorator must be configured or documented to use Blackboard observation and appropriate branch abort behavior so a mode change can promptly abort the previous long-running task.

Do not rely on polling the mode every frame inside all tasks when Blackboard observation can perform the transition.

## Required native Behavior Tree nodes

Provide native nodes conceptually equivalent to:

```text
UBTTask_ParadoxRunIntentReplay
UBTTask_ParadoxInvestigateObservation
UBTTask_ParadoxGoapPlaceholder
UBTDecorator_ParadoxCloneBehaviorMode — optional if the native Blackboard decorator is sufficient
```

Use project naming conventions.

### Replay task

The Replay task must:

- resolve the controlled clone and coordinator safely;
- verify that current mode is `Replay`;
- ensure the IntentReplay playback session is active or start it through the existing controlled API;
- remain active while Replay owns control;
- finish or abort cleanly when mode changes;
- not treat `InterruptedByInvestigation` as a replay fracture;
- release all delegates when aborted or completed;
- not restart or duplicate a playback session on every Behavior Tree search.

### Investigation task

The Investigation task must:

- resolve the active investigation context;
- fail predictably when the context is invalid;
- start investigation through the project investigation component;
- remain latent until investigation completes, fails, or is aborted;
- support cancellation when the mode changes;
- not directly set `BehaviorMode`;
- report completion to the coordinator;
- release all delegates and action handles on abort.

### GOAP placeholder task

For this milestone, the placeholder must:

- perform no GOAP planning or execution;
- make accidental entry visible through a warning or debug state;
- not spin by completing every frame;
- remain safely latent or wait for external shutdown;
- contain no transition back to Replay;
- be documented as a temporary asset-authoring placeholder.

No runtime code in this milestone should request `Goap` mode.

---

# Behavior coordinator responsibilities

The coordinator must own at least:

```text
Current mode
Previous mode
Mode revision
Last transition reason
Bound IntentReplay component
Bound IntentReplayPerception component
Bound PerceptionKnowledge listener
Bound Investigation component
Bound AI Controller / BehaviorTreeComponent when applicable
Current investigation context
Replay resume context
Future GOAP handoff state
```

## Initialization

Initialization must:

1. resolve required components explicitly or through controlled same-owner/controller-pawn relationships;
2. validate public APIs from Milestones 1 and 2;
3. bind to immutable comparison events;
4. bind to Behavior Tree and Blackboard when present;
5. initialize mode to `Replay` unless an existing project lifecycle provides a more authoritative initial state;
6. write the initial Blackboard mode once;
7. verify that no GOAP system is active;
8. emit useful diagnostics when required bindings are missing.

Do not enter gameplay mode from constructors or the CDO.

## Transition validation

For this milestone, permitted transitions are:

```text
Replay → Investigating
Investigating → Replay
```

A controlled project API may exist for future:

```text
Replay → Goap
Investigating → Goap
```

but no current runtime trigger may call it.

Forbidden transition:

```text
Goap → Replay
Goap → Investigating
```

Once future GOAP is entered during a run, the coordinator must treat it as terminal.

Invalid transitions must fail predictably and remain observable through logs/debug.

## Blackboard synchronization

The coordinator is authoritative. The Blackboard mirrors its state.

Required direction:

```text
Coordinator mode change
    ↓
Blackboard BehaviorMode update
    ↓
Behavior Tree branch abort/selection
```

Do not let arbitrary Blueprint code modify the Blackboard key and thereby silently change authoritative project state.

If external Blackboard changes are detected, log or validate them according to project conventions.

---

# Comparison event handling

The coordinator or a dedicated project adapter must subscribe to the generic comparison event from `IntentReplayPerception`.

Conceptual callback:

```text
HandleObservationCompared(const FIntentReplayObservationComparison& Comparison)
```

Required processing:

1. verify the event belongs to this clone's active playback and Observation Journal;
2. ignore stale sessions and late callbacks;
3. verify current mode;
4. pass the result through the project response policy;
5. preserve current observation and matched expected-record context;
6. reject duplicates and ignored classifications;
7. create an immutable project investigation request when relevant;
8. request `Replay → Investigating` through the coordinator.

Do not infer relevance from Actor class names.

## Default relevance examples

### Unexpected hearing event

```text
Current sense: Hearing
Result: Unexpected Event
Policy: relevant
    ↓
Create investigation context at stimulus location
```

### Changed visible state

```text
Current sense: Sight
Expected: PC.State.Powered = false
Current:  PC.State.Powered = true
Result: Unexpected State Value
Policy: relevant
    ↓
Create investigation context for PC actor/location
```

### Self-caused or replay-justified state

```text
Expected value differs
Comparison context reliably links the change to the current clone's replay action
Policy: ignored
    ↓
Remain in Replay
```

### Raw sight acquisition

```text
Actor acquired by sight
No unexpected track comparison
    ↓
Remain in Replay
```

---

# Investigation context

Create a project-specific immutable value type conceptually named:

```cpp
FParadoxInvestigationContext
```

It must contain enough information to investigate without retaining unsafe mutable comparison internals.

Store at least:

```text
Investigation ID
Triggering comparison result
Mismatch reason
Sense tag
Observation kind: State or Event
Semantic state/event tag
Source entity ID
Source Actor weak reference when valid
Instigator entity ID when available
World location
Grid cell when available
Stimulus location when different
Confidence / strength
Replay-relative time
Observation Track ID
Observation Journal ID
Recorded Observation ID when matched
Journal Entry ID
Cause / correlation metadata
Creation world time for diagnostics
```

Do not store a raw pointer to a mutable journal entry.

The context must be copyable for Blackboard/debug use where practical.

---

# Replay suspension and interruption

When entering `Investigating`, the coordinator must perform an atomic transition sequence.

Required conceptual order:

```text
1. Validate current mode is Replay.
2. Capture Replay resume context.
3. Prevent IntentReplay from issuing new actions.
4. Suspend the replay-relative playback clock through the existing API.
5. Interrupt the current replay-owned GameplayAction with a specific reason.
6. Preserve the recorded intent/action cursor.
7. Store the investigation context.
8. Set authoritative mode to Investigating.
9. Mirror mode and context into Blackboard.
10. Let Behavior Tree abort Replay and enter Investigating.
```

The exact order may be adjusted to the actual component lifecycle, but the transition must prevent both Replay and Investigation from issuing concurrent actions.

## Required interruption reason

Use or add a semantically explicit reason equivalent to:

```text
InterruptedByInvestigation
```

Do not classify it as:

```text
Path failure
Target failure
Replay fracture
Unexpected action failure
```

The Replay Execution Journal must preserve that the action was interrupted by the project investigation mode.

Do not modify the immutable Replay Track.

## Replay resume context

Capture an immutable project/runtime context conceptually named:

```cpp
FParadoxReplayResumeContext
```

It should contain at least:

```text
Recorded Intent ID
Recorded Action ID
Semantic action tag/type
Target entity or target Actor identity
Destination or semantic target
Action parameters required to recreate the request
Expected outcome when available
Required interaction position or slot data when available
Required orientation when available
Original runtime request ID for diagnostics
Interruption result
```

Reuse an existing IntentReplay resume or action reconstruction API when one already exists.

Do not duplicate serialized action payload formats unnecessarily.

---

# Investigation execution

Create a project-specific component conceptually named:

```cpp
UParadoxCloneInvestigationComponent
```

The component must execute a simple, deterministic investigation procedure.

It is not GOAP.

It must not select arbitrary goals or alternative puzzle solutions.

## Minimal investigation flow

```text
Created investigation context
    ↓
Resolve investigation destination
    ↓
Move to destination when movement is required
    ↓
Face or inspect the source/location
    ↓
Wait for configurable inspection duration
    ↓
Complete investigation
    ↓
Coordinator attempts Replay resume
```

## Destination policy

The first implementation must support:

```text
Hearing event
    destination = semantic stimulus location or nearest valid reachable grid cell

Sight state discrepancy
    destination = source Actor observation/interaction vicinity or configured inspection point
```

Use existing GridWorld and GameplayActions movement APIs.

Do not use EQS unless the project already uses it and it is the smallest compatible solution.

Do not implement world exploration.

## Multiple discrepancies while Investigating

Nested investigations are out of scope.

Default policy:

```text
While Investigating:
    record and debug additional comparisons
    do not enter a second Investigating state
```

A narrowly configurable option may update the current destination for the same source/event category, but do not create an unbounded queue in this milestone.

## Investigation failure

Investigation failure does not activate GOAP in this milestone.

Required behavior:

- record the failure reason;
- clean up the investigation action;
- ask the coordinator to attempt safe Replay resume;
- expose a future extension event equivalent to `OnReplayContinuityCannotBeRestored` only when the existing architecture needs it;
- do not call future GOAP handoff automatically.

---

# Returning from Investigating to Replay

After investigation completes or safely terminates, the coordinator must reconcile the interrupted Replay action.

## Movement action resume

If the interrupted action was a semantic movement action:

```text
Preserve original semantic destination/target.
Discard the obsolete runtime path.
Request a new movement action toward the same destination.
Resume playback from the same recorded intent/action context.
```

Do not resume an old path point-by-point.

## Non-movement action resume

If the interrupted action was not movement:

1. verify the target still exists and is valid;
2. verify whether the expected outcome is already satisfied;
3. verify whether the clone is currently in a valid execution position;
4. verify required orientation or interaction slot when applicable;
5. if already valid, reissue the interrupted action;
6. if not valid, move to the correct execution position through GameplayActions;
7. after positioning succeeds, reissue the interrupted action;
8. restore replay playback and clock ownership safely.

This local correction is Replay recovery, not GOAP.

## Already-satisfied action

If the action's expected outcome is already satisfied when investigation ends:

```text
Record a runtime result equivalent to SatisfiedExternally
Advance replay according to existing IntentReplay policy
```

Use an existing generic result if already available.

Do not modify the source Replay Track.

## Resume failure

Because GOAP activation is out of scope:

- expose the failure clearly;
- leave the clone in a safe controlled state according to existing project conventions;
- do not silently pretend Replay resumed;
- do not implement an automatic fallback planner;
- do not introduce a temporary fake GOAP behavior.

Document the expected future hook.

---

# Future irreversible GOAP handoff contract

Implement only the project-level seam required for the future milestone.

Conceptual controlled API:

```text
RequestEnterGoapMode(FParadoxGoapHandoffContext)
```

For now:

- no caller invokes this from gameplay;
- no failure automatically invokes it;
- no planner is created;
- the GOAP Behavior Tree branch remains a placeholder.

The future handoff sequence must be documented and structurally possible:

```text
1. Mark authoritative mode as Goap.
2. Freeze/abandon IntentReplay playback for the run.
3. Cancel Investigation if active.
4. Write Goap to the Blackboard for diagnostics.
5. Stop the Behavior Tree through the verified Engine API.
6. Ensure the BehaviorTreeComponent/Brain no longer owns actions.
7. Activate the external GOAP system.
8. Mark GOAP as irreversible for the current run.
```

Do not merely disable component Tick as a substitute for a proper Behavior Tree stop unless the actual Engine API and lifecycle require it.

Codex must inspect the project's Engine version and use the verified Behavior Tree/Brain API.

The coordinator must preserve a terminal flag conceptually equivalent to:

```text
bGoapHandoffCommittedForCurrentRun
```

Once true, requests to return to Replay or Investigating must fail.

---

# Hearing-range mesh renderer

The hearing-range renderer belongs in the generic `PerceptionKnowledge` plugin.

Architectural reason:

```text
Hearing range visualization
    represents generic listener configuration.

Replay / Investigating / clone logic
    represents project behavior.
```

Do not implement the renderer in the Paradox project module unless a verified plugin limitation makes generic ownership impossible.

## Required component

Add an optional component conceptually named:

```cpp
UPerceptionKnowledgeHearingRangeRendererComponent
```

or a renderer/provider matching existing plugin conventions.

The renderer must:

- visualize the configured effective Hearing Range around the actual perception Body Actor;
- use a mesh-based representation suitable for assigning a translucent material;
- support a configurable hollow cylinder, ring, or equivalent radial mesh;
- expose the mesh and material as designer-configurable assets;
- support an optional plugin-provided default mesh/material only when the asset workflow is valid;
- work when the listener component lives on a Controller and the visual anchor is the possessed Pawn;
- update when possession, Body Actor, perception profile, hearing range, or scale changes;
- avoid Tick when values are unchanged;
- be disabled by default;
- never affect collision, perception, hearing queries, navigation, or gameplay;
- never be used as the paradox vision cone;
- support runtime visibility during PIE and optional editor preview when safe;
- support world-space height and thickness controls;
- support draw-above-ground offset;
- support material override and dynamic material parameters when available;
- fail predictably when no valid mesh, material, listener, or Hearing config is resolved.

## Mesh implementation policy

Preferred implementation:

```text
UStaticMeshComponent owned or managed by the renderer
+
configurable hollow-cylinder/ring Static Mesh
+
configurable translucent material
```

The component must scale the mesh based on the actual effective hearing radius rather than a duplicated manually entered radius.

If the plugin supplies a default mesh, document its authored radius and scaling convention.

Do not add a heavy procedural-geometry dependency unless the repository already uses it or a static mesh solution is impossible.

Do not hardcode Engine asset paths without validation.

## Binding policy

Support explicit binding to:

```text
PerceptionKnowledge listener component
Perception profile
Body Actor / visual anchor
```

Controlled same-owner or Controller-to-Pawn resolution may be offered as convenience.

Do not perform repeated world searches.

## Debug control

The renderer is part of the PerceptionKnowledge debug/visualization domain and must obey:

```text
PerceptionKnowledge Global Debug Enabled
AND
Local Renderer Enabled
```

Also provide a project-facing option to show the hearing range independently when the game intentionally exposes it to the player.

If debug visibility and gameplay-facing visibility are both supported, keep them as separate explicit policies so a global debug CVar does not accidentally hide intentional gameplay UI.

## Rendering requirements

The default or documented setup should support:

```text
semi-transparent / translucent material
no collision
no shadow casting
no navigation influence
no overlap generation
no physics interaction
owner-no-see / visibility options when useful
configurable depth priority only through safe supported APIs
```

Do not introduce per-frame dynamic material updates unless necessary.

## Documentation

Update `PerceptionKnowledge` documentation with:

- setup;
- listener binding;
- mesh scale convention;
- material requirements;
- runtime versus debug visibility;
- Controller/Pawn Body Actor behavior;
- troubleshooting for incorrect radius or anchor;
- performance notes.

---

# Testing Actors

Create two placeable project-level native testing Actors.

They belong in the Paradox project module because they validate the project integration behavior, not the generic plugin alone.

Guard or organize them as developer/testing content according to repository conventions.

They must not become dependencies of generic plugins.

## 1. Observable-state test cube

Create a placeable Actor conceptually named:

```cpp
AParadoxPerceptionStateTestCube
```

Required components:

```text
Root Scene Component
Cube Static Mesh Component
Overlap Collision Component
PerceptionKnowledge observable/source component
```

Required behavior:

```text
Initial state:
    Test.State.Active = false
    visual color = Inactive Color

Configured overlap occurs:
    Test.State.Active = true or toggles according to settings
    visual color changes to Active Color
    observable state is updated through PerceptionKnowledge public API
```

The cube must test the scenario where an Actor remains or becomes visible with a changed semantic state.

Configurable properties:

```text
State Tag
Initial value
Toggle or Set-True behavior
Allowed overlap classes/tags
Inactive Color
Active Color
Reset delay — optional
One-shot or repeatable
Local debug
```

Use a dynamic material instance only when a valid material with a documented color parameter is configured.

Do not assume the Engine basic-shape material exposes a color parameter.

If a project test material asset can be created safely, create one with a clearly named vector parameter.

Otherwise expose the material requirement and use a safe visual fallback while keeping semantic state changes functional.

Required test scenario:

```text
Original run:
    cube observed with Test.State.Active = false

Replay:
    another allowed Actor overlaps the cube before or while the clone observes it
    cube becomes active and changes color
    clone observes Test.State.Active = true
    IntentReplayPerception classifies Unexpected State Value
    project policy requests Investigating
```

## 2. Semantic-noise test sphere

Create a placeable Actor conceptually named:

```cpp
AParadoxPerceptionNoiseTestSphere
```

Required components:

```text
Root Scene Component
Sphere Static Mesh Component
Overlap Collision Component
PerceptionKnowledge semantic-noise source capability
```

Required behavior:

```text
Configured overlap occurs
    ↓
EmitSemanticNoise through PerceptionKnowledge public API
    ↓
Unreal Hearing determines which listeners receive it
```

Configurable properties:

```text
Event Tag
Cause Tag
Loudness
Max Range
Strength when separately supported
Noise location policy
Allowed overlap classes/tags
Cooldown
One-shot or repeatable
Visual pulse duration — optional
Local debug
```

Prevent overlap spam through a configurable cooldown or one-shot policy.

Do not broadcast directly to clone listeners.

Required test scenario:

```text
Original run:
    player does not trigger the sphere
    no matching hearing observation exists in the source track

Replay:
    player or another configured Actor overlaps the sphere
    sphere emits semantic noise
    clone hears it
    IntentReplayPerception classifies Unexpected Event
    project policy requests Investigating
```

## Asset authoring

The native test Actors must be directly placeable from the Class Viewer even if Blueprint assets are not created.

When binary asset creation is available and safe, also create convenient Blueprint children and test materials using project naming conventions.

Do not block code completion on optional cosmetic Blueprint assets when native Actors are fully functional.

---

# Optional integration test map

When project asset authoring is supported, create or document a minimal map conceptually named:

```text
L_ParadoxPerceptionIntegrationTest
```

Recommended contents:

```text
Player/recording setup
One clone spawn/setup
Observable-state test cube
Semantic-noise test sphere
Simple reachable navigation/grid area
Debug controls enabled through local flags
No paradox cone overlap requirement
```

The map should make it possible to validate:

- recording an original Observation Track;
- matching a conforming state;
- seeing a changed cube state;
- hearing a new sphere noise;
- Replay-to-Investigating switch;
- investigation movement;
- Replay resume;
- hearing-range renderer;
- comparative green/red debug.

If the map cannot be created by Codex, provide exact setup documentation and a validation command/checklist.

---

# Debug integration

Milestones 1 and 2 already divide debug ownership.

Preserve that split.

## `PerceptionKnowledge` debug

Owns:

- registered observable Actors;
- current Sight/Hearing perception;
- Current Knowledge Store;
- recent semantic events;
- listener location and direction;
- Sight ranges/FOV;
- Hearing Range;
- the hearing-range mesh renderer;
- generic listener-to-source lines and bounds.

## `IntentReplayPerception` debug

Owns:

- expected versus current observation comparisons;
- Observation Track and Journal state;
- green matching Actors;
- red changed/unexpected Actors;
- orange ambiguous Actors;
- purple reliably justified/self-caused differences;
- time windows and matched/consumed record IDs;
- unexpected event locations.

## Paradox project debug

The project module adds behavior-state information only:

```text
Current behavior mode
Previous behavior mode
Mode revision
Last transition reason
Active investigation ID
Investigation source and destination
Replay suspended state
Replay resume context validity
Behavior Tree running/stopped state
GOAP handoff committed state — false in this milestone
```

Do not duplicate the plugin comparison bounds with a third indistinguishable box.

Prefer a project debug HUD, Gameplay Debugger line, or text label that references the existing plugin visualization.

## Required visual test feedback

When comparative debug is enabled:

```text
Conforming observed cube
    green comparison bounds

Changed observed cube
    red comparison bounds and mismatch label

Expected hearing event
    green event visualization

Unexpected sphere noise
    red event visualization at stimulus location
```

When behavior debug is enabled:

```text
Replay
    mode label shows Replay

Unexpected observation accepted by policy
    transition reason visible
    mode label changes to Investigating

Investigation completed
    mode label returns to Replay
```

All debug systems must obey their global and local controls.

Debug must have negligible cost while disabled.

---

# Logging

The project runtime module must use its existing primary log category and scoped macros.

Do not create a second project-wide category only for one component unless project conventions explicitly require it.

Do not use committed `LogTemp`.

Useful behavior logs should include:

```text
Clone/Controller name
Clone persistent identity
Current and requested mode
Transition reason
Playback Session ID
Observation Track ID
Observation Journal ID
Comparison result and mismatch reason
Investigation ID
Source entity and semantic tag
Replay action/intent ID
Behavior Tree state
Failure reason
```

Do not log every matching observation at normal verbosity.

Unexpected transitions and binding failures must remain observable.

---

# Performance

The integration must remain event-driven.

Requirements:

- do not Tick solely to poll comparison results;
- do not Tick solely to poll behavior mode;
- use Blackboard observation/decorator abort behavior for branch changes;
- use delegates for action and investigation completion;
- do not rebuild debug strings when debug is disabled;
- do not scan the entire world for listeners or sources;
- cache controlled component references;
- do not update the hearing mesh every frame when range and anchor are unchanged;
- do not perform extra perception traces for project behavior;
- do not duplicate Observation Track matching in the project module.

Use Unreal Insights instrumentation for meaningful scopes such as:

```text
ParadoxClone_HandleObservationComparison
ParadoxClone_EnterInvestigating
ParadoxClone_CompleteInvestigation
ParadoxClone_ReconcileReplayAction
ParadoxClone_UpdateBlackboardMode
PerceptionKnowledge_UpdateHearingRangeRenderer
```

Do not instrument trivial getters.

---

# Lifecycle and cleanup

Initialization and teardown must be symmetrical.

Required cleanup includes:

- unbind comparison delegates;
- unbind replay playback delegates;
- unbind GameplayAction completion delegates;
- abort active investigation actions;
- clear Blackboard object references when appropriate;
- stop or detach project debug providers;
- destroy or unregister hearing renderer mesh components safely;
- handle Controller unpossession and repossession;
- handle clone destruction;
- handle world reset;
- handle Behavior Tree shutdown;
- reject late callbacks from stale Playback Sessions or Journals;
- reset the future GOAP terminal flag only when a new run is authoritatively initialized.

Do not allow a late comparison callback from an old run to enter Investigating.

Do not allow `EndPlay` cleanup to request new actions.

---

# Blueprint API requirements

Expose only intentional authoring and observation hooks.

Useful Blueprint-facing operations may include:

```text
Get Clone Behavior Mode
Request Replay Mode — controlled/limited
Get Active Investigation Context
Get Last Transition Reason
Is Replay Suspended For Investigation
Get Replay Resume Status
Enable Local Behavior Debug
```

Do not expose unrestricted mutable access to:

- mode fields;
- Blackboard internals;
- resume contexts;
- plugin journal arrays;
- active action handles;
- GOAP terminal flags.

Blueprint categories and tooltips must explain:

- authoritative owner;
- valid transitions;
- that GOAP is a placeholder;
- that AI Sight does not generate paradoxes;
- that unexpected observations are produced by timeline comparison.

---

# Validation and Data Validation

Add validation where appropriate for:

```text
Clone missing Behavior Coordinator
Clone missing Investigation component
Clone missing IntentReplay component
Clone missing IntentReplay Observation component
Clone missing PerceptionKnowledge listener
AI Controller missing BehaviorTree/Blackboard setup
Blackboard missing required keys
Blackboard BehaviorMode type incompatible with project enum
Behavior Tree asset missing or optional configuration unresolved
Comparison component bound to another clone's playback
Investigation movement action unavailable
Hearing renderer missing listener or mesh/material
Test cube missing semantic state tag
Test sphere missing semantic event tag
Duplicate persistent entity IDs
Paradox module accidentally used as a plugin dependency
```

Validation must produce actionable messages containing the affected Actor/component and expected correction.

Do not silently fall back to unrelated world objects.

---

# Mandatory automated tests

Use Unreal Automation Tests where feasible and project runtime tests where required.

## Behavior mode tests

Verify:

- initial mode is Replay;
- Replay can transition to Investigating;
- Investigating can transition to Replay;
- invalid duplicate transitions are handled predictably;
- Goap-to-Replay is rejected by the future transition contract;
- no current comparison automatically enters Goap;
- coordinator remains authoritative over Blackboard.

## Comparison response tests

Verify:

- matched observation does not investigate;
- duplicate does not investigate;
- irrelevant result does not investigate;
- unexpected Hearing event accepted by policy requests Investigating;
- unexpected Sight state accepted by policy requests Investigating;
- raw Sight acquisition without mismatch does not investigate;
- reliably self-caused/justified comparison is ignored by default policy;
- stale session comparison is ignored.

## Replay interruption tests

Verify:

- entering Investigating pauses/suspends Replay;
- current replay action receives `InterruptedByInvestigation` or equivalent;
- immutable Replay Track is unchanged;
- Execution Journal records the interruption reason;
- Replay does not emit concurrent actions during investigation.

## Replay resume tests

Verify:

- interrupted movement reissues toward the semantic destination;
- old path data is not resumed blindly;
- interrupted non-movement action executes immediately when position is valid;
- non-movement action first repositions when required;
- already-satisfied expected outcome advances according to policy;
- failed reconciliation does not silently report success;
- completing investigation returns mode to Replay only after controlled reconciliation.

## Behavior Tree integration tests

Verify:

- Blackboard initial mode mirrors coordinator;
- changing coordinator mode updates Blackboard;
- Replay task aborts when mode becomes Investigating;
- Investigation task starts once;
- Investigation task completion is delivered once;
- task aborts unbind delegates;
- GOAP placeholder does not spin;
- future stop-tree seam can stop the Behavior Tree through the verified API.

## Test cube tests

Verify:

- cube registers as observable;
- initial semantic state is exposed;
- allowed overlap changes semantic state;
- visual color change follows semantic state when material is valid;
- continuous visibility state refresh produces a new observation;
- changed state can become an unexpected comparison.

## Test sphere tests

Verify:

- allowed overlap emits one semantic noise according to cooldown;
- noise uses Unreal Hearing rather than direct listener broadcast;
- listeners outside effective range do not receive it;
- listener inside range receives semantic event;
- missing source-track event becomes an unexpected comparison;
- overlap spam does not create unbounded duplicate events.

## Hearing renderer tests

Verify:

- renderer resolves listener and Body Actor;
- mesh radius matches effective Hearing Range within documented scale convention;
- possession change updates anchor;
- profile/range change updates scale;
- no collision or gameplay overlap is generated;
- global debug disable hides debug visualization immediately;
- local disable affects only that renderer;
- disabled renderer does not Tick or rebuild continuously.

## Paradox separation tests

Verify:

- AI Sight observation never invokes paradox-generation APIs;
- hearing renderer has no paradox collision;
- dynamic paradox cone remains separate;
- unexpected state causes Investigating, not paradox generation.

---

# Required manual PIE validation

Codex must document and perform, where possible, these scenarios.

## Scenario A — Conforming visible state

```text
1. Record original run while cube is inactive.
2. Replay without changing cube.
3. Clone sees inactive cube.
4. Comparative debug is green.
5. Clone remains in Replay.
```

## Scenario B — Changed visible state

```text
1. Record original run while cube is inactive.
2. During replay, trigger cube before clone observes it.
3. Cube becomes active and changes color.
4. Clone observes active state.
5. Comparative debug becomes red.
6. Policy accepts unexpected Sight state.
7. Clone enters Investigating.
8. Clone inspects cube/location.
9. Clone resumes Replay.
10. No paradox is generated by AI Sight.
```

## Scenario C — Unexpected hearing event

```text
1. Record original run without triggering the sphere.
2. During replay, trigger sphere inside clone hearing radius.
3. Sphere emits semantic noise.
4. Hearing debug shows source and radius.
5. Observation comparison is red/unexpected.
6. Clone enters Investigating.
7. Clone reaches the investigation point.
8. Clone waits/inspects.
9. Clone resumes Replay.
```

## Scenario D — Noise outside range

```text
1. Place sphere outside displayed hearing radius.
2. Trigger sphere.
3. Clone does not receive the hearing observation.
4. No investigation occurs.
```

## Scenario E — Replay movement resume

```text
1. Clone is replaying MoveTo Target A.
2. Unexpected noise causes Investigating.
3. Replay movement is interrupted with the correct reason.
4. Investigation completes.
5. A new path is calculated to Target A.
6. Replay continues.
```

## Scenario F — Non-movement resume

```text
1. Clone is about to interact with Target B.
2. Unexpected observation causes Investigating.
3. Clone moves away to inspect.
4. Investigation completes.
5. Clone validates interaction position.
6. Clone moves back to the required point.
7. Original semantic interaction is reissued.
```

## Scenario G — GOAP remains inactive

```text
1. Run all discrepancy scenarios.
2. No event enters GOAP.
3. GOAP placeholder branch remains unused.
4. Behavior Tree continues to own Replay/Investigating.
```

---

# Required documentation

Update or create project module documentation under its `Docs` folder.

At minimum document:

## `Docs/CLONE_BEHAVIOR.md`

- three-mode architecture;
- active transitions in this milestone;
- Behavior Tree ownership;
- future irreversible GOAP handoff;
- coordinator authority;
- Blackboard keys;
- Replay interruption and resume behavior.

## `Docs/PERCEPTION_INTEGRATION.md`

- plugin bindings;
- observation comparison response flow;
- unexpected Hearing and Sight-state policies;
- explicit separation from paradox Sight/cone logic;
- player recording versus clone playback setup.

## `Docs/INVESTIGATION.md`

- investigation context;
- destination policy;
- GameplayActions integration;
- completion, failure, and cancellation;
- Replay reconciliation.

## `Docs/BEHAVIOR_TREE_SETUP.md`

- required Blackboard keys;
- exact recommended three-branch graph;
- decorator abort settings;
- required native tasks;
- GOAP placeholder setup;
- future Behavior Tree shutdown seam.

## `Docs/TESTING.md`

- test cube setup;
- test sphere setup;
- optional test map;
- expected debug colors;
- manual validation scenarios.

Update `PerceptionKnowledge` documentation for the hearing renderer.

Update `IntentReplayPerception` documentation only when integration requires a public contract clarification; do not put Paradox-specific behavior rules inside generic plugin Docs.

---

# Required deliverables

At minimum, this milestone must deliver:

```text
Project runtime integration
    authoritative clone behavior coordinator
    investigation component
    project observation-response policy
    Replay suspension/resume integration
    future GOAP handoff seam

Behavior Tree support
    behavior-mode enum
    Blackboard key contract
    Replay task
    Investigation task
    GOAP placeholder task
    setup documentation
    optional BT/Blackboard assets when safe

Testing
    observable-state cube Actor
    semantic-noise sphere Actor
    automated tests
    manual PIE validation
    optional integration test map

PerceptionKnowledge extension
    mesh-based hearing-range renderer
    material/mesh configuration
    debug integration
    tests and documentation

Documentation
    project module Docs
    updated plugin Docs

Validation
    successful editor-target compilation
    reviewed diff
```

---

# Explicit out of scope

Do not implement:

- GOAP planner;
- GOAP Goals or Actions;
- GOAP Belief adapter;
- autonomous level solving;
- autonomous exploration;
- multi-tick planning;
- thought-bubble UI;
- clone coordination;
- maintained GOAP actions;
- GOAP transition triggers;
- GOAP execution through Behavior Tree;
- StateTree integration;
- paradox generation through AI Perception;
- replacement of the dynamic paradox vision mesh;
- Smart Object integration unless an existing action target already requires it;
- nested investigation queues;
- project-wide AI refactors unrelated to this milestone;
- changes to concrete puzzle logic except minimal test Actor integration.

---

# Core invariants

The following invariants are mandatory:

```text
PerceptionKnowledge produces knowledge; it does not change clone mode.

IntentReplayPerception compares timelines; it does not change clone mode.

The Paradox coordinator is the only mode authority.

Blackboard mirrors the coordinator; it is not the authority.

AI Perception Sight never generates paradoxes.

Paradox generation remains dynamic-cone overlap plus Temporal Index policy.

Replay and Investigation never issue actions concurrently.

Investigation interruption is not a Replay fracture.

Replay Track and Observation Track remain immutable.

GOAP has no trigger and no behavior in this milestone.

Future GOAP handoff stops the Behavior Tree and is irreversible for the run.

The hearing-range renderer is visual only and belongs to PerceptionKnowledge.

Generic plugins never depend on the Paradox project module.
```

---

# Acceptance criteria

The milestone is complete only when all of the following are true.

## Integration

- the Paradox project module binds to both generic systems through public APIs;
- comparison events are filtered by a project policy;
- relevant unexpected Hearing events can request Investigating;
- relevant unexpected Sight-state differences can request Investigating;
- matched or ignored observations do not change mode.

## Behavior Tree

- the project exposes a stable three-mode enum and Blackboard contract;
- native tasks make the three-branch Behavior Tree easy to create;
- Replay and Investigating switch through coordinator-driven Blackboard updates;
- branch aborts are safe and deterministic;
- GOAP branch is an inert placeholder;
- no runtime trigger enters GOAP;
- a documented future API can stop the Behavior Tree before external GOAP activation.

## Replay and investigation

- Replay suspends atomically before Investigation owns actions;
- current replay action is interrupted with a dedicated investigation reason;
- investigation executes through existing action infrastructure;
- movement Replay resumes toward the semantic target using a fresh path;
- non-movement Replay validates/reacquires execution position before reissuing;
- no concurrent action ownership occurs;
- failure remains visible and does not silently activate GOAP.

## Testing assets

- a placeable cube exposes and changes a semantic visible state on overlap;
- the cube has clear visual feedback when configured;
- a placeable sphere emits semantic noise through Unreal Hearing on overlap;
- noise cooldown prevents event spam;
- both Actors are documented and usable without project-specific code edits.

## Hearing renderer

- the renderer lives in `PerceptionKnowledge`;
- it follows the effective listener Hearing Range;
- it supports a translucent mesh/material;
- it anchors to the actual Body Actor/Pawn;
- it has no collision or gameplay effect;
- global and local controls work;
- it does not continuously Tick while unchanged.

## Separation

- AI Sight does not invoke paradox logic;
- no generic plugin references the Paradox project module;
- no GOAP or StateTree implementation was introduced;
- no Observation Track or Knowledge Store ownership was duplicated.

## Quality

- affected targets compile successfully;
- required automated tests pass where available;
- manual PIE scenarios are validated and documented;
- user-facing Docs are updated;
- logs use project/plugin categories rather than `LogTemp`;
- debug has negligible disabled cost;
- final diff contains no unrelated changes.

If the affected editor target does not compile, the milestone is not complete.
