# Pressure Plate

`APressurePlate` is the concrete, Blueprintable Paradox pressure plate. It converts physical
overlaps into the inherited `APuzzleSwitch` input state, publishes the configured PuzzleSystem
signal, animates a visual plate, optionally produces audio/Niagara feedback and semantic Hearing
noise, and participates in WorldState restoration.

The class works without a Blueprint override. A Blueprint child is needed only to assign content,
adjust collision, or specialize the provided events.

## Blueprint setup

1. Create a Blueprint class derived from `APressurePlate`.
2. Assign the static support mesh to `FloorMesh` and the moving surface to `PlateMesh`.
3. Adjust `OccupancyVolume` extent, relative transform, and collision responses so only intended
   object channels overlap it.
4. Set inherited `InitialInputState` to `Released` or `Pressed`. The Pressure Plate derives its
   initial output from this setting, so inherited `bStartActive` is intentionally read-only here.
5. Configure `PressDepth`, `PressDuration`, `ReleaseDuration`, and optionally `MovementCurve`.
6. Keep the inherited switch `Mode` on `Hold` for the normal pressure-plate behavior. The inherited
   delay, pulse, latch, start-state, signal, and debug settings remain available when another policy
   is intentionally required.
7. Assign optional movement sounds or Niagara systems. Direction-specific assets override the asset
   authored directly on `MovementAudio` or `MovementVFX`.

The default output tag is `Puzzle.Signal.Pressed`. Connect a normal `APuzzleController` to this tag;
the pressure plate never drives receivers directly.

`BillboardRoot` is the actual root; the optional generic `APuzzleSwitch` scene root is deliberately
not constructed for this native subclass. `FloorMesh` is the stationary support. `PlateMesh` moves only along its authored local negative Z
axis and never affects navigation. `OccupancyVolume` is attached to the floor, not the moving mesh,
so the detector does not leave an occupant while the plate descends.

## Occupancy rules

The first accepted Actor is the single logical occupant. Multiple overlapping components owned by
that Actor are tracked as one occupant, so leaving with one foot or collision shape does not release
the plate while another accepted component still overlaps.

A second Actor does not create another `Press` edge. If the current Actor leaves or is destroyed,
the plate first looks only at the volume's current overlaps and transfers ownership directly to one
valid replacement when available. An `Occupied -> Occupied` replacement does not emit an
intermediate `Release` or `Press`.

`RequiredOccupantActorTags` uses ordinary `AActor::Tags` (`FName`) with ALL semantics. To configure
an occupant, select its Blueprint Class Defaults or a placed instance, expand **Actor → Tags**, add
an entry, and type the same name used by the pressure plate filter—for example `Heavy`. No Gameplay
Tag interface or component is required. An empty filter accepts every otherwise valid Actor.

The protected BlueprintNativeEvent `CanOccupantActivatePlate` runs after native validity and Actor
Tag checks and can add project-specific acceptance rules. Its native implementation accepts the
candidate, so a Blueprint child with no override remains fully functional.

## Initial condition and overlap reconciliation

`InitialInputState` is applied at runtime initialization and every `ResetSwitch`, including the reset
phase used by WorldState. The editor debug view reports this configured state. For the pressure plate:

- `Released` initializes input/output released and the visual plate raised;
- `Pressed` initializes input/output pressed and the visual plate down;
- after initialization completes, `OccupancyVolume` explicitly refreshes its overlap cache;
- a valid overlapping Actor that passes collision, Actor Tags, and `CanOccupantActivatePlate`
  preserves `Pressed`;
- without a valid occupant, the class performs a normal inherited `Release`, including
  `ReleaseDelay` when configured.

The same local box refresh is performed after manual and WorldState resets. It never searches the
world and reset reconciliation remains free of audio, Niagara, and semantic noise.

## Movement and feedback

The normalized presentation value is exposed by `GetPlateMovementAlpha`:

```text
0.0 = authored raised transform
1.0 = authored transform minus PressDepth on local Z
```

