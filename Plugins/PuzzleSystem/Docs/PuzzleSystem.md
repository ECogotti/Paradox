# PuzzleSystem

`PuzzleSystem` provides a generic event-driven framework for environmental puzzles. The runtime flow is always:

```text
UPuzzleEmitterComponent -> APuzzleController -> UPuzzleReceiverComponent
```

Emitters publish stateful signals. Controllers observe configured signals, evaluate a condition tree, and request receiver activation. Receivers aggregate requests from one or more controllers and expose effective activation state to the owning gameplay Actor.

## Core Roles

### Emitters

Add `UPuzzleEmitterComponent` to any gameplay Actor that can publish puzzle state, such as a pressure plate, lever, trigger volume, rotating mechanism, or breakable object.

Use `SetSignalState` when gameplay state changes:

```cpp
PuzzleEmitter->SetSignalState(PressedTag, true, Payload);
PuzzleEmitter->SetSignalState(PressedTag, false, nullptr);
```

Signals are identified by `FGameplayTag`. The active flag is the core signal state. Optional custom data must be stored in a `UPuzzleSignalPayload` subclass, not in generic float/int/bool fields.

Use `RepublishSignal` after mutating payload data in place. Conditions only reevaluate when the emitter publishes or republishes a signal.

### Reusable Switch Actor Template

`APuzzleSwitch` is an abstract native Actor for controls whose gameplay input can be expressed as `Press()` and `Release()`. Create a Blueprint child such as `BP_PressurePlate`, `BP_Button`, or `BP_Lever`, add only the mesh, collision, interaction, audio, and animation required by that child, then forward its concrete input to the inherited operations. The native base creates a minimal optional scene root and owns `UPuzzleEmitterComponent`. A specialized native child that supplies a more meaningful root may suppress that optional default subobject through Unreal's object-initializer pattern; ordinary Blueprint children retain it.

Configure these inherited properties:

- `SwitchMode`: `Hold`, `Toggle`, `Latch`, or `Pulse`.
- `InitialInputState`: stable `Released` or `Pressed` logical input restored by initialization and
  `ResetSwitch()` without manufacturing a Press/Release edge.
- `OutputSignalTag`: the single stateful signal published by the owned emitter.
- `bStartActive`: independent initial output restored by `ResetSwitch()`.
- `PressDelay` and `ReleaseDelay`: continuous raw-input time required before a logical edge is confirmed.
- `PulseDuration` and `PulseRetriggerMode`: pulse lifetime and `Ignore`/`Restart` behavior for an already-running pulse.

Mode behavior is:

```text
Hold:   confirmed Press -> Active; confirmed Release -> Inactive
Toggle: each confirmed Press toggles output; Release only rearms input
Latch:  first confirmed Press -> Active until ResetSwitch
Pulse:  confirmed Press -> Active until PulseDuration expires
```

Input and output are independent. `InitialInputState` establishes only the initial raw/logical input;
`bStartActive` establishes the initial published output. Concrete physical controls may deliberately
keep those settings synchronized. `GetInputState()` returns `Released`, `PressPending`, `Pressed`, or `ReleasePending`; `IsInputPressed()` reports raw input while `IsPressed()` reports confirmed logical input. Cancelling `PressPending` produces no Press or Release edge. Re-pressing during `ReleasePending` cancels the release and does not create another Press edge.

Blueprint children can override the protected `BlueprintNativeEvent` hooks `HandleInputPressed`, `HandleInputReleased`, the `HandlePressDelay...` and `HandleReleaseDelay...` Started/Cancelled/Completed hooks, `HandleSwitchActivated`, `HandleSwitchDeactivated`, and `HandleSwitchReset`. This is the preferred extension point for logic owned by the switch subclass and requires no delegate binding in `BeginPlay`. Native subclasses override the corresponding `_Implementation` function; Blueprint overrides should call Parent when they need to preserve an implementation inherited from C++ or another Blueprint.

