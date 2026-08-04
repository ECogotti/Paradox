# Paradox Time Loop - V0 (Milestones 0-12)

## Purpose and authority

The Paradox time loop coordinates Chrono Spawn selection, semantic intent recording, World State
reset, clone reconstruction, synchronized run start, isolated clone playback, authoritative
temporal perception, paradox recovery, Game Over, and Level Complete. The authoritative
`UParadoxTimeLoopComponent` is owned by the single `AParadoxGameMode` and initializes from
`StartPlay`, after actors and World State participants have completed `BeginPlay`.

The component is disabled by default. `/Game/TopDown/MA_Playground` enables it through
`BP_TimeLoopGameMode`; `/Game/TopDown/Lvl_TopDown` remains on its legacy camera and gameplay flow.
Do not place a second loop authority in the level.

## Runtime flow

The implemented successful cycle is:

```text
LevelPreparation
  -> ChronoSpawnSelection
  -> RunPreparation
  -> AwaitingSynchronizedStart
  -> ActiveRun
  -> RewindPreparation
  -> WorldReset
  -> TimelineReconstruction
  -> ChronoSpawnSelection
```

At startup the component:

1. validates the required independent camera;
2. discovers enabled `AParadoxChronoSpawn` actors;
3. finalizes World State registration;
4. adopts a valid baseline or captures one when absent;
5. hides and blocks the possessed player until a spawn is selected.

When no resettable puzzle participant exists, the loop creates a transient, externally managed
World State anchor. It captures no gameplay state and exists only because World State intentionally
rejects an empty baseline.

Invalid requests return `FParadoxTimeLoopOperationResult` without changing phase. Failures after
irreversible recording or reset mutations enter `Error` and retain diagnostics.

An accepted temporal paradox branches from `ActiveRun` to `ParadoxFailure`, then returns to
`ChronoSpawnSelection` after presentation-authorized recovery. Consolidating the final playable
timeline enters `GameOver`. An external puzzle authority can branch from `ActiveRun` to
`LevelComplete`.

## Chrono Spawns and recording

Place enabled `AParadoxChronoSpawn` actors on distinct reachable GridWorld cells. Each one
contributes one playable timeline and exposes presentation states `Available`, `Hovered`,
`Selected`, `Occupied`, and `Disabled`.

`ReceiveVisualStateChanged` is a Blueprint Native Event. Its native implementation provides the
fallback mesh scale, visibility, label, and color for every state. A Blueprint override can call
the parent to extend that presentation, or omit `Call to Parent` to replace all native Chrono Spawn
visual effects completely. The event is presentation-only; `NewState` has already been committed
by the authoritative loop.

The selection mesh is Visibility query-only and never blocks movement. Click/touch selects only
during `ChronoSpawnSelection`; the same pointer commits movement only during `ActiveRun`.

Selection immediately:

- moves and enables the player at the chosen spawn;
- assigns role `Player` and the next numeric Temporal Index;
- marks the spawn `Selected`;
- initializes the player recorder without starting it;
- enters `AwaitingSynchronizedStart`.

`OnChronoSpawnSelected` is immediate. `OnRunStarted` is delayed until the synchronized barrier
actually releases.

The default rewind input is `IA_Rewind`, currently mapped to Enter in `IMC_Default`. The player
controller's `RequestTimeRewind` submits `/Game/Data/GameplayActions/DA_ParadoxTimeTravel`; it no
longer invokes reset directly. UI and alternative input should use that controller command so Time
Travel enters the immutable replay track. `UParadoxTimeLoopComponent::RequestTimeRewind` remains
the internal authoritative consolidation/reset operation and is called only after the recorded
action completes.

Every `AParadoxCharacter` owns an inherited `TimeTravelNiagaraComponent`, disabled by default.
Assign a non-looping Niagara System to this component in the player/clone Character Blueprint. A
player Time Travel action preempts movement, blocks new movement/stance input, activates the
component, and schedules the authoritative rewind on the next tick after `OnSystemFinished`. With
no Niagara System it rewinds immediately through the same recorded path.

The replay clone executes the same action and VFX. At completion the time loop retires the clone in
place: listener, semantic Source and temporal detection are disabled; movement and GridWorld
occupancy are released; collision is disabled; and the Actor is hidden. Retaining the Actor until
the next reconstruction keeps playback delegate and runtime-array ownership stable. With no
Niagara System the clone is retired immediately.

## Synchronized start barrier

