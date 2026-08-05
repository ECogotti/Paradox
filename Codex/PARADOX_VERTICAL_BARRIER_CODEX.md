# Paradox Vertical Barrier — Codex Implementation Specification

> **Endpoint clarification (authoritative):** the production door retracts downward. `Start` is the
> raised/closed/blocking endpoint and `End` is the lowered/open/traversable endpoint below the
> floor. Any later statement in this original specification that assigns the opposite endpoint
> meaning is superseded by this clarification. Raising/occupant safety therefore applies while
> moving toward `Start`; Receiver activation in the default PingPong setup moves toward open `End`.

This file defines the project-specific vertical door / rising-wall Actor built on top of the existing PuzzleSystem activator template.

It supplements, and must be read together with:

- the root `AGENTS.md`;
- `PUZZLE_SYSTEM_ARCHITECTURE.md`;
- `PuzzleSystemDoc.md`;
- `PUZZLE_ACTIVATOR_TEMPLATES.md`;
- the existing `GridWorld` documentation and local `CODEX` files;
- the existing `WorldState` documentation and local `CODEX` files;
- the existing `PerceptionKnowledge` documentation and local `CODEX` files;
- the existing `GameplayActions`, player click-to-move, clone movement, and path-following documentation when those systems are involved;
- the local `CODEX` and `Docs` files of the Paradox runtime gameplay module that owns project-specific integration.

Do not duplicate or redesign rules already established by those documents.

The conceptual names in this specification describe required responsibilities. Codex must inspect and use the actual class names, APIs, delegates, Gameplay Tags, module names, and lifecycle hooks implemented in the repository.

---

# PURPOSE

Implement one reusable native project Actor:

```text
AParadoxVerticalBarrier : public APuzzleTransformMover
```

`AParadoxVerticalBarrier` represents a vertically retractable gameplay barrier such as:

```text
rising door
rising wall
floor-emerging partition
security bulkhead
vertical gate
retractable passage blocker
```

The class must:

- use the complete inherited `APuzzleTransformMover` state machine and Receiver integration;
- treat the inherited Start endpoint as the fully lowered/open position;
- treat the inherited End endpoint as the fully raised/closed position;
- use the existing `UGridNavigationModifierComponent` from GridWorld to open and close the affected grid passage;
- keep the affected grid region blocked whenever the barrier is raised, moving, paused mid-travel, or otherwise not completely lowered;
- use a box overlap to track every Actor currently occupying the affected passage;
- expose an editor flag that chooses between waiting for a clear passage and lifting current occupants;
- when waiting is enabled, defer raising until the passage is free;
- when waiting is disabled, raise normally while moving all detected occupants upward through project-appropriate handling;
- temporarily suppress locomotion authority for player and clone Characters while they are being lifted;
- attach eligible non-Character Actors to the moving barrier component for the duration of the lift;
- restore every temporary movement lock, action interruption, attachment, and physics override safely;
- publish project-appropriate semantic state and movement noise through the existing PerceptionKnowledge integration;
- participate correctly in the existing WorldState capture and restore lifecycle;
- remain fully usable as a Blueprint parent;
- contain no hard-coded mesh, sound, Niagara, tag, player-class, clone-class, or content asset references.

The Actor remains a Receiver-driven gameplay object.

The normal puzzle flow must remain:

```text
UPuzzleEmitterComponent
    -> APuzzleController
    -> inherited UPuzzleReceiverComponent
    -> APuzzleTransformMover policy
    -> AParadoxVerticalBarrier project integration
```

`AParadoxVerticalBarrier` must never:

- subscribe directly to puzzle Emitters;
- evaluate Controller conditions;
- bypass source-aware Receiver requests;
- place GridWorld, WorldState, PerceptionKnowledge, player, or clone knowledge inside the generic PuzzleSystem plugin.

---

# MODULE OWNERSHIP

Create this class inside the existing Paradox runtime gameplay module.

The architecture overview refers conceptually to that module as `ParadoxGameplay`. Codex must identify the actual module name in the repository and use it.

Do not create a new module or plugin only for this Actor.

Do not place the class inside:

```text
PuzzleSystem
GridWorld
WorldState
PerceptionKnowledge
GameplayActions
IntentReplay
```

The project module may depend on all required generic plugins. None of those plugins may acquire a dependency on the project module.

Suggested paths, adapted to the existing module structure:

```text
Source/<ParadoxRuntimeModule>/Public/Puzzles/ParadoxVerticalBarrier.h
Source/<ParadoxRuntimeModule>/Private/Puzzles/ParadoxVerticalBarrier.cpp
Source/<ParadoxRuntimeModule>/Docs/PARADOX_VERTICAL_BARRIER.md
```

If the module intentionally keeps project-only Actor headers private, follow that convention instead of reorganizing unrelated code.

---

# REQUIRED INVESTIGATION BEFORE IMPLEMENTATION

Before writing code, Codex must:

1. read the root `AGENTS.md`;
2. find and read every relevant local `CODEX` folder before touching each module or plugin;
3. read the relevant human-facing `Docs` folders;
4. inspect the real implementation and public/protected API of `APuzzleTransformMover`;
5. inspect the real `UPuzzleReceiverComponent` transition API;
6. inspect the existing `UGridNavigationModifierComponent`, including:
   - how its bounds are authored;
   - whether it consumes a shape component;
   - how traversable/non-traversable state is changed;
   - how it updates affected cells and revisions;
   - whether it already exposes runtime state-change events;
7. inspect the existing GridWorld path invalidation and `UGridPathFollowingComponent` behavior;
8. inspect the WorldState participant component/interface and exact restore lifecycle phases;
9. inspect existing project Actors that rebuild derived collision/navigation state after WorldState restore;
10. inspect the PerceptionKnowledge semantic state and noise publication APIs;
11. inspect the player click-to-move owner and determine how movement requests are cancelled and temporarily blocked;
12. inspect the clone movement/action owner and determine how a running movement action is interrupted and how new locomotion actions are temporarily prevented;
13. inspect the actual player and clone identification convention;
14. inspect existing movement-lock, execution-lock, resource-lock, or source-token patterns before adding another one;
15. inspect Character moving-base behavior and the exact transform movement API used by `APuzzleTransformMover`;
16. inspect attachment and physics conventions for movable gameplay Actors;
17. inspect module logging, Gameplay Tag, debug, audio, Niagara, and Details-panel conventions;
18. inspect the exact Unreal Engine headers for every unfamiliar navigation, CharacterMovement, attachment, collision, delegate, and component API;
19. preserve Blueprint and serialized-data compatibility;
20. compile the affected target after every meaningful implementation stage.

Do not invent API names for any project or engine system.

When an existing public API already provides the required behavior, reuse it.

When a required capability is genuinely missing, add the smallest safe extension to the module that owns that responsibility:

- generic mover gating or runtime-state restoration belongs in `PuzzleSystem` only when it is generic;
- dynamic cell blocking belongs in `GridWorld`;
- click-to-move locking belongs in the player movement owner;
- clone locomotion interruption/locking belongs in the action or clone movement owner;
- project-specific coordination belongs in the Paradox runtime module.

Do not solve a missing API through private-member access, world searches, hard-coded casts, or duplicated state machines.

---

# REQUIRED CLASS ROLE

`AParadoxVerticalBarrier` is a concrete Paradox gameplay Actor and a reusable Blueprint parent.

It is not a new PuzzleSystem architectural role.

It specializes the generic mover through composition and protected extension points:

```text
APuzzleTransformMover
    owns Receiver state, movement modes, timing, easing, alpha, direction, pause, reversal, and endpoint transitions

AParadoxVerticalBarrier
    owns concrete barrier components, passage occupancy, GridWorld state, occupant handling, PerceptionKnowledge, and WorldState integration
```

Do not copy, fork, or shadow the inherited mover state machine.

---

# REQUIRED COMPONENT HIERARCHY

The final native hierarchy must be equivalent to:

