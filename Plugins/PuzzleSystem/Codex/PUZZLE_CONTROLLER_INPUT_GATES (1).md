# Puzzle Controller Input Gates — Codex Specification

This file defines a structural extension to the existing `PuzzleSystem` runtime plugin.

It supplements:

- the root `AGENTS.md`;
- `PUZZLE_SYSTEM_ARCHITECTURE.md`;
- the current user-facing `PuzzleSystem` documentation;
- every closer `CODEX` instruction found inside the plugin or module being modified.

Do not duplicate or redesign the established Emitter → Controller → Receiver architecture.

---

# PURPOSE

Extend every individual Controller input binding with an optional, Controller-local gate system.

A primary input signal may be admitted or suppressed according to additional Emitter signals and the same polymorphic condition system already used by `APuzzleController`.

Conceptually:

```text
Primary Emitter signal
        +
Gate Emitter signals
        +
Gate conditions
        ↓
Effective state of this Controller input
        ↓
Existing Controller root condition
        ↓
Existing Receiver request flow
```

The feature is generic. It must not encode any specific gameplay concept, resource, device type, or project-specific rule.

---

# ARCHITECTURAL BOUNDARY

The established world-level flow remains:

```text
UPuzzleEmitterComponent -> APuzzleController -> UPuzzleReceiverComponent
```

Input gates are owned and evaluated by `APuzzleController`.

They must not introduce:

- direct Emitter-to-Receiver communication;
- a global routing subsystem;
- a fourth puzzle-system role;
- Tick-based polling;
- project-specific dependencies;
- a separate duplicated gate-condition hierarchy.

## Controller-local semantics

A gate belongs to one specific `FPuzzleInputBinding` inside one specific Controller.

The gate does **not** stop the source `UPuzzleEmitterComponent` from publishing globally.

It determines whether that published signal is admitted as the effective state of this particular Controller input.

Therefore, the same Emitter signal may be:

```text
admitted by Controller A
suppressed by Controller B
admitted by another input binding in Controller B
```

according to the gates configured on each binding.

Do not mutate the source Emitter state when a gate closes.

Do not republish a modified signal through the source Emitter.

---

# EXISTING BEHAVIOR THAT MUST REMAIN

Existing Controller assets with no gate configuration must preserve their current behavior exactly.

The normal path remains:

```text
Primary input binding
    -> cached primary Emitter state
    -> existing Controller RootCondition
    -> existing Receiver requests
```

Existing:

- `InputBindings`;
- `RootCondition`;
- `ReceiverBindings`;
- condition subclasses;
- payload behavior;
- source-aware Receiver requests;
- lifecycle behavior;
- reentrancy protection;
- Blueprint assets;
- public APIs;
- debug enable/disable controls;

must remain compatible unless the smallest required extension explicitly changes them.

Do not require designers to rebuild existing Controllers.

---

# DATA MODEL

## Primary input binding extension

Extend each existing primary Controller input binding with two new designer-facing arrays equivalent to:

```text
EmitterGates
GateConditions
```

The exact reflected type and property declarations must follow the current implementation and Unreal Engine version.

Conceptually:

```text
FPuzzleInputBinding
├── existing primary input fields
│   ├── InputId
│   ├── EmitterActor
│   ├── bSpecifyEmitterComponent
│   ├── EmitterComponentName, when explicit selection is enabled
│   └── SignalTag
│
├── EmitterGates[]
└── GateConditions[]
```

Do not remove or rename the existing primary binding fields.

---

## Gate Emitter binding

Create a dedicated leaf binding type equivalent to:

```text
FPuzzleEmitterGateBinding
```

Each element must expose the same Emitter-resolution information used by a normal Controller input:

```text
FName InputId
Emitter Actor reference
bool bSpecifyEmitterComponent
Emitter component name, visible only when explicit selection is enabled
FGameplayTag SignalTag
```

The exact Actor/component reference types must match the current `FPuzzleInputBinding` implementation.

The gate binding is a leaf type. It must not recursively contain more gate arrays.

Do not reuse `FPuzzleInputBinding` directly if doing so would create recursive reflected data or serialized-layout problems.

Prefer the smallest compatible implementation:

- introduce a dedicated gate binding struct; or
- extract genuinely shared Emitter reference fields only when that can be done without breaking serialized assets.

Do not refactor existing serialized types merely for aesthetic deduplication.

