# IntentReplay Perception Timeline — Milestone 2

## Task purpose

Extend the existing `IntentReplay` plugin with generic support for recording and comparing semantic observations produced by the `PerceptionKnowledge` plugin.

This milestone must implement:

- a synchronized immutable `Observation Track` recorded during the original run;
- a mutable `Observation Journal` for one replay execution;
- deterministic comparison between currently perceived observations and the source Observation Track;
- explicit and separate matching rules for observed states and perceived events;
- shared use of the existing IntentReplay recording and playback clocks;
- correlation between the Action Replay Track and the Observation Track without merging their data;
- detailed runtime, textual, and visual comparison debugging;
- stable public events that a later project-level module can use to switch a clone from Replay to Investigating.

This milestone must not implement clone behavior or project-specific reactions.

In particular, it must not contain:

- Replay, Investigating, or GOAP behavior states;
- Behavior Trees or StateTrees;
- clone classes;
- Temporal Index rules;
- paradox generation;
- the dynamic visual-cone overlap rule;
- investigation movement or search behavior;
- GOAP planning or Belief-State conversion;
- concrete puzzle logic;
- project-specific rules deciding which discrepancy must trigger a state change.

The module records and compares observations. It does not decide what an agent should do after a discrepancy.

---

# Mandatory preliminary rules

Before modifying the project, Codex must:

1. read the root `AGENTS.md`;
2. locate and read all relevant `CODEX` instructions for `IntentReplay`, `PerceptionKnowledge`, and any touched module;
3. read the existing `IntentReplay` and `PerceptionKnowledge` user documentation;
4. inspect the current `IntentReplay` implementation instead of assuming that the original architecture document exactly matches the final code;
5. identify the existing recording, playback, timing, pause, resume, finalization, and journal APIs;
6. inspect the actual Unreal Engine version and all Engine APIs used;
7. make the smallest backward-compatible extension that satisfies this milestone;
8. compile the appropriate editor target after every meaningful change;
9. update user-facing documentation inside `Docs`;
10. not consider the task complete until the affected target compiles successfully.

Do not redesign the existing action replay system unless a verified limitation prevents the observation extension from being implemented safely.

---

# Binding architectural decisions

## 1. Extend the existing plugin, do not create a competing replay system

The existing `IntentReplay` core remains the authoritative system for:

- action recording sessions;
- immutable Action Replay Tracks;
- action Execution Journals;
- playback sessions;
- relative timeline clocks;
- pause and resume;
- deterministic timestamp and sequence ordering;
- stable recorded-intent identity.

This milestone must reuse those concepts rather than creating a second independent timeline clock.

Required conceptual structure:

```text
IntentReplay plugin
├── IntentReplay core module
│   ├── Action Replay Track
│   ├── Action Execution Journal
│   ├── Recording Session
│   ├── Playback Session
│   └── shared relative timing contracts
│
└── IntentReplayPerception runtime module
    ├── Observation Track
    ├── Observation Journal
    ├── recording adapter
    ├── comparison session
    ├── matching policy
    └── comparative debug
```

The observation feature should normally be implemented as a new optional runtime module inside the existing plugin:

```text
IntentReplayPerception
```

Do not add a direct `PerceptionKnowledge` dependency to the core `IntentReplay` module unless the actual repository structure proves that a separate module is impossible. The preferred dependency direction is:

```text
IntentReplayPerception → IntentReplay
IntentReplayPerception → PerceptionKnowledge
IntentReplay → GameplayActions
```

Never introduce:

```text
IntentReplay → IntentReplayPerception
PerceptionKnowledge → IntentReplay
GameplayActions → IntentReplayPerception
```

The core action replay functionality must remain usable without enabling AI Perception or `PerceptionKnowledge`.

---

## 2. Action and observation data share a timeline but remain different tracks

An action request and a perceived observation have different semantics.

Example:

```text
Action:
    Interact with Door_A

Observation:
    Door_A emitted Event.Noise.Door

Observation:
    Door_A State.Open became true
```

The action must be replayed.

The observations must be compared with what is perceived during replay. They must not be submitted as executable actions.

Required invariant:

```text
Action Replay Track
    immutable list of semantic action requests to submit again

Observation Track
    immutable list of semantic observations expected from the source run
```

Do not:

- insert observations into `FRecordedIntent`;
- represent hearing as a fake Gameplay Action;
- make the action playback scheduler execute observation entries;
- mix action runtime results into observation records;
- mutate an Action Replay Track to store comparison results.

---

## 3. Observation Track and Observation Journal must remain separate

This separation is mandatory and must mirror the existing Replay Track versus Execution Journal architecture.

```text
Observation Track
    what the original observer perceived during the recorded run

Observation Journal
    what the current observer perceived during one replay attempt,
    how it was matched, and why it was classified
```

A finalized Observation Track is immutable.

A playback comparison must never:

- consume records by mutating the source track;
- overwrite recorded values;
- mark source records as matched inside the track object;
- rewrite timestamps to fit the current run;
- remove records because they were not observed.

Consumed-record state, comparison results, duplicate detection, and current runtime observations belong to the Observation Journal or comparison session.

---

## 4. PerceptionKnowledge remains the owner of current knowledge

`PerceptionKnowledge` produces semantic observations and retains the current Knowledge Store of each observer.

This milestone must subscribe to its public immutable observation notifications.

It must not duplicate or replace the current Knowledge Store.

Required information flow:

```text
World
    ↓
PerceptionKnowledge
    ├── produces state/event observations
    └── updates current observer knowledge
            ↓
IntentReplayPerception
    ├── records observations into the source Observation Track
    └── compares current observations during playback
```

The future GOAP system will obtain current knowledge from `PerceptionKnowledge`, not by replaying the Observation Journal.

The Observation Track is historical reference data, not the GOAP Belief State.

---

## 5. State observations and event observations remain explicitly different

