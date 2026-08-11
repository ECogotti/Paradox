# Paradox runtime module

The runtime module owns the default player character and controller setup.

## Source layout

The module follows Unreal's standard public/private boundary. Public headers are grouped by
responsibility and their implementations use the matching path under `Private`:

- `Public/Characters` and `Private/Characters` contain the playable character;
- `Public/Controllers` and `Private/Controllers` contain player controllers;
- `Public/Actions` and `Private/Actions` contain project Gameplay Action definitions and instances;
- `Public/GameModes` and `Private/GameModes` contain game modes;
- `Public/Components` and `Private/Components` contain reusable project components;
- `Public/Camera` and `Private/Camera` contain the independent orthographic rig, map bounds, and
  camera value types;
- `Public/Settings` and `Private/Settings` contain the Project Settings defaults;
- `Public/TimeLoop` and `Private/TimeLoop` contain Chrono Spawns, temporal identity, and the
  authoritative recording/reset/reconstruction coordinator;
- `Public/Perception` and `Private/Perception` contain clone-owned physical temporal vision;
- `Public/Behavior`, `Public/Investigation`, and their private mirrors contain authoritative clone
  behavior, project response policy, replay recovery, and native Behavior Tree tasks;
- `Public/Relations` and `Private/Relations` contain the project temporal-ordering policy;
- `Public/Puzzles` and `Private/Puzzles` contain concrete project puzzle Actors built on the reusable
  PuzzleSystem primitives;
- `Public/Presentation` and `Private/Presentation` contain native, Blueprint-replaceable outcome
  presentation;
- `Public/Interaction` and `Private/Interaction` contain player selection, selectable Actor
  presentation, the Blueprint-extensible world-space widget base, multi-interaction Smart Object
  slot queries/submission, and the replay-safe interaction Gameplay Action template;
- `Public/Paradox.h` exposes the module log category and native gameplay tags;
- `Private/Tests` contains module automation tests.

Include public types by their responsibility-qualified path, for example
`#include "Characters/ParadoxCharacter.h"` or
`#include "Controllers/ParadoxPlayerController.h"`.

The unused Unreal template implementations previously stored under `Variant_TwinStick` and
`Variant_Strategy` are not part of the Paradox runtime module. The current Top Down level uses the
Paradox character, controller, and game mode described below.

## Character and controller hierarchy

`AParadoxCharacter` is the abstract temporal-avatar base. It owns only capabilities shared by both
runtime roles:

- `UGameplayActionComponent`, the authoritative scheduler for semantic actions;
- `UIntentReplayComponent`, configured to observe that action component;
- `UEntityIdentityComponent`, the generic Entity Relations identity;
- `UParadoxTemporalEntityComponent`, the Paradox role, Temporal Index, and optional replay track.

`AParadoxPlayerCharacter` keeps the character-mounted camera as a fallback for maps without a
Paradox camera volume and adds `UTacticalPauseActionQueueComponent`. `AParadoxCloneCharacter`
instead adds `UWorldStateParticipantComponent`, `UParadoxTemporalVisionComponent`,
`UParadoxCloneBehaviorCoordinatorComponent`, and `UParadoxCloneInvestigationComponent`; it has no
player camera or tactical-planning adapter. Both roles inherit the synchronized
`UIntentReplayObservationComponent`.

The Top Down content uses these native parents:

- `BP_PlayerCharacter` -> `AParadoxPlayerCharacter`;
- `BP_CloneCharacter` -> `AParadoxCloneCharacter`;
- `BP_PlayerController` -> `AParadoxPlayerController`;
- `BP_CloneController` -> `AParadoxCloneController`.

Player and clone controllers intentionally do not share a project controller base:
`AParadoxPlayerController` requires `APlayerController` input/local-player behavior, while
`AParadoxCloneController` derives from `AGridWorldAIController` for AI possession and precise
GridWorld path following.

The replay component is initialized with the character, but recording is not started automatically.
Blueprint or C++ gameplay code can explicitly start and stop recording when the relevant gameplay
phase begins and ends.

Clone replay uses `UParadoxCloneReplayExecutionStrategy`. It re-stamps controller-bound exact
GridWorld paths on a per-clone runtime request copy, while the consolidated Intent Replay track
remains immutable. When investigation displaced the clone, an InvalidStart recovery emits one
warning and stamps a fresh exact path from the current cell to the original semantic goal. The time
loop also removes occupancy and traffic parking for inactive temporal avatars, preventing hidden
players or destroyed clones from blocking later timelines.

Player and clone controllers own a semantic PerceptionKnowledge listener plus an event-driven
Hearing Range renderer. Clone setup and policy are documented in:

- [Clone behavior authority](CLONE_BEHAVIOR.md);
- [Perception integration and priorities](PERCEPTION_INTEGRATION.md);
- [Investigation and recovery](INVESTIGATION.md);
- [Behavior Tree setup](BEHAVIOR_TREE_SETUP.md);
- [Footsteps, semantic Hearing, and crouch](FOOTSTEPS.md);
- [Automation and PIE verification](TESTING.md).

Native tag ownership and registered strings are indexed in
[Gameplay Tag declarations](GAMEPLAY_TAGS.md).

Hover, RMB single-Actor selection, outline stencil ownership, optional world-space widgets,
multi-interaction Smart Object catalogs, exact Gameplay Action requests, claim lifecycle, semantic
Intent Replay, selected GridWorld cells, World State cleanup, and input arbitration are documented in
[Selection and world-space interaction UI](SELECTION_AND_INTERACTION.md).

The selected Actor's read-only PuzzleSystem circuit, default multi-threaded Distributed Repulsive,
Standard/Multi-Thread execution, calculation lifecycle delegates, Ordered Bundles and deprecated
legacy routing strategies, WireTarget authoring, GridWorld surface use, Input/Output stencil
ranges, and wire-material contract are documented in
[Puzzle Circuit Overlay](PUZZLE_CIRCUIT_OVERLAY.md).

## Player movement

`AParadoxPlayerController` no longer applies direct movement input or calls `SimpleMoveToLocation`.
When the destination input is released, it submits a semantic `MoveToGridCell` request to the
possessed `AParadoxCharacter`.

The controller uses the ready-to-use definition:

`/GameplayActionsGridWorld/Definitions/DA_GameplayAction_MoveToGridCell`

The definition remains configurable on derived controller Blueprints. `RequestMoveToGridCell` is
also Blueprint-callable for other input or UI flows.

The project movement contract uses `BP_GridQueryFilter_Balanced` for prediction, exact injection and
execution. Its Pawn occupancy policy is **Ignore** because **Reserved Corridor** is the owner-aware
authority along the route. `AParadoxPlayerController::MoveGoalContentionPolicy` defaults to
**Stop Before Occupied**. Clicking another Pawn therefore preserves the path computed toward that
Pawn but presents and executes it only through the immediately preceding cell. This is a path-prefix
decision, not an arbitrary neighbor redirect; the effective predecessor is atomically claimed.

Each newer player movement request receives a higher priority. This lets it preempt an older action
holding the Movement lock while preserving action submission, interruption, and completion in the
Intent Replay journal.

Player crouch is also semantic rather than a direct Character call. `RequestSetCrouched` submits
`GameplayAction.Type.Paradox.Character.SetCrouched` with the absolute `DesiredCrouched` parameter through
`/Game/Data/GameplayActions/DA_ParadoxSetCrouched`. The action owns only
`GameplayAction.Lock.Stance`; movement owns only `GameplayAction.Lock.Movement`. Exact lock
matching therefore applies crouch or uncrouch immediately while an active movement remains
`Running`. `BlockedPolicy=Queue` affects only another concurrent stance owner, not movement.
Intent Replay records and reproduces both absolute stance transitions.

Time Travel is likewise semantic. `RequestTimeRewind` on the player controller submits
`GameplayAction.Type.Paradox.TimeLoop.TimeTravel` through
`/Game/Data/GameplayActions/DA_ParadoxTimeTravel`. Its high-priority exact locks for Movement,
Stance and TimeTravel stop the current move and reject duplicate departure commands without
affecting the immutable track. Every `AParadoxCharacter` owns an inherited, non-auto-activating
`TimeTravelNiagaraComponent`; assign a finite Niagara System in the Character Blueprint. The
player rewinds after the system finishes, while a replay clone is then hidden and removed from
perception, collision and GridWorld occupancy. An empty Niagara component selects the immediate
fallback for both roles.

The controller now owns inherited `UGridCellPointerComponent` and `UGridPathPreviewComponent`
components. For a local mouse, `PlayerTick` continuously resolves the hovered cell and requests a
prediction; semantic deduplication means unchanged cursor cells do not run another path search.
Touch updates the same components while the gesture is active. Paradox keeps the pointer's semantic
hover target but disables `bApplyHoverToVisualization` by default, so line-only preview and active
paths do not show an independent yellow destination cell. Enable that pointer option explicitly when
a cell-hover affordance is desired; selection remains a separate gameplay/UI operation.

Configure the inherited preview component to enable either renderer independently and assign
separate `Grid Cell Visual Style` and `Grid Path Line Visual Style` assets. Configure accepted-path
presentation only on the inherited `UGridWorldPathFollowingComponent`: its master, cell-overlay,
and strict-line flags are the authoritative settings. At Begin Play the controller enables only the
global backends selected by that component and never overwrites its authored values. Consequently,
a line-only preview and active path do not implicitly enable cell rendering.

