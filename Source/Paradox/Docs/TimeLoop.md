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
  -> AwaitingSynchronizedStart (runtime spawn selection remains open)
  -> ActiveRun
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

An accepted temporal paradox, Player death, or shared-global Oxygen depletion branches from
`ActiveRun` to `ParadoxFailure`. Before the first timeline has been consolidated,
presentation-authorized recovery returns to the initial blocking `ChronoSpawnSelection`. Once a
consolidated timeline exists, recovery uses the same forced-Tactical-Pause/runtime-selection start
path as a normal rewind. All use `FParadoxRunFailureContext`; its reason distinguishes
`TemporalParadox`, `PlayerDeath`, and `GlobalOxygenDepleted`, while Player death retains damage type,
instigator, and causer. Consolidating the final playable timeline enters `GameOver`. An external
puzzle authority can branch from `ActiveRun` to `LevelComplete`.

## Chrono Spawns and recording

Place enabled `AParadoxChronoSpawn` actors on distinct reachable GridWorld cells. Each native Actor
owns an Automatic `UPuzzleReceiverComponent`, a `UParadoxSelectableComponent`, a
`UParadoxInteractionComponent` containing the non-spatial Spawn interaction, and a native
`UWorldStateParticipantComponent`. It deliberately owns no Smart Object component or Definition.
The participant restores the placed Actor transform and `bChronoSpawnEnabled`, but does not capture
existence, puzzle activation, timeline assignment, or presentation state. A spawn with one or more incoming Puzzle links is active
only while its Receiver is active; a spawn with no incoming links is active by default. World State
restoration temporarily makes every spawn inactive, then activation is recomputed from the restored
Receiver state and current graph topology. The Actor exposes presentation states `Available`,
`Inactive`, `Hovered`, `Selected`, `Occupied`, and `Disabled`.

Chrono Spawn has no native state label and applies no native state-dependent material, visibility,
collision, or scale effect. `SelectionMesh` is initialized once at relative scale `(1,1,1)` and its
scale is never changed by Chrono Spawn state transitions. Author presentation manually in the
Chrono Spawn Blueprint through either `ReceiveVisualStateChanged`, whose native implementation is
intentionally empty, or the Blueprint-assignable `OnChronoSpawnStateChanged` delegate. The delegate
provides the Chrono Spawn plus previous and new state; the authoritative state is already committed
before either hook executes. `ReceiveStateInitialized` is a separate Blueprint Native Event called
after initial `BeginPlay` reconciliation and after every successful Time Loop world reset. Use it to
apply or rebuild effects even when the reconciled state value did not change, in which case the
ordinary change event correctly remains silent. `GetChronoSpawnState()` supplies the current state.

The selection mesh is Visibility query-only and never blocks movement. Chrono Spawns use the same
generic hover/selection authority as every other selectable Actor: RMB selects on mouse and touch
release uses the same `UParadoxSelectionComponent` path. Selection never spawns a Character; it
only shows Puzzle connections and the optional interaction widget. Enabled inactive and occupied
spawns remain selectable so the player can inspect the circuit, while
`CanAssignToNewTimeline()` remains false for both states.

Chrono Spawn's Spawn action uses
`EParadoxInteractionExecutionMode::ExecuteWithoutSmartObject`, so request execution skips slot
resolution, claim, GridWorld pathing, and requester movement while retaining normal semantic
validation, action locks, journaling, replay, effect checks, and cleanup. Its selectable opts into
Puzzle connection rendering and opts out of interaction-cell presentation. Do not add duplicate
Selectable, Receiver, Interaction, or World State participant components to a Chrono Spawn
Blueprint. See
[Selection and world-space interaction UI](SELECTION_AND_INTERACTION.md).

The interaction widget's Spawn button calls
`UParadoxTimeLoopComponent::RequestChronoSpawnInteraction` with the selected Actor. The code hook
`CanRequestInteraction(Interaction.Paradox.ChronoSpawn.Spawn)` is refreshed when activation,
assignment, or the Time Loop phase changes; it is true only when a new spawn is needed and the
target is active and free. A valid button request:

- starts the player recorder before submitting the spawn request;
- submits `DA_ParadoxChronoSpawn` as the first semantic Gameplay Action in the run;
- materializes and enables the player at the chosen active spawn;
- assigns role `Player` and the next numeric Temporal Index;
- marks the spawn `Selected`;
- enters `AwaitingSynchronizedStart`.

The Time Loop and native Chrono Spawn catalog resolve
`/Game/Data/GameplayActions/DA_ParadoxChronoSpawn` by default. The asset must use
`UParadoxChronoSpawnActionDefinition`, the standard replay-safe `Target` and `InteractionTag`
parameters, Required journaling, and non-spatial execution mode.