The explicit distinction introduced by `PerceptionKnowledge` must be preserved throughout recording, matching, journaling, debugging, and Blueprint APIs.

### State observation

A state observation describes a value perceived on an entity.

Examples:

```text
(PC_04, State.Powered) = true
(Door_02, State.Open) = false
(Terminal_01, State.Broken) = true
```

State comparison must consider:

- entity identity;
- state tag;
- known/unknown/invalidated status;
- typed value;
- sense;
- relative time window;
- optional location and confidence rules.

### Event observation

An event observation describes something that occurred at one moment.

Examples:

```text
Event.Noise.Impact.Heavy
Event.Device.PoweredOn
Event.Door.Slammed
```

Event comparison must consider:

- event tag;
- source entity identity;
- instigator when available;
- sense;
- time window;
- location tolerance;
- strength/confidence policy;
- optional causal correlation.

Do not use one ambiguous optional-field structure whose meaning changes implicitly.

A discriminated wrapper may be used, but the payload types and matching paths must remain explicit.

---

# Required core IntentReplay extension

The observation module must synchronize with existing IntentReplay sessions without accessing private mutable state.

Codex must first inspect whether the following capabilities already exist.

Only add missing APIs, and keep additions generic, read-only, and backward-compatible.

## Required session information

The observation module needs access to a safe snapshot equivalent to:

```text
Recording Session ID
Playback Session ID
Source Track ID
Session-relative time
Session state
Paused state
Timeline rate or timing context when relevant
Deterministic sequence allocation or equivalent ordering support
```

Possible conceptual API:

```cpp
FIntentReplayTimelineClockSnapshot GetRecordingClockSnapshot() const;
FIntentReplayTimelineClockSnapshot GetPlaybackClockSnapshot() const;
```

The actual API name and shape must follow the existing implementation.

Do not expose mutable Recording Session or Playback Session internals.

## Required lifecycle notifications

The observation module needs safe notifications equivalent to:

```text
OnRecordingStarted
OnRecordingPaused
OnRecordingResumed
OnRecordingStoppedAccepting
OnRecordingFinalized
OnRecordingFailed
OnRecordingCancelled

OnPlaybackStarted
OnPlaybackPaused
OnPlaybackResumed
OnPlaybackStopped
OnPlaybackCompleted
OnPlaybackFailed
```

Reuse existing events where available.

If new events are required, they must:

- use immutable payloads;
- be emitted exactly once for each authoritative transition;
- remain valid under teardown and reentrancy;
- not allow an observer to mutate the core session;
- not mention PerceptionKnowledge or Paradox.

## Shared clock requirement

Observation timestamps must come from the same logical relative clock used by the corresponding IntentReplay recording or playback session.

Do not use world time as the authoritative matching time.

World time may be retained only as diagnostic metadata.

Required behavior:

```text
IntentReplay recording pauses
    → observation recording relative time stops

IntentReplay recording resumes
    → observation recording continues from the same relative offset

IntentReplay playback pauses
    → observation comparison relative time stops
```

No second timer may accumulate independent drift.

## Deterministic sequence ordering

Every recorded observation must preserve:

```text
Relative timestamp
Deterministic sequence index
```

Timestamp alone is insufficient because multiple actions and observations may occur at the same logical time.

Prefer a generic timeline-ordering service already present in `IntentReplay`.

If none exists, add the smallest generic extension needed to allocate deterministic sequence values for synchronized external channels.

Do not depend on:

- pointer addresses;
- hash-map iteration order;
- delegate registration order;
- frame-dependent unordered traversal.

---

# Proposed module structure

Adapt paths to current repository conventions while preserving responsibilities.

```text
Plugins/IntentReplay/
├── IntentReplay.uplugin
├── CODEX/
├── Docs/
└── Source/
    ├── IntentReplay/
    │   ├── Public/
    │   ├── Private/
    │   ├── CODEX/
    │   └── Docs/
    │
    └── IntentReplayPerception/
        ├── IntentReplayPerception.Build.cs
        ├── Public/
        │   ├── Components/
        │   ├── Data/
        │   ├── Matching/
        │   ├── Settings/
        │   └── Types/
        ├── Private/
        │   ├── Components/
        │   ├── Data/
        │   ├── Matching/
        │   ├── Debug/
        │   └── Tests/
        ├── CODEX/
        └── Docs/
```

Do not create empty folders solely to match this example.

Create or modify an editor module only if editor-only validation or visualization genuinely requires it.

Runtime modules must never depend on editor code.

---

# Module dependencies

The new runtime module may depend on:

```text
Core
CoreUObject
Engine
GameplayTags
IntentReplay
PerceptionKnowledge
DeveloperSettings — when required
GameplayDebugger — only when actually used and correctly guarded
```

It must not depend on:

```text
ParadoxGameplay
GoalAgents
GridWorld
WorldState
EntityRelations
SmartObjectsModule
BehaviorTree editor modules
any concrete puzzle plugin
```

The existing `IntentReplay` core module must preserve its current generic dependency boundary.

---

# Primary runtime component

Create a component conceptually named:

```cpp
UIntentReplayObservationComponent
```

The final name may follow existing conventions.

The component is the primary runtime owner for:

- binding to one `UPerceptionKnowledgeListenerComponent` or equivalent public listener API;
- binding to one `UIntentReplayComponent`;
- source observation recording;
- Observation Track builder lifetime;
- finalized Observation Track ownership or publication;
- playback comparison-session lifetime;
- Observation Journal lifetime;
- match policy selection;
- pause, resume, enable, disable, stop, and teardown;
- debug data for the selected observer and timeline.

It must not own the observer's Current Knowledge Store.

## Binding

Support:

- explicit designer-assigned references where appropriate;
- explicit C++ initialization;
- safe automatic discovery on the same owner as a convenience;
- binding across different owners when explicitly configured.