Every run, including the first run without clones, passes through the same barrier.

For each reconstructed clone the loop calls `PrepareReplay` using:

- strict schema compatibility;
- `StopPlayback` on submission rejection;
- `StopPlayback` on terminal Gameplay Action failure.

The barrier waits until every clone is either `Ready` or has entered stationary `Failed` fallback.
Preparation callbacks are correlated with both clone and playback Session ID; stale callbacks from
an earlier run are ignored.

At barrier release, in one logical frame, the loop:

1. starts the player recorder;
2. authorizes every ready clone coordinator;
3. enters `ActiveRun`;
4. broadcasts `OnRunStarted`.

The Replay Behavior Tree task is the only caller that starts the already prepared clone replay;
observation comparison is armed before authorization. No recorder or replay starts before the
barrier. If the player recorder cannot start, prepared
clone sessions are stopped/unbound, the selected spawn is released, the player is deactivated, and
the loop returns to `ChronoSpawnSelection` with `SynchronizedStartFailed`.

Before rewind or clone destruction, active replay sessions are stopped and all replay delegates are
removed.

## Rewind, World State, and track ownership

A legal rewind:

1. rejects new gameplay input and enters `RewindPreparation`;
2. stops and unbinds clone playback;
3. aborts player Gameplay Actions with the system-reset result;
4. finalizes the player recording synchronously in `Immediate` mode;
5. validates and retains the immutable Timeline Bundle (Action Track plus Observation Track), while
   retaining `ReplayTrack` as a compatibility view in the reflected consolidated-timeline record;
6. saves the registered player `PerceptionKnowledge` Entity ID in that consolidated timeline;
7. marks the selected Chrono Spawn occupied;
8. disables and unregisters the player Perception Source, then destroys only loop-created runtime
   clones;
9. restores the World State baseline;
10. reapplies occupied Chrono Spawn states;
11. reconstructs every consolidated timeline in Temporal Index order.

Intent Replay tracks live in the transient package. The reflected consolidated-timeline array owns
each full Timeline Bundle across reset. An empty finalized Action Track is valid. Callers receive
value copies and cannot mutate the coordinator's storage, Replay Track, or Observation Track.
Explicit legacy action-only timelines still replay with a warning and no perceptual comparison.

## Stable perception identity

`FParadoxConsolidatedTimeline::AvatarPerceptionEntityId` is the stable perceptual identity of the
avatar that originally produced that run. A reconstructed clone receives this ID while its
`UPerceptionKnowledgeSourceComponent` is disabled and unregistered, before deferred spawning
finishes. Registration and exact ID equality are validated before the clone can join the
synchronized start.

The player Source is disabled at the end of a run. Selecting the next Chrono Spawn assigns a fresh,
collision-checked ID before re-enabling it, so T0, T1, and the current player are distinct live
sources. Reconstructing T0 repeatedly still produces the same T0 ID. Consequently a T0 footstep
heard and recorded during the original T1 run has the same strict event key when T1 is later
replayed and is `Matched`; a genuinely new source still produces an unexpected observation.

Identity reassignment or registration collisions are blocking setup failures with diagnostics.
Legacy action-only timelines may omit the ID only because they also disable perceptual comparison.

Every newly activated player run establishes standing as its deterministic stance baseline before
recording starts. Crouch and uncrouch after that point are absolute, instantaneous Gameplay Actions
and therefore remain in the immutable Action Track.

When the player occupies the last available timeline, rewind still finalizes and retains that
timeline, marks the final Chrono Spawn `Occupied`, and enters `GameOver`. It does not reconstruct a
future run that cannot exist.

## Temporal avatars and clone playback

Every `AParadoxCharacter` owns:

- `UGameplayActionComponent` and `UIntentReplayComponent`;
- `UEntityIdentityComponent` for generic Entity Relations identity;
- `UParadoxTemporalEntityComponent` for role, numeric Temporal Index, and optional source track.

`AParadoxPlayerCharacter` owns the fallback character camera and Tactical Pause planning adapter,
but no World State participant or temporal vision. `AParadoxCloneCharacter` owns an externally
managed `UWorldStateParticipantComponent`, `UParadoxTemporalVisionComponent`, has no player camera,
and uses `AParadoxCloneController`.

Clone playback state is recipient-local and separate from the immutable track:

```text
Unprepared -> Preparing -> Ready -> Playing -> Completed
                                   \-> Failed
Any prepared/active state ----------> Stopped
```

