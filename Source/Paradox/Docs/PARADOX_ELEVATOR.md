# Paradox Elevator

`AParadoxElevator` is a Blueprintable, self-triggered moving platform derived from
`AParadoxVerticalBarrier`. Its two positions are the inherited `StartArrow` and `EndArrow`. The
first valid button entry at Start sends the platform to End; the next valid entry at End sends it
back to Start. The inherited `PuzzleReceiver` remains Manual: the button requests activation only
after its full press. An optional PuzzleSystem Controller can provide additional prerequisites;
its request alone never starts a trip.

## Blueprint setup

1. Create a Blueprint child of `AParadoxElevator`. Assign a walkable, colliding platform mesh to
   `BarrierMesh` and the central button mesh to `ButtonMesh`. `ButtonMesh` defaults to no collision,
   but you can configure its collision in the Blueprint. The platform mesh supports passengers;
   `ButtonOccupancyVolume` remains the trigger that decides whether the button is pressed.
2. Place the inherited green `StartArrow` and red `EndArrow` at the two platform transforms. Start
   is the default initial position. The native End marker is 240 cm below Start; move either marker
   to fit the level. Set movement timing and easing on the inherited mover properties.
3. Fit `ButtonOccupancyVolume` to the central button, independently of `ButtonMesh`. It follows
   `BarrierMesh`, so the trigger does not descend when the visual button is pressed. Fit the
   inherited `PassageOccupancyVolume` to the full passenger area of the platform. The two volumes
   have different jobs and should not be merged.
4. Configure `RequiredButtonActorTags` for objects allowed to operate the button. All listed names
   must occur in the object's ordinary **Actor > Tags** array. Any `ACharacter` passes this tag
   filter. Use the `CanActorActivateButton` BlueprintNativeEvent to add a more specific acceptance
   rule if needed; call Parent if an intermediate Blueprint supplies policy.
5. Optionally connect a Pressure Plate or other PuzzleSystem emitter through a `PuzzleController`
   to the elevator's `PuzzleReceiver`. The Receiver remains in **Manual** activation mode. With no
   Controller registered, the default Manual fallback enables the button without starting
   movement at BeginPlay. Once a Controller is registered, its active request enables the button;
   an inactive request disables it. The button supplies the manual activation request after its full
   press. Selecting the elevator
   displays the incoming puzzle connection, and selecting the Plate displays its outgoing connection.
6. Set `PressDepth`, `PressDuration`, and `ReleaseDuration` for the visual button. Both durations
   may be zero. Assign inherited elevator travel sound and Niagara assets if desired.
7. Size the inherited stationary `GridNavigationModifier` around the shaft cells that must be
   blocked while the platform moves. `BarrierMesh` contributes walkable GridWorld geometry at both
   exact endpoints; give it query collision that blocks the collision profile used by the relevant
   `GridNavigationBoundsVolume`, include both heights in those bounds, and enable **Auto Rebuild On
   Geometry Changes**. Build and save GridWorld after configuring the Blueprint in an existing map.

The platform, button, and trigger are direct native components. The inherited selectable outlines
both meshes and shows PuzzleSystem connections. No extra selectable, Smart Object, or interaction
component is required.

## Button and travel behavior

The button arms when raised at an endpoint and its trigger becomes empty. While armed, the first
valid Actor in the trigger consumes that arm and starts pressing the button. The platform stays
stationary until the button reaches its full `PressDepth`. If every valid occupant leaves before
that point, the press is canceled: the button rises and no trip starts. This also applies when an
object is picked up or removed from the trigger, or when a registered Controller stops satisfying
the prerequisite. A fresh valid entry can press it again once the button is enabled. Multiple
primitive overlaps or additional Actors do not start another trip. If the movement request fails
after a complete press, the button rises and a fresh empty-to-occupied cycle is required after the
configuration problem is fixed.

The elevator keeps Actor Tick active while the button is pressing or releasing, even when the
platform itself is stationary. Tick stops once both the button animation and platform movement
are idle.