Cross-owner binding is required because the original-run architecture may use:

```text
PlayerController
    PerceptionKnowledge listener

Possessed Pawn or Character
    IntentReplay action component
```

Do not repeatedly search the world.

Cache resolved references with correct Unreal lifetime handling.

## Binding failure

A missing required component must produce an explicit initialization result and useful diagnostics.

Do not silently:

- record without a valid IntentReplay clock;
- compare without a source Observation Track;
- subscribe to a listener while no valid timeline is bound;
- use world time as an unannounced fallback.

## Lifecycle symmetry

All delegates and registrations must be unbound during:

- component deactivation;
- `EndPlay`;
- owner destruction;
- IntentReplay source replacement;
- PerceptionKnowledge listener replacement;
- world teardown.

Late callbacks must not revive a terminal recording or comparison session.

---

# Observation recording session

Create a transient session conceptually named:

```cpp
UIntentReplayObservationRecordingSession
```

or an equivalent private/runtime type.

It is the sole mutable builder of one future Observation Track.

## Recording states

Use explicit states such as:

```text
Created
Recording
Draining
Finalized
Failed
Cancelled
```

Align lifecycle semantics with the existing IntentReplay Recording Session where possible.

The observation session must be linked to one authoritative IntentReplay recording session.

It must not continue accepting observations after the bound IntentReplay session stops accepting new source-track data.

## Start behavior

Starting observation recording must:

1. verify valid IntentReplay and PerceptionKnowledge bindings;
2. verify that the IntentReplay recording session is active;
3. capture the source Track ID and Recording Session ID;
4. use the same relative clock;
5. create a unique Observation Track ID;
6. create empty mutable observation data;
7. reset deterministic observation ordering state;
8. subscribe to immutable observations from PerceptionKnowledge;
9. emit one structured recording-start event.

Prefer automatic synchronized startup from IntentReplay lifecycle events after bindings are configured.

Also provide an explicit controlled setup path for C++ integration.

## Stop and finalization behavior

When the bound IntentReplay recording stops accepting new actions:

- stop accepting new observations immediately;
- unbind or gate observation callbacks safely;
- validate observation IDs and ordering;
- finalize the immutable Observation Track;
- associate it with the finalized Action Replay Track;
- emit one finalized event.

Observation recording normally does not need to wait for outstanding gameplay actions to end, because it records perceived observations rather than action lifecycle.

However, it must use the authoritative IntentReplay finalization order and must not finalize with an invalid or unknown source Track ID.

If the Action Replay Track finalization fails or is cancelled, the observation-session policy must explicitly choose between:

```text
Cancel observation data
Keep diagnostic observation data without creating a valid timeline bundle
```

The default should avoid publishing an authoritative paired timeline when the source Action Replay Track is invalid.

---

# Observation Track

Create a Blueprint-visible immutable runtime data object conceptually named:

```cpp
UIntentReplayObservationTrack
```

## Required track metadata

Store at least:

```text
ObservationTrackId
FormatVersion
SourceIntentReplayTrackId
SourceRecordingSessionId when appropriate
RecordedDuration
Observation entries
Track tags or generic metadata
Source label/context without a hard runtime Actor dependency
Finalization state
```

Recommended stable identifier:

```cpp
FIntentReplayObservationTrackId
```

It must be:

- invalid by default;
- comparable and hashable;
- Blueprint-visible;
- stable for the finalized track lifetime;
- distinct from Track IDs, Recorded Intent IDs, runtime action handles, and observation-entry IDs.

`FGuid`-backed identifiers are acceptable when implemented consistently.

## Immutability

A finalized Observation Track must:

- own copied observation data;
- expose read-only queries;
- expose no public mutable entry array;
- never retain authoritative runtime Actor pointers;
- remain unchanged during every playback attempt;
- not store match or consumed state;
- not store the current observer's Knowledge Store.

## Runtime data object

The first implementation may use transient runtime UObjects.

Do not require editor asset creation in packaged builds.

Persistent save-game export and a custom timeline editor are outside this milestone.

---

# Timeline bundle

Create a generic pairing object or value conceptually named:

```cpp
UIntentReplayTimelineBundle
```

or an equivalent immutable association.

The bundle should contain read-only references to:

```text
Action Replay Track
Observation Track
Shared timeline identity or source Track ID
Format metadata
```

The bundle must not merge the entries into one executable array.

It exists to ensure that a project-level system cannot accidentally pair observations from one run with actions from another run.

Required validation:

- both tracks are finalized;
- the Observation Track references the correct Action Replay Track ID;
- format versions are supported;
- durations are coherent according to policy;
- source context is compatible.

If the existing IntentReplay architecture already has an appropriate generic container, extend it minimally instead of creating a duplicate concept.

---

# Recorded observation types

Names may be adapted to existing conventions, but semantics must remain equivalent.

## Recorded observation ID

Create a stable ID such as:

```cpp
FRecordedObservationId
```

It must be distinct from the runtime `ObservationId` produced by PerceptionKnowledge.

The recorded ID identifies one immutable occurrence in one Observation Track.

## Recorded state observation

Conceptual data:

```cpp
FRecordedStateObservation
{
    FRecordedObservationId RecordedObservationId;
    FPerceptionKnowledgeEntityId EntityId;
    FGameplayTag StateTag;
    FPerceptionKnowledgeValue Value;
    EPerceptionKnowledgeFactStatus Status;

    FGameplayTag SenseTag;
    float Confidence;
    FVector ObservationLocation;

    double RelativeTimestamp;
    int64 SequenceIndex;

    optional generic correlation metadata;
}
```

Record a copied semantic snapshot.

Do not retain a pointer to mutable Knowledge Store data.

## Recorded event observation

Conceptual data:

```cpp
FRecordedEventObservation
{
    FRecordedObservationId RecordedObservationId;
    FGameplayTag EventTag;
    FGameplayTag SenseTag;

    FPerceptionKnowledgeEntityId SourceEntityId;
    FPerceptionKnowledgeEntityId InstigatorEntityId;

    FVector WorldLocation;
    float Strength;
    float Confidence;

    FGameplayTag CauseTag;

    double RelativeTimestamp;
    int64 SequenceIndex;

    optional generic correlation metadata;
}
```

## Discriminated recorded wrapper

A wrapper may contain:

```text
Type = State
    RecordedStateObservation

Type = Event
    RecordedEventObservation
```

The type must be explicit and validated.

Do not infer the type from whether a Gameplay Tag happens to use a particular prefix.

---

# Correlation with recorded actions

Observation records may optionally carry generic correlation metadata linking them to an action or causal context.

Conceptual metadata may include:

```text
RecordedIntentId
Runtime action correlation captured during source recording
OriginTag
Instigator entity
CauseTag
External correlation ID
```

This metadata is optional and must not redefine the semantic observation itself.

Preferred use:

```text
Recorded Intent 27
    Interact with Door_A

Recorded Observation 51
    Event.Noise.Door
    CausalRecordedIntentId = 27
```

Codex must inspect the existing GameplayActions and IntentReplay correlation APIs.

If the available APIs are sufficient, reuse them.

If they are insufficient, add only the smallest generic backward-compatible extension.

Do not add clone-specific or puzzle-specific correlation fields to the core modules.

## PerceptionKnowledge compatibility

`PerceptionKnowledge` must remain independent from IntentReplay.

If its immutable observation payload already provides an extension context, copy the required correlation through the adapter.

If not, the `IntentReplayPerception` module may wrap the base observation with additional correlation data resolved from the current IntentReplay/GameplayActions context.

Avoid modifying `PerceptionKnowledge` unless a verified public-contract limitation makes it necessary.

Any required modification must remain generic and backward-compatible.

---

# Observation recording policy

Create a configurable policy conceptually named:

```cpp
UIntentReplayObservationRecordPolicy
```

or an equivalent strategy.

It must independently control state and event recording.

## State recording

The default policy should record meaningful observation occurrences such as:

- first sight acquisition of an entity;
- first observed value of a state;
- an observed state value change while the entity remains visible;
- reacquisition after the entity was lost or forgotten when the observation is semantically meaningful;
- explicit invalidation or transition to Unknown when configured.

Do not record identical state snapshots every frame.

`PerceptionKnowledge` should already avoid high-frequency redundant observations, but the recording policy must still defend against accidental duplication.

Deduplication must not erase semantically distinct occurrences that happen at different points in the timeline.

## Event recording

The default policy should record every accepted semantic event occurrence.

Events may be filtered through configurable Gameplay Tag queries for:

- Sense Tags;
- Event Tags;
- Cause Tags;
- source categories when exposed generically.

Do not hardcode Paradox noise classes.

## Record rejection

Return structured rejection for:

```text
No active synchronized recording
Invalid observation type
Invalid entity identity where required
Unsupported value
Invalid semantic tag
Duplicate runtime callback
Policy filtered
Missing authoritative clock
Internal consistency failure
```

Filtered observations are not errors.

Required-data failures must remain observable.

---

# Observation comparison session

Create a transient runtime owner conceptually named:

```cpp
UIntentReplayObservationComparisonSession
```

It compares observations from one current observer against one finalized source Observation Track during one IntentReplay playback attempt.

## Session state

Use explicit states such as:

```text
Created
Comparing
Paused
Completed
Failed
Cancelled
```

The session must be correlated with:

```text
Playback Session ID
Source Action Replay Track ID
Source Observation Track ID
Current observer identity
Observation Journal ID
```

## Startup validation

Comparison must not start unless:

- the IntentReplay playback session is valid;
- the Action Replay Track is finalized;
- the Observation Track is finalized;
- the tracks belong to the same timeline bundle;
- the PerceptionKnowledge listener is valid;
- the match policy is valid;
- format versions are supported.

A failed validation must return a structured result.

Do not silently run without comparisons.

## Pause and resume

The comparison session must follow IntentReplay playback pause and resume.

While paused:

- the relative comparison clock remains frozen;
- incoming observations must not accidentally match future records;
- policy may either ignore them or write them to the journal as `IgnoredWhilePaused`;
- they must not generate an `Unexpected` result by default.

The project-level Milestone 3 will decide exactly when comparison is enabled during Replay and disabled during Investigating.

This module must provide explicit APIs for controlled enable, disable, pause, resume, and cancellation without knowing behavior-state names.

## Completion

The comparison session may complete when the bound playback session completes or is stopped.

Do not modify the source track to mark unobserved records.

Expected observations that were never received may be reported in the journal or debug summary according to policy, but they must not generate project behavior in this milestone.

---

# Observation Journal

Create a runtime object conceptually named:

```cpp
UIntentReplayObservationJournal
```

The journal records what happened during one comparison attempt.

## Required journal metadata

Store at least:

```text
ObservationJournalId
Playback Session ID
Source Action Replay Track ID
Source Observation Track ID
Current observer identity
Session start/end status
Comparison policy identity/version
Journal entries
Summary counts
```

## Journal entry

Conceptual data:

```cpp
FIntentReplayObservationJournalEntry
{
    FGuid JournalEntryId;

    FPerceptionKnowledgeObservation CurrentObservation;
    optional FRecordedObservationId MatchedRecordedObservationId;

    EIntentReplayObservationMatchResult Result;
    EIntentReplayObservationMismatchReason Reason;

    double CurrentRelativeTime;
    optional double ExpectedRelativeTime;
    optional double TimeDelta;

    optional expected/current value details;
    optional expected/current location details;

    bool bConsumedExpectedRecord;
    int64 JournalSequenceIndex;
}
```

The journal must own copied immutable snapshots required for diagnostics.