Movement is derived from the inherited switch output, not directly from overlaps. Delays and other
switch modes therefore remain authoritative. Mid-motion reversals start from the current alpha
without a transform jump, and their duration is scaled by the remaining distance. Zero duration or
zero depth snaps safely. Actor Tick is used only while movement or requested debug drawing needs it.

One meaningful movement start may play the selected sound, activate the selected Niagara system,
and emit one PerceptionKnowledge semantic Hearing event. Defaults are:

- press event: `PerceptionKnowledge.Event.Noise.PressurePlate.Press`;
- release event: `PerceptionKnowledge.Event.Noise.PressurePlate.Release`;
- cause: `PerceptionKnowledge.Cause.PressurePlate.Movement`.

Use `bEmitNoiseOnPressMovement` and `bEmitNoiseOnReleaseMovement` independently. Loudness and max
range control native Hearing; strength is retained in semantic observations. Initialization and
WorldState restoration never play movement feedback or emit noise.

## Blueprint extension points

The class exposes protected BlueprintNativeEvents for internal specialization without delegate
binding in `BeginPlay`:

- `CanOccupantActivatePlate`;
- `HandleOccupantAccepted`;
- `HandleOccupantReleased`;
- `HandleOccupantReplaced`;
- `HandleOccupancyStateChanged`;
- `HandlePlateMovementStarted`;
- `HandlePlateMovementCompleted`.

The inherited `APuzzleSwitch` BlueprintNativeEvents still report raw input, delay, activation,
deactivation, and reset boundaries. Its matching public multicast delegates are notifications for
external observers and always run after the internal native/Blueprint event hook.

Blueprint overrides are presentation or policy hooks. They must not publish the puzzle signal,
maintain occupant component sets, or write the plate transform. Call Parent when preserving native
or intermediate Blueprint behavior is required.

## Public queries and operations

- `GetOccupancyState` and `IsOccupied` report physical ownership.
- `GetCurrentOccupant` returns the weakly tracked Actor or null.
- `RefreshOccupantFromVolume` explicitly updates the detector's local overlap cache, applies Actor
  Tag filtering, and selects at most one Actor.
- `GetPlateMovementAlpha` reports visual travel.
- `IsPlateMoving` reports active interpolation.
- inherited `ResetSwitch` clears transient occupancy, restores the switch start state, snaps visual
  presentation, then performs one feedback-suppressed reconciliation of current overlaps.

C++ callers include the class with:

```cpp
#include "Puzzles/PressurePlate.h"
```

## WorldState behavior

`WorldStateParticipant` captures the inherited authoritative `bIsActive` property. Reset first
restores `InitialInputState` and its derived pressure-plate output, then the post-restore box refresh
allows current physical occupancy and Actor Tags to preserve or normally release that state. Actor transform,
occupant pointers, overlap components, timers, active feedback, and interpolation progress are not
snapshot authority. Before restore, the class clears transient state and cancels work. After restore,
it republishes the restored signal only when necessary, reconstructs the plate transform from the
semantic state, and reconciles the detector once without feedback.

Do not add the moving `PlateMesh` transform as a second authoritative captured property.

## Debugging and validation

Enable the instance's inherited `bEnableDebug` setting and then enable the module gate:

```text
Paradox.PressurePlate.Debug 1
```

The plate draws its detector, occupant relationship, Actor Tag filter result, current/initial switch state, movement alpha,
pending delay, reset guard, and noise configuration. The inherited PuzzleSwitch diagnostics use the
separate `PuzzleSystem.Debug.Visual` global gate.

Editor validation reports invalid hierarchy, navigation relevance, overlap configuration, negative
movement values, invalid signal/noise tags, and missing integration components. If the plate does not
activate, first check the volume's collision responses, the candidate Actor's **Actor → Tags**, and any
Blueprint `CanOccupantActivatePlate` override. Optional sound and Niagara assets may remain unset;
occupancy and signal publication still work.