---

## Gate conditions

Each primary input binding owns an instanced array of condition objects equivalent to:

```text
TArray<UPuzzleCondition*> GateConditions
```

Use the actual Unreal-safe pointer and instancing pattern already established by the plugin.

Gate conditions must use the **same** `UPuzzleCondition` hierarchy used by the Controller's normal root logic.

This includes existing native and Blueprint condition types such as:

```text
UPuzzleInputStateCondition
UPuzzleAllCondition
UPuzzleAnyCondition
UPuzzleNotCondition
UPuzzleThresholdCondition
custom Blueprint UPuzzleCondition subclasses
```

Do not create parallel types such as:

```text
UPuzzleGateCondition
UPuzzleGateAllCondition
UPuzzleGateInputCondition
```

unless a small internal adapter is technically unavoidable. The designer-facing condition hierarchy must remain the existing `UPuzzleCondition` hierarchy.

Gate conditions are owned by their containing primary input binding and must be valid instanced subobjects with correct UObject ownership, duplication, serialization, Blueprint compilation, and garbage collection behavior.

Codex must inspect the existing Details customization and condition ownership implementation before choosing the exact reflected storage shape.

---

# GATE INPUT NAMESPACE

Gate condition `InputId` references are local to the `EmitterGates` array of their containing primary input binding.

Example:

```text
Primary InputBinding InputId = DoorRequest

EmitterGates:
- InputId = SafetyEnabled
- InputId = MechanismReady

GateConditions:
- InputState(SafetyEnabled, active)
- InputState(MechanismReady, active)
```

These gate IDs do not resolve against:

- the Controller's main `InputBindings` array;
- another primary binding's `EmitterGates`;
- Actor names;
- Gameplay Tags.

They resolve only inside the owning primary binding.

Uniqueness rules:

```text
Primary InputId
    unique across the Controller's main InputBindings

Gate InputId
    unique only inside one primary InputBinding's EmitterGates
```

The same gate `InputId` may be reused inside another primary input binding because each gate namespace is isolated.

---

# BYPASS SEMANTICS

Gate evaluation is enabled only when **both** arrays contain at least one valid configured element:

```text
EmitterGates.Num() > 0
AND
GateConditions.Num() > 0
```

Required behavior:

## Both arrays empty

```text
EmitterGates empty
GateConditions empty
    -> gate bypassed
    -> primary signal always admitted
```

## Gates exist, conditions empty

```text
EmitterGates not empty
GateConditions empty
    -> gate bypassed
    -> primary signal always admitted
```

## Conditions exist, gates empty

```text
EmitterGates empty
GateConditions not empty
    -> gate bypassed
    -> primary signal always admitted
```

## Both arrays populated

```text
EmitterGates not empty
GateConditions not empty
    -> evaluate gate configuration
```

Do not fail closed merely because only one of the two arrays is populated.

The unpaired array is ignored at runtime.

The editor may display a non-fatal configuration notice explaining that gate evaluation is bypassed, but this must not become an error and must not suppress the primary signal.

Gate condition objects must not be evaluated while gate evaluation is bypassed.

---

# GATE CONDITION AGGREGATION

When gate evaluation is enabled, every top-level element in `GateConditions` must pass.

Conceptually:

```text
GateResult = GateConditions[0]
          AND GateConditions[1]
          AND GateConditions[2]
          ...
```

Each array element may itself be a composite `UPuzzleCondition`, so designers can express arbitrary nested logic:

```text
GateConditions[]
├── InputState(A, active)
└── Any
    ├── InputState(B, active)
    └── Not(InputState(C, active))
```

Do not add a second array-level aggregation enum in the initial implementation.

Designers already have `All`, `Any`, `Not`, and `Threshold` condition composition.

An empty `GateConditions` array remains bypassed according to the previous section; it is not treated as an empty `All` condition.

---

# RAW AND EFFECTIVE INPUT STATE

The Controller must distinguish the source signal state from the state exposed to its normal root conditions.

For each primary input binding, retain semantics equivalent to:

```text
Raw primary state
Gate evaluation state
Effective primary state
```

Conceptually:

```text
EffectiveActive = RawPrimaryActive AND GateAllowsSignal
```

where:

```text
GateAllowsSignal = true
    when gate evaluation is bypassed

GateAllowsSignal = result of GateConditions
    when both arrays are populated and valid
```