Do not rely on a source Actor remaining alive.

## Consumed-record state

The comparison session or journal must retain which recorded observation occurrences have already been matched.

The finalized Observation Track must remain unchanged.

One event occurrence must not satisfy unlimited future events.

State-observation occurrences must also be matched deterministically according to policy.

---

# Match results

Provide a structured result enum conceptually equivalent to:

```text
Matched
UnexpectedObservation
UnexpectedStateValue
UnexpectedStateStatus
Duplicate
Ambiguous
IgnoredByPolicy
IgnoredWhilePaused
ComparisonUnavailable
```

Optional diagnostic-only results may include:

```text
ExpectedRecordExpiredUnobserved
ExpectedRecordPending
```

Do not call every non-match `Unexpected` without preserving the reason.

The project-level integration will decide which results are behaviorally relevant.

## Mismatch reasons

Provide structured reasons such as:

```text
NoCandidateInTimeWindow
EntityMismatch
StateTagMismatch
StateValueMismatch
StateStatusMismatch
EventTagMismatch
SenseMismatch
SourceMismatch
InstigatorMismatch
LocationOutsideTolerance
StrengthOutsideTolerance
CausalCorrelationMismatch
AllCandidatesAlreadyConsumed
AmbiguousBestCandidate
UnsupportedComparison
InvalidCurrentObservation
InvalidRecordedObservation
```

A useful debug message may supplement the enum, but must not replace it.

---

# Matching policy

Create a replaceable strategy conceptually named:

```cpp
UIntentReplayObservationMatchPolicy
```

C++ must provide a useful native default.

Blueprint may specialize policy decisions at intentional extension points, but critical deterministic matching and collection ownership should remain in C++.

## Candidate filtering order

The native default should perform deterministic filtering and scoring.

Recommended order:

1. observation type must match;
2. Sense Tag must be compatible;
3. semantic state key or Event Tag must be compatible;
4. entity/source identity must match according to policy;
5. relative timestamp must fall inside the configured window;
6. optional causal correlation is evaluated;
7. state value/status or event attributes are evaluated;
8. location and strength tolerances are evaluated;
9. consumed candidates are excluded from normal matching;
10. the best candidate is selected deterministically.

## Deterministic candidate selection

If more than one candidate remains, use stable scoring and tie-breaking.

Possible priority:

```text
Exact causal correlation
Exact persistent entity identity
Exact semantic key/tag
Smallest absolute time delta
Smallest location delta
Lowest recorded sequence index
```

The final implementation must document its exact ordering.

Do not use container iteration order as a tie-breaker.

## Strict identity default

The first milestone should default to strict persistent entity identity for states and identifiable event sources.

A configurable semantic fallback may be exposed for non-persistent physical noise sources, but it must be opt-in and clearly debugged.

Do not silently treat two different doors or PCs as equivalent because they emit the same tag.

## Time windows

Use configurable early and late tolerances:

```text
ExpectedTime - EarlyTolerance
ExpectedTime + LateTolerance
```

Separate defaults may exist for:

- state observations;
- hearing events;
- other event categories.

Do not hardcode project tuning values in matching code.

## State-value comparison

Use deterministic typed comparison from `PerceptionKnowledge`.

Support configurable float/vector tolerance only where the value type requires it.

Do not compare formatted strings.

`Unknown`, `Known`, and `Invalidated` must remain semantically distinct.

## Event matching and consumption

A recorded event occurrence should normally be consumed by one matching current event occurrence.

If the same current event callback is delivered twice, classify it as duplicate rather than consuming another expected record.

## Missing expected observations

For this milestone, an expected observation that is never perceived may be:

- retained as pending;
- marked expired for diagnostics after its window;
- included in the end-of-session summary.

It must not directly trigger project behavior.

The later Paradox integration may choose a policy after gameplay validation.

---

# Self-caused and externally justified changes

The generic matcher must preserve enough context for a project-level system to determine whether a discrepancy was caused by the current observer or by an expected replay-owned action.

Useful context may include:

- current observation instigator entity;
- current runtime action correlation;
- recorded causal intent ID;
- replay request origin;
- Cause Tag;
- source entity;
- time relationship to replay-owned actions.

Do not hardcode a result such as `CausedByClone` into the generic module unless the existing generic identity and correlation contracts can establish it reliably.

Preferred design:

```text
Comparison result
    UnexpectedStateValue

Comparison context
    Instigator = current observer
    Causal Recorded Intent = 27
```

The project integration can then classify the discrepancy as behaviorally irrelevant.

If a reliable generic classification can be implemented, expose it as an additional justification field rather than replacing the base match result.

Do not guess causal ownership from proximity alone.

---

# Public events for Milestone 3

Expose stable generic notifications such as:

```text
OnObservationRecorded
OnObservationTrackFinalized
OnObservationCompared
OnObservationMatched
OnObservationUnexpected
OnObservationAmbiguous
OnObservationJournalCompleted
```

The actual delegate names must follow module conventions.

Payloads must include copied immutable context sufficient for the future project module to decide:

- current observation;
- source track and playback IDs;
- result;
- reason;
- matched expected-record ID when available;
- current and expected relative times;
- causal/instigator metadata;
- source entity and world location.

These delegates must not:

- enter Investigating;
- pause or resume IntentReplay automatically;
- modify a Behavior Tree;
- activate GOAP;
- generate a paradox.

Milestone 3 will subscribe and decide behavior.

---

# Detailed comparative debugging

Comparative debug that depends on the Observation Track belongs to `IntentReplayPerception`, not `PerceptionKnowledge`.

`PerceptionKnowledge` remains responsible for generic perception and current-knowledge debug.

This module must add a detailed timeline-comparison overlay using public PerceptionKnowledge debug/query data rather than duplicating source registration.

## Required debug ownership split