Movement is enabled only for the clone whose replay starts. Completion, failure, and explicit stop
freeze that clone's controller, Character Movement, and Gameplay Actions.

Replay remains `Playing` through the track's full `RecordedDuration`, even when the last Gameplay
Action ended earlier. This preserves a recorded idle tail: perception comparison and the Behavior
Tree Replay branch remain authoritative during a final ten-second standstill instead of stopping
at the last movement's completion.

Exact GridWorld paths carry the query context of the controller that created them, including the
requesting Pawn's occupancy identity. `UParadoxCloneReplayExecutionStrategy` therefore copies each
prepared clone movement request and re-stamps its exact cell sequence for that clone's controller
immediately before submission. If investigation moved the clone, only an `InvalidStart` path under
`RecalculateToOriginalGoal` is replaced by a fresh controller-aware exact path from the current
cell; one contextual warning is emitted. The consolidated `UIntentReplayTrack` is never modified.
Topology, filter, traversal, link, goal, and destination-contention validation remain authoritative.

Replay/Investigation behavior, priorities, recovery, and the native BT setup are documented in
[CLONE_BEHAVIOR.md](CLONE_BEHAVIOR.md).

The native clone strategy enables `bOverrideGoalContentionPolicy` by default and applies
`RedirectOnCompletion` only to the runtime request copy. This lets a clone preserve the recorded
route while `ReservedCorridor` coordinates moving agents, then claim a nearby free destination if
the recorded final cell is occupied. A Blueprint subclass can disable the override to preserve the
recorded player policy, or select another `EGridGoalContentionPolicy`; neither option mutates the
immutable replay track.

When that default override is active, the strategy stamps the replay `ExactInjectedPath` with
transient dynamic-conflict tolerance from the beginning. This is necessary even when the route is
free at submission time: later occupancy publications from moving clones must not invalidate the
recorded sequence and force repeated recalculation. The strategy does not replace the request with
destination pathfinding and does not remove or reorder recorded cells. Static navigation
validation remains strict, while the materialized path keeps `ReservedCorridor`: the clone
therefore follows the original sequence and waits at temporary intermediate conflicts instead of
repathing away from it. If the final cell is still reserved when the clone reaches its predecessor,
the follower hands the conflict to `RedirectOnCompletion`; if the other clone has moved away first,
the original destination completes normally.

The loop also owns each temporal avatar's GridWorld presence. Deactivating the player or destroying
a runtime clone releases its traffic corridor/parking record and disables its non-reservation
occupancy. Player activation teleports first and then republishes occupancy at the selected Chrono
Spawn. A hidden player or a clone from the previous reconstruction can therefore never leave a
ghost occupied destination.

A preparation or playback failure:

- affects only that clone;
- does not stop the player or other clones;
- does not mutate the track;
- leaves clone identity and Temporal Index valid;
- does not create a paradox;
- keeps the failed clone visible and stationary.

The retained failure snapshot includes clone, Temporal Index, Session ID, executor state, recorded
intent ID, track entry index, Action/Reason tags, terminal message, current clone position, and the
intended `GoalLocation` for `MoveToGridCell` intents.

## Temporal Vision and paradox authority

Every clone's `UParadoxTemporalVisionComponent` derives from `ULineOfSightComponent`. Line traces
shape its procedural field-of-view mesh around occluders, but trace-derived `BeginOverlap` and
`EndOverlap` events are disabled and never authorize a paradox. The authority is procedural-mesh
collision delivered through native `OnComponentBeginOverlap` and `OnComponentEndOverlap`.

The component is `QueryOnly`, object type `WorldDynamic`, overlaps the configurable temporal target
channel (`Pawn` by default), ignores every other channel, and uses synchronous collision cooking.
At the synchronized barrier the loop builds and refreshes all meshes with detection passive. After
recorder and ready replay sessions start and the loop enters `ActiveRun`, it enables detection and
immediately reconciles already-existing physical overlaps in the same logical frame.

Overlap state is deduplicated per Observer Actor/Target Actor while retaining the number of
overlapping target primitives. Collision rebuilds or multiple target components therefore do not
produce duplicate candidates for one authorized session.

`/Game/Data/EntityRelations/DA_ParadoxTimeLoopRelations` is the per-world
`UEntityRelationPolicySet`. Its `UParadoxTemporalOrderingPolicy` evaluates the
`VisualPerception` domain and is deliberately non-cacheable:

- `ObserverIndex < TargetIndex` denies the relation with outcome
  `Paradox.Relation.Outcome.FutureObserved`;
- reverse or equal ordering is safe;
- self-overlap, missing identity, non-temporal actors, invalid indices, stale sessions, and failed
  relation queries are ignored with a copied diagnostic snapshot.

The first valid future-observation candidate creates an immutable `FParadoxContext` with both
actors and physical overlap components, Temporal Indices, generation, detection session, cause,
a copied `FEntityRelationResult`, positions, and diagnostics. The loop accepts only one paradox
per run, enters `ParadoxFailure`, blocks player gameplay, disables every detection component,
cancels the partial player recording, stops clone replay and Gameplay Actions, and invalidates
stale callbacks. Consolidated tracks remain unchanged.

## Recovery, Game Over, and Level Complete

At full black, the controller-owned outcome presenter acknowledges the paradox event. The loop
then destroys only runtime clones, restores the World State baseline, reapplies occupied spawn
states, reconstructs consolidated timelines, releases the failed run's selected spawn, and returns
to `ChronoSpawnSelection`. A paradox on the last selectable spawn is still retryable because the
failed partial run was never consolidated.

The native presentation fallback uses real time and never changes input mode, mouse capture, or UI
focus:

- paradox: `TIMELINE COLLAPSE` and
  `T{Observer} witnessed T{Target}. The past saw the future.`;
- Game Over: `NO TIMELINES REMAIN` and `The loop has no future left`;
- completion: `LEVEL COMPLETE`.

Paradox presentation fades to black, authorizes recovery, holds briefly, then fades back to
gameplay. In headless worlds or without a local presenter, recovery is immediate. Game Over and
Level Complete remain terminal and show Restart. `RequestRestartLevel` stops runtime systems and
reopens the current map, constructing a fresh World, GameMode, and loop.

`RequestLevelComplete` is valid only in `ActiveRun`. It cleanly stops detection, the partial
recording, and replay, then enters `LevelComplete`. The puzzle or objective system owns the victory
rule and calls this API; the time loop never invents one.

`UParadoxOutcomeWidget::ReceiveOutcomeDataChanged` and
`UParadoxOutcomePresentationComponent::ReceiveOutcomePresentationStarted` are Blueprint Native
Events. Derived Blueprints may call the parent to extend the complete native fallback, or omit it
to replace layout and animation. Custom presentation must preserve the presenter's recovery
acknowledgement contract and should not change input mode or focus.

## Blueprint API and events

Primary commands:

- `Initialize Time Loop`;
- `Select Chrono Spawn`;
- `Request Time Rewind`;
- `Continue Paradox Recovery` for the current event ID;
- `Request Level Complete`;
- `Request Restart Level`.

Primary loop queries:

- current phase and last structured operation result;
- selected Chrono Spawn;
- maximum and consolidated timeline counts;
- copied consolidated timeline records;
- whether gameplay movement is allowed;
- whether temporal detection is authoritative, how many vision participants exist, and the
  aggregate deduplicated actor-pair count;
- a copied Temporal Vision debug snapshot by Temporal Index, including local/global debug gates,
  detection session, authority, actor pairs, and physical primitive count;
- last copied temporal candidate and last paradox context;
- copied Game Over and Level Complete contexts.

Playback queries return copies:

- `Get Clone Playback Participant Count`;
- `Get Clone Playback Snapshot` by Temporal Index;
- `Get Last Clone Playback Failure`.

Events:

- phase changed, spawn selected/rejected;
- synchronized start awaiting;
- run started/ended;
- timeline consolidated, World State reset, clone reconstructed;
- clone ready, playback started/completed/failed/stopped;
- temporal overlap, ignored candidate, paradox accepted, recovery completed;
- Game Over, level completed, and restart requested;
- operation failed and terminal error.

No public API exposes the internal runtime arrays or a mutable replay track.

The controller command named `Request Time Rewind` returns success when the Time Travel action is
accepted; the actual phase change occurs after its Niagara component finishes. A second request is
rejected while that action is pending.

## Camera dependency

An enabled time loop requires exactly one valid `AParadoxCameraBoundsVolume`. Camera setup failure
returns `CameraConfigurationFailed` and prevents loop startup. See [Camera.md](Camera.md) for the
volume, settings, input, formulas, Tactical Pause behavior, and debug controls.