External observers can bind to the matching `OnInputPressed`, `OnInputReleased`, delay Started/Cancelled/Completed delegates, `OnSwitchActivated`, `OnSwitchDeactivated`, and `OnSwitchReset`. For every transition the authoritative native state is updated first, the matching `BlueprintNativeEvent` runs second, and the multicast delegate broadcasts last. `RestartPressDelay()`, `RestartReleaseDelay()`, `CancelPendingPress()`, and `CancelPendingRelease()` provide controlled pending-transition operations without exposing timer handles.

`ResetSwitch()` is virtual for concrete native controls that must clear transient physical state around the stable switch reset contract. The base implementation cancels input and pulse timers, invalidates obsolete callbacks, restores `InitialInputState`, and restores `bStartActive`. Native subclasses that override it must preserve the parent call. A `PulseDuration` less than or equal to zero still publishes Active synchronously, then publishes Inactive on the next timer-manager tick so observers can see both transitions. `Release()` never ends a pulse early.

Native subclasses with their own event-driven Tick work may override protected `ShouldEnableSwitchTick()` and call `RefreshSwitchTickState()` when that work starts or ends. The default preserves the existing debug-only Tick policy.

For switch visualization, enable the inherited instance property `bEnableDebug` while `PuzzleSystem.Debug.Visual` is enabled. The world-space label shows mode, authoritative and initial input states, configured/current output, tag, pending delay time, and pulse state. Switch gameplay remains event-driven; Tick is enabled only for requested visual debug.

### Reusable Transform Mover Actor Template

`APuzzleTransformMover` is an abstract Blueprint parent for Receiver-driven doors, platforms, bridges,
elevators, barriers, and similar objects. It owns a generic `UPuzzleReceiverComponent`; Controllers target
that Receiver through normal PuzzleSystem wiring, and the Actor converts effective Receiver edges into
movement. The mover never reads Emitters or Controller conditions directly.

The native component layout is:

```text
APuzzleTransformMover
├── UBillboardComponent BillboardRoot
├── UArrowComponent StartArrow (green)
├── UArrowComponent EndArrow (red)
└── UPuzzleReceiverComponent PuzzleReceiver
```

All four component references are `VisibleDefaultsOnly`. They remain visible in a Blueprint's component
hierarchy and Class Defaults, but they do not add editable component-reference fields to every placed
instance's Actor Details panel. The native class intentionally creates no mesh: a child supplies the scene
component that represents its door, platform, or other visual object.

To create a mover Blueprint:

1. Create a Blueprint child of `APuzzleTransformMover`.
2. Add a `StaticMeshComponent`, `SkeletalMeshComponent`, or other movable `SceneComponent` under
   `BillboardRoot`. Set Mobility to `Movable` and leave physics simulation disabled.
3. Compile the Blueprint, open Class Defaults, and choose that component in
   `Puzzle | Mover | Configuration > Default Moved Component`.
4. Place and orient the green `StartArrow` and red `EndArrow`. Their transforms are relative to
   `BillboardRoot`, so moving the Actor in the level moves the whole authored path.
5. Wire an `APuzzleController` Receiver binding to this Actor's `PuzzleReceiver`.

`MovementAlpha` is the authoritative linear progress: zero is the exact Start transform and one is the
exact End transform. Translation, quaternion rotation using the shortest stable path, and scale are all
derived from the two marker transforms. Reversal never uses the current component transform as a new
origin, so it preserves progress without accumulating drift.

Movement modes are:

```text
Latch:    first activation moves toward End; completion occurs only after End is reached
FlipFlop: every activation selects the endpoint opposite the current direction or endpoint
PingPong: Receiver active commands End; Receiver inactive at End commands Start
```

When the Receiver deactivates during movement, `DeactivationBehavior` controls the current traversal:

```text
Stop:     pause at current MovementAlpha; next activation resumes the preserved direction
Return:   reverse immediately toward the logical origin endpoint without snapping
Continue: ignore this deactivation and finish; no automatic reversal is queued at the endpoint
```

Native specializations can validate a target before state changes through
`EvaluateMovementRequestNative`. Returning `Defer` or `Reject` preserves state, alpha, latch
completion, Tick, and presentation events. `OnMovementTargetRequestedNative` observes the semantic
request even when it is otherwise deduplicated. Core native lifecycle hooks run before the matching
`BlueprintNativeEvent`, which in turn runs before the public multicast delegate.

`CaptureRuntimeState()` returns the generic state, alpha, pause, and latch authority.
`RestoreRuntimeState()` validates that whole snapshot, rebuilds easing and the moved-component
transform, and emits no movement presentation events. Project actors can therefore select one
complete `FPuzzleTransformMoverRuntimeState` property in a save/world-state system without storing
the derived mesh transform as a competing authority.

The explicit state is `AtStart`, `MovingTowardEnd`, `AtEnd`, or `MovingTowardStart`. A separate paused
flag preserves direction during `Stop`. Tick is enabled only while a valid component is actively
interpolating; stable endpoints, paused movement, invalid components, and shutdown disable Tick.

Timing supports two exclusive models:

- `Speed` interprets `ForwardSpeed`/`ReturnSpeed` as translation units per second. It cannot represent a
  pure rotation or pure scale path; use `MovementTime` for those paths.
- `MovementTime` interprets `ForwardMovementTime`/`ReturnMovementTime` as full endpoint-to-endpoint
  durations. Partial movement and reversals automatically use only their proportional remaining time.

Enable `bUseSeparateReturnTiming` when movement toward Start needs an independent speed or duration.
`GetRemainingMovementTime` returns failure instead of inventing a value when the selected timing model is
invalid.

Interpolation keeps linear `MovementAlpha` separate from visible `EasedAlpha`. `BuiltInEasing` calls
Unreal's verified `EEasingFunc` implementation and exposes its exponent and step parameters.
`CustomCurve` expects a `UCurveFloat` with normalized X input; its output is clamped to `[0, 1]`.
Changing easing does not change remaining-time calculations.

At runtime initialization the mover resolves `DefaultMovedComponent`, restores `InitialPosition`, binds to
the owned Receiver, and queries its current effective state without depending on Actor `BeginPlay` order.
With `bAnimateInitialReceiverState` disabled (the default), an already-active Receiver snaps to its logical
endpoint without movement-start or endpoint-arrival presentation events. An inactive Receiver does not
manufacture a deactivation edge and therefore preserves the explicitly authored `InitialPosition` until a
real Receiver transition occurs. Enable `bAnimateInitialReceiverState` to process either initial Receiver
state as a normal runtime transition. An explicit later `SynchronizeWithCurrentReceiverState(false)` still
applies both active and inactive states as an event-free snap.

`SetMovedComponent` can replace the controlled component at runtime. A valid replacement must be
registered, movable, owned by the same Actor, not be an internal marker, and not simulate physics. It is
immediately synchronized to current eased progress while direction, pause state, and `MovementAlpha`
remain unchanged. `RestoreDefaultMovedComponent` selects the authored component again. If the active
component is destroyed, movement Tick stops without claiming an endpoint; assigning a valid replacement
allows the preserved traversal to continue.

`ResetMover` stops interpolation, clears pause state, restores `InitialPosition`, restores Latch completion
consistently, and synchronizes the moved component. It does not modify Controller requests or replay the
current Receiver state. Call `SynchronizeWithCurrentReceiverState` explicitly when a reset workflow also
needs that second behavior.

Blueprint children can override the protected `BlueprintNativeEvent` hooks for movement started, resumed,
reversed, paused, per-frame updated, Start/End reached, component changed, and reset. The authoritative
native state is updated first, the matching hook runs second, and the public multicast delegate broadcasts
last. `HandleMovementUpdated` is intentionally a protected per-frame hook rather than a public per-frame
delegate; keep Blueprint work in it small.