## Gate open

```text
RawPrimaryActive = true
GateAllowsSignal = true
    -> EffectiveActive = true
```

## Gate closed

```text
RawPrimaryActive = true
GateAllowsSignal = false
    -> EffectiveActive = false
```

## Primary input inactive

```text
RawPrimaryActive = false
GateAllowsSignal = true or false
    -> EffectiveActive = false
```

The Controller's existing normal `RootCondition` must evaluate the **effective** primary input state.

It must not see the raw active state as active while the gate is closed.

---

# VALIDITY SEMANTICS

Invalid and closed are different states.

## Valid closed gate

When every configured gate binding and condition is valid, but the evaluated result is false:

```text
GateValid = true
GateAllowsSignal = false
Primary input remains valid
EffectiveActive = false
```

A normal root condition that intentionally checks that primary input as inactive may therefore succeed.

## Invalid configured gate

When gate evaluation is enabled but its configuration or required runtime data is invalid:

```text
GateValid = false
Primary effective input is invalid
```

This must fail closed according to the existing PuzzleSystem rules.

A normal condition expecting the primary input to be inactive must not accidentally pass because a required gate Actor, component, signal, or input ID is broken.

Examples of invalid gate configuration:

- duplicate gate `InputId` inside one primary binding;
- invalid gate signal tag;
- missing gate Emitter Actor;
- configured Emitter Actor has no matching Emitter component;
- explicit component selection enabled with empty or unresolved component name;
- GateCondition references an unknown gate `InputId`;
- null condition object;
- invalid composite child;
- impossible threshold;
- destroyed required gate Emitter at runtime.

## Bypassed unpaired arrays

When only one array is populated, its contents are ignored for runtime gate validity.

Invalid entries inside an ignored unpaired array must not suppress the primary signal.

Editor validation may still report that the ignored data will have no effect.

---

# PAYLOAD SEMANTICS

Gate conditions must be able to inspect gate Emitter payloads using the existing typed `UPuzzleSignalPayload` workflow.

The primary input's raw payload must remain cached internally.

When the gate is open:

```text
normal Controller condition queries
    -> expose the primary payload normally
```

When the gate is valid but closed:

```text
normal Controller condition queries
    -> must not expose the gated primary payload as an admitted value
```

When the gate configuration is invalid:

```text
normal Controller condition queries
    -> primary input is invalid
    -> payload query fails
```

This prevents a custom normal `UPuzzleCondition` from bypassing a closed gate by reading a payload even though the active state was suppressed.

Gate-condition evaluation must still have read-only access to the raw cached gate payloads within its local gate namespace.

Do not mutate payloads during gate evaluation.

---

# CONDITION EVALUATION CONTEXT

The same `UPuzzleCondition` types must work in two scopes:

```text
Normal Controller root scope
    -> resolves InputId against main InputBindings

Per-binding gate scope
    -> resolves InputId against that binding's EmitterGates
```

Codex must inspect the current concrete condition API before implementation.

Do not guess or replace existing Blueprint extension signatures without checking existing source and assets.

Use the smallest backward-compatible solution that allows conditions to query a read-only input source.

Acceptable architectural directions include:

- a reusable read-only condition-evaluation context;
- a scoped Controller query context with safe non-reentrant semantics;
- an internal adapter that preserves existing Blueprint-facing APIs;
- another existing project pattern that provides the same behavior safely.

Required invariants:

- existing normal conditions continue to work unchanged;
- existing Blueprint condition subclasses remain valid;
- gate conditions resolve only gate-local IDs;
- condition evaluation remains side-effect free;
- nested composite conditions inherit the same scope;
- reentrant Controller updates cannot accidentally switch or corrupt another condition's scope;
- no mutable runtime cache is exposed publicly;
- no duplicated condition hierarchy is introduced.

Do not use a fragile global variable, static active context, or unguarded mutable pointer shared across Controllers.

If a new context API is introduced, document its ownership and lifetime clearly and keep it read-only.

---

# RUNTIME CACHE

The Controller must cache gate signal states in addition to primary signal states.

Each enabled primary binding needs a gate-local observed-state cache with semantics equivalent to:

```text
InputId
bIsValid
bIsActive
Payload
Revision
```

Do not poll gate Emitters.

The Controller must subscribe to gate Emitter signal changes using the same event-driven principles as normal inputs.