```text
PerceptionKnowledge debug
    observable Actors
    current Sight/Hearing perception
    current known states
    recent events
    listener viewpoint and ranges

IntentReplayPerception debug
    expected versus current observations
    matched/consumed records
    discrepancies and reasons
    timeline times and tolerances
    Observation Track and Journal status
```

## Global and local controls

Provide:

- one module-wide global debug enable/disable mechanism;
- per-component local debug enable/disable;
- filters for States, Events, Matched, Unexpected, Ambiguous, Pending, and Consumed;
- selected observer/session filtering;
- negligible runtime cost while disabled.

Effective state:

```text
Global Debug Enabled AND Local Debug Enabled
```

## Actor bounding boxes

For every relevant registered observable Actor associated with the selected observer or source track, draw its bounds.

Required color coding:

```text
Green
    latest relevant current observation matches the source track

Red
    observation differs from the source track and is a candidate discrepancy

Orange
    ambiguous comparison, outside tolerance, or multiple valid candidates

Purple
    discrepancy has generic self-caused or justified correlation metadata
    only when reliably established

White or light blue
    expected observation exists but is not due yet

Dark green
    expected occurrence has already been matched and consumed

Gray
    known/registered Actor with no active comparison result,
    comparison paused, or data unavailable
```

Colors must be configurable in settings rather than scattered hardcoded literals.

If `PerceptionKnowledge` already draws a generic box, avoid drawing an indistinguishable duplicate. Either:

- add a clearly offset comparative box;
- request an extension overlay through a public debug provider;
- or replace only the comparison layer for the selected mode.

Document the chosen integration.

## Observer-to-Actor lines

Draw lines from the selected observer's perception body/viewpoint to relevant Actors.

The line color must match the comparative status.

Labels should include:

```text
Entity ID
Actor name
Observation type
Sense
Expected semantic key/tag
Current semantic key/tag
Expected value
Current value
Expected relative time
Current relative time
Time delta
Match result
Mismatch reason
Recorded Observation ID
Journal Entry ID
Consumed status
```

Avoid unreadable overlap by supporting distance culling, selected-only mode, and text detail levels.

## State detail visualization

For state observations, show per-state rows such as:

```text
State.Powered
    Expected: false @ 10.00
    Current:  true  @ 10.24
    Result: UnexpectedStateValue
    Reason: StateValueMismatch
```

An Actor must be red when at least one behaviorally unresolved state comparison is unexpected, even if other states match.

Also show the count of matching and mismatching states.

## Hearing-event visualization

For hearing observations:

- draw the event location;
- draw the source location when different;
- draw a line or arrow from observer to event;
- show recorded and current location tolerance spheres when enabled;
- show Event Tag, source, instigator, strength, expected/current time, and result;
- keep recent unexpected events visible for a configurable short duration.

## Timeline HUD

Provide a compact textual debug HUD or Gameplay Debugger category showing:

```text
Action Replay Track ID
Observation Track ID
Playback Session ID
Observation Journal ID
Comparison state
Paused/enabled state
Current replay-relative time
Recorded observation count
Matched count
Unexpected count
Ambiguous count
Duplicate count
Pending expected count
Expired-unobserved count
Last comparison result and reason
```

## Track and journal inspection

Expose read-only debug queries for:

- observations near the current relative time;
- matched records;
- consumed records;
- pending records;
- unexpected current observations;
- journal entries for one entity;
- journal entries for one semantic state/event tag.

Do not expose mutable internal arrays.

## Comparative debug must not trigger behavior

Debug drawing and inspection must never:

- alter match results;
- consume records;
- modify the journal;
- pause playback;
- trigger project delegates twice;
- change observer knowledge.

---

# Logging

The module must use one primary log category, conceptually:

```text
LogIntentReplayPerception
```

Provide scoped shortcut macros for:

```text
Info
Warning
Error
```

Do not use committed `LogTemp` calls.

Useful logs should include relevant context:

- component and owner;
- Recording or Playback Session ID;
- Action Replay Track ID;
- Observation Track ID;
- Observation Journal ID;
- observer identity;
- source entity identity;
- Recorded Observation ID;
- match result and reason;
- current and expected relative times.

Do not log every matching observation at normal verbosity by default.

High-volume logging must require explicit debug configuration.

---

# Performance and multi-listener considerations

Observation matching may run frequently and must avoid unnecessary world scans.

Requirements:

- subscribe to PerceptionKnowledge observation events;
- index recorded observations for efficient candidate lookup;
- avoid scanning the entire Observation Track for every current observation;
- index at least by observation type and semantic identity;
- include source entity and time-range lookup when useful;
- avoid allocation-heavy debug data when debug is disabled;
- avoid copying the entire source track per comparison;
- never Tick solely to wait for observations when event-driven callbacks suffice.

Possible indexes:

```text
State observations:
    Entity ID + State Tag + Sense Tag

Event observations:
    Event Tag + Source Entity ID + Sense Tag
```

Time-window filtering must then narrow candidates.

Use Unreal Insights instrumentation for meaningful scopes such as:

```text
IntentReplayPerception_RecordObservation
IntentReplayPerception_FindCandidates
IntentReplayPerception_CompareObservation
IntentReplayPerception_FinalizeTrack
IntentReplayPerception_BuildDebugSnapshot
```

Do not instrument trivial getters.

The system must support multiple clones or observers, but each comparison session is independently owned and must not share consumed-record state with another observer.

---

# Lifecycle, ownership, and garbage collection

All UObject references must follow Unreal ownership and GC rules.

Requirements:

- finalized tracks must own copied entry data;
- tracks must not keep runtime world Actors alive unnecessarily;
- runtime source resolution must use stable entity identity and safe weak references when required;
- session objects must have clear owners;
- components must unregister delegates symmetrically;
- world teardown must cancel active comparison and recording sessions;
- late PerceptionKnowledge callbacks after cancellation must be ignored;
- duplicate terminal transitions must not finalize twice;
- replacing the bound IntentReplay component must terminate or migrate sessions only through explicit policy;
- replacing the bound listener must not leave stale delegate bindings.