Enable local `bEnableDebug` together with `PuzzleSystem.Debug.Visual` to draw the active path, direction,
current progress, Receiver state, mode, timing, easing source, and remaining time. Debug does not enable a
separate idle Tick; the green/red editor arrows remain the passive authoring visualization at endpoints.

Editor data validation skips transient Actor objects created while Blueprint classes are compiled or
reinstanced. Persistent Blueprint defaults and placed map Actors still receive the complete native
component, endpoint, timing, and moved-component validation.

Current limitations are intentional: cross-Actor component control, physics-driven movement, networking,
multi-point paths, and obstacle resolution are not part of the base. Movement currently uses deterministic
non-swept transform updates. UE 5.8 only sweeps root-component translation for this API, so exposing a sweep
option would not safely reconcile authoritative alpha for non-root components, rotation, scale, and custom
easing.

### Gameplay Tag namespace

Puzzle signal Gameplay Tags use the `Puzzle` root. The plugin does not prescribe production signal
names because emitters and controllers accept designer-configured tags. Its native automation
fixtures declare `Puzzle.Test.Pressed`, `Puzzle.Test.Powered`, and `Puzzle.Test.Completed` in
`Private/Tests/PuzzleSystemAutomationTests.cpp`. Project modules may declare their concrete
production signals under the same root, such as Paradox's `Puzzle.Signal.Pressed`.

### Controllers

Place an `APuzzleController` in the level for each boolean puzzle decision. A controller owns:

- `InputBindings`: local input IDs mapped to emitter Actor, optional explicit emitter selection, and signal tag.
- `RootCondition`: one instanced `UPuzzleCondition` object, shown under `Puzzle | Logic` in the Details panel.
- `ReceiverBindings`: receiver Actor references and optional explicit receiver selection.

Controllers use a `UBillboardComponent` as their root component so they are visible and easy to select in the level. The billboard is hidden in game and uses the plugin texture `/PuzzleSystem/Textures/T_PuzzleControllerIcon` when that asset is available. If the icon cannot be loaded, the controller keeps Unreal's default billboard sprite.

Conditions reference local `InputId` values, not level Actors.

The controller resolves component references during initialization:

- `bSpecifyEmitterComponent` and `bSpecifyReceiverComponent` are disabled by default.
- With the corresponding flag disabled, the controller uses the first valid emitter or receiver component owned by the configured Actor. Any stored component name is ignored.
- Enable the corresponding flag only when a specific component is required, then set `EmitterComponentName` or `ReceiverComponentName`.
- Missing components, invalid explicit names, duplicate bindings, or other invalid configuration fail closed and keep receivers inactive.

### Per-Input Gates

Every element of `InputBindings` can optionally filter its own primary signal through additional
Emitter signals and the existing `UPuzzleCondition` hierarchy. This is local Controller wiring: closing
a gate does not modify or republish the source Emitter signal, so another Controller or another binding
may continue to admit the same signal.

Expand one primary input binding and configure both arrays:

- `Emitter Gates`: gate-local inputs with `Input Id`, `Emitter Actor`, optional explicit component
  selection, and `Signal Tag`.
- `Gate Conditions`: inline instanced `UPuzzleCondition` objects. All top-level entries must pass; use
  `All`, `Any`, `Not`, or `Threshold` inside an entry for more complex formulas.

Gate `Input Id` values belong only to the `Emitter Gates` array of their containing primary binding.
They do not resolve against the Controller's main inputs or gates owned by another primary binding. Two
different primary bindings may therefore both use a local gate ID such as `Enabled` without interfering.

The gate is enabled only when both arrays contain at least one element:

```text
Emitter Gates empty + Gate Conditions empty     -> bypass
Emitter Gates populated + Gate Conditions empty -> bypass
Emitter Gates empty + Gate Conditions populated -> bypass
both arrays populated                            -> enabled gate
```