Gate state updates must identify every affected primary binding and gate `InputId` without world searches.

When the same Emitter component and signal are referenced by multiple:

- primary bindings;
- gate bindings;
- gate bindings across different primary inputs;

avoid unnecessary duplicate delegate subscriptions where the existing architecture can safely share one subscription.

A single signal notification may update multiple cached destinations and then request one collapsed Controller reevaluation.

---

# EFFECTIVE REVISION AND CHANGE PROPAGATION

A primary input's effective state may change even when its primary Emitter does not republish.

Example:

```text
Primary raw signal remains active
Gate signal changes true -> false
    -> effective primary state changes active -> inactive
```

The Controller must treat this as a meaningful effective-input update.

Maintain an effective revision or equivalent change identity so debugging and condition reevaluation can distinguish:

- raw primary updates;
- gate updates;
- effective-state transitions;
- effective payload admission/suppression changes;
- no meaningful effective change.

A gate update must trigger reevaluation of the Controller's normal root condition whenever it can change:

- primary validity;
- effective active state;
- effective payload;
- effective revision-dependent custom logic.

Do not require the primary Emitter to republish when a gate changes.

Do not publish a modified signal back through the Emitter component.

---

# INITIALIZATION AND LIFECYCLE

The feature must remain independent of Actor `BeginPlay` ordering.

## Controller startup

For every unique primary and gate Emitter component referenced by active configuration:

1. validate and resolve the configured Actor/component reference;
2. bind to the Emitter's signal notification;
3. query its current state;
4. populate the appropriate primary or gate cache entries;
5. evaluate enabled gates;
6. build effective primary input states;
7. evaluate the existing Controller root condition;
8. apply the initial result to Receivers.

A gate Emitter that published before Controller startup must still initialize correctly from its persistent current state.

Gate conditions in bypassed configurations do not require runtime gate subscriptions.

## Gate Emitter destruction

If a required gate Emitter component or Actor is destroyed while gate evaluation is enabled:

```text
affected gate input becomes invalid
    -> owning primary effective input becomes invalid
    -> Controller reevaluates
    -> normal root conditions fail closed where required
```

If the gate configuration is bypassed, ignored gate Actors must not affect runtime behavior.

## Controller shutdown

Controller shutdown must:

- unbind primary and gate Emitter delegates;
- clear gate runtime caches;
- release Receiver requests using the existing lifecycle path;
- avoid callbacks after EndPlay or world teardown.

---

# REENTRANCY

Gate updates are part of normal Controller reevaluation and must use the Controller's existing reentrancy protection.

A Receiver reaction may synchronously cause another Emitter to update, including an Emitter used as a gate by the same Controller.

Required behavior:

```text
if Controller is already evaluating
    -> update the latest cache
    -> mark reevaluation requested
    -> do not recursively enter the same evaluation stack

repeat evaluation until no new reevaluation was requested
```

The reevaluation pass must always use the latest primary and gate cache state.

Do not add a global Tick-based queue.

---

# DETAILS PANEL

Update the existing Controller Details panel and any relevant property customization so that each `InputBindings` array element exposes:

```text
Input Id
Emitter Actor
Specify Emitter Component
Emitter Component Name, conditionally visible
Signal Tag
Emitter Gates
Gate Conditions
```

## Emitter Gates UI

`EmitterGates` is an expandable array inside the owning primary binding.

Each gate element exposes:

```text
Input Id
Emitter Actor
Specify Emitter Component
Emitter Component Name, only when explicit selection is enabled
Signal Tag
```

Use naming, tooltips, categories, conditional visibility, and component-selection behavior consistent with normal input bindings.

## Gate Conditions UI

`GateConditions` is an expandable instanced condition array inside the owning primary binding.

Gate condition objects must use the existing inline condition-editing workflow.

Composite conditions must preserve the current safe depth limit and editor stability rules.

Do not require designers to create external assets for ordinary gate conditions.

## Editor notices

When exactly one gate array is populated, display a non-fatal notice where practical:

```text
Gate evaluation is bypassed because both Emitter Gates and Gate Conditions are required.
```

Do not treat this as a runtime error.

When both arrays are populated, normal validation errors must be clearly associated with the owning primary input binding and, where possible, the gate array index or condition.

---

# VALIDATION

Extend Controller validation to cover enabled gate configurations.