```text
AParadoxVerticalBarrier : APuzzleTransformMover
├── inherited UBillboardComponent                 BillboardRoot                [Root]
├── inherited UArrowComponent                     StartArrow
├── inherited UArrowComponent                     EndArrow
├── inherited UPuzzleReceiverComponent            PuzzleReceiver
├── UStaticMeshComponent                          FrameMesh                    [attached to BillboardRoot]
├── UStaticMeshComponent                          BarrierMesh                  [attached to BillboardRoot]
│   ├── UAudioComponent                           MovementAudio                [attached to BarrierMesh]
│   └── UNiagaraComponent                         MovementVFX                  [attached to BarrierMesh]
├── UBoxComponent                                 PassageOccupancyVolume       [attached to BillboardRoot]
├── UGridNavigationModifierComponent              GridNavigationModifier
└── existing WorldState / PerceptionKnowledge capability required by real APIs
```

The WorldState and PerceptionKnowledge integrations may use:

```text
Actor Components
interfaces
subsystems
project adapter components
delegates
```

Use the existing implementation pattern. Do not create duplicate integration components when the Actor can use an established public API directly.

---

# INHERITED ENDPOINT SEMANTICS

For this class, the inherited endpoints have fixed gameplay meaning:

```text
StartArrow
    = fully lowered barrier
    = passage open
    = passage navigable
    = MovementAlpha 0
    = EPuzzleTransformMoverState::AtStart

EndArrow
    = fully raised barrier
    = passage closed
    = passage non-navigable
    = MovementAlpha 1
    = EPuzzleTransformMoverState::AtEnd
```

State mapping:

```text
AtStart
    -> Open
    -> Traversable

MovingTowardEnd
    -> Closing / Raising
    -> Non-traversable

AtEnd
    -> Closed / Raised
    -> Non-traversable

MovingTowardStart
    -> Opening / Lowering
    -> Non-traversable until AtStart is reached
```

A barrier paused or blocked at a partial alpha remains non-traversable.

A raise request deferred while the barrier is still exactly `AtStart` remains traversable until physical movement actually begins.

Do not make endpoint meaning designer-invertible inside this class.

When a puzzle requires active Receiver state to open the passage instead of closing it, invert the puzzle condition in `APuzzleController` rather than introducing two competing endpoint conventions.

---

# STATIC MESH COMPONENTS

## `FrameMesh`

`FrameMesh` represents static surrounding geometry such as:

```text
door frame
floor slot
wall guides
bulkhead housing
static decorative structure
```

Required defaults:

- attached to `BillboardRoot`;
- static mobility unless the established Actor pattern requires otherwise;
- no runtime movement from barrier logic;
- designer-assignable mesh and materials;
- no hard-coded asset;
- normal collision and navigation relevance remain designer-configurable.

## `BarrierMesh`

`BarrierMesh` is the default moved component supplied to the inherited `APuzzleTransformMover`.

Required defaults:

- attached to `BillboardRoot`;
- movable mobility;
- designer-assignable mesh and materials;
- collision configured through normal component settings;
- must not directly affect GridWorld or Unreal navigation every movement frame;
- no dynamic navigation rebuild from the visual mesh;
- no accumulated transform offsets;
- no hard-coded asset.

During controlled initialization, register `BarrierMesh` through the inherited moved-component API rather than bypassing mover validation.

The class may still permit runtime replacement through the inherited `SetMovedComponent()` API when that feature is valid for this Actor.

When the moved component is replaced:

```text
release all carried occupants safely
remove every movement/input/action lock owned by this barrier
clear passenger records
allow the inherited mover to replace and synchronize the component
rebind any component-specific feedback origin if required
revalidate collision and GridWorld configuration
```

Do not silently transfer passenger attachments from one moved component to another.

---

# GRID NAVIGATION MODIFIER

Use the existing GridWorld component:

```text
UGridNavigationModifierComponent
```

Do not create:

```text
a second GridWorld modifier class
a door-specific GridWorld subsystem
a custom navigation grid inside this Actor
an Unreal NavMesh replacement path
per-frame cell rebuild logic
```

`GridNavigationModifier` is the authoritative mechanism that marks the passage cells traversable or non-traversable.

## Shared bounds rule

The overlap region and GridWorld modifier must describe the same authored passage region.

There must be one authoritative bounds source.

Codex must inspect the actual `UGridNavigationModifierComponent` design and use the correct existing pattern:

### When the modifier accepts a shape component

Use `PassageOccupancyVolume` as the shared bounds source for both:

```text
PassageOccupancyVolume
    -> overlap queries
    -> GridNavigationModifier bounds
```

### When the modifier owns explicit bounds configuration

Synchronize `PassageOccupancyVolume` from the modifier's real bounds through construction/editor-safe logic and validate that they remain equivalent.

Do not expose two unrelated editable extents that can silently diverge.

### When the modifier uses another established bounds-provider interface

Implement or reuse that interface through `PassageOccupancyVolume` without duplicating GridWorld logic.

The final design must allow a designer to author the passage region once.

## Navigation state rule

Centralize navigation state in one operation equivalent to:

```text
UpdateGridNavigationStateFromBarrierState()
```

Required result:

```text
AtStart and not physically travelling
    -> GridNavigationModifier = Traversable

MovingTowardEnd
MovingTowardStart
AtEnd
paused partial movement
blocked partial movement
restored partial movement
    -> GridNavigationModifier = NonTraversable
```

A pending raise request while still at `AtStart` remains traversable.

## Ordering when raising

The required order is:

```text
validate movement request
reconcile occupancy
resolve occupant policy
prepare occupant locks/attachments when lifting
mark GridNavigationModifier non-traversable
allow GridWorld to update affected cells/revision
start inherited movement toward End
```

Grid blocking must occur before the first physical barrier movement step.

If preparation fails critically before movement starts, revert only the temporary locks/attachments acquired by that failed attempt and do not leave the grid blocked accidentally.

## Ordering when lowering

```text
request inherited movement toward Start
keep GridNavigationModifier non-traversable for the complete movement
reach exact AtStart endpoint
update GridNavigationModifier to traversable
allow affected path queries to observe the new revision
```

Do not make the passage navigable early based on alpha threshold.

## Path invalidation responsibility

Use the modifier's normal GridWorld update path.

The Actor must not:

- enumerate all AI agents;
- call player or clone path-following components directly to announce a grid revision;
- rebuild the whole grid;
- recalculate paths every Tick;
- poll for path changes.

Existing GridWorld revision and path invalidation mechanisms remain authoritative.

---

# PASSAGE OCCUPANCY VOLUME

`PassageOccupancyVolume` is always enabled for query overlap, regardless of occupant policy.

The flag described later changes the response to occupants. It never disables overlap detection or occupant counting.

Required defaults:

- attached to `BillboardRoot`, not to `BarrierMesh`;
- query-only collision;
- overlap generation enabled;
- no physics simulation;
- no direct navigation relevance;
- hidden in game unless project debug requires otherwise;
- designer-authored through the shared GridNavigationModifier bounds contract;
- collision object responses configurable through the normal Details panel;
- no world searches or permanent polling.

The volume represents:

```text
the passage cells affected by GridNavigationModifier
+
the clearance region that must be checked before a safe raise
+
the Actor-acquisition region used by lift mode
```

Do not attach it to the moving barrier or its overlap population will move away from the passage.

---

# AUTHORITATIVE OCCUPANT TRACKING

The barrier tracks every distinct overlapping Actor, not every overlap callback.

Use lifecycle-safe state equivalent to:

```text
TMap<TWeakObjectPtr<AActor>, FBarrierOverlapRecord> OverlappingActors
```

Conceptual record:

```text
FBarrierOverlapRecord
    TSet<TWeakObjectPtr<UPrimitiveComponent>> Components
```

Required semantics:

- one Actor counts once even when several of its components overlap;
- duplicate begin-overlap notifications for one component do nothing;
- one component ending overlap does not remove the Actor while another component remains;
- invalid/destroyed Actors are pruned safely;
- the barrier itself is never counted;
- `BarrierMesh`, `FrameMesh`, inherited helper components, and child components owned by this Actor are never counted as external occupants;
- Actor attachment to `BarrierMesh` does not make the barrier count its own passenger repeatedly;
- no raw persistent Actor pointer is authoritative;
- no per-frame overlap polling;
- no global Actor search.

Required queries:

```text
GetPassageOccupantCount() -> int32
IsPassageOccupied() -> bool
GetPassageOccupants() -> array of Actor references
IsActorOccupyingPassage(AActor*) -> bool
RefreshPassageOccupants() -> structured success/result
```