An unpaired array is intentionally ignored at runtime: its Emitters are not subscribed, its conditions
are not evaluated, and invalid ignored entries do not suppress the primary signal. Editor data validation
reports a non-fatal warning so the unused setup is visible.

For an enabled gate, the Controller maintains three separate states:

```text
raw primary state
    + gate-local cached states and Gate Conditions
    -> effective primary state read by RootCondition
```

A valid open gate admits the primary active state and payload. A valid closed gate leaves the effective
input valid but inactive and hides its primary payload; a normal condition that explicitly expects the
input to be inactive can pass. A missing/destroyed gate source, unpublished required gate signal, unknown
local ID, invalid condition tree, or other invalid enabled configuration makes the effective primary input
invalid, so even an inactive-state condition fails closed.

Gate Emitters are subscribed event-first and queried for their persistent state during Controller
initialization. Changing a gate closes or reopens the effective input immediately without requiring the
primary Emitter to republish. Shared Emitter components use one native subscription and fan a notification
out to every affected primary/gate cache before one collapsed Controller reevaluation.

Example:

```text
InputBindings[MainRequest]
  Emitter Actor: BP_MainSwitch
  Signal Tag: Puzzle.Signal.Active

  Emitter Gates
    PermissionA -> BP_A / Puzzle.Signal.Enabled
    PermissionB -> BP_B / Puzzle.Signal.Ready

  Gate Conditions
    All
      InputState(PermissionA, active)
      InputState(PermissionB, active)
```

`MainRequest` is effectively active only while its raw signal is active and both permissions pass.
Custom native or Blueprint `UPuzzleCondition` subclasses use the existing `TryGetInputState`,
`IsInputActive`, `GetInputPayload`, and revision queries; the Controller automatically presents the
gate-local namespace while a Gate Condition is evaluating and effective main inputs while the normal
`RootCondition` is evaluating.

Blueprint diagnostics can query `IsInputGateBypassed`, `IsInputGateValid`,
`DoesInputGateAllowSignal`, `TryGetGateInputState`, and `TryGetEffectiveInputState`. C++ diagnostics may
also use `TryGetRawInputState`. Raw state is intentionally not exposed to Blueprint conditions, preventing
ordinary condition graphs from reading a payload that a closed gate suppressed.

### Receivers

Add `UPuzzleReceiverComponent` to any gameplay Actor that should react to puzzle activation, such as a door, bridge, moving platform, trap, spawner, or checkpoint.

The generic receiver does not perform gameplay actions by itself. Bind to:

- `OnReceiverActivated`
- `OnReceiverDeactivated`
- `OnReceiverStateChanged`

The owning Actor decides what activation means, such as opening a door or disabling a trap.

Native C++ systems that need a direct transition subscription can use
`OnReceiverStateChangedNative`. Receiver virtual hooks execute first, this native notification executes
second, and the existing Blueprint-assignable transition delegates execute last. Blueprint workflows
continue to use `OnReceiverActivated`, `OnReceiverDeactivated`, and `OnReceiverStateChanged` unchanged.

A receiver can be targeted by multiple controllers and OR-aggregates their prerequisite requests. In the
default Automatic mode it remains active while at least one valid Controller requests active. When a
Controller ends play, its request is removed.

Every Receiver exposes `ActivationMode`:

- `Automatic` is the default and preserves the original behavior for existing content: the Receiver is
  active whenever at least one valid Controller requests active.
- `Manual` treats the OR-aggregated Controller result as an activation prerequisite. The Receiver remains
  inactive until `RequestManualActivation` is called, and that command fails while no Controller requests
  active. `RequestManualDeactivation` is always allowed in Manual mode.

If the final active Controller request disappears, a Manual Receiver deactivates immediately and clears
its manual latch. Restoring the prerequisite does not reactivate it; a new explicit Open request is
required. Use one Controller with an `All` condition when several signals must all authorize activation.
Multiple independent Controllers targeting one Receiver intentionally retain OR semantics.

