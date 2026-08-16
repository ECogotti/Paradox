# Perception and response policy integration

## Component layout

- Every `AParadoxCharacter` owns `UIntentReplayObservationComponent` alongside Intent Replay.
- Player and clone controllers own `UPerceptionKnowledgeListenerComponent`.
- Controllers also own `UPerceptionKnowledgeHearingRangeRendererComponent`.
- Clones own the behavior coordinator and investigation executor.
- The time loop initializes player observation recording, stores the resulting Timeline Bundle,
  and initializes clone comparison against the matching bundle.

`AParadoxItemSlotActor` also owns a native `UPerceptionKnowledgeSourceComponent`. It publishes
Active, Occupied, Locked, and Removable as persistent event-driven states. It deliberately does not
publish item compatibility because compatibility depends on the requester inventory and traits.
Occupancy, lock/activity notification, destruction, and WorldState completion refresh these states
without Tick. See [Paradox insertable items and item slots](ITEM_SLOTS.md).

Clone temporal-paradox vision remains separate from native AI Sight. Its line traces deform a
collisionless procedural cone around occluders. A clone-owned collisionless sphere shape,
automatically sized from the cone radii in Blueprint construction and at runtime, drives an
explicit Pawn-only overlap query; it is not a persistent moving physics body. Distance, angle, and
occlusion filters decide whether a candidate reaches Temporal Index evaluation. Native AI Sight
still cannot generate a paradox.

The listener profile is shared data. The hearing renderer reads profile, effective range, and Body
Actor through read-only listener APIs; it follows possession changes without ticking. Its mesh has
no collision, overlap, or navigation effect. Gameplay visibility and debug visibility are separate;
debug rendering requires both `PerceptionKnowledge.Debug 1` and the renderer's local debug flag.
For Controller ownership, the configured component creates its visible transient primitive on the
possessed Pawn because Unreal Controller Actors are hidden; `GetRendererDiagnostic` reports every
missing readiness condition.

`AParadoxPlayerController` overrides `GetActorEyesViewPoint`: eye location comes from the possessed
Pawn and view rotation comes from the Pawn's actor rotation. This is required because the
independent top-down camera leaves `ControlRotation` unrelated to the astronaut's physical facing,
and UE's `Attach to Pawn` Controller option only follows position. Native AI Sight and the
Perception Knowledge debug cone now consume the same Pawn-facing direction. Clone controllers keep
their normal AI gameplay-eyes path.

Assign the project Data Asset to the `Profile` property of the controller's
`PerceptionKnowledgeListener` component. That component-level assignment is authoritative. The
controller-level `PerceptionProfile` property is retained only as a native/legacy fallback when the
Listener is empty or still references the controller-owned default profile. During `BeginPlay`
(player) or `OnPossess` (clone), the selected profile is deliberately reapplied even when its
pointer did not change. This ordering is required for dynamically spawned Blueprint controllers:
their native sense configs may register with constructor defaults before Blueprint component
template values are serialized. The reapplication updates native Sight, Hearing, and the Hearing
renderer together; time travel and clone reconstruction must never replace an authored Listener
profile with the 1500/1800/3000 fallback.

## Policy ownership

Priorities belong to project policy `UParadoxObservationResponsePolicy`. `PerceptionKnowledge` and
`IntentReplayPerception` remain neutral producers of observations and comparisons.

The native default policy is:

| Observation | Rule ID | Priority | Result |
|---|---|---:|---|
| Sight state `PerceptionKnowledge.State.Paradox.Computer.Powered`, unexpected value/status | `Sight.ComputerPowered.High` | 300 | Investigate |
| Sight state, unexpected value/status | `Sight.UnexpectedState.Default` | 200 | Investigate |
| Hearing event, unexpected observation | `Hearing.UnexpectedEvent.Default` | 100 | Investigate |
| Matched, duplicate, raw Sight without mismatch | none | 0 | Ignore |
| Verified `ObserverCaused` | none | 0 | Ignore |

Higher values are more important. Create a `Paradox Observation Response Policy` Data Asset and
assign it on the clone coordinator to replace or extend these rules. Rules filter by observation
type, comparison result, mismatch reason, sense, semantic tag, source category, confidence, and
allowed behavior mode. They never inspect concrete Actor classes. When several rules match, the
highest-priority rule wins.

## Authority filtering

The coordinator accepts a comparison only when its Playback Session ID, Observation Track ID, and
Journal ID exactly match the current run. Foreign and late callbacks cannot change mode.

While `Investigating`:

- lower priority is journal/debug only;
- equal priority is journal/debug only, preventing oscillation;
- higher priority replaces the single current target immediately;
- no priority creates a queue.

The replacement is validated before commit. If destination or GameplayActions preflight is invalid,
the current investigation remains authoritative.

## Native PIE fixtures

