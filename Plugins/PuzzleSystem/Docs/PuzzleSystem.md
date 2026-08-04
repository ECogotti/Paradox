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
the owned Receiver, queries its current effective state, and processes it without depending on Actor
`BeginPlay` order. With `bAnimateInitialReceiverState` disabled (the default), the logical result is snapped
without movement-start or endpoint-arrival presentation events. Enable it to process the initial state as a
normal runtime transition.

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

A receiver can be targeted by multiple controllers. It remains active while at least one valid controller requests active. When a controller ends play, its request is removed.

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
- green lines to receiver Actors it controls;
- cyan and green endpoint spheres plus a yellow controller marker;
- a world-space label with input IDs, signal tags, validity, active state, revisions, and final result.

Debug visual output is disabled by default.

## Failure Behavior

The system fails closed. Invalid configuration or missing runtime state should make a controller evaluate inactive rather than accidentally activating receivers.

Common causes:

- duplicate `InputId`;
- invalid signal tag;
- missing root condition;
- condition references an unknown input;
- binding Actor has zero matching components;
- explicit component selection is enabled but its component name is empty or does not match;
- duplicate receiver binding;
- threshold requires more passing children than it owns.