Do not access destroyed source Actors merely to format debug text.

---

# Blueprint API requirements

Expose intentional, safe Blueprint functionality.

Possible operations:

```text
Bind IntentReplay Source
Bind PerceptionKnowledge Listener
Start Synchronized Observation Recording
Stop Observation Recording
Get Finalized Observation Track
Create Timeline Bundle
Start Observation Comparison
Pause Observation Comparison
Resume Observation Comparison
Stop Observation Comparison
Get Active Observation Journal
Get Comparison Summary
Set Local Debug Enabled
```

Prefer automatic synchronization with IntentReplay lifecycle once bindings are configured.

Public functions must return structured success/failure information.

Do not expose mutable entry arrays or internal candidate indexes.

Use clear categories and tooltips.

---

# Validation and Data Validation

Implement runtime validation and editor Data Validation where appropriate.

Detect at least:

```text
IntentReplay source missing
PerceptionKnowledge listener missing
source track not finalized
Observation Track not finalized
track pairing mismatch
unsupported format version
invalid recorded observation ID
invalid persistent entity ID when required
invalid semantic tags
unsupported state value
non-deterministic or duplicate sequence index
duplicate Observation Track ID
recorded duration inconsistent with source timeline
comparison started without playback
comparison bound to the wrong source track
```

Runtime failures must produce structured results and preserve valid state.

Do not hide invalid configuration behind fallback behavior.

---

# Mandatory tests

Use Unreal Automation Tests or the repository's established testing pattern.

## Core timing tests

1. observations use the same relative recording clock as IntentReplay actions;
2. pausing recording freezes observation relative time;
3. resuming preserves the original offset;
4. observations with equal timestamps receive deterministic sequence order;
5. world-time changes do not alter relative ordering.

## Track tests

1. finalized Observation Track is immutable;
2. recorded state payloads are deep copies;
3. recorded event payloads are deep copies;
4. runtime Actor destruction does not corrupt finalized data;
5. source Action Replay Track ID is preserved;
6. invalid track pairing is rejected;
7. comparison does not mutate the source track.

## State recording tests

1. first observed state is recorded;
2. changed state while continuously visible is recorded;
3. identical redundant state callbacks are filtered according to policy;
4. reacquisition behavior follows policy;
5. Unknown and false remain distinct;
6. state and event entries never enter the wrong payload path.

## Event recording tests

1. a perceived hearing event is recorded with correct relative time;
2. multiple equal-tag events receive distinct Recorded Observation IDs;
3. source and instigator identity are copied correctly;
4. policy-filtered events do not create entries;
5. duplicate runtime callbacks are handled deterministically.

## State matching tests

1. same entity, tag, value, sense, and valid time window produces `Matched`;
2. different value produces `UnexpectedStateValue`;
3. Unknown versus false produces a mismatch;
4. same tag on a different entity does not match by default;
5. candidate outside the time window does not match;
6. multiple candidates use deterministic tie-breaking;
7. consumed state occurrence is not reused incorrectly.

## Event matching tests

1. exact event match consumes one expected occurrence;
2. a second current event does not reuse the consumed occurrence;
3. wrong source produces the correct reason;
4. location outside tolerance produces the correct reason;
5. ambiguous candidates produce `Ambiguous`;
6. optional causal correlation selects the correct candidate;
7. an incoming event while paused is not unexpectedly classified by default.

## Session tests

1. comparison starts only with valid paired tracks;
2. pause and resume follow IntentReplay playback;
3. stopping comparison unbinds observation callbacks;
4. owner `EndPlay` leaves no active bindings;
5. late callbacks do not alter a terminal journal;
6. two observers using the same source track have independent consumed-record state.

## Debug tests

1. debug disabled performs no comparative drawing;
2. global disable overrides local enable;
3. matched Actor displays green comparative status;
4. mismatching Actor displays red comparative status;
5. ambiguous result displays orange;
6. debug reads do not consume records or add journal entries;
7. destroyed runtime Actor does not crash debug rendering.

---

# Required documentation

Update or create user-facing documentation in the relevant `Docs` folders.

At minimum document:

## `IntentReplay` core documentation

- any new generic clock snapshot or lifecycle API;
- any new synchronized-channel ordering API;
- backward compatibility;
- why the core remains independent from PerceptionKnowledge.

## `IntentReplayPerception/Docs/README.md`

- module purpose;
- dependencies;
- component setup;
- recording and comparison workflows;
- Track versus Journal;
- State versus Event matching;
- public delegates;
- limitations.

## `ARCHITECTURE.md`

- dependency direction;
- clock synchronization;
- ownership and lifecycle;
- timeline bundle association;
- candidate indexing;
- matching policy;
- correlation with Recorded Intents.

## `SETUP.md`

- source-run Player Controller listener setup;
- binding to the IntentReplay component on a Pawn when owners differ;
- clone/runtime observer setup without Paradox-specific behavior;
- starting and finalizing recording;
- starting comparison;
- configuring tolerances.

## `DEBUGGING.md`

- global and local controls;
- color legend;
- bounding boxes and lines;
- state and event labels;
- Timeline HUD;
- common mismatch reasons;
- performance considerations.

Documentation must explain both C++ and Blueprint workflows where supported.

---

# Required deliverables

Codex must deliver:

1. the new optional `IntentReplayPerception` runtime module;
2. the smallest required generic extensions to the existing `IntentReplay` core;
3. synchronized observation recording;
4. immutable Observation Track;
5. Observation Journal;
6. timeline bundle or equivalent validated track association;
7. deterministic state and event matching;
8. configurable matching and recording policies;
9. structured results and mismatch reasons;
10. public events required by the future Paradox integration;
11. detailed comparative visual debug;
12. logging and Unreal Insights instrumentation;
13. automated tests;
14. updated human-facing documentation;
15. a successful build of the affected editor target.