`RefreshPassageOccupants()` must inspect only the current overlaps of `PassageOccupancyVolume`, rebuild/prune records deterministically, and avoid a world search.

Expose occupant collections read-only.

Do not expose mutable access to the internal maps or component sets.

## Occupant filter

Provide a protected Blueprint/C++ extension point equivalent to:

```text
CanBarrierHandleOccupant(
    AActor* OccupantActor,
    UPrimitiveComponent* OccupantComponent
) -> bool
```

Native default must reject at least:

```text
null or invalid Actor
this Actor
components owned by this Actor
Actors already being destroyed
Actors explicitly excluded through existing project convention
```

The volume's collision channels remain the coarse filter.

Do not hard-code only player and clone Characters. Movable props and future Actor types must be accepted when their collision channel permits it.

---

# OCCUPANT POLICY

Expose one designer-facing flag:

```text
bool bWaitForClearPassage = true
```

Required semantics:

```text
bWaitForClearPassage = true
    -> safe door policy
    -> raising is deferred while one or more valid Actors occupy the passage

bWaitForClearPassage = false
    -> lifting barrier policy
    -> overlap and occupant count still run
    -> raising starts after occupants are prepared for transport
    -> Characters have locomotion authority temporarily blocked
    -> eligible non-Character Actors are attached to BarrierMesh
```

Native default:

```text
bWaitForClearPassage = true
```

Typical content configuration:

```text
Door
    bWaitForClearPassage = true

Rising wall / partition
    bWaitForClearPassage = false
```

Do not replace this flag with collision-channel tricks or disable the overlap volume when false.

---

# SAFE POLICY — WAIT FOR CLEAR PASSAGE

When `bWaitForClearPassage` is true, a request toward End must pass a clearance gate before the inherited movement begins.

Required transient state:

```text
bool bRaiseRequestPending
```

This flag is orthogonal to `EPuzzleTransformMoverState`.

A deferred raise while the barrier remains lowered must keep:

```text
State = AtStart
MovementAlpha = 0
GridNavigationModifier = Traversable
movement Tick disabled
```

## Initial request flow

```text
request movement toward End
    -> RefreshPassageOccupants()
    -> OccupantCount == 0?
```

When clear:

```text
clear bRaiseRequestPending
mark GridNavigationModifier non-traversable
start inherited movement toward End
```

When occupied:

```text
set bRaiseRequestPending
remain AtStart
remain traversable
emit one deferred notification
update PerceptionKnowledge state without movement noise
```

Repeated End requests while the same raise is already pending must be deduplicated.

Do not replay the deferred event every overlap callback or every frame.

## Automatic retry

When the last valid Actor leaves:

```text
if bRaiseRequestPending
and the original End request is still semantically valid
and the Actor is not restoring WorldState
and the moved component is valid
    -> clear pending state
    -> mark grid non-traversable
    -> start inherited movement toward End
```

Do not retry through Tick or a repeating timer.

## Pending-request cancellation

Cancel `bRaiseRequestPending` when:

```text
a valid request toward Start supersedes it
PingPong Receiver state no longer commands End
ResetMover or WorldState restore begins
MovedComponent becomes invalid or changes
Actor EndPlay begins
configuration becomes invalid
```

Latch and FlipFlop cancellation semantics must follow the inherited mover's actual command state. Do not infer intent only from current Receiver bool when the movement mode defines different semantics.

## Actor enters during raising

A new valid occupant may enter after the initial clearance check.

In safe mode:

```text
BeginOverlap while MovingTowardEnd
    -> mark raise request pending if End is still desired
    -> request reversal toward Start through inherited protected API
    -> keep grid non-traversable during return
    -> release no passengers because safe mode did not acquire any
    -> reach AtStart
    -> mark grid traversable
    -> wait for the volume to become free
    -> retry End only when the request is still valid
```

Do not stop permanently on top of the Actor.

Do not teleport the occupant.

Do not use `EPuzzleTransformMoverDeactivationBehavior` to model this safety reversal. Receiver deactivation policy and physical obstruction policy are separate concerns.

---

# LIFT POLICY — RAISE WITH OCCUPANTS

When `bWaitForClearPassage` is false, occupants do not defer the End request.

The barrier must still:

- refresh overlap state;
- count distinct Actors;
- classify each Actor;
- prepare Characters and other Actors before physical movement;
- expose failures visibly;
- clean up every acquired temporary state.

Required sequence:

```text
request movement toward End
    -> RefreshPassageOccupants()
    -> prepare every current occupant
    -> mark GridNavigationModifier non-traversable
    -> start inherited movement toward End
```

A failure to prepare one non-critical occupant must not crash the Actor or silently corrupt other occupants.

Use a structured result and one warning/event for the failed Actor.

Whether a preparation failure cancels the entire raise or allows the barrier to continue must be an explicit native policy. For the initial implementation, prefer:

```text
continue raising after reporting the failed occupant
```

unless project safety conventions require fail-closed behavior.

Document the chosen behavior.

---

# PASSENGER STATE MODEL

Keep overlap population separate from occupants acquired for the active lift.

Required transient collection:

```text
TMap<TWeakObjectPtr<AActor>, FBarrierPassengerRecord> LiftedActors
```

Distinction:

```text
OverlappingActors
    = Actors currently inside PassageOccupancyVolume

LiftedActors
    = Actors whose movement/attachment state is owned temporarily by this barrier's current lift cycle
```

An acquired Actor may leave the overlap volume while remaining a passenger.

`EndOverlap` must not release a passenger automatically.

Passenger records are transient runtime state and must not be captured as authoritative WorldState data.

Conceptual passenger classification:

```text
EBarrierPassengerType
    Character
    AttachedActor
```

The exact enum name may follow project naming conventions.

---

# CHARACTER LIFT HANDLING

Characters are handled differently from ordinary movable Actors.

Default Character policy:

```text
do not attach the Character to BarrierMesh
stop current locomotion request/velocity through the real owner
prevent new locomotion commands from competing with the barrier
leave CharacterMovement capable of receiving moving-base/collision motion
allow BarrierMesh movement and collision to carry/push the Character upward
restore locomotion authority when the lift cycle ends
```

Do not use `AActor::DisableInput()` as the generic solution.

Disabling all player input would also block camera, pause, UI, or unrelated controls.

Do not set Character movement to a mode that prevents valid moving-base behavior without first verifying the engine and project behavior.

Prefer stopping the current request and acquiring a source-owned locomotion lock while leaving the CharacterMovement component able to be transported by the moving barrier.

Codex must validate the final behavior in PIE with the actual `APuzzleTransformMover` component movement method.

If the current mover update path does not allow reliable Character moving-base transport, implement the smallest project-level transport adapter that applies the barrier's actual world-space movement delta while preserving the requirement that Characters are not attached by default.

Do not redesign `APuzzleTransformMover` into a Character transport system.

## Player Character

For the player:

```text
identify the real click-to-move owner
cancel the active click-to-move request
stop current path following through its public API
acquire a temporary source-owned lock that rejects new click movement requests
preserve camera, pause, UI, and non-movement input
record the lock/token in the passenger record
```

Required lock semantics:

- the lock is owned by this barrier instance;
- repeated acquisition by the same barrier is deduplicated;
- releasing this barrier's lock does not remove locks owned by other systems;
- destruction or reset of the barrier releases only its own lock;
- player possession changes and controller destruction are handled safely.

When no source-aware lock currently exists, add the smallest reusable lock API to the click-to-move owner rather than placing a hard-coded boolean in the barrier.

## Clone Character

For a clone:

```text
identify the actual clone through the existing project convention
inspect the active GameplayAction / movement action
interrupt or cancel the current movement action through the public action API
acquire the existing execution/locomotion lock that prevents a new movement action while lifted
record only the barrier-owned lock/token
```

The barrier must not:

- edit Replay Track data;
- edit Execution Journal data directly;
- advance or rewind recorded time;
- change Replay/Investigating/GOAP state itself;
- call IntentReplay directly;
- mark the interrupted movement as completed successfully.

The authoritative action system decides the structured interruption result.

## Other Characters