On release, the controller calls `PreparePreviewForCommit`. This synchronously refreshes a stale or
start-changed preview and submits `PathSource = ExactInjectedPath` to the Gameplay Action. Failure to
produce a committable preview is observable and does not silently fall back to a destination-only
move. Once accepted, the preview is cleared and the path follower owns the active presentation.
`RequestMoveToGridCell` remains the Blueprint destination API; `RequestMoveAlongGridPath` is the
Blueprint exact-payload API.

Hovering a cell occupied or claimed by another Pawn produces the preview failure
`GoalOccupied` and disables commit. Execution repeats the test through an atomic goal claim, so a
second Pawn that arrives between preview and click causes the action to fail with no fallback.
`UGridWorldPathFollowingComponent` automatically gives the possessed player Pawn a transient,
non-blocking occupancy identity; the Pawn's own footprint is ignored by its requests.

## Tactical planning

World pause does not disable player planning. `AParadoxPlayerController` performs its full tick while
paused, and its mouse and touch destination Input Actions are configured to trigger during pause.
Consequently the GridWorld pointer continues resolving hovered cells and the path preview continues
replanning whenever the pointer moves or the grid revision changes.

When Tactical Pause begins, `UTacticalPauseActionQueueComponent` pauses the character's
`UGameplayActionComponent`. A running action enters its normal Gameplay Actions paused state and
retains its execution locks. Selecting a destination still commits the exact preview path, but the
move is submitted to the adapter's replaceable next-action slot instead of starting immediately.
The selected goal remains marked while the cursor can continue previewing alternatives. Selecting a
different destination cancels only the previous queued plan and replaces it, so obsolete movement
orders do not accumulate.

When Tactical Pause ends, the adapter resumes the scheduler only if it owns that scheduler pause.
The planned action then participates in normal priority and execution-lock scheduling; movement path
presentation transfers from the preview to the active GridWorld path follower. An externally paused
scheduler remains paused.

The planning slot is action-type agnostic. Future command presenters can create any valid
`FGameplayActionRequest` and pass it to `SubmitOrReplaceNextAction`. The request is forced to Queue
policy and remains part of the same Gameplay Actions and Intent Replay lifecycle. This is currently a
single "next action" slot, not an ordered multi-action timeline. Whether an action starts alongside or
after another action on resume remains governed by its Gameplay Actions execution locks.

## Free camera

Time-loop maps use a controller-owned `AParadoxCameraRig` as an independent orthographic view
target while the controller continues possessing the player Character. `AParadoxCameraBoundsVolume`
contains the complete projected view, not only the focus point. W/A/S/D pans, the mouse wheel
zooms, Space recenters, and Q/E rotate left/right in exact 90-degree steps. These actions continue
during Tactical Pause without changing Common UI focus or input mode. Quarter turns use
configurable duration/easing and are rejected without changing zoom when the complete intermediate
footprint cannot remain inside the camera volume. The effective zoom-out ceiling is derived
dynamically from the current volume, margin, aspect ratio, and complete yaw arc, so ordinary camera
use keeps every quarter turn available without changing zoom on Q/E.

See [Paradox free camera - Milestones 5-6](Camera.md) for setup, configuration, containment
formula, Blueprint API, debugging, and troubleshooting.

## Level requirements

Player movement requires:

- a possessed `AParadoxPlayerCharacter`;
- the controller's `UGridWorldPathFollowingComponent`;
- valid navigation data for the character;
- GridWorld runtime data covering the requested destination;
- a walkable destination cell.

The action projects the selected world point to the center of the GridWorld cell before building the
navigation path. If setup or projection fails, the action ends with an observable failure result and
the controller writes a warning through `LogParadox`.

`UGridWorldPathFollowingComponent` consumes the movement style authored on each
`GridNavigationBoundsVolume`. Use **Center-Constrained** or **Cell-by-Cell** when the physical
character movement must pass through the grid's center gates instead of using Unreal's standard
waypoint acceptance.

## Time loop

See [Paradox Time Loop V0](TimeLoop.md) for Chrono Spawn selection, semantic recording, World State
reset, synchronized clone preparation/playback, authoritative temporal perception, paradox
recovery, Game Over, Level Complete, presentation hooks, and Blueprint APIs.

## Puzzles

See [Pressure Plate](PRESSURE_PLATE.md) for the concrete Blueprint class, collision and tag setup,
single-occupant behavior, movement feedback, semantic Hearing, WorldState restoration, extension
events, and debug controls.

See [Vertical Barrier](PARADOX_VERTICAL_BARRIER.md) for the concrete Receiver-driven rising barrier,
GridWorld passage bounds, safe/lift occupant policies, locomotion lock ownership, WorldState restore,
PerceptionKnowledge state, Blueprint events, and PIE validation.