Validate at least:

- duplicate gate `InputId` inside one primary binding;
- invalid gate signal tags;
- missing gate Emitter Actors;
- missing matching Emitter components;
- invalid explicitly selected component names;
- gate conditions referencing unknown local gate `InputId` values;
- null top-level gate condition entries;
- invalid composite children;
- impossible threshold values;
- invalid instanced-object ownership;
- duplicate gate bindings when they have no semantic value.

Validation messages must identify:

```text
Controller
primary InputId or primary binding index
gate InputId or gate index when applicable
condition when applicable
```

Enabled invalid gates fail closed.

Bypassed unpaired arrays remain behaviorally allowed, with an informational or warning message only.

Do not weaken existing primary input or Receiver validation.

---

# DEBUGGING

Update the existing Controller visual debug system.

Preserve the established debug controls:

```text
PuzzleSystem.Debug
PuzzleSystem.Debug.Visual
Controller-local bEnableDebug
```

Effective visual debug remains:

```text
Global visual debug enabled
AND
Controller local debug enabled
```

Debug work must remain negligible while disabled.

## Required connection colors

Use the following semantic colors:

```text
Primary Emitter connections = existing cyan
Gate Emitter connections    = red
Receiver connections        = existing green
Controller marker           = existing yellow
```

Every configured gate Emitter must be indicated with a **red line** from the Controller and a red endpoint marker/sphere using the existing visual language.

Do not replace the existing cyan and green meanings.

If one Actor is used as both a primary Emitter and a gate Emitter, preserve both semantic relationships. Use the smallest clear visualization supported by the current debug implementation, such as separate slightly offset lines or endpoint indicators, rather than silently dropping one relationship.

## Debug label content

Extend the Controller world-space debug label to show, for every primary input binding:

```text
Primary InputId
Primary signal tag
Raw primary validity
Raw primary active state
Raw primary revision
Gate mode: Bypassed / Open / Closed / Invalid
Effective primary validity
Effective primary active state
Effective revision
```

When gate evaluation is enabled, also show each gate input:

```text
Gate InputId
Gate signal tag
Validity
Active state
Revision
Payload class, when present
```

Show the result of every top-level `GateConditions` element and the final aggregated gate result when practical.

The debug output must make it possible to answer:

```text
Is the primary Emitter publishing active?
Is this binding bypassing gate evaluation?
Which gate Emitter is closed or invalid?
Which gate condition failed?
Why is the effective Controller input inactive?
Why did the Controller root result change even though the primary Emitter did not republish?
```

Do not log or draw every frame unless the existing visual-debug implementation already requires one-frame redraws.

State-change logs must use `LogPuzzleSystem` and the module's established logging macros, never committed `LogTemp`.

---

# PUBLIC AND BLUEPRINT API

Preserve existing Controller and condition APIs wherever possible.

Add only the minimum safe query support required for debugging, Blueprint condition evaluation, and tests.

Potential semantics that may be required internally or publicly include:

```text
IsInputGateBypassed(PrimaryInputId)
IsInputGateValid(PrimaryInputId)
DoesInputGateAllowSignal(PrimaryInputId)
TryGetRawInputState(PrimaryInputId)
TryGetEffectiveInputState(PrimaryInputId)
TryGetGateInputState(PrimaryInputId, GateInputId)
```

These names are conceptual only.

Codex must inspect the existing API and use naming consistent with the plugin.

Do not expose mutable cache maps, mutable condition contexts, or delegate handles.

Normal existing condition queries must continue to return effective primary states in normal root scope.

Gate-condition scope must return gate-local states.

---

# SERIALIZATION AND BACKWARD COMPATIBILITY

New gate arrays must default empty.

Therefore every previously serialized `APuzzleController` instance must load with:

```text
EmitterGates empty
GateConditions empty
Gate bypassed
existing behavior unchanged
```

Do not rename existing reflected fields or condition classes as part of this task.

Do not require content migration unless an actual engine/reflection constraint makes it unavoidable.

If a reflected migration is unavoidable:

- implement the smallest safe migration path;
- preserve existing asset values;
- document the migration;
- validate representative existing Blueprint and level assets.

Gate runtime caches and evaluation contexts are transient and must not be serialized as designer configuration.

---

# PERFORMANCE

The implementation remains event-driven.

Do not add Tick or periodic polling for gate evaluation.