For a Character that is neither the player nor a clone:

- use the most generic existing locomotion-stop and lock path available;
- do not assume an AI Controller class by name;
- do not search the world for a controller;
- fail predictably when no compatible lock owner exists;
- still preserve cleanup and debug visibility.

## Character release

When releasing a Character passenger:

```text
remove the barrier-owned locomotion lock/token
restore only state explicitly changed by this barrier
clear barrier-owned delegates
leave unrelated movement locks intact
allow normal movement to resume
```

Do not automatically restart the cancelled player path or clone movement action.

A cancelled movement must be re-requested by its authoritative system if appropriate.

---

# NON-CHARACTER ACTOR LIFT HANDLING

Eligible non-Character Actors may be attached to `BarrierMesh` for the active lift cycle.

Required validation:

```text
Actor valid
Actor != this
RootComponent valid
RootComponent movable or otherwise compatible
Actor not already acquired
Actor not owned by this barrier
Actor not excluded by filter
```

Use verified attachment APIs with world-transform preservation.

Conceptual behavior:

```text
cache previous attachment state
cache only physics/gravity state that will be changed
prepare physics when required
AttachToComponent(BarrierMesh, KeepWorldTransform)
mark Actor as acquired
```

## Previous attachment state

The passenger record must preserve enough information to restore:

```text
previous parent component
previous socket name
whether the Actor was unattached
world transform required for safe release
```

At release:

```text
previous parent remains valid
    -> restore previous attachment using a transform rule that preserves the intended world result

previous parent invalid or Actor was originally unattached
    -> detach while preserving world transform
```

Do not blindly attach every component independently. Prefer the Actor's validated root component unless an existing project interface defines another transport root.

## Physics simulation

A physics-simulating component requires deliberate handling.

Inspect the real project convention and engine API.

The expected initial policy is equivalent to:

```text
cache simulation and gravity state
stop or temporarily disable simulation when required for attachment
attach safely
on release, detach first when required
restore simulation and gravity exactly to their prior values
```

Do not use a fake physics success when attachment failed.

Do not leave simulation disabled after reset or EndPlay.

A missing or unsupported physics policy must produce an observable structured failure for that Actor.

---

# ACTORS ENTERING DURING AN ACTIVE RAISE

Overlap remains active while the barrier moves.

## Lift mode

When `bWaitForClearPassage` is false:

```text
new valid Actor enters during MovingTowardEnd
    -> acquire it immediately when not already a passenger
    -> Character: cancel/lock locomotion
    -> non-Character: attach to BarrierMesh
    -> carry it through the remaining upward movement
```

Do not teleport the Actor to the top of the barrier.

Preserve its current world transform at acquisition and apply only future barrier motion.

Actors entering during `MovingTowardStart` are tracked as overlapping but are not automatically acquired as lift passengers.

## Safe mode

When `bWaitForClearPassage` is true, use the safety-return behavior defined earlier.

---

# PASSENGER LIFETIME

Passengers remain acquired until the active traversal resolves at a stable endpoint or is aborted by lifecycle cleanup.

Required release points:

```text
MovingTowardEnd reaches AtEnd
MovingTowardEnd reverses and eventually reaches AtStart
WorldState restore begins
ResetMover begins
Actor EndPlay begins
MovedComponent changes
passenger becomes invalid
critical transport failure requires abort
```

Do not release passengers merely because:

```text
they leave PassageOccupancyVolume
Receiver state changes
movement reverses
movement is paused by Stop
movement is temporarily blocked
```

## Stop behavior

When inherited deactivation behavior `Stop` pauses a lift mid-travel:

- passengers remain acquired;
- player/clone movement locks remain active;
- non-Character attachments remain active;
- GridNavigationModifier remains non-traversable;
- resuming movement continues with the same passengers;
- reset or EndPlay must still release them.

Document that long or indefinite Stop configurations can intentionally immobilize passengers.

## Continue behavior

When inherited `Continue` allows the current traversal to finish, passengers remain acquired until that endpoint is reached.

## Return behavior

When inherited `Return` reverses the barrier, passengers remain acquired through the return and are released at the reached endpoint.

---

# PASSENGER CLEANUP AUTHORITY

All cleanup must pass through one idempotent operation equivalent to:

```text
ReleaseAllLiftedActors(EBarrierPassengerReleaseReason Reason)
```

Conceptual release reasons:

```text
ReachedEnd
ReachedStart
Reset
WorldStateRestore
EndPlay
MovedComponentChanged
InvalidPassenger
TransportAborted
```

For every passenger, cleanup must:

```text
remove only this barrier's movement/action/input lock
restore attachment when applicable
restore physics and gravity when applicable
unbind destruction/end-play delegates
remove the passenger record
emit completion/failure events only when appropriate
```

Idempotence requirements:

- releasing twice has no additional effect;
- an invalid Actor does not crash cleanup;
- releasing one Actor does not remove another Actor's state;
- cleanup during world teardown avoids unsafe world access;
- cleanup never enables movement globally when another system still owns a lock.

Use weak references and symmetrical delegate unbinding.

---

# MOVEMENT REQUEST GATING

The subclass must be able to validate or defer movement toward End before inherited state changes or movement events are emitted.

First inspect whether `APuzzleTransformMover` already exposes a protected generic gate or request hook.

Required generic semantics are equivalent to:

```text
CanBeginMovementTowardEnd()
CanBeginMovementTowardStart()
OnMovementRequestDeferredOrRejected(...)
```

When the existing API is sufficient, use it.

When it is missing, add the smallest generic protected extension to `APuzzleTransformMover`.

A generic extension may know only:

```text
requested endpoint/direction
current mover state
whether the request is accepted, deferred, or rejected
```

It must not know:

```text
GridWorld
passage occupants
doors
Paradox
player
clones
PerceptionKnowledge
WorldState
```

A deferred End request must not:

- change inherited movement state;
- change `MovementAlpha`;
- enable Tick;
- emit `OnMovementStarted`;
- consume Latch completion;
- restart timing.

---

# DEFAULT MOVER CONFIGURATION

Use inherited properties rather than shadowing them.

Recommended native defaults:

```text
MovementMode = PingPong
DeactivationBehavior = Return
InitialPosition = Start
TimingMode = MovementTime or established project default
bAnimateInitialReceiverState = false
bWaitForClearPassage = true
```

Required reason:

```text
Receiver active
    -> raise barrier / close passage

Receiver inactive
    -> lower barrier / open passage

Receiver deactivates during raising
    -> return toward Start
```

Designers may select inherited `Latch` or `FlipFlop` when content requires them.

The class must preserve all inherited mode semantics.

## Latch

A Latch raise is consumed only when `AtEnd` is actually reached.

A safe-mode deferred raise does not consume the latch.

A return to Start before reaching End does not consume the latch.

## FlipFlop

Each accepted activation selects the inherited opposite endpoint.

Clearance/lift handling runs only when the accepted target is End.

## PingPong

Active commands End and inactive commands Start according to inherited deactivation behavior.

---

# COLLISION AND PHYSICAL TRANSPORT

The class must use a collision configuration capable of representing a physical vertical barrier.

Do not assume one collision profile name without inspecting project conventions.

Required outcomes:

- lowered barrier permits the intended passage;
- raised barrier blocks the intended Actors;
- moving barrier can physically carry/push Characters in lift mode;
- non-Character passengers attached to the barrier do not fight physics state;
- FrameMesh and BarrierMesh do not generate self-occupancy records;
- movement does not create per-frame GridWorld rebuilds.

Inspect the inherited `bSweepMovement` support.

For this class, enable or enforce the movement/collision behavior required by the verified Character transport implementation.

Do not silently use a sweep mode that stops the barrier on every Character when lift mode is supposed to carry them.

Do not disable collision globally merely to force endpoint completion.

If the current base movement API cannot satisfy both deterministic endpoint interpolation and Character lift behavior, add the smallest project-level movement adapter or generic protected application hook, document it, and validate it in PIE.

---

# AUDIO AND NIAGARA

`MovementAudio` and `MovementVFX` are optional presentation components.

Required defaults:

- attached to `BarrierMesh`;
- auto activation disabled;
- no hard-coded asset;
- missing asset does not break movement or navigation;
- stopped during reset and EndPlay;
- no feedback during construction or initial synchronization.