`IsButtonOccupied` reports any external Actor in the trigger, including objects that fail the tag
filter. Such an object cannot start a trip, but it prevents rearming until the volume is physically
empty. An empty `RequiredButtonActorTags` list accepts any non-Character Actor with a valid overlap.

Leaving the button during movement does not release the visual button or stop the platform. At the
destination, the elevator deactivates its Manual Receiver request and the button rises even if an
Actor is still on it. The trigger continues accepting
overlaps; it rearms as soon as it becomes completely empty at the endpoint, including while the
button is rising. A fresh valid entry then presses it again before the opposite trip starts. An
Actor left on the button must leave and enter again. An Actor leaving and returning during the
same trip also needs a post-arrival exit and re-entry.

`bWaitForClearPassage` is fixed off. The inherited lift policy acquires Characters and movable
objects inside `PassageOccupancyVolume`, including objects outside the central button. Characters
retain the barrier's source-owned Movement lock for the trip and are released at the endpoint.
Movable non-Character passengers are attached for transport and restored at the endpoint. A
rejected passenger produces the inherited lift-failure diagnostic.

The stationary GridWorld passage is unblocked at both endpoints and blocked during travel. The
platform mesh can generate floor cells only at stable endpoints. During movement its navigation
relevance is removed, so it does not rebuild navigation every frame.

`IsButtonOccupied`, `IsButtonPressed`, `IsButtonArmed`, `IsButtonEnabled`, and
`GetButtonMovementAlpha` are read-only
Blueprint queries. `RefreshButtonOccupancy` reconciles the button's local overlap cache and can be
called after scripted placement. `AParadoxPickupableActor` already calls it after Drop when its
authored world collider overlaps this button. Enable **Enable Authored World Collision** and a
query-capable overlap response on such pickupables; an Actor Tag alone cannot create an overlap.

## Reset, WorldState, and debugging

`ResetMover` restores the configured initial endpoint and raises the button. WorldState retains the
inherited authoritative mover state and derives the button presentation from it: a restored trip
has a pressed button, while a restored endpoint has a raised button. Neither path manufactures a
button entry or movement feedback. If the trigger is occupied after reset or restore, it remains
disarmed until an exit and a new entry.

Enable the instance's inherited `bEnableDebug` and the global `Paradox.VerticalBarrier.Debug 1`
console variable together. The elevator adds a button-volume box to the inherited travel and
passenger display while moving. Green means armed, orange occupied, and red disarmed. If the
elevator does not start, check that any connected Controller satisfies the Receiver prerequisites,
that the Receiver uses Manual mode, trigger collision, object Actor Tags, the optional acceptance
hook, and whether the platform is at a stable endpoint. If a Controller is connected, its request
must remain active until the button is completely down; losing it during travel does not interrupt
an ongoing trip.

For button diagnostics, filter the Output Log or `Saved/Logs/Paradox.log` for `[ElevatorButton]`.
`bLogButtonDiagnostics` is enabled by default in non-Shipping builds and can be disabled on each
elevator instance. It records `BeginPlay` with trigger collision and Receiver configuration,
`BeginOverlap`/`EndOverlap`, candidate acceptance and the first missing Actor Tag, reconciled
occupancy, Receiver prerequisite changes, press and release animation transitions, the manual
activation result, movement request decisions, and arrival. It does not log each animation or
movement Tick. If Blueprint reports an overlap but no native `BeginOverlap` appears, compare the
Blueprint's overlap source with `ButtonOccupancyVolume` and check for the `BeginPlay` line. If the
overlap appears but `enabled=0`, inspect `registeredControllers`, `activeRequests`, and
`prerequisites` in the same line. If `Candidate` says `accepted=0`, inspect
`firstMissingActorTag`; `None` with a valid overlap points to the `CanActorActivateButton` hook.
`PressBegin`, `PressAnimationComplete`, `ManualActivationResult`, `MovementRequest`, and
`JourneyStarted` show the remaining stages in order.

`Paradox.Elevator.*` automation tests cover the
button cycle, passengers, Drop, reset, and WorldState; verify the assigned meshes and GridWorld
bounds in PIE as well.