The loop deactivates the possessed player before validating downstream camera, component, and
World State dependencies. A startup error therefore remains visible through phase/result
diagnostics without leaving a visible or collidable temporal avatar on a Chrono Spawn.

## World State ownership

Puzzle and level actors that must rewind should use World State participants. The player
deliberately does not participate: activation, transform, collision, and temporal assignment belong
to the loop.

Clone participants capture transforms but not existence and use `ExternallyManaged`. The loop is
the sole authority that destroys and reconstructs clones around a baseline restore. Do not add a
second World State participant in `BP_CloneCharacter`.

## Troubleshooting

- `CameraConfigurationFailed`: inspect the controller's camera initialization result and confirm
  exactly one valid enabled volume. An incompatible logical-center override also prevents both
  free-camera creation and entry into `ChronoSpawnSelection`.
- `InvalidConfiguration`: confirm enabled Chrono Spawns and configured native clone classes.
- `MissingPlayer`: the first controller must possess `AParadoxPlayerCharacter`.
- `RecordingFailed`: verify Gameplay Actions and Intent Replay components and initialization.
- `SynchronizedStartFailed`: the player recorder could not start; clone sessions were cancelled
  and the loop returned to selection.
- `PlaybackFailed`: inspect `Get Last Clone Playback Failure`; only the indicated clone was frozen.
- `TemporalDetectionFailed`: verify that reconstructed clones inherit exactly one
  `UParadoxTemporalVisionComponent`, collision cooking succeeded, and the target channel overlaps
  the target primitive's object channel.
- A visible actor that produces no paradox should be checked with physical collision
  visualization, not the plugin's trace-derived events. Temporal authority requires actual
  procedural-mesh overlap during `ActiveRun`.
- `RelationQueryFailed`: verify that
  `/Game/Data/EntityRelations/DA_ParadoxTimeLoopRelations` loads, validates, contains
  `UParadoxTemporalOrderingPolicy`, and both actors have registered
  `UEntityIdentityComponent` instances.
- Repeated physical primitives for one target should raise the deduplicated component count, not
  additional candidate events.
- A recorded movement replacement (a later click interrupting the current `MoveToGridCell`) is
  replay-owned preemption and does not enter stationary fallback. A higher-priority action outside
  that clone's replay session remains a terminal playback failure.
- `ParadoxRecoveryFailed`: inspect World State restore and clone reconstruction diagnostics. The
  accepted paradox context and consolidated tracks remain available for inspection.
- `GameOverReached` is the expected result of rewinding the final playable run; it is not an error.
- `LevelCompleteReached` is expected only after an external `RequestLevelComplete`.
- Repeated `FilterMismatch` followed by `Blocked` on a clone indicates that clone requests are not
  using `UParadoxCloneReplayExecutionStrategy`, or that a custom clone bypassed deferred
  reconstruction. Loop-created clones enforce the strategy even when their Blueprint saved another
  default.
- A visually free destination reported as occupied should be checked for a non-loop actor or
  authored GridWorld reservation. Inactive temporal avatars release both occupancy and traffic
  parking before reset, so they are not valid blockers.
- `WorldStateFailed`: inspect World State diagnostics and participant selection.
- `CloneSpawnFailed`: configure classes derived from `AParadoxCloneCharacter` and
  `AParadoxCloneController`.
- Movement rejected outside `ActiveRun` and a duplicate rewind during transition are intentional.
- `Time Travel Action Definition is not configured`: confirm
  `/Game/Data/GameplayActions/DA_ParadoxTimeTravel` is assigned on the player controller.
- A Time Travel VFX that never rewinds is normally a looping Niagara System. Use a finite system;
  an unassigned system deliberately selects the immediate fallback.

All module diagnostics use `LogParadox`; hover, camera tick, and replay polling do not emit
high-frequency logs.

## Debug

Temporal Vision visual debugging is off by default and requires both controls:

1. enable `bEnableDebug` on the specific inherited `TemporalVisionComponent`;
2. set `Paradox.TimeLoop.Debug 1`.

The one-frame overlay labels the clone, Temporal Index, passive/authoritative state, detection
session, and deduplicated pair count. Lines to current physical targets show whether a candidate
was already delivered; target labels show the overlapping primitive count. Setting
`Paradox.TimeLoop.Debug 0` immediately disables all Paradox temporal debug draw.

Line-of-sight trace drawing has its own global gate, `LineOfSight.Debug`, and is not evidence that a
physical temporal overlap occurred.