The implementation may expose project-appropriate direction-specific configuration equivalent to:

```text
RaiseSound
LowerSound
RaiseNiagaraSystem
LowerNiagaraSystem
```

or use one preconfigured component when that matches current conventions.

Required behavior:

- start once on meaningful movement start;
- distinguish resume from fresh start when useful;
- allow direction-specific feedback on reversal;
- do not replay feedback for duplicate target requests;
- do not play feedback for a safe-mode deferred raise;
- do not play feedback during WorldState restore;
- do not emit per-frame audio or VFX commands.

---

# PERCEPTIONKNOWLEDGE INTEGRATION

The barrier must integrate through the existing project PerceptionKnowledge API.

Do not create a second perception system.

Do not call IntentReplay directly.

Do not write Observation Tracks or Journals directly.

Do not use generic Unreal `MakeNoise` as a parallel path unless PerceptionKnowledge intentionally uses it internally.

## Observable semantic state

Expose/update project-appropriate persistent state equivalent to:

```text
Barrier.Open
Barrier.BlockingPassage
Barrier.Moving
Barrier.WaitingForClearance
Barrier.TransportingOccupants
```

Optional occupant count may be exposed only through the existing typed-state/payload model:

```text
Barrier.OccupantCount
```

Do not invent an unrelated primitive payload structure inside this Actor.

Required state mapping:

```text
Barrier.Open
    true only at exact AtStart with traversable GridNavigationModifier

Barrier.BlockingPassage
    true in every state other than exact navigable AtStart

Barrier.Moving
    true only while interpolation is actively advancing
    false while idle or paused by Stop

Barrier.WaitingForClearance
    true while bRaiseRequestPending

Barrier.TransportingOccupants
    true while LiftedActors is not empty
```

Update state on meaningful transitions, not every Tick.

The PerceptionKnowledge state and GridNavigationModifier state must not disagree after a completed transition or restore.

## Semantic noise/events

Use existing project noise settings/profile types.

Provide optional configuration equivalent to:

```text
bEmitNoiseOnRaiseStart
bEmitNoiseOnLowerStart
bEmitNoiseOnMovementResume
bEmitNoiseOnDirectionReverse
bEmitNoiseOnReachedEndImpact
bEmitNoiseOnReachedStartImpact
```

Use only the subset justified by existing project patterns.

Possible semantic events:

```text
Event.Noise.Barrier.Movement
Event.Noise.Barrier.Impact
Event.Barrier.RaiseDeferred
Event.Barrier.TransportStarted
Event.Barrier.TransportCompleted
```

Use registered project tags and existing event types. Do not create tags from strings in runtime callbacks.

Do not emit noise/events:

- every movement Tick;
- for duplicate movement requests;
- while a raise is only pending and no physical movement starts, except an optional non-noise semantic deferred event;
- during construction;
- during initial state synchronization;
- during WorldState restore;
- from overlap callbacks that do not change meaningful state.

Noise source/instigator/location must follow the established PerceptionKnowledge source-identity model.

---

# WORLDSTATE INTEGRATION

`AParadoxVerticalBarrier` must participate through the existing WorldState component/interface architecture.

Do not create a custom save manager.

Do not add barrier knowledge to the generic WorldState plugin.

WorldState remains responsible for capture and ordered restoration. The barrier remains responsible for rebuilding its derived physical, navigation, perception, and transient state.

## Authoritative state

The inherited mover owns authoritative movement state:

```text
EPuzzleTransformMoverState
MovementAlpha
bIsMovementPaused
Latch completion when relevant
configured initial state
```

The actual `BarrierMesh` transform is derived from inherited endpoint transforms, alpha, and interpolation rules.

First inspect whether `APuzzleTransformMover` already exposes reflected WorldState-capturable properties or a generic runtime-state snapshot/restore API.

When it does, use the real API.

When it does not, add the smallest generic and project-independent mover state snapshot contract to `PuzzleSystem`, conceptually equivalent to:

```text
FPuzzleTransformMoverRuntimeState
CaptureRuntimeState()
RestoreRuntimeState(..., suppress presentation)
RebuildMovedComponentTransformFromState()
```

Such an API must contain only generic mover state and must not mention Paradox systems.

Do not create a second barrier-owned boolean that competes with inherited movement state.

## State that may belong to WorldState capture

Depending on the verified mover contract, capture or reconstruct:

```text
inherited mover authoritative state
MovementAlpha
paused state when snapshots intentionally preserve mid-movement state
Latch completion
configured runtime moved-component selection only when the project intentionally treats it as restorable
any barrier-specific persistent gameplay option intentionally changed at runtime
```

Use WorldState's normal selected-property / complete-struct / relative-transform model.

Do not capture `BarrierMesh` relative transform as a second independent authority when it is already derived from mover state.

## Transient state that must not be authoritative snapshot data

Do not persist:

```text
OverlappingActors
component overlap sets
LiftedActors
passenger attachment records
player click-movement lock tokens
clone action/execution lock tokens
destruction delegate handles
bRaiseRequestPending
movement Tick enabled state
GridNavigationModifier derived active state
currently playing audio
currently active Niagara
PerceptionKnowledge publication cache owned by another system
temporary feedback suppression flags
stale deferred callbacks
```

These values must be cleared and rebuilt safely.

---

# WORLDSTATE RESTORE LIFECYCLE

Use the real WorldState lifecycle phases and exactly-once terminal semantics.

The required behavior is equivalent to the phases below.

## Before restore/application

```text
set a barrier-owned restore/suppression guard
cancel bRaiseRequestPending
stop inherited movement advancement safely
release all lifted Actors through the idempotent cleanup path
remove every barrier-owned click/action/locomotion lock
restore every temporary attachment and physics override
clear/prune overlap records without generating gameplay movement
stop MovementAudio
stop/deactivate MovementVFX
suppress PerceptionKnowledge noise and presentation events
keep GridNavigationModifier conservatively non-traversable until restored state is known
invalidate barrier-owned deferred callbacks
```

Do not allow stale overlap, movement, passenger, or feedback callbacks to apply after restore begins.

## During authoritative restoration

Restore the inherited mover state through the verified WorldState/PuzzleSystem contract.

Do not:

- run clearance gating;
- acquire passengers;
- cancel the restore because an Actor overlaps the volume;
- synthesize Receiver activation/deactivation;
- replay audio, VFX, or semantic noise.

A snapshot is authoritative. The barrier must rebuild the requested state rather than silently choose a safer different state.

## Derived-state reconstruction

After authoritative properties are restored:

```text
rebuild exact BarrierMesh transform from StartArrow, EndArrow, MovementAlpha, and mover state
recalculate whether the passage must be traversable
update GridNavigationModifier once
update persistent PerceptionKnowledge state without noise
leave audio and Niagara stopped
leave passenger collections empty
leave bRaiseRequestPending false
```

Required navigation result:

```text
restored exact AtStart
    -> traversable

restored AtEnd, partial alpha, MovingTowardStart, MovingTowardEnd, or paused movement
    -> non-traversable
```

## Restore completion

After the global restore has completed and dependent Actors are valid:

```text
clear suppression guard at the correct phase
perform one lifecycle-safe RefreshPassageOccupants()
rebind overlap/destruction observation safely
resume inherited movement only when the restored mover contract explicitly represents active movement
```

Post-restore overlap reconciliation must not automatically acquire lift passengers.

Passenger acquisition occurs only when a genuine future End movement begins.

Do not emit retroactive movement noise or presentation for restored state.

---

# RESETMOVER INTEGRATION

The inherited generic `ResetMover()` is not a complete project reset by itself.

Override or wrap reset through the protected extension points required to guarantee:

```text
cancel pending raise
release all passengers
remove all barrier-owned movement/action/input locks
restore attachments and physics
clear overlap runtime state safely
reset inherited mover
rebuild grid navigation state
rebuild PerceptionKnowledge persistent state
stop audio/VFX
suppress physical feedback caused only by reset
```

Do not mutate puzzle Controller requests.

Do not publish fake Receiver transitions.

Do not use `ResetMover()` as a replacement for WorldState capture/restore orchestration.

---

# PUBLIC API