`OnChronoSpawnSelected` is immediate. `OnRunStarted` is delayed until the synchronized barrier
actually releases. After a rewind, `OnRunStarted` may precede `OnChronoSpawnSelected` when the
technical clone-readiness barrier releases before the player chooses a spawn.

After at least one timeline has been consolidated, reset reconstructs all clones in dormant
gameplay state, prepares their replays, initializes the hidden Player recorder, opens runtime spawn selection, enters
`AwaitingSynchronizedStart`, and calls only `UTacticalPauseWorldSubsystem::RequestPause`. There is
no post-rewind delay or Time Loop timer. An already-paused World is accepted only when the Tactical
Pause subsystem owns a pause that `RequestPlay` can later release; external pause conflicts or
application failures roll back to safe spawn selection with diagnostics.

The Spawn interaction during forced pause records and executes only the Chrono Spawn system action, then
restores the player Gameplay Action scheduler pause. It materializes the Player, enables its
perception listener, source, collision and GridWorld occupancy, and refreshes active Temporal
Vision filters without releasing Tactical Pause or starting queued planned work. If clone
readiness releases first, clones, recorder and `ActiveRun` become authoritative while the World
remains paused. The Player can stay hidden, non-collidable, unoccupied and perceptually disabled
while its empty prefix is recorded from the global run epoch; Play may resume clones before
selection, and a later selection activates the Player without restarting either clock. The
Gameplay HUD remains visible throughout selection and pause.

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

The replay clone executes the same action and VFX. `CloneTimeTravelCompletionBehavior`, editable on
the GameMode's time-loop component, controls completion:

- `EnterGoap` (default) finishes the recorded action, then performs the GOAP handoff on the next
  tick. Replay, investigation, Behavior Tree, Gameplay Actions and movement stop, while the clone
  remains visible, collidable, GridWorld-occupied, semantically observable and consuming Oxygen.
  Its perception listener and Temporal Vision stay disabled by Time Travel. A failed handoff keeps
  it stationary and is reported through playback diagnostics.
- `RetireInPlace` preserves the legacy behavior: listener, semantic Source and temporal detection
  are disabled; movement and GridWorld occupancy are released; collision is disabled; and the
  Actor is hidden until the next reconstruction.

With no Niagara System the action still completes immediately; the default GOAP handoff remains
deferred to the next tick to avoid reentrant interruption of the completing action.

## Synchronized start barrier

Every run, including the first run without clones, passes through the same technical barrier. The
first run reaches it only after the blocking spawn selection. Post-reset runs enter it immediately
after acquiring Tactical Pause. The first run does not force Tactical Pause.

For each reconstructed clone the loop calls `PrepareReplay` using:

- strict schema compatibility;
- `StopPlayback` on submission rejection;
- `StopPlayback` on terminal Gameplay Action failure.

The barrier waits until every clone is either `Ready` or has entered stationary `Failed` fallback;
there is no gameplay delay gate. Preparation callbacks are correlated with both clone and playback
Session ID; stale callbacks from an earlier run are ignored. Play can be pressed before readiness:
the World resumes, but recorder/replay authorization still waits for this technical barrier.

At barrier release, in one logical frame, the loop:

1. starts the player recorder, whether or not a runtime spawn has already been selected;
2. authorizes every ready clone coordinator;
3. enters `ActiveRun`;
4. broadcasts `OnRunStarted`.

After a reset these steps are allowed while the World is paused. The existing Tactical Pause
events update its widget; the Time Loop does not mutate button state directly and does not
implicitly resume after a spawn selection.

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

At restore start each Chrono Spawn drops its transient activation and generic selection
availability through its participant lifecycle. World State restores the authored transform and
enabled flag. After a successful restore the spawn derives activation again from the restored
Automatic Receiver, while the Time Loop reapplies timeline assignment independently and then calls
the state-initialization hook with the final reconciled state. A failed restore leaves the spawn
unavailable and reports the failure.

Smart Object interaction claims follow the same Gameplay Action lifecycle as movement and stance.
Step 3 is therefore the authority that aborts a running `UParadoxInteractionActionBase` and releases
its claim before WorldState mutation begins. The selection component's restore-start callback is
presentation-only: it clears hover, outline, widget context, cached interaction options, and cell
overlays, but never cancels actions or releases claims. Do not move claim ownership into WorldState,
IntentReplay, or selection cleanup.

