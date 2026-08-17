# Paradox Vertical Barrier

`AParadoxVerticalBarrier` is the concrete Blueprintable vertical barrier in the Paradox runtime
module. It derives from `APuzzleTransformMover`: the inherited Receiver, timing, easing, movement
modes, pause/reversal rules, alpha, endpoint events, and reset remain authoritative.

Start always means fully raised, closed, and GridWorld-blocked. End always means fully lowered below
the passage, open, and traversable. Every partial, moving, or paused state blocks navigation. With
the default PingPong setup, activating the inherited Receiver lowers the barrier toward End and
opens the passage only after exact End arrival; deactivation raises it back toward Start.

## Blueprint setup

1. Create a Blueprint child of `AParadoxVerticalBarrier`.

The native Actor owns `SelectableComponent`, which outlines its direct `BarrierMesh` and may show
an optional world-space widget. A frame authored as a separate Actor is outside this ownership and
needs its own selectable component. Do not add a duplicate selectable component to the barrier
Blueprint. See [Selection and world-space interaction UI](SELECTION_AND_INTERACTION.md).

The native selectable enables `bShowPuzzleConnectionsWhenSelected`. Selecting the barrier renders
its incoming primary and gate relationships as Input wires through the shared
[Puzzle Circuit Overlay](PUZZLE_CIRCUIT_OVERLAY.md). No extra renderer component is required in the
Blueprint, and circuit presentation never drives the inherited Receiver.

Wire ports use `BarrierMesh` bounds by default. Add exactly one collision-disabled
`UBoxComponent` with Component Tag `WireTarget` when the desired terminal boundary should remain
independent from the moving door mesh. The overlay observes that box's transform without Tick.

It also owns one native `SmartObjectComponent` attached to `BillboardRoot` and one
`InteractionComponent`. Assign an engine-valid Smart Object Definition and configure any number of
Paradox `InteractionDefinitions` in the Blueprint defaults. A definition can match several slots,
and a slot can expose several matching definitions. Null Definition and empty catalog are valid
until content is authored and produce no interaction cells. The selectable already enables
`bShowInteractionCellsWhenSelected`; do not add duplicate Smart Object or interaction components.

Set the barrier's `PuzzleReceiver` to `Manual`, then create Open and Close Data Assets based on
`UParadoxReceiverInteractionActionDefinition`. Choose Activate/Deactivate and leave
`ReceiverComponentName` empty only when the Actor owns exactly one Receiver. The native Definition
already owns the correct schema, locks, journaling, and action class. Open is disabled until a
Controller satisfies the Receiver prerequisites; Close remains independent of those prerequisites.
The action claims a Smart Object slot before moving to the cheapest reachable exact cell, then runs
the Receiver command after authoritative revalidation.

2. Assign the moving door mesh to `BarrierMesh`; it is already movable and selected as the
   inherited moved component. Build the static frame on a separate Actor (for example, the Puzzle
   Emitter that controls this barrier). Leave `Generate Navigation At Stable Endpoints` disabled for
   an ordinary door. Enable it for an elevator or moving wall whose top must become a walkable
   navigation surface after reaching Start or End.
3. Place the inherited green `StartArrow` at the raised/closed transform and red `EndArrow` at the
   lowered/open transform below the floor. The native End default is 240 cm below Start. Keep
   `InitialPosition=Start` to begin closed or choose End to begin open.
4. Author passage shape only on `GridNavigationModifier`: set its relative transform and
   `BoxExtent`. `PassageOccupancyVolume` mirrors those values during construction, registration, and
   runtime initialization.
5. Keep the occupancy volume on a collision setup that generates overlaps for intended Characters
   and props. The native `Trigger` profile is the default.
6. Bind an `APuzzleController` output to the inherited `PuzzleReceiver`. The barrier never binds
   directly to Emitters.
7. Optionally assign direction-specific sounds and Niagara Systems. Empty assets never affect
   movement, navigation, occupancy, WorldState, or PerceptionKnowledge.

All native component references use `VisibleDefaultsOnly`, so placed instances expose tuning rather
than duplicate editable component-reference fields.

## Occupant filters and policies

`RequiredOccupantActorTags` uses ordinary `AActor::Tags` (`FName`), not Gameplay Tags. Add these in
the occupant Blueprint or placed Actor under **Actor > Tags**. Every configured name is required.
Collision channels remain the coarse filter, followed by the optional BlueprintNativeEvent
`CanActorOccupyPassage`.

`GetPassageOccupants` returns a copy of the distinct-Actor view and
`IsActorOccupyingPassage` provides the direct query. `RefreshPassageOccupants` inspects only the
volume's current overlaps and returns `EParadoxBarrierOccupancyRefreshResult`, distinguishing a
successful rebuild from an uninitialized Actor or missing native volume.

With `bWaitForClearPassage=true` (default), an occupied Start request (closing upward) is deferred
without changing the mover, alpha, latch, Tick, navigation, audio, or noise. Multiple primitive
overlaps from one Actor count once. The final component exit retries the still-valid request
event-driven. If an Actor enters during raising, the barrier returns to open End while remaining
blocked during motion, waits for clearance, then retries only if the original command remains valid.

With `bWaitForClearPassage=false`, current occupants are prepared before GridWorld is blocked and
movement starts. Paradox player and clone Characters have current Movement actions interrupted with
`GameplayAction.Result.Interrupted.Paradox.Barrier.Transport`; the barrier then owns an exact
`GameplayAction.Lock.Movement` on each Character scheduler. New click/action movement is rejected,
while camera, pause, UI, stance, and unrelated input remain available. Characters are not attached:
engine moving-base motion is preserved and a world-delta adapter applies only when the Character is
not based on `BarrierMesh`.