Expose only controlled operations and queries useful to project code and Blueprint designers.

Required conceptual queries:

```text
IsPassageOpen() -> bool
IsPassageBlockingNavigation() -> bool
IsRaiseRequestPending() -> bool
IsPassageOccupied() -> bool
GetPassageOccupantCount() -> int32
GetPassageOccupants() -> array of Actor references
GetLiftedActorCount() -> int32
IsTransportingOccupants() -> bool
IsActorBeingLifted(AActor*) -> bool
GetGridNavigationModifier() -> UGridNavigationModifierComponent*
GetPassageOccupancyVolume() -> UBoxComponent*
```

Required controlled operations:

```text
RefreshPassageOccupants()
CancelPendingRaiseRequest()
ReleaseAllLiftedActors(...), protected or intentionally restricted
```

Movement target requests must normally remain protected and use inherited invariant-preserving APIs.

Do not expose public setters for:

```text
inherited State
MovementAlpha
bIsMovementPaused
bRaiseRequestPending
OverlappingActors
LiftedActors
GridNavigationModifier internal state
```

Do not expose raw mutable lock tokens or delegate handles.

---

# BLUEPRINT EXTENSION HOOKS

Provide the smallest useful Blueprint-observable events consistent with existing project style.

Conceptual events:

```text
OnPassageOccupancyChanged(int32 OccupantCount)
OnRaiseDeferred(int32 OccupantCount)
OnPendingRaiseCancelled()
OnPassageClearanceRestored()
OnPassageBecameBlocked()
OnPassageBecameNavigable()
OnOccupantLiftStarted(AActor* Occupant)
OnOccupantLiftFailed(AActor* Occupant, structured reason)
OnOccupantLiftCompleted(AActor* Occupant)
OnAllOccupantsPrepared(int32 Count)
OnAllOccupantsReleased(int32 Count)
```

Also reuse inherited mover events for:

```text
movement started
movement resumed
movement reversed
reached Start
reached End
movement blocked
moved component changed
reset
```

Events are observational/presentation hooks.

The native class must remain correct with no Blueprint implementation.

Blueprint events must not be required to:

- update GridWorld;
- release locks;
- restore physics;
- maintain passenger maps;
- rebuild WorldState state;
- publish authoritative PerceptionKnowledge state.

---

# DETAILS PANEL ORGANIZATION

Organize properties into coherent categories equivalent to:

```text
Paradox Barrier | Components
Paradox Barrier | Passage
Paradox Barrier | Occupants
Paradox Barrier | Grid Navigation
Paradox Barrier | Movement
Paradox Barrier | Feedback
Paradox Barrier | Perception
Paradox Barrier | World State
Paradox Barrier | Debug
```

Use inherited PuzzleSystem categories for inherited mover properties instead of shadowing them.

Recommended editor behavior:

- `bWaitForClearPassage` is prominent under Occupants;
- lift-only settings are hidden or disabled while waiting is enabled;
- wait-only settings are hidden or disabled while waiting is disabled;
- optional feedback settings use edit conditions;
- Passage bounds are authored from one source;
- runtime-only maps, locks, and state flags are not editable;
- tooltips explain that Start is lowered/open and End is raised/closed;
- tooltips explain that any state other than exact Start blocks GridWorld navigation.

---

# INITIALIZATION

Initialization must not depend on Actor `BeginPlay` ordering.

Required conceptual sequence:

```text
construct native component hierarchy
resolve/assign BarrierMesh through inherited mover API
validate Start and End semantics
resolve GridNavigationModifier bounds source
bind PassageOccupancyVolume overlap delegates
initialize inherited mover state and Receiver synchronization
rebuild exact BarrierMesh transform
apply derived GridNavigationModifier state
publish initial PerceptionKnowledge persistent state without noise
perform one controlled overlap reconciliation after components are registered
leave passenger collection empty
leave feedback inactive
```

If initial Receiver synchronization animates according to inherited configuration, occupant policy must still be respected for a genuine runtime movement.

Default `bAnimateInitialReceiverState = false` avoids raising during startup solely because Controller initialization order differs.

Do not acquire passengers during construction or editor preview.

---

# ENDPLAY AND CLEANUP

During `EndPlay` or destruction:

```text
set teardown guard
cancel pending raise
unbind overlap delegates
release all lifted Actors idempotently when world access remains safe
remove all barrier-owned movement/input/action locks
restore attachment and physics where safe
stop audio and Niagara
unbind PerceptionKnowledge/WorldState delegates
clear overlap and passenger maps
prevent deferred callbacks from changing state
allow inherited mover cleanup
```

Account for world teardown where some external owners may already be invalid.

Do not crash while trying to restore a lock owner or parent component that no longer exists.

Do not leave a player or clone permanently movement-locked because the barrier was destroyed.

---

# DEBUGGING

Follow root and module debug rules:

```text
effective visual debug = module global debug enabled AND Actor bEnableDebug
```

Debug must answer:

```text
Where are Start and End?
What is the current inherited mover state and alpha?
Is movement paused?
Is the passage currently navigable?
Which GridWorld cells/bounds are affected?
How many Actors overlap the passage?
Which Actors are occupants?
Is a raise pending?
Which occupant policy is active?
Which Actors are currently passengers?
Which Character locks or Actor attachments are owned by this barrier?
Why was one Actor not transported?
Is WorldState restore suppression active?
Which PerceptionKnowledge states are being exposed?
```

Suggested visual colors:

```text
PassageOccupancyVolume green
    -> exact AtStart and navigable

PassageOccupancyVolume red
    -> non-traversable

PassageOccupancyVolume orange
    -> raise deferred while occupied

PassageOccupancyVolume cyan
    -> lift mode actively transporting occupants
```

Suggested debug elements:

- inherited Start/End arrows and path;
- box for shared passage bounds;
- line to each current occupant when local debug is enabled;
- different marker/label for each acquired passenger;
- compact text with state, alpha, timing, policy, occupancy, passenger count, pending state, and GridWorld state;
- last structured defer/failure reason.

Do not:

- draw or enumerate Actor details while debug is disabled;
- log every movement Tick;
- emit the same warning every frame;
- allocate large debug containers every frame;
- expose private lock tokens in shipping UI.

Use the owning Paradox module log category and macros, not `LogTemp`.

---

# VALIDATION

Provide editor and runtime validation through the established project pattern.

Validate at least:

```text
inherited BillboardRoot exists
inherited StartArrow exists
inherited EndArrow exists
inherited PuzzleReceiver exists
FrameMesh exists
BarrierMesh exists
BarrierMesh is the valid inherited moved component
BarrierMesh mobility is Movable
PassageOccupancyVolume exists
PassageOccupancyVolume generates query overlaps
PassageOccupancyVolume and GridNavigationModifier share equivalent bounds
GridNavigationModifier exists
GridNavigationModifier can switch traversability at runtime
Start and End transforms are not accidentally identical
End is above Start according to the vertical-barrier contract, within an intentional tolerance
required collision responses are compatible with configured occupant policy
Character lift transport is compatible with the actual mover transform path
WorldState participant/integration is valid
PerceptionKnowledge settings are valid when publication is enabled
optional audio/Niagara assets may be absent without breaking gameplay
player click-to-move lock owner is resolvable when a player is transported
clone locomotion/action lock owner is resolvable when a clone is transported
```

A non-vertical End transform should produce a clear validation warning or error according to strictness.

The class derives from a general transform mover, but this project Actor promises vertical barrier behavior.

Invalid critical configuration must fail predictably.

Do not silently:

- use an unrelated GridWorld modifier;
- mark the whole level non-navigable;
- ignore occupant preparation failures;
- fall back to disabling all player input;
- treat an unsupported Actor as successfully transported;
- publish an invalid Gameplay Tag.

---

# BUILD DEPENDENCIES

Inspect exact module names before changing `Build.cs`.

Expected dependencies may include:

```text
PuzzleSystem
GridWorld
WorldState
PerceptionKnowledge
GameplayActions
GameplayTags
Niagara
Engine
<actual Paradox movement/input modules when separate>
```

Add only dependencies actually required.

Choose Public versus Private correctly:

- types present in public headers require public dependencies or appropriate abstraction;
- implementation-only integrations should remain private;
- forward declare where valid;
- avoid leaking large project-system headers into the public Actor header;
- avoid circular dependencies;
- prefer project adapter interfaces/components when direct public dependencies would be excessive.

---

# DOCUMENTATION DELIVERABLE

Create or update human-facing documentation inside the owning Paradox runtime module `Docs` folder.

At minimum document:

```text
class purpose and ownership
component hierarchy
Start = lowered/open and End = raised/closed convention
how to author the shared passage bounds
GridNavigationModifier behavior
navigation state for every mover state
bWaitForClearPassage semantics
safe door behavior
lift barrier behavior
occupant counting and multi-component Actors
player click-to-move cancellation and temporary lock
clone movement-action interruption and temporary lock
Character moving-base/collision transport
non-Character attachment and physics restoration
passenger release points
Latch, FlipFlop, and PingPong interaction
Stop, Return, and Continue interaction
PerceptionKnowledge state/noise setup
WorldState capture, restore, and derived-state rebuild
Blueprint events
validation and debug controls
common failure cases
known Character/physics limitations verified during implementation
```

Update the module README/index when one exists.

If a generic `APuzzleTransformMover` API is extended, update PuzzleSystem user documentation in the same task.

If a generic click-to-move or GameplayActions lock API is added, update the owning module documentation in the same task.

Do not put Codex workflow instructions in human-facing `Docs`.

---

# REQUIRED BEHAVIOR SCENARIOS

Validate every applicable scenario before considering the implementation complete.

## 1. Lowered initial barrier

```text
InitialPosition = Start
State = AtStart
BarrierMesh matches StartArrow
GridNavigationModifier is traversable
no movement feedback or noise
no passengers
```

## 2. Clear safe raise

```text
bWaitForClearPassage = true
passage empty
Receiver commands End
-> grid becomes non-traversable
-> movement starts toward End
-> reaches AtEnd
-> remains non-traversable
```

## 3. Occupied safe raise

```text
bWaitForClearPassage = true
one valid Actor overlaps
Receiver commands End
-> no movement
-> remains AtStart
-> remains traversable
-> bRaiseRequestPending = true
-> deferred event occurs once
```

## 4. Multiple components on one Actor

One Actor overlaps using two primitive components.

Occupant count remains one until its final overlapping component leaves.

## 5. Multiple distinct occupants

Two different Actors overlap.

Occupant count is two. Safe raising remains deferred until both leave.

## 6. Safe automatic retry

The final occupant leaves while End is still semantically requested.

```text
pending clears
GridWorld blocks the passage
raising begins exactly once
```

## 7. Safe request cancellation

A pending PingPong raise is cancelled because the Receiver no longer commands End.

The barrier remains AtStart and traversable after the passage becomes empty.

## 8. Actor enters during safe raising

```text
MovingTowardEnd
new Actor enters
-> reverse toward Start
-> keep grid blocked during return
-> reach AtStart
-> grid becomes traversable
-> wait until passage clear
-> retry only when End remains requested
```

## 9. Empty lift raise

```text
bWaitForClearPassage = false
passage empty
-> grid blocks
-> barrier raises normally
```

## 10. Player Character lift

```text
player overlaps
active click-to-move path exists
barrier raises in lift mode
-> current click movement cancelled
-> only click movement requests are locked
-> camera/UI/pause remain usable
-> Character is carried upward by verified moving-base/collision behavior
-> endpoint reached
-> barrier-owned movement lock released
-> cancelled destination is not automatically resumed
```

## 11. Clone Character lift

```text
clone overlaps while executing a movement GameplayAction
barrier raises
-> movement action interrupted through authoritative API
-> locomotion execution/resource lock acquired by this barrier
-> clone is carried upward
-> endpoint reached
-> only this barrier's lock released
-> Replay Track is unchanged
```

## 12. Ordinary Character lift

A non-player/non-clone Character is stopped and temporarily prevented from issuing locomotion through the most generic compatible project path, then restored safely.

## 13. Movable Actor lift

```text
non-Character movable Actor overlaps
-> previous attachment/physics state cached
-> Actor attached to BarrierMesh with world transform preserved
-> Actor moves upward
-> AtEnd reached
-> Actor released
-> prior attachment and physics state restored
```

## 14. Physics Actor lift

A physics-simulating Actor follows the documented preparation and restoration policy without remaining attached, non-simulating, or gravity-disabled after release.

## 15. Actor enters during lift raise

An Actor entering during `MovingTowardEnd` is acquired once and receives only the remaining barrier movement.

## 16. Actor enters during lowering

The Actor is counted as an overlap but is not acquired automatically as a passenger.

## 17. Passenger leaves overlap

A passenger exits `PassageOccupancyVolume` while still attached/carried.

It remains a passenger until endpoint cleanup.

## 18. Stop mid-lift

```text
DeactivationBehavior = Stop
barrier pauses mid-travel
-> grid remains blocked
-> passengers remain acquired
-> player/clone locks remain active
-> reactivation resumes same direction and passengers
```

## 19. Return mid-lift

```text
DeactivationBehavior = Return
barrier reverses
-> passengers remain acquired
-> release occurs only at reached endpoint
```

## 20. Continue mid-lift

```text
DeactivationBehavior = Continue
barrier finishes current traversal
-> passengers release at endpoint
-> ignored deactivation is not queued automatically
```

## 21. Latch deferred raise

A Latch request is deferred by occupants and is not considered completed until the barrier actually reaches AtEnd.

## 22. FlipFlop direction handling

Occupancy gating/transport runs only when the accepted FlipFlop target is End. Movement toward Start does not acquire new passengers.

## 23. Lowering navigation

The barrier remains non-traversable for the complete lowering movement and becomes traversable only after exact AtStart completion.

## 24. Paused or blocked partial barrier

Any partial barrier state remains non-traversable.

## 25. Duplicate requests

Repeated requests for the current target do not restart movement, reprepare passengers, duplicate locks, duplicate attachments, replay sound/VFX, or republish noise.

## 26. Moved component replacement

Replacing the moved component releases every passenger and lock safely before inherited replacement continues.

## 27. Passenger destroyed during lift

The weak passenger record is pruned safely. Other passengers and the barrier continue without crash.

## 28. Barrier destroyed during lift

All valid passengers are released; player/clone movement locks owned by the barrier are removed; physics/attachments are restored where world teardown permits.

## 29. WorldState restore at Start

```text
restore AtStart
-> passengers released
-> transient occupancy state rebuilt later
-> BarrierMesh snapped/rebuilt at Start
-> GridWorld traversable
-> PerceptionKnowledge state Open
-> no sound/VFX/noise
```

## 30. WorldState restore at End

```text
restore AtEnd
-> BarrierMesh rebuilt at End
-> GridWorld non-traversable
-> PerceptionKnowledge state BlockingPassage
-> no passenger acquisition
-> no sound/VFX/noise
```

## 31. WorldState restore mid-movement

When supported by the generic mover snapshot contract:

```text
restore state and alpha
-> exact partial transform rebuilt
-> grid non-traversable
-> passengers empty
-> movement resumes only according to restored mover contract
-> no stale locks or feedback
```

## 32. Post-restore overlap reconciliation

Current overlapping Actors are counted once after restore completion but are not automatically transported until a genuine future raise begins.

## 33. PerceptionKnowledge consistency

`Barrier.Open`, `Barrier.BlockingPassage`, `Barrier.Moving`, pending, and transport state always match authoritative mover/GridWorld state after transitions and restore.

## 34. Missing optional feedback assets

Movement, navigation, occupancy handling, locks, attachment, Perception state, and WorldState still work when audio or Niagara assets are unset.

## 35. Blueprint child without overrides

The complete native class works without Blueprint event implementations.

---

# AUTOMATED TESTS

Add focused automated tests where the existing project test architecture can support them reliably.

Prioritize tests for:

```text
navigation state mapping for every mover state
safe raise defer and retry
safe pending-request cancellation
multi-component Actor occupant counting
multiple Actor counting
safe-mode entry during raise triggers return
lift mode does not defer movement
passenger map separated from overlap map
player movement lock ownership and cleanup through test adapter
clone movement-action interruption and lock cleanup through test adapter
non-Character attachment restoration
physics-state restoration
passenger persistence after EndOverlap
Stop/Return/Continue passenger lifetime
Latch not consumed by deferred raise
MovedComponent replacement cleanup
WorldState restore cleanup and navigation reconstruction
Perception state transition deduplication
no noise/audio/VFX during restore
idle Tick disabled
no per-frame GridNavigationModifier updates
```