Performance requirements:

- bind only to Emitters needed by active primary or enabled gate configuration;
- avoid repeated world searches;
- update only affected gate cache entries;
- reevaluate only Controllers affected by a signal update;
- collapse multiple synchronous changes through the existing reentrancy guard;
- avoid allocations and string formatting when debug is disabled;
- do not repeatedly reconstruct all condition objects at runtime;
- consider Unreal Insights instrumentation only for meaningful Controller evaluation scopes, following existing project rules.

---

# REQUIRED BEHAVIOR SCENARIOS

Validate at least the following scenarios.

## 1. Existing Controller without gates

```text
EmitterGates empty
GateConditions empty
```

Primary input and Receiver behavior remain identical to the implementation before this feature.

## 2. Gate bindings only

```text
EmitterGates populated
GateConditions empty
```

Gate evaluation is bypassed and the primary signal is always admitted.

## 3. Gate conditions only

```text
EmitterGates empty
GateConditions populated
```

Gate evaluation is bypassed and the primary signal is always admitted.

## 4. Active primary with open gate

```text
Primary raw active = true
Gate condition result = true
    -> effective primary active = true
```

## 5. Active primary with closed gate

```text
Primary raw active = true
Gate condition result = false
    -> effective primary active = false
```

## 6. Closed but valid gate and inactive query

A normal root `InputState` condition expecting the primary input to be inactive may succeed when the gate is valid but closed.

## 7. Invalid gate fails closed

A required gate Emitter is missing or a GateCondition references an unknown gate `InputId`.

The effective primary input becomes invalid, and a normal condition expecting inactive must not accidentally pass.

## 8. Multiple top-level gate conditions

All top-level `GateConditions` must pass.

One false condition closes the gate.

## 9. Composite gate condition

An `Any`, `All`, `Not`, or `Threshold` condition evaluates gate-local IDs correctly.

## 10. Payload-dependent gate condition

A custom Blueprint condition reads a typed payload from a gate Emitter and controls admission without modifying the payload or source Emitter.

## 11. Gate closes without primary republish

The primary raw signal remains active.

A gate signal changes from active to inactive.

The effective primary state becomes inactive, the normal root condition reevaluates, and Receivers update as required.

## 12. Gate reopens

The primary raw signal remained active while the gate was closed.

When the gate becomes true again, the effective primary state becomes active without requiring the primary Emitter to republish.

## 13. Initial gate state before Controller startup

A gate Emitter is already active before the Controller initializes.

The Controller queries persistent state and initializes correctly regardless of BeginPlay order.

## 14. Gate Emitter destruction

Destroying a required gate source invalidates the affected effective primary input and reevaluates the Controller.

## 15. Shared gate source

The same Emitter signal is used by several primary bindings as a gate.

One update refreshes all affected bindings without duplicate behavioral transitions or recursive growth.

## 16. Same source used as primary and gate

One Actor/component/signal participates as both a normal input and a gate input.

Both caches and debug relationships remain correct.

## 17. Multiple primary bindings use local identical gate IDs

Two primary bindings both define a local gate `InputId = Enabled`.

They remain isolated and evaluate their own gate sources.

## 18. Reentrant puzzle chain

A Receiver transition synchronously changes a gate Emitter used by the same Controller.

The Controller settles through the existing reentrancy guard without recursive stack growth or stale scope use.

## 19. Blueprint condition compatibility

Existing Blueprint `UPuzzleCondition` subclasses continue to evaluate normal Controller inputs.

The same compatible condition type can be instanced as a gate condition and resolve gate-local inputs.

## 20. Debug visualization

With global and local visual debug enabled:

```text
primary Emitter lines are cyan
gate Emitter lines and endpoint markers are red
Receiver lines are green
```

The label reports raw, gate, and effective states accurately.

## 21. Debug disabled

No gate debug drawing, expensive label construction, or debug-only iteration occurs when global or local visual debug is disabled.

## 22. Existing serialized assets

Previously created Controllers load with empty gate arrays and unchanged runtime behavior.

---

# AUTOMATED TESTS

Add or update focused automated tests where the plugin's current test architecture permits.

Prioritize tests for:

- bypass truth table;
- valid-open versus valid-closed versus invalid-gate distinction;
- all-top-level-condition aggregation;
- local gate InputId resolution;
- effective state changes caused only by gate updates;
- gate payload access;
- late initialization;
- source destruction;
- shared subscription behavior;
- reentrancy;
- backward-compatible empty defaults.

