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

### Controllers

Place an `APuzzleController` in the level for each boolean puzzle decision. A controller owns:

- `InputBindings`: local input IDs mapped to emitter Actor, optional emitter component name, and signal tag.
- `RootCondition`: one instanced `UPuzzleCondition` object, shown under `Puzzle | Logic` in the Details panel.
- `ReceiverBindings`: receiver Actor references and optional receiver component names.

Controllers use a `UBillboardComponent` as their root component so they are visible and easy to select in the level. The billboard is hidden in game and uses the plugin texture `/PuzzleSystem/Textures/T_PuzzleControllerIcon` when that asset is available. If the icon cannot be loaded, the controller keeps Unreal's default billboard sprite.

Conditions reference local `InputId` values, not level Actors.

The controller resolves component references during initialization:

- If the Actor has exactly one matching component, leave the component name empty.
- If the Actor has multiple matching components, set `EmitterComponentName` or `ReceiverComponentName`.
- Missing, ambiguous, duplicate, or invalid bindings fail closed and keep receivers inactive.

### Receivers

Add `UPuzzleReceiverComponent` to any gameplay Actor that should react to puzzle activation, such as a door, bridge, moving platform, trap, spawner, or checkpoint.

The generic receiver does not perform gameplay actions by itself. Bind to:

- `OnReceiverActivated`
- `OnReceiverDeactivated`
- `OnReceiverStateChanged`

The owning Actor decides what activation means, such as opening a door or disabling a trap.

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

An Actor may own more than one emitter or receiver component. In that case, component names are required in controller bindings.

Example:

```text
Actor: RuneMechanism
  UPuzzleEmitterComponent named RotationEmitter
  UPuzzleEmitterComponent named PowerEmitter

Controller InputBinding
  EmitterActor = RuneMechanism
  EmitterComponentName = PowerEmitter
  SignalTag = Puzzle.Signal.Powered
```

If the component name is omitted and multiple components match, the controller logs an error and fails closed.

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
- binding Actor has multiple matching components and no component name;
- duplicate receiver binding;
- threshold requires more passing children than it owns.