Blueprint and C++ callers can inspect `CanRequestManualActivation`,
`AreActivationPrerequisitesSatisfied`, `IsManualActivationRequested`, and `GetActivationMode` before
calling the two command functions. Each command returns `FPuzzleReceiverActivationCommandResult`, which
contains a precise status, a settled state snapshot, and a diagnostic. `Applied` and
`AlreadyInRequestedState` are accepted outcomes. Bind `OnReceiverActivationPrerequisitesChanged` (or its
native counterpart) when UI availability must refresh while a Manual Receiver remains effectively
inactive.

For a Paradox interaction Gameplay Action, resolve the action's semantic target Actor and then its
`UPuzzleReceiverComponent`; do not search for a Controller:

```text
GetInteractionTarget
  -> Get Component By Class (PuzzleReceiverComponent)
  -> CanRequestManualActivation
  -> RequestManualActivation
  -> CompleteInteractionSuccess / CompleteInteractionFailure
```

Close uses `RequestManualDeactivation`. If an Actor owns multiple Receiver components, the concrete action
must select the intended component explicitly by authored name or equivalent stable local selector. The
PuzzleSystem plugin does not depend on Paradox or GameplayActions.

## Read-Only Graph Queries

`UPuzzleGraphSubsystem` is a world-scoped observer for the topology already resolved by initialized
`APuzzleController` instances. It does not evaluate conditions, route signals, activate Receivers, or
change the existing `Emitter -> Controller -> Receiver` flow. It has no Tick and performs no world scan;
queries use indices populated by Controller initialization and teardown.

Get the subsystem from the relevant gameplay world in Blueprint with **Get World Subsystem** or in C++:

```cpp
#include "Graph/PuzzleGraphSubsystem.h"

UPuzzleGraphSubsystem* Graph = World->GetSubsystem<UPuzzleGraphSubsystem>();
const FPuzzleActorGraphView View = Graph->QueryActorGraph(ObservedActor);

for (const FPuzzleGraphLink& Link : View.OutgoingPrimaryLinks)
{
    FPuzzleGraphLinkState State;
    if (Graph->TryGetLinkState(Link.LinkHandle, State))
    {
        // State is a read-only snapshot for this Controller-local relationship.
    }
}
```

The public query API provides:

- `QueryActorGraph`, grouped into incoming/outgoing primary and gate relationships;
- `QueryIncomingLinksForActor` and `QueryOutgoingLinksForActor`, when grouping is not required;
- `QueryLinksForEmitterComponent` and `QueryLinksForReceiverComponent`, preserving exact component
  identity on Actors with multiple PuzzleSystem components;
- `TryGetLink` for the immutable relationship descriptor and `TryGetLinkState` for current state;
- `GetGraphTopologyRevision` for invalidating consumer-side topology caches.

All returned arrays use deterministic order: Controller path, authored primary binding index, link kind,
then gate or Receiver index. Registration order and pointer addresses do not affect semantic order.

### Link semantics

`PrimarySignal` represents one resolved primary input binding targeting one resolved Receiver. A primary
binding with three Receivers therefore creates three primary links. Its descriptor identifies the exact
primary Emitter, Controller, primary input and target Receiver.

`GateInfluence` represents one enabled gate input influencing one primary binding. It is created once per
gate input, independently of the number of Receivers, and its Receiver fields are intentionally null.
Unpaired gate arrays are runtime-bypassed and create no gate links because the Controller neither resolves
nor subscribes to them.

An Actor can participate in more than one role. `FPuzzleActorGraphView` preserves those roles separately:

- `OutgoingPrimaryLinks`: the Actor owns a primary Emitter endpoint;
- `OutgoingGateLinks`: the Actor owns a gate Emitter endpoint, including gate-only Actors;
- `IncomingPrimaryLinks`: the Actor owns a Receiver targeted by a primary link;
- `IncomingGateLinks`: one of the Actor's primary bindings is influenced by a gate link.