An accepted interaction is journaled semantically by Gameplay Actions. Its replay payload keeps the
Definition identity, soft world-authored `Target`, exact `InteractionTag`, and any authored action
parameters. A reconstructed clone resolves current slots, GridWorld position, and a new Smart
Object claim at playback time; runtime slot/claim/cell handles are never stored in the immutable
track.

Intent Replay tracks live in the transient package. The reflected consolidated-timeline array owns
each full Timeline Bundle across reset. An empty finalized Action Track is valid. Callers receive
value copies and cannot mutate the coordinator's storage, Replay Track, or Observation Track.
Explicit legacy action-only timelines still replay with a warning and no perceptual comparison.

## Stable perception identity

`FParadoxConsolidatedTimeline::AvatarPerceptionEntityId` is the stable perceptual identity of the
avatar that originally produced that run. A reconstructed clone receives this ID while its
`UPerceptionKnowledgeSourceComponent` is disabled and unregistered, before deferred spawning
finishes. The clone may join the synchronized replay barrier while still dormant; Source
registration and exact ID equality are validated only when its recorded Chrono Spawn action
materializes it.

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

Clone reconstruction and clone materialization are intentionally separate. A reconstructed clone
starts hidden, non-collidable, outside GridWorld occupancy, without active perception or Oxygen,
and exposes `WaitingForRecordedTime` in its temporal spawn snapshot. When replay reaches the
recorded `GameplayAction.Type.Paradox.TimeLoop.ChronoSpawn` entry:

- an active target spawn materializes the clone and changes the state to `Materialized`;
- an inactive target spawn changes the state to `PendingActivation`, leaves the spawn action
  running, and pauses only that clone's Intent Replay session;
- a later Receiver activation materializes the clone, completes the action, and resumes that same
  replay session from its preserved clock offset.

There is no timeout or fabricated fallback position for pending materialization. Other clones and
the current player continue independently. Invalid/destroyed targets or failed materialization use
the structured Gameplay Action failure path and mark the temporal spawn state `Failed`.

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

The same transient validation tolerance is always applied to replayed Drop approach paths. A Drop
cannot redirect to another approach cell because its semantic target must retain the recorded
ordinary predecessor; `RejectOccupied` remains authoritative at the approach destination. When an
`InvalidStart` recovery creates a fresh exact path, it retains this tolerance instead of becoming
stale as soon as the clone publishes a new traffic reservation.

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
shape its procedural field-of-view mesh around occluders. That mesh is visual and always uses
`NoCollision`; trace-derived `BeginOverlap` and `EndOverlap` events are also disabled and never
authorize a paradox. Mesh deformation therefore remains exact without creating or cooking a
procedural physics body.

Each clone instead owns `TemporalVisionCandidateSphere`, a collisionless query shape attached to
the Temporal Vision component. It never registers a large moving body in the physics broad phase.
Temporal Vision performs one explicit sphere overlap query restricted to the `Pawn` object channel
and ignores every other channel. Its unscaled radius is synchronized to the largest
configured cone radius during native construction, Blueprint construction, preparation, and
runtime tick, so changing `Radius1` or `Radius2` on the inherited component requires no duplicated
sphere setting.

Normal runtime changes are observed on the next Temporal Vision tick. Blueprint code that requires
same-frame reconciliation after changing cone parameters can call `Refresh Temporal Candidate
Filter`; it resizes the shape, executes the Pawn-only query, and reevaluates active candidates once.

A Pawn inside the sphere becomes authoritative only after it passes the configured inner/outer
distance and half-angle checks and a clear-line query on `MeshOcclusionTraceChannel`. The filter is
reevaluated while detection is authoritative, so a Pawn already inside the sphere is detected when
the clone rotates toward it and is rejected again when it leaves the cone or becomes occluded.
At the synchronized barrier the loop builds and refreshes the collisionless visual meshes with
detection passive. After recorder and ready replay sessions start and the loop enters `ActiveRun`,
it enables detection and immediately queries already-existing sphere candidates in the same
logical frame.

Candidate state is deduplicated per Observer Actor/Target Actor while retaining the number of
overlapping Pawn primitives. Multiple target components therefore do not produce duplicate
candidates for one authorized session.

`/Game/Data/EntityRelations/DA_ParadoxTimeLoopRelations` is the per-world
`UEntityRelationPolicySet`. Its `UParadoxTemporalOrderingPolicy` evaluates the
`VisualPerception` domain and is deliberately non-cacheable:

- `ObserverIndex < TargetIndex` denies the relation with outcome
  `Relation.Outcome.Paradox.FutureObserved`;