---

# Explicit out of scope

Do not implement:

- Replay/Investigating/GOAP Behavior Tree logic;
- automatic clone state switching;
- investigation movement;
- GOAP planning;
- GOAP Belief-State snapshot conversion;
- paradox detection;
- Temporal Index rules;
- visual-cone overlap logic;
- Smart Object integration;
- coordinated clone behavior;
- maintained GOAP actions;
- missing-observation gameplay consequences;
- multiplayer replication;
- save-game persistence;
- custom timeline editor UI;
- project-specific puzzle state interpretation;
- hardcoded noise or Actor classes from Paradox.

Leave intentional extension points, but do not implement speculative systems beyond the defined milestone.

---

# Core invariants

The implementation is incorrect if any of these invariants can be violated:

1. `IntentReplayPerception` depends on `IntentReplay` and `PerceptionKnowledge`; neither depends on `IntentReplayPerception`.
2. The core `IntentReplay` module remains usable without `PerceptionKnowledge`.
3. Action Replay Tracks and Observation Tracks remain separate.
4. Observation Track and Observation Journal remain separate.
5. Finalized Observation Tracks are immutable.
6. Match and consumed state never mutate the source Observation Track.
7. Current world knowledge remains owned by `PerceptionKnowledge`.
8. Observation timestamps use the authoritative IntentReplay relative clock.
9. Pause and resume do not introduce clock drift.
10. Deterministic ordering uses timestamp plus explicit sequence.
11. State and Event observations use explicit separate payloads and matching paths.
12. A recorded event occurrence cannot satisfy unlimited current events.
13. Runtime Actor pointers are not authoritative persistent identity.
14. Comparison results include structured reasons.
15. Comparative debug never changes runtime matching state.
16. The module does not change clone behavior.
17. The module does not generate paradoxes.
18. The module contains no project-specific puzzle or clone rules.
19. Teardown leaves no delegate bindings, sessions, timers, or stale mappings.
20. If the affected code does not compile, the task is not complete.

---

# Acceptance criteria

## Scenario A — Original-run state recording

1. A Player Controller owns a valid PerceptionKnowledge listener.
2. Its possessed Pawn owns or references the active IntentReplay recording component.
3. The player sees `PC_A` powered off.
4. PerceptionKnowledge produces a State Observation.
5. IntentReplayPerception records it using the active recording-relative time.
6. The finalized Observation Track contains:

```text
Entity = PC_A
State = State.Powered
Value = false
Sense = Sight
Relative time = source timeline time
```

## Scenario B — Original-run hearing recording

1. A semantic hearing event is perceived during recording.
2. PerceptionKnowledge produces an Event Observation.
3. IntentReplayPerception records one immutable event occurrence.
4. The entry preserves source, instigator when available, Event Tag, location, strength, relative time, and deterministic sequence.

## Scenario C — Matching state during replay

1. A clone/runtime observer compares against the finalized Observation Track.
2. It sees `PC_A` powered off inside the configured time window.
3. The comparison result is `Matched`.
4. The Observation Journal references the correct Recorded Observation ID.
5. The comparative Actor box and line are green.

## Scenario D — Changed state during replay

1. The source track expects `PC_A.State.Powered = false`.
2. The current observer sees `PC_A.State.Powered = true`.
3. The result is `UnexpectedStateValue`.
4. The reason is `StateValueMismatch`.
5. The journal stores expected and current values and times.
6. The Actor box and observer line are red.
7. A generic unexpected-observation delegate is emitted exactly once.
8. No behavior state is changed by this module.

## Scenario E — Unexpected hearing event

1. The current observer hears an event not present in a valid matching window.
2. The result is `UnexpectedObservation` with a structured reason.
3. The journal preserves the event location and source context.
4. The comparative debug displays the event in red.
5. No investigation behavior starts inside this module.

## Scenario F — Event consumption

1. The source track contains one expected `Event.Noise.Door` occurrence.
2. The first compatible current event matches and consumes it in the comparison session.
3. A second current event cannot reuse the same source record.
4. The source Observation Track remains unchanged.

## Scenario G — Pause synchronization

1. IntentReplay playback is paused.
2. The comparison relative clock stops.
3. Perception observations arriving while paused follow the configured ignored/paused policy.
4. They do not match future records or generate an unexpected result by default.
5. Resume continues without drift.

## Scenario H — Independent current knowledge

1. PerceptionKnowledge updates the observer's current state for `PC_A`.
2. IntentReplayPerception records or compares the observation.
3. Destroying the Observation Journal does not erase current knowledge.
4. Entering a future GOAP mode can still query PerceptionKnowledge without requiring IntentReplay history.

## Scenario I — Detailed debug

With comparative debug enabled for one observer:

- relevant registered Actors have bounding boxes;
- lines connect the observer to compared Actors;
- matched Actors are green;
- mismatching Actors are red;
- ambiguous Actors are orange;
- labels show expected/current facts, times, IDs, results, and reasons;
- hearing event locations remain visible for the configured duration;
- the Timeline HUD displays track, session, journal, and count information;
- disabling global debug immediately stops all comparative drawing.

---

# Note for Milestone 3

The next project-level milestone will consume the generic events from this module inside the `Paradox` project module.

Expected future flow:

```text
IntentReplayPerception
    OnObservationUnexpected
        ↓
Paradox clone behavior integration
        ↓
Behavior Tree state changes from Replay to Investigating
```

That integration will decide:

- which mismatch results are relevant;
- whether a discrepancy was caused by the current clone;
- when IntentReplay playback must pause;
- how investigation starts and ends;
- when replay comparison resumes;
- how the future GOAP handoff disables the Behavior Tree.

None of those decisions belong in this milestone.