No relationship is deduplicated across Controllers. If two Controllers consume the same Emitter, their
links remain distinct because gate policy and effective result are Controller-local.

### Raw, gate, effective, Controller, and Receiver state

`FPuzzleGraphLinkState` deliberately keeps topology separate from runtime state:

```text
raw primary
    + aggregated gate mode/result
    -> effective primary
    -> Controller result
    -> Receiver prerequisite aggregation
    + Receiver Automatic/Manual policy
    -> Receiver effective state
```

Raw and effective primary snapshots expose validity, active state, revision, and a weak payload reference.
Gate state exposes `Bypassed`, `Open`, `Closed`, or `Invalid`, plus validity and admission result. A
`GateInfluence` additionally exposes the validity, active state, revision, and payload of its particular
gate input. Controller result is separate from the individual input result. On a `PrimarySignal`, Receiver
validity, activation mode, aggregated prerequisites, manual latch, and current effective state are also
available.

For example, one Emitter may feed `ControllerA` through an open gate and `ControllerB` through a closed
gate. Both links expose the same raw primary state, while their gate modes, effective primary states, and
Controller results differ. If both Controllers target the same Receiver, that Receiver can still report
effective active because `ControllerA` requests activation even though `ControllerB` does not.

Receiver effective state is read on demand by `TryGetLinkState`. Ordinary Receiver aggregation changes do
not independently emit graph state events, avoiding partial notifications during reentrant Controller
chains. Explicit manual commands refresh every retained link for that Receiver after the synchronous
notification chain settles, without changing topology revision. Receiver destruction emits invalidation
through the graph.

### Handles, revisions, events, and lifecycle

`FPuzzleGraphLinkHandle` is opaque and valid only in the current runtime world. It is not an array index,
save identifier, or authored identity and must not be persisted. Removing or reinitializing a Controller
makes only its old handles stale; `TryGetLink` and `TryGetLinkState` then return `false` for those handles.
A gameplay-invalid relationship is different: `TryGetLinkState` returns `true` and reports the relevant
validity flags as false.

`GraphTopologyRevision` starts at zero and increments once after each Controller add, remove, or structural
refresh. Raw signal, gate, effective, and Controller-result changes never increment it.

Bind `OnPuzzleGraphTopologyChanged` for completed index changes. The event includes the new revision, the
affected Controller, and `Added`, `Removed`, or `Refreshed`. Bind `OnPuzzleGraphLinkStateChanged` for
publicly observable per-link state changes; it supplies the handle and previous/new snapshots. Native C++
observers may bind the matching `OnPuzzleGraphTopologyChangedNative` and
`OnPuzzleGraphLinkStateChangedNative` delegates. A gate transition updates its gate link and all primary
links for that binding after Controller reentrancy has settled, without duplicate events when the public
snapshot is unchanged.

Destroyed primary Emitters, gate Emitters, and Receivers remain as contextual relationships while their
Controller stays registered. Their weak endpoint references and state validity flags become invalid, so
consumers can explain the broken connection without losing its handle. The relationship disappears when
the Controller is reinitialized, shuts down, ends play, or the world subsystem deinitializes. A transient
Controller without a `UWorld` continues to run normally but is not graph-indexed.

## Built-In Conditions

- `UPuzzleInputStateCondition`: checks whether one local input is active or inactive. Invalid input always fails.
- `UPuzzleAllCondition`: true only when every child condition is true. Empty child list evaluates false.
- `UPuzzleAnyCondition`: true when at least one child condition is true. Empty child list evaluates false.
- `UPuzzleNotCondition`: inverts one child condition. Missing child fails closed.
- `UPuzzleThresholdCondition`: true when at least `RequiredCount` child conditions are true. Impossible thresholds fail validation.