`AParadoxSemanticStateCube` exposes a configurable bool state and visual material/color feedback.
Its default state is `PerceptionKnowledge.State.Paradox.Computer.Powered`. Its native
`UPerceptionKnowledgeSourceComponent` already has Sight enabled, fills the native stimuli-sense
list in `OnRegister`, registers itself with AI Perception, and exposes newly-created semantic
states through Sight. Do not call `Register for Sense` from the Blueprint `BeginPlay`; that node is
redundant. For an overlap-driven child Blueprint, overlap on gameplay channels and `Block` on
`Visibility` are compatible with Sight as long as the trace can resolve the cube Actor.

`SetPowered` is absolute and logs every real bool transition once. Do not connect an unfiltered
`ActorBeginOverlap` directly to a `FlipFlop`: both the current player and replay clones can enter
that overlap, so a later clone can execute the opposite branch and restore the recorded value
before Sight reacquisition. Filter `Other Actor` to the intended mutator (for example the current
player-controlled Pawn), or use two explicit test commands that call `SetPowered(true/false)`.
Collision overlap does not identify who is authorized to mutate semantic state.

`AParadoxSemanticNoiseSphere` exposes `EmitSemanticNoise`, overlap emission, cooldown, and one-shot
configuration. It calls only `UPerceptionKnowledgeSourceComponent::EmitSemanticNoise`; it never
contacts a listener directly.

## Character footsteps

Every Paradox Character also owns a Hearing-only semantic Source and
`UParadoxFootstepNoiseComponent`. The adapter converts immutable generic `FFootstepEvent` values
into native semantic noise. It preserves the contact location and owning Character as instigator,
and uses:

```text
PerceptionKnowledge.Event.Paradox.Noise.Character.Footstep
PerceptionKnowledge.Cause.Paradox.CharacterMovement.Footstep
```

The default astronaut noise profile uses listener-controlled range and scales loudness/strength by
the generic normalized intensity. Crouch suppression prevents emission before PerceptionKnowledge,
so suppressed contacts never enter Observation Tracks. Audio and Niagara remain owned by the
generic component and are unaffected.

See [Footsteps, semantic Hearing, and crouch](FOOTSTEPS.md) for setup and validation.

## Timeline-stable Source identity

Strict event matching includes the source Entity ID. The time loop therefore stores
`AvatarPerceptionEntityId` beside each consolidated Timeline Bundle without modifying either
immutable track. At run closure the player Source ID is captured, then that Source is disabled and
unregistered. Every clone reconstruction assigns the captured ID before source registration; the
next player run receives a fresh ID before it is enabled.

This makes a reconstructed temporal avatar perceptually identical to its original timeline:
footsteps from T0 that T1 actually heard are `Matched` when T1 replays. The policy still sees a
different Entity ID for a truly new source, so strict matching and unexpected-Hearing
investigation remain unchanged. Duplicate IDs, assignment failures, and registration mismatches
block reconstruction instead of silently degrading comparison. An action-only legacy timeline may
lack an ID only when perceptual comparison is disabled.

Paradox also enables
`FIntentReplayObservationMatchOptions::bTreatVerifiedCausalEventsAsOccurrenceIdentity`. A footstep
emitted while T0 is executing one replay-owned action inherits that action's stable
`RecordedIntentId` during both T1 recording and T1 replay. The exact verified correlation matches
one expected occurrence even if investigation, continuity recovery, or a fresh injected path
changes its timestamp, position, loudness, or strength. Matching still requires the same Source
Entity ID, Event Tag, Sense, Cause and Instigator, and consumes exactly one record. Extra footsteps
and noises from a new Source remain unexpected.

`CorrelatedReplayIntent` identifies the external Source's event; it is not an ignore reason.
When T0 has no matching expected event for a newly created T1 clone, T1's verified replay
correlation remains attached to the `UnexpectedObservation` and the normal Hearing rule moves T0
to `Investigating` with priority 100. Only `ObserverCaused + Verified` is filtered by
`bIgnoreVerifiedSelfCausedObservations`.

Paradox enables
`bTreatPersistentStateObservationsAsOrderedSnapshots` with strict persistent identity. If T0 saw
`PerceptionKnowledge.State.Paradox.Computer.Powered=false`, lost Sight, and recorded a later reacquisition, that second State
snapshot remains pending even when replay movement reacquires the cube more than 0.25 seconds
late. Reacquiring it as `true` produces `UnexpectedStateValue`, so
`Sight.ComputerPowered.High` starts priority-300 investigation. Snapshot order, Entity ID,
State Tag and Sense remain strict; an unobserved snapshot only expires when comparison completes.

Every successful transition into `Investigating` emits one `LogParadox` warning containing result,
mismatch reason, Source ID, semantic tag, current/expected time and location, causal intent,
priority, rule and revision. This is transition-driven diagnostics, not a per-frame log.

Observation comparison follows the full Action Track `RecordedDuration`, not merely the lifetime
of its last action. A clone that reaches its final cell and replays a recorded ten-second idle tail
therefore continues receiving unexpected Hearing/Sight comparisons for all ten seconds. When the
recorded Time Travel action begins, a clone disables its listener and temporal-vision authority so
a late stimulus cannot divert the terminal departure VFX; its semantic Source remains visible to
other listeners until the VFX completes and the time loop retires it.