Movable non-Character Actors are attached to the moved component with world transform preserved.
Their previous parent/socket and any changed physics/gravity state are restored. Leaving the overlap
volume does not release a passenger. Release occurs at a stable endpoint, reset, WorldState restore,
moved-component replacement, invalidation, or EndPlay. Cleanup removes only locks owned by this
barrier and is idempotent.

An unsupported ordinary Character is stopped through its Controller/CharacterMovement when
possible, reports `MissingLocomotionLockOwner`, and is not recorded as a safely locked passenger.
The barrier continues with other occupants rather than fabricating a lock.

## GridWorld, WorldState, and perception

`UGridNavigationModifierComponent` remains the passage-blocking authority. It changes once per
semantic transition, using its normal overlay revision/invalidation path; the barrier never
enumerates path followers or rebuilds the grid every frame.

`Generate Navigation At Stable Endpoints` is disabled by default. In that mode, `BarrierMesh` keeps
**Can Ever Affect Navigation** off, so GridWorld format 8 excludes its physical collision from both
floor and clearance sampling. This prevents an ordinary raised door from creating floating base
cells or permanently removing the floor cells that its modifier must unblock.

Enable the option only when `BarrierMesh` is also a walkable platform. The mesh becomes
navigation-relevant at exact Start and End, is removed before any movement update, remains removed
during an intermediate pause, and is restored only after exact endpoint arrival. Each traversal
therefore produces an endpoint removal and endpoint addition through Unreal's native dirty-area
pipeline instead of one rebuild per movement frame. For the endpoint surface to produce GridWorld
cells:

- `BarrierMesh` must have query collision and block the collision profile selected on the
  intersecting `GridNavigationBoundsVolume`;
- the bounds volume must include the top surface at both endpoint heights;
- **Auto Rebuild On Geometry Changes** must be enabled on that bounds volume;
- `GridNavigationModifier` must cover the passage without overlapping the top cells that should
  remain walkable while the passage itself is blocked.

The modifier auto-activates in Game/PIE, while GridWorld also composes its construction-time state
in editor worlds so **Show Navigation** reflects the authored passage state before Begin Play.

After upgrading an existing level, run **Build > Grid World > Build Grid World** once and save the
level. Format 7 snapshots are deliberately rejected because they may contain topology baked from
navigation-irrelevant moving geometry.

After enabling stable endpoint navigation on an existing Blueprint, rebuild and save GridWorld once
with the barrier at its authored initial endpoint. Runtime generation then handles later endpoint
transitions.

WorldState captures one complete `FPuzzleTransformMoverRuntimeState` property. Pre-restore clears
pending requests, occupants, passengers, attachments, locks, feedback, and stale callbacks while
blocking navigation conservatively. Property restoration rebuilds the exact moved transform from
the endpoint markers and alpha. After the global terminal callback, overlap state is reconciled once
without acquiring passengers or producing feedback.

The Perception source exposes:

- `PerceptionKnowledge.State.Paradox.Barrier.Open`;
- `PerceptionKnowledge.State.Paradox.Barrier.BlockingPassage`;
- `PerceptionKnowledge.State.Paradox.Barrier.Moving`;
- `PerceptionKnowledge.State.Paradox.Barrier.WaitingForClearance`;
- `PerceptionKnowledge.State.Paradox.Barrier.TransportingOccupants`.

These observable-state tags are intentionally separate from the interaction commands authored in
the Door catalog and UI: `Interaction.Paradox.Barrier.Open` and
`Interaction.Paradox.Barrier.Close`. A state tag must not be reused as an interaction identity.

Raise, lower, and optional endpoint impact noises use the registered
`PerceptionKnowledge.Event.Paradox.Noise.Barrier.*` tags. Construction, deferred requests, reset, and restore
never emit movement noise.

## Blueprint events and debugging

Barrier-specific BlueprintNativeEvents cover occupancy, deferral/cancellation, clearance,
navigation, passenger preparation/failure/completion, and batch prepare/release. Native correctness
runs first, the Blueprint hook second, and the matching multicast delegate last. Inherited mover
events follow the same native-hook/Blueprint/delegate order.

Enable local `bEnableDebug` and global `Paradox.VerticalBarrier.Debug 1` together. During active
movement the overlay shows shared bounds, navigation/pending policy, occupants, passengers, mover
state, alpha, restore guard, and the last structured diagnostic. Disabled debug performs no Actor
enumeration or drawing.

Editor validation permits the native empty interaction catalog and null Smart Object Definition.
Once content is added, it reports a missing direct Smart Object component, invalid or duplicate
interaction tags, a missing/unloadable Gameplay Action Definition, an incompatible interaction
action class, disabled journaling, incorrect required Property Bag fields, or a non-empty catalog
without a Smart Object Definition.

## PIE transport validation

`Paradox.VerticalBarrier.PIEMovingBaseCharacterTransport` runs a Paradox player Character on a
colliding engine test mesh in a PIE world at 60 Hz. It verifies engine moving-base transport, the
barrier-owned Movement lock, exact Start arrival, and lock cleanup. Production Blueprint mesh and
collision settings must still be checked in an interactive PIE session:

1. place a Character inside the passage and start a long click-to-move action;
2. raise in lift mode and confirm the action becomes `Interrupted` with `ByBarrierLift`;
3. verify upward motion is continuous, the Character is not attached, and camera/UI/pause remain
   usable;
4. issue movement during the lift and confirm it is rejected;
5. leave the overlap while still rising and confirm the lock persists to the endpoint;
6. at Start and after reset/destruction, confirm movement is unlocked and the old destination is not
   resumed automatically;
7. repeat with a replay clone and confirm its immutable Replay Track is unchanged;
8. repeat with an attached prop and a physics prop, confirming parent, simulation, and gravity are
   restored.