Create Blueprint subclasses of `UPuzzleCondition` for payload-dependent or project-specific rules. Custom conditions should query controller input state, validate payload class, cast to the expected payload subclass, and return a boolean without modifying world state.

Condition objects are edited inline in the Details panel. Composite child conditions are shown under their own child categories, and inline expansion is intentionally depth-limited to keep recursive condition trees stable in the editor. If a puzzle needs a very deep condition tree, split it into smaller controllers or reusable Blueprint condition subclasses.

## Actor Wiring Examples

Single pressure plate opens one door:

```text
PressurePlate Actor
  UPuzzleEmitterComponent publishes Puzzle.Signal.Pressed

PuzzleController
  InputBindings[Pressed] -> PressurePlate / Puzzle.Signal.Pressed
  RootCondition -> InputState(Pressed, active)
  ReceiverBindings -> Door Actor

Door Actor
  UPuzzleReceiverComponent OnReceiverActivated -> OpenDoor
```

Two generators must both be powered:

```text
Inputs: LeftGeneratorPowered, RightGeneratorPowered
RootCondition: All(InputState(LeftGeneratorPowered), InputState(RightGeneratorPowered))
```

At least two of three plates:

```text
RootCondition: Threshold RequiredCount=2
  InputState(PlateA)
  InputState(PlateB)
  InputState(PlateC)
```

## Actors With Multiple Puzzle Components

An Actor may own more than one emitter or receiver component. By default, the controller selects the first matching component in the Actor's component order. Use explicit selection when a different component is required.

Example:

```text
Actor: RuneMechanism
  UPuzzleEmitterComponent named RotationEmitter
  UPuzzleEmitterComponent named PowerEmitter

Controller InputBinding
  EmitterActor = RuneMechanism
  bSpecifyEmitterComponent = true
  EmitterComponentName = PowerEmitter
  SignalTag = Puzzle.Signal.Powered
```

`EmitterComponentName` is hidden and ignored while `bSpecifyEmitterComponent` is false. Receiver bindings use the equivalent `bSpecifyReceiverComponent` and `ReceiverComponentName` fields.

## Debugging

Runtime logging uses `LogPuzzleSystem`.

Console variables:

```text
PuzzleSystem.Debug 1
PuzzleSystem.Debug.Visual 0
```

`PuzzleSystem.Debug` enables verbose logging. `PuzzleSystem.Debug.Visual` is a global visual-debug kill switch and defaults to enabled; set it to `0` to suppress all PuzzleSystem debug drawing. Controller visual debug also requires the controller instance property `bEnableDebug` to be true. When enabled, the controller draws in PIE and editor viewports:

- cyan lines to emitter Actors it listens to;
- red lines and endpoint spheres to per-input gate Emitters;
- green lines to receiver Actors it controls;
- cyan and green endpoint spheres plus a yellow controller marker;
- a world-space label with raw primary state, gate mode (`Bypassed`, `Open`, `Closed`, or `Invalid`),
  gate-local states and payload classes, top-level condition results, effective state/revision, and final
  Controller result.

If one Actor is both a primary and gate source, the cyan and red relationships are drawn with a small
offset so both remain visible.

Debug visual output is disabled by default.

## Failure Behavior

The system fails closed. Invalid configuration or missing runtime state should make a controller evaluate inactive rather than accidentally activating receivers.

Common causes:

- duplicate `InputId`;
- duplicate gate-local `InputId` inside one primary binding;
- invalid signal tag;
- missing root condition;
- condition references an unknown input;
- enabled Gate Condition references an ID outside its containing `Emitter Gates` array;
- enabled gate condition is null, invalid, or not instanced under its Controller;
- duplicate Emitter/signal gate source inside one primary binding;
- enabled gate source is missing, destroyed, or has not published its required signal;
- binding Actor has zero matching components;
- explicit component selection is enabled but its component name is empty or does not match;
- duplicate receiver binding;
- threshold requires more passing children than it owns.