Use adapters/test doubles for player, clone, PerceptionKnowledge, and GridWorld when full gameplay worlds are unnecessary.

Do not replace PIE validation for Character moving-base behavior with a pure unit test.

---

# PERFORMANCE REQUIREMENTS

Idle cost must remain near zero.

Required constraints:

- no permanent Actor Tick;
- movement Tick only while inherited interpolation actively advances;
- no periodic overlap polling;
- no world searches;
- no per-frame GridNavigationModifier changes;
- no per-frame path invalidation calls;
- no per-frame PerceptionKnowledge publication;
- no per-frame logging;
- no repeated passenger preparation for duplicate movement requests;
- no repeated allocations in the movement hot path when avoidable;
- overlap and passenger references use lifecycle-safe containers;
- debug work occurs only when global and local debug are enabled.

Add Unreal Insights scopes only to meaningful operations such as:

```text
ParadoxBarrier_RefreshOccupants
ParadoxBarrier_PreparePassengers
ParadoxBarrier_ReleasePassengers
ParadoxBarrier_UpdateGridNavigation
```

Do not instrument trivial getters or every movement frame unless profiling identifies a real need.

---

# FORBIDDEN SHORTCUTS

Do not:

```text
implement another transform movement state machine in AParadoxVerticalBarrier
subscribe directly to puzzle Emitters
activate Receivers directly
create a second GridWorld modifier implementation
rebuild GridWorld every movement Tick
use BarrierMesh navigation relevance as the dynamic passage authority
allow GridNavigationModifier and overlap bounds to drift silently
turn off overlap tracking when bWaitForClearPassage is false
count primitive components as separate occupants
world-search for occupants
poll passage occupancy every frame
use one raw boolean to disable every player input
hard-code player or clone class names when an existing interface/tag/component convention exists
edit Replay Track or Execution Journal
mark interrupted clone movement as successful
remove movement locks owned by other systems
attach Characters by default when the required policy is movement-lock plus moving-base/collision transport
assume Character transport works without PIE validation
attach physics Actors without preserving/restoring simulation state
release passengers merely on EndOverlap
release passengers immediately on reversal or Stop
serialize lock tokens, passenger maps, overlap maps, or delegate handles into WorldState
allow reset to emit movement noise, sound, or VFX
run clearance gating while applying an authoritative WorldState snapshot
leave the grid traversable while the barrier is partially raised
make the grid traversable before exact AtStart
consume Latch when a raise is only requested or deferred
use LogTemp in committed code
invent Unreal or plugin APIs
skip compilation
```

---

# IMPLEMENTATION ORDER

Use this order unless existing repository structure requires a safer sequence:

1. inspect all relevant `CODEX`, `Docs`, public APIs, and engine headers;
2. identify actual Paradox runtime module and existing component conventions;
3. inspect `APuzzleTransformMover` extension points and add only a minimal generic movement-request gate when genuinely missing;
4. create `AParadoxVerticalBarrier` and native component hierarchy;
5. configure `BarrierMesh` as inherited moved component;
6. integrate the existing `UGridNavigationModifierComponent` and shared bounds source;
7. implement event-driven distinct-Actor overlap tracking;
8. implement navigation state mapping from inherited mover state;
9. implement `bWaitForClearPassage = true` defer/retry/cancellation behavior;
10. implement safe-mode entry-during-raise return behavior;
11. inspect and implement player click-to-move source-owned lock path;
12. inspect and implement clone movement action interruption and locomotion lock path;
13. implement non-Character attachment and physics preservation;
14. implement passenger acquisition for current and newly entering Actors;
15. implement idempotent passenger release and lifecycle cleanup;
16. connect inherited movement events to passenger and navigation transitions;
17. add audio and Niagara presentation;
18. add PerceptionKnowledge persistent state and semantic noise through existing APIs;
19. add WorldState capture/restore and derived-state reconstruction;
20. add Blueprint API, events, validation, and debug;
21. update human-facing documentation in every changed module;
22. add focused automated tests;
23. compile the affected editor/game target;
24. fix all errors and recompile until successful;
25. validate all required PIE scenarios, especially Character transport;
26. review the complete diff and remove unrelated changes.

---

# IMPLEMENTATION SCOPE

This task includes:

```text
AParadoxVerticalBarrier
native FrameMesh and BarrierMesh
native PassageOccupancyVolume
existing UGridNavigationModifierComponent integration
shared bounds authoring/validation
Start lowered/open and End raised/closed convention
bWaitForClearPassage
safe defer/retry/cancellation policy
event-driven distinct-Actor occupancy tracking
lift-mode Character handling
player click-to-move cancellation and source-owned movement lock
clone movement action interruption and source-owned locomotion/execution lock
ordinary Character fallback handling
non-Character attachment
physics-state preservation/restoration
passenger lifetime and idempotent cleanup
navigation state updates only on semantic transitions
PerceptionKnowledge state/noise integration
WorldState restoration and derived-state rebuild
optional movement audio and Niagara
Blueprint hooks and queries
debug and validation
user documentation
tests, compilation, and PIE validation
```

---

# OUT OF SCOPE

Do not implement in this task:

```text
horizontal sliding doors
swinging doors
multi-panel synchronized doors
spline or multi-point barriers
crushing damage
character death or ragdoll policy
complex object stacking solver
physics-force launch tuning
network replication
multiplayer authority prediction
automatic navmesh generation
new GridWorld pathfinding algorithms
new Replay/Investigating/GOAP behavior
automatic replay recovery after a clone action interruption
new generic PerceptionKnowledge payload families without a real existing need
new global movement manager
new global puzzle manager
```

Leave intentional extension points, but do not add speculative systems.

---

# DEFINITION OF DONE

The task is complete only when:

- the class lives in the existing Paradox runtime module;
- it derives from the real `APuzzleTransformMover`;
- it reuses the inherited Receiver, movement states, modes, timing, easing, alpha, pause, and reversal;
- Start means fully lowered/open and End means fully raised/closed;
- `FrameMesh`, `BarrierMesh`, `PassageOccupancyVolume`, `MovementAudio`, and `MovementVFX` exist with the required hierarchy;
- `BarrierMesh` is assigned through the inherited moved-component API;
- the existing `UGridNavigationModifierComponent` is used;
- overlap and GridWorld modifier use one authoritative passage bounds definition;
- exact `AtStart` is the only navigable barrier state;
- movement, paused partial state, and `AtEnd` are non-traversable;
- no per-frame GridWorld update exists;
- overlap tracking remains enabled in both occupant policies;
- distinct Actors are counted once despite multiple overlapping components;
- `bWaitForClearPassage` defaults to true;
- safe mode defers raising until clear and retries event-driven;
- safe mode returns toward Start when an Actor enters during raising;
- lift mode prepares current occupants and raises without waiting;
- player click movement is cancelled and locked without disabling unrelated input;
- clone movement action is interrupted through the authoritative API without editing replay data;
- Character locomotion is temporarily blocked while moving-base/collision transport remains functional;
- non-Character Actors are attached and restored safely;
- physics state is restored correctly;
- passengers are not released merely because they leave the overlap volume;
- Stop, Return, and Continue preserve the specified passenger lifetime;
- every barrier-owned lock, attachment, delegate, and physics override is cleaned up on endpoint, reset, component change, EndPlay, or failure;
- PerceptionKnowledge integration uses existing APIs and emits no per-frame or reset noise;
- WorldState restore releases transient passengers/locks and rebuilds transform, navigation, and perception state deterministically;
- no generic plugin acquires a Paradox dependency;
- Blueprint hooks are optional and do not own core correctness;
- local/global debug rules and module logging conventions are respected;
- relevant human-facing documentation is updated;
- required automated tests are added where practical;
- Character lift behavior is validated in PIE;
- the affected Unreal target compiles successfully;
- the final diff contains no unrelated changes.

If the affected target does not compile, the task is not finished.