- reverse or equal ordering is safe;
- self-overlap, missing identity, non-temporal actors, invalid indices, stale sessions, and failed
  relation queries are ignored with a copied diagnostic snapshot.

The first valid future-observation candidate creates an immutable `FParadoxContext` with both
actors and the representative Pawn broad-phase components, Temporal Indices, generation, detection
session, cause,
a copied `FEntityRelationResult`, positions, and diagnostics. The loop accepts only one paradox
per run, enters `ParadoxFailure`, blocks player gameplay, disables every detection component,
cancels the partial player recording, stops clone replay and Gameplay Actions, and invalidates
stale callbacks. Consolidated tracks remain unchanged.

## Recovery, Game Over, and Level Complete

At full black, the controller-owned outcome presenter acknowledges the run-failure event. The loop
then destroys only runtime clones, restores the World State baseline, reapplies occupied spawn
states, reconstructs consolidated timelines, and releases the failed run's selected spawn. With
consolidated timelines, recovery opens runtime selection and forces the same resumable Tactical
Pause as a normal rewind; without one, it returns to the initial blocking
`ChronoSpawnSelection`. A failure on the last selectable spawn remains retryable because the
partial run was never consolidated.

In `PerPawn`, the persistent Player is reactivated with `ResetHealth` and `ResetOxygen`, while
reconstructed Clones are newly spawned at full Health and Oxygen. In `SharedGlobal`, successful
Time Travel promotes the current World reservoir to the next checkpoint and never refills it;
run-failure recovery restores the exact failed-run checkpoint and clears all transient Oxygen
participants/effects. Shared depletion is accepted once as `GlobalOxygenDepleted`, without killing
each Pawn reentrantly. Player consumption is active only after spawn selection; replay and terminal
GOAP Clones count, while hidden/dead/retired avatars do not. Tactical Pause and simulation speed
work through Unreal's paused, dilated game-time clock in both modes. See
[Paradox Oxygen System](OXYGEN_SYSTEM.md).

The native presentation fallback uses real time and never changes input mode, mouse capture, or UI
focus:

- paradox: `TIMELINE COLLAPSE` and
  `T{Observer} witnessed T{Target}. The past saw the future.`;
- Player death: `LIFE SIGNS LOST` and a distinct recoverable-run message;
- shared depletion: `OXYGEN RESERVE DEPLETED` and a distinct checkpoint-recovery message;
- Game Over: `NO TIMELINES REMAIN` and `The loop has no future left`;
- completion: `LEVEL COMPLETE`.

Run-failure presentation fades to black, authorizes recovery, holds briefly, then fades back to
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
- `Continue Run Failure Recovery` for the current event ID;
- `Continue Paradox Recovery`, retained as a paradox-specific compatibility wrapper;
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
  detection session, authority, filtered actor pairs, and broad-phase primitive count;
- last copied temporal candidate, paradox context, and generic run-failure context;
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
- temporal overlap, ignored candidate, paradox accepted, run failure accepted, recovery completed;
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
- `InvalidConfiguration` mentioning Chrono Spawn Definition: confirm
  `/Game/Data/GameplayActions/DA_ParadoxChronoSpawn` exists, uses
  `UParadoxChronoSpawnActionDefinition`, exposes `Target` plus `InteractionTag`, uses Required
  journaling, and executes without a Smart Object slot.
- `MissingPlayer`: the first controller must possess `AParadoxPlayerCharacter`.
- `RecordingFailed`: verify Gameplay Actions and Intent Replay components and initialization.
- `SynchronizedStartFailed`: the player recorder could not start; clone sessions were cancelled
  and the loop returned to selection.
- `PlaybackFailed`: inspect `Get Last Clone Playback Failure`; only the indicated clone was frozen.
- `TemporalDetectionFailed`: verify that reconstructed clones inherit exactly one
  `UParadoxTemporalVisionComponent` and one `TemporalVisionCandidateSphere`, and that the target
  primitive uses the `Pawn` object channel with query collision enabled. Target-side overlap events
  are not required.
- A visible actor that produces no paradox should be checked against sphere range, configured cone
  half-angle, and `MeshOcclusionTraceChannel`. The procedural mesh itself is deliberately
  collisionless and the plugin's trace-derived events are not temporal authority.
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
session, and filtered pair count. Lines to accepted sphere candidates show whether a candidate was
already delivered; target labels show the overlapping Pawn primitive count. Setting
`Paradox.TimeLoop.Debug 0` immediately disables all Paradox temporal debug draw.

Line-of-sight trace drawing has its own global gate, `LineOfSight.Debug`, and is not evidence that a
filtered temporal candidate was accepted.