Do not rely only on visual PIE testing for core truth-table semantics.

---

# DOCUMENTATION

Update the relevant `Docs` Markdown files in the PuzzleSystem plugin/module.

Document at least:

- purpose of per-input gates;
- Controller-local rather than global Emitter semantics;
- `EmitterGates` fields;
- `GateConditions` behavior;
- local gate `InputId` namespace;
- bypass behavior when either array is empty;
- AND aggregation across top-level gate conditions;
- raw versus effective input state;
- valid-closed versus invalid behavior;
- typed payload support;
- Details panel workflow;
- red gate debug connections;
- examples;
- common validation failures.

Example workflow:

```text
Controller InputBinding: MainRequest
Primary Emitter: BP_MainSwitch / Puzzle.Signal.Active

EmitterGates:
- InputId: PermissionA
  Emitter: BP_A
  Signal: Puzzle.Signal.Enabled

- InputId: PermissionB
  Emitter: BP_B
  Signal: Puzzle.Signal.Ready

GateConditions:
- All
  - InputState(PermissionA, active)
  - InputState(PermissionB, active)

Result:
The MainRequest input is effectively active only while its primary signal is active and the gate condition passes.
```

Do not place Codex-specific implementation instructions inside user-facing `Docs`.

---

# FORBIDDEN SHORTCUTS

Do not:

```text
modify the source Emitter's cached state when a gate closes
prevent the source Emitter from notifying other Controllers
publish a second modified signal from the Controller
activate Receivers directly from gate logic
create a duplicated gate-condition hierarchy
resolve gate condition IDs against the Controller's main InputBindings
share one mutable gate condition context globally
use Tick to poll gate states
perform world searches during signal propagation
treat a valid false gate as invalid
treat an invalid gate as merely inactive
let custom root conditions read a gated primary payload while the gate is closed
fail closed when exactly one gate array is empty
silently evaluate ignored GateConditions without Gate Emitters
serialize runtime gate caches
break existing Blueprint conditions or serialized Controllers
replace existing cyan or green debug semantics
use any color other than red for gate Emitter debug connections
introduce project-specific gameplay concepts into the plugin
```

---

# IMPLEMENTATION ORDER

Use this order after reading the actual source and all relevant local instructions:

1. inspect the existing `FPuzzleInputBinding`, Controller cache, condition API, Details customization, debug renderer, tests, and documentation;
2. define the smallest backward-compatible gate binding data type;
3. add empty-default `EmitterGates` and instanced `GateConditions` to each primary input binding;
4. add validation and component resolution for enabled gate configurations;
5. add gate-local runtime cache and subscription routing;
6. make existing condition types evaluate safely in normal or gate-local scope;
7. calculate raw, gate, and effective primary input state;
8. integrate gate-driven updates with existing reentrancy and Receiver application;
9. update Details panel editing and notices;
10. update visual debug with red gate lines and gate-state labels;
11. add automated tests;
12. update user documentation;
13. compile the affected target;
14. fix every error caused by the change;
15. validate representative existing and new Controller assets in editor/PIE;
16. review the final diff and remove unrelated changes.

---

# DEFINITION OF DONE

The task is complete only when:

- every primary Controller input binding can own gate Emitter bindings and gate conditions;
- gate conditions use the existing `UPuzzleCondition` hierarchy;
- gate `InputId` values resolve only inside their owning primary binding;
- gate evaluation is bypassed when either array is empty;
- all top-level GateConditions must pass when gate evaluation is enabled;
- raw primary state remains separate from effective primary state;
- valid closed gates suppress the input without invalidating it;
- invalid enabled gates fail closed;
- closed gates suppress admitted primary payload access;
- gate changes reevaluate the normal Controller logic without primary republish;
- initialization remains independent of BeginPlay order;
- lifecycle cleanup and reentrancy remain safe;
- old Controllers behave exactly as before because new arrays default empty;
- the Details panel supports practical inline gate configuration;
- gate Emitter debug connections are red;
- debug labels explain raw, gate, and effective state;
- documentation is updated;
- automated and PIE scenarios pass where applicable;
- the affected target compiles successfully;
- the final diff contains no unrelated changes.

If the affected code does not compile, the task is not finished.
