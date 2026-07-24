# Intent Replay Core

## Purpose

This document defines the architectural foundation of the `IntentReplay` Unreal Engine plugin.

The plugin must provide a generic, data-driven system for recording semantic gameplay action requests and replaying them later through the `GameplayActions` plugin.

The plugin records **what was requested**, not frame-by-frame transforms or arbitrary actor state.

The plugin is responsible for:

- implementing the generic journal sink contract exposed by `GameplayActions`;
- validating whether accepted action snapshots are recordable and replay-safe;
- building immutable replay tracks from accepted action requests;
- keeping replay-track data separate from runtime action instances;
- recording execution outcomes without mutating the original replay track;
- replaying recorded entries as new action requests;
- preserving action parameters, effective priority, order and relative timing;
- preventing replay-generated actions from being recursively appended to their source track;
- exposing deterministic lifecycle events, structured failures and debugging data;
- providing a clean future integration point for higher-level intent executors such as a `GoalAgents` plugin.

The plugin must not contain project-specific gameplay rules.

---

# 1. Architectural boundaries

`IntentReplay` records and reissues semantic action requests.

It does not execute gameplay behavior directly. Actual execution remains the responsibility of `GameplayActions`.

Conceptual recording flow:

```text
External requester
    ↓
FGameplayActionRequest
    ↓
UGameplayActionComponent
    ↓ immutable journal event
UIntentReplayComponent
    ↓
Mutable recording session
    ↓ finalize
Immutable replay track
```

Conceptual playback flow:

```text
Immutable replay track
    ↓
Replay playback session
    ↓ create fresh request
UGameplayActionComponent
    ↓
New UGameplayActionInstance
```

A replay must never attempt to reuse:

- an original runtime action instance;
- an original runtime action handle;
- mutable request memory owned by the original caller;
- pointers to transient execution state.

## 1.1 Required dependency direction

The dependency direction must remain:

```text
IntentReplay → GameplayActions
```

Never introduce:

```text
GameplayActions → IntentReplay
```

`IntentReplay` consumes public contracts exposed by `GameplayActions`, including:

- immutable action journal events;
- action Definition identity;
- copied Property Bag snapshots;
- action origin and correlation data;
- effective priority and blocked policy;
- action state and terminal result;
- action submission and cancellation APIs.

If the existing `GameplayActions` correlation field is insufficient, extend it only through the smallest generic and backward-compatible change. Do not add replay-specific classes to the `GameplayActions` module.

## 1.2 Core dependency scope

The first runtime implementation should depend directly only on generic Unreal runtime modules and `GameplayActions`.

Do not add core dependencies on:

- `GoalAgents`;
- `GridWorld`;
- `WorldState`;
- iteration or rewind orchestration;
- puzzle systems;
- UI systems;
- editor-only modules.

A future optional integration module may depend on both `IntentReplay` and `GoalAgents`.

Recommended future dependency direction:

```text
IntentReplayGoalAgents → IntentReplay
IntentReplayGoalAgents → GoalAgents
```

## 1.3 Runtime and optional integration modules

The initial plugin should create one runtime module:

```text
IntentReplay
```

Optional modules may be added later:

```text
IntentReplayEditor
IntentReplayGoalAgents
```

Runtime code must never depend on editor code.

---

# 2. Core concepts

The architecture must keep the following concepts separate:

1. **Journal Sink** — receives immutable lifecycle events from `GameplayActions`.
2. **Recording Session** — mutable builder collecting eligible action requests.
3. **Replay Track** — finalized immutable sequence of recorded intents.
4. **Recorded Intent** — one copied semantic action request plus recording metadata.
5. **Execution Journal** — what actually happened during one recording or playback execution.
6. **Playback Session** — transient runtime state while replaying a track.
7. **Playback Strategy** — translates a recorded intent into executable work.
8. **Recordability Policy** — validates whether parameters are safe to preserve and replay.
9. **Timing Source** — supplies monotonic session time.
10. **Persistent Record ID** — correlates one recorded intent across executions.

Do not merge Replay Track data with runtime Playback Session state.

Do not use an Execution Journal as the authoritative Replay Track.

---

# 3. Replay Track versus Execution Journal

This separation is mandatory.

## 3.1 Replay Track

A Replay Track stores the immutable semantic requests that should be issued again.

Example conceptual contents:

```text
Record 0
    Definition identity
    Parameter snapshot
    Effective priority
    Blocked policy
    Relative submission time
    Stable record ID

Record 1
    ...
```

The Replay Track describes intended submissions.

It must not be rewritten to match what later playback attempts happened to do.

## 3.2 Execution Journal

An Execution Journal stores runtime facts such as:

- request accepted;
- action started;
- action paused;
- action resumed;
- action ended;
- terminal result;
- submission rejection;
- runtime handle;
- playback-session correlation;
- timing of actual events.

A replayed action may:

- start later than the original action;
- remain queued longer;
- be interrupted;
- fail for a different reason;
- be rejected during submission.

Those differences belong in the Execution Journal.

They must not mutate the source Replay Track.

## 3.3 Required invariant

```text
Replay Track = immutable requested intent
Execution Journal = mutable history of one execution attempt
```

The implementation is incorrect if playback outcomes can silently alter the source track.

---

# 4. Primary runtime owner

Create an Actor Component conceptually named:

```cpp
UIntentReplayComponent
```

Each actor that records or replays semantic actions may own one component.

The component coordinates:

- connection to one `UGameplayActionComponent`;
- journal-sink registration;
- recording-session lifetime;
- execution-journal lifetime;
- playback-session lifetime;
- replay scheduling;
- replay request correlation;
- pause, resume and stop behavior;
- component teardown.

## 4.1 One authoritative action source

The initial implementation should bind one `UIntentReplayComponent` to one authoritative `UGameplayActionComponent`.

Do not implement multi-actor track aggregation in the first milestone.

A higher-level system may coordinate several components later.

## 4.2 Action Component binding

Support:

- an explicit designer-assigned Action Component reference where appropriate;
- safe automatic discovery on the same owning actor as a convenience;
- an explicit initialization path for C++ integration.

Do not repeatedly search the world for an Action Component.

The resolved Action Component must be cached with correct Unreal lifetime handling.

## 4.3 Journal sink registration

During controlled initialization, the Intent Replay component must register a sink implementation with the Action Component.

During teardown it must unregister symmetrically.

If the Action Component requires journaling, initialization must complete before gameplay systems are allowed to submit actions.

A missing required recording connection must remain observable and must not silently allow unrecorded execution.

---

# 5. Journal sink implementation

Implement the generic `GameplayActions` journal sink contract.

The exact class or interface name must match the actual public API created by `GameplayActions`.

Conceptually, the sink receives one immutable structured event:

```text
FGameplayActionEvent
```

The sink must not depend on mutable `UGameplayActionInstance` state.

## 5.1 Initial accepted-event transaction

When `GameplayActions` journaling is configured as `Required`, the accepted-action snapshot must be validated and stored synchronously before the action may:

- start;
- enter the authoritative queue;
- interrupt another action;
- acquire Execution Locks.

The Intent Replay sink must return a structured acceptance result.

Conceptual result states:

```text
Accepted
RejectedNoRecordingSession
RejectedUnrecordableParameters
RejectedInvalidDefinition
RejectedInvalidSnapshot
RejectedCapacityExceeded
RejectedInternalError
```

Use the actual interface shape supported by `GameplayActions`, but preserve these semantics.

## 5.2 No partial mutation on rejection

Accepted-event processing must be transactional.

If validation fails:

- no Replay Track entry is appended;
- no incomplete Execution Journal entry remains;
- no stable Record ID is consumed if the implementation can avoid it;
- the sink returns rejection;
- `GameplayActions` does not preempt or start anything.

## 5.3 Later lifecycle events

Events after acceptance, such as Started or Ended, update the Execution Journal and optionally the original-execution diagnostic data associated with the recorded entry.

They must never be allowed to fail in a way that rolls back an action already executing.

Unexpected later events must:

- produce an observable warning or error;
- preserve valid existing state;
- avoid creating duplicate terminal events;
- avoid corrupting the Replay Track.

## 5.4 Reentrancy

The journal sink may be called from inside the Action Component's controlled state transitions.

Do not synchronously submit, cancel or replay another action from inside initial journal acceptance.

If journal processing causes follow-up work, defer that work until the current Action Component transition has completed.

Do not mutate a collection while iterating over it because an observer reentered the system.

---

# 6. Recording availability policy

The sink may exist while no Replay Track recording session is active.

Provide a policy conceptually equivalent to:

```text
JournalOnly
RejectAcceptedActions
```

## 6.1 `JournalOnly`

Accepted actions may execute and are written only to an Execution Journal.

They are not appended to a Replay Track.

This is the recommended generic default.

## 6.2 `RejectAcceptedActions`

An accepted-action snapshot is rejected when no active Recording Session exists.

Use this policy when every executable action must belong to an authoritative recording session.

The policy must be explicit and configurable. Do not infer it from the presence of listeners.

---

# 7. Recording Session

Create a transient UObject conceptually named:

```cpp
UIntentRecordingSession
```

A Recording Session is the sole mutable builder of one future Replay Track.

The session must not be exposed as a freely mutable container.

## 7.1 Recording states

Use an explicit state machine.

Recommended states:

```text
Created
Recording
Draining
Finalized
Failed
Cancelled
```

### `Created`

Configured but not yet accepting track entries.

### `Recording`

Eligible accepted actions may be appended.

### `Draining`

No new entries are accepted into the Replay Track, but already tracked actions may still emit terminal events.

### `Finalized`

The immutable Replay Track has been produced.

### `Failed`

The session encountered an unrecoverable consistency failure.

### `Cancelled`

The mutable recording was discarded intentionally.

Terminal states must not transition back to Recording.

## 7.2 Starting a recording

Starting a recording must:

1. verify that the component is initialized;
2. verify that no incompatible recording session is active;
3. create a unique Track ID;
4. reset the session-relative clock;
5. create empty mutable track data;
6. create or attach an Execution Journal according to policy;
7. enter `Recording`;
8. emit one structured start event.

## 7.3 Stopping a recording

Stopping a recording must stop accepting new Replay Track entries immediately.

Default behavior should enter `Draining` until every action already included in the track has reached a terminal state.

This preserves optional original-execution outcomes without mutating a finalized track later.

A higher-level coordinator may first cancel or abort outstanding actions, then allow their terminal events to complete the drain.

Do not block the Game Thread while waiting.

When draining completes:

1. validate final ordering and record uniqueness;
2. seal the track data;
3. create the immutable Replay Track;
4. enter `Finalized`;
5. emit one finalized event.

## 7.4 Immediate finalization

An optional policy may allow finalization without waiting for original terminal outcomes.

When used:

- unresolved original outcomes remain explicitly absent;
- late events continue only in the Execution Journal;
- the immutable track is not modified.

Do not represent an unavailable result as fake success.

---

# 8. Replay Track data object

Create a Blueprint-visible UObject conceptually named:

```cpp
UIntentReplayTrack
```

The first implementation should treat tracks as runtime data objects, not automatically as authored `UDataAsset` instances.

Runtime recording in a packaged game must not rely on editor asset creation.

## 8.1 Track ownership and immutability

A finalized track must:

- own copied record data;
- expose read-only Blueprint queries;
- expose no public mutable array;
- remain unchanged while any playback session uses it;
- be safe to duplicate explicitly when a mutable copy is required later;
- not retain runtime Action Instances or runtime handles as authoritative identity.

The track may be transient in the first milestone.

Save-game export, asset creation and external serialization are future features.

## 8.2 Required track metadata

The track should contain at least:

```text
TrackId
FormatVersion
Entries
RecordedDuration
Track metadata or tags
Source label/context without a hard runtime actor dependency
Finalization state
```

Recommended identifiers:

```cpp
FIntentReplayTrackId
FRecordedIntentId
```

They must be:

- invalid by default;
- comparable and hashable;
- Blueprint-visible;
- stable for the lifetime of the finalized track;
- distinct from `FGameplayActionHandle`.

`FGuid`-backed identifiers are acceptable if implemented consistently.

## 8.3 Format version

Store a format version even though persistent saving is outside the first milestone.

Do not assume that Property Bag schemas, Definition assets or plugin serialization will never change.

---

# 9. Recorded Intent entry

Create a value type conceptually named:

```cpp
FRecordedIntent
```

One entry represents one accepted action request selected for the Replay Track.

## 9.1 Required entry data

Store at least:

```text
RecordedIntentId
Definition identity
Definition soft reference or equivalent resolvable reference
ActionTag snapshot
Parameter snapshot
Effective priority
Effective blocked policy
Execution Locks snapshot for compatibility/debugging
Original OriginTag
Original correlation data when recordable
Track-local sequence index
Original submission sequence
Relative accepted timestamp
Optional original terminal result
Optional record metadata
```

## 9.2 Definition identity

`UGameplayActionDefinition` is expected to provide stable authored identity.

Prefer a persistent identity such as:

- `FPrimaryAssetId` when valid;
- a soft object reference/path as a resolvable fallback or cache.

Do not use a loaded UObject pointer as the only recorded Definition identity.

## 9.3 Parameter snapshot

Store a deep copy of the accepted action's immutable `FInstancedPropertyBag` snapshot.

Do not rebuild the recorded values from the current Definition defaults during recording.

The recorded snapshot must remain unchanged if:

- the original request variable changes;
- the Definition defaults change;
- the Action Instance changes internal state;
- another request using the same Definition is submitted.

## 9.4 Priority and blocked policy

Record the **effective** priority and blocked policy used by `GameplayActions`, not merely the Definition defaults.

This is required to reproduce scheduling, queuing and priority preemption behavior.

## 9.5 Execution Locks snapshot

Record the accepted Execution Locks snapshot for:

- validation;
- compatibility diagnostics;
- explaining later replay divergence.

The Replay Track does not directly seize locks. `GameplayActions` remains authoritative.

The current Definition provides the actual runtime lock configuration when the replayed request is accepted.

If the current Definition locks differ from the recorded snapshot, the compatibility policy decides whether replay may continue.

## 9.6 Original result

The original terminal result is diagnostic context.

It must not force the replayed action to return the same result.

A replayed action always produces its own runtime result.

---

# 10. Track eligibility policy

Not every journaled action must automatically enter a Replay Track.

The Intent Replay plugin must decide eligibility. `GameplayActions` must not own this policy.

## 10.1 Eligibility inputs

Eligibility may consider immutable event data such as:

- event type;
- OriginTag;
- ActionTag;
- Definition identity;
- request correlation;
- recordability validation;
- configured Gameplay Tag queries;
- component or session policy.

## 10.2 Default rule

Only successfully journal-accepted action submissions should create Replay Track entries.

Rejected submissions may be stored in the Execution Journal but must not become executable Replay Track records by default.

## 10.3 Origin filtering

Use an extensible `FGameplayTagQuery` or equivalent policy instead of hardcoded project branches.

A replay-generated request must normally be excluded from the currently authored Replay Track.

The plugin should expose a configured replay origin tag, conceptually:

```text
GameplayAction.Origin.Replay
```

The direct playback strategy applies this tag to replay-generated requests.

The default track eligibility policy must exclude that tag.

Do not rely only on object identity or runtime handles to prevent recursive recording.

## 10.4 Execution Journal remains complete

An action excluded from the Replay Track may still be recorded in the Execution Journal.

This includes replay-generated, derived or system-originated actions when journal policy allows them.

---

# 11. Recordability validation

Before accepting an action into a required recording session, validate that its snapshot can be preserved and replayed safely.

Create a validation service or policy conceptually named:

```cpp
UIntentRecordabilityPolicy
```

or an equivalent internal strategy.

Do not scatter recordability rules across Blueprint nodes and playback code.

## 11.1 Value types

The default policy should support normal deterministic value data such as:

- bool;
- integer and floating-point values;
- name and string;
- Gameplay Tags and tag containers;
- enums;
- vectors;
- rotators;
- transforms;
- supported structs whose nested fields are also recordable;
- class references where persistence is valid;
- soft class references;
- asset references according to policy;
- soft object references.

Verify the actual Property Bag type APIs against the Unreal Engine version.

## 11.2 Hard UObject references

Do not treat every hard UObject reference as replay-safe.

Default behavior should distinguish:

- stable asset references;
- class references;
- runtime world objects;
- transient objects;
- instanced subobjects.

Recommended default:

- hard references to stable assets may be allowed when verified as assets;
- hard references to runtime world objects are rejected for Replay Track recording;
- transient or non-path-addressable UObjects are rejected;
- no reference is silently converted to another representation.

## 11.3 Soft object references

Soft object references are appropriate for path-addressable assets and world-authored objects that should not be kept alive by the track.

The recordability validator should preserve the soft path exactly.

The Intent Replay plugin must not assume that every soft path can be resolved at every moment.

The concrete action remains responsible for deciding what an unavailable target means during execution.

## 11.4 Runtime-generated identities

Runtime-generated objects may require a future stable-identity provider or resolver.

Do not build a project-specific entity registry into this core milestone.

Provide a clear validation failure and an extension point rather than pretending a transient pointer is replay-safe.

## 11.5 Nested struct validation

When a Property Bag contains structs, validation must recursively inspect supported nested properties where the engine APIs permit it safely.

Do not approve an entire struct only because its top-level type is a `UScriptStruct`.

## 11.6 Structured validation result

Return a result containing at least:

```text
Status
Parameter name/path
Property type
Reason
Optional diagnostic message
```

Do not reject a required-journal action with only a generic log line.

---

# 12. Recording timestamps and deterministic order

Every Replay Track entry must preserve both:

```text
Relative accepted timestamp
Deterministic sequence order
```

Time alone is not sufficient because several actions may be accepted at the same timestamp.

Sequence alone is not sufficient because intentional gaps between requests would be lost.

## 12.1 Relative time

Record time relative to the Recording Session start.

Do not store raw wall-clock time as the playback schedule.

Use a monotonic gameplay/session time source.

## 12.2 Time source

Provide a small abstraction conceptually equivalent to:

```cpp
IIntentReplayTimeSource
```

or a replaceable UObject policy.

The initial implementation may provide a default world game-time source.

The abstraction must allow a future integration to provide a custom simulation clock without rewriting track data or playback scheduling.

## 12.3 Tie breaking

When two entries have the same recorded timestamp, order them by their track-local sequence index.

Never depend on:

- pointer order;
- hash-map iteration order;
- frame-dependent collection order.

## 12.4 Recording pause

A paused Recording Session must pause its relative clock explicitly.

The plugin must document that pausing only the recorder does not automatically pause unrelated gameplay execution.

Higher-level orchestration should pause both action execution and recording time when complete simulation suspension is required.

---

# 13. Execution Journal

Create a runtime journal conceptually named:

```cpp
UIntentExecutionJournal
```

It stores one execution attempt's observed lifecycle events.

## 13.1 Journal entries

Create a value type conceptually named:

```cpp
FIntentExecutionEvent
```

Store at least:

```text
Event type
Observed relative timestamp
Runtime action handle
RecordedIntentId when correlated
PlaybackSessionId when applicable
Definition identity
ActionTag
OriginTag
Effective priority
Execution Locks snapshot
Submission result or terminal result
Immutable parameter snapshot when appropriate
Diagnostic reason
```

Runtime action handles are allowed as execution-journal data but must not become persistent Replay Track identity.

## 13.2 Matching lifecycle events

Because one Intent Replay component is bound to one Action Component in the first milestone, runtime handles may be matched within that component's scope.

Still preserve persistent correlation data whenever available.

Late or duplicate events must not create duplicate terminal outcomes.

## 13.3 Capacity

Execution Journals can grow without bound during long sessions.

Provide an explicit capacity policy such as:

```text
UnboundedForCurrentSession
BoundedRingBuffer
Disabled
```

Use a reasonable bounded default for diagnostic-only journals unless a Recording Session requires complete history.

Never silently discard Replay Track entries because an Execution Journal reached capacity.

## 13.4 Replay divergence

The journal should make it possible to compare:

- original result;
- replay result;
- original timing;
- replay timing;
- submission rejection reason;
- changed Definition compatibility;
- changed scheduling outcome.

Do not automatically classify divergence as an error. It is diagnostic information unless policy says otherwise.

---

# 14. Playback Session

Create a transient UObject conceptually named:

```cpp
UIntentReplayPlaybackSession
```

A Playback Session owns runtime state for replaying one immutable track through one target Action Component.

## 14.1 Playback states

Use an explicit state machine.

Recommended states:

```text
Created
Preparing
Ready
Playing
Paused
Stopping
Completed
Failed
Cancelled
```

### `Preparing`

Validate the track, resolve required Definition assets and prepare parameter compatibility.

### `Ready`

Preparation succeeded and playback may start.

### `Playing`

Scheduled entries may be submitted.

### `Paused`

No new entries are submitted and the playback clock is paused.

### `Stopping`

Replay-owned active or queued actions are being cancelled according to stop policy.

### Terminal states

`Completed`, `Failed` and `Cancelled` are terminal.

## 14.2 Playback ownership

The Intent Replay component owns the active Playback Session through reflected lifetime management.

The session owns:

- a reference to the immutable track;
- current playback options;
- playback clock state;
- next entry index;
- record-to-runtime-handle mappings;
- replay-owned action handles;
- current failure state;
- execution journal reference.

The session must not own or mutate runtime Action Instances directly.

---

# 15. Playback preparation

Playback must not begin by blindly submitting the first entry.

Preparation must validate:

- track is finalized and internally valid;
- record IDs are unique;
- ordering is deterministic;
- timestamps are valid and non-negative;
- each Definition identity can be resolved or scheduled for loading;
- each parameter snapshot is structurally valid;
- parameter compatibility policy can build a request;
- replay origin tag is configured;
- target Action Component is valid;
- no incompatible Playback Session is already active.

## 15.1 Definition loading

Definition assets may be represented by soft references or primary asset IDs.

Use Unreal asset loading APIs appropriate to the engine version.

Do not synchronously load every asset by default without considering runtime impact.

A preparation phase may load required Definition assets asynchronously and enter `Ready` only after completion.

Async completion must handle:

- component destruction;
- playback cancellation;
- world teardown;
- missing assets;
- duplicate callbacks.

## 15.2 World-object soft parameters

Do not attempt to load arbitrary world Actor soft references as if they were standalone content assets.

Preserve those soft paths in the request.

The action implementation decides whether the current world object is available when execution begins.

---

# 16. Parameter compatibility during replay

A recorded Property Bag snapshot may be replayed after the current Definition schema has changed.

Make this behavior explicit.

Provide a policy conceptually equivalent to:

```text
StrictRecordedSchema
CopyCompatibleValuesUseCurrentDefaults
```

## 16.1 `StrictRecordedSchema`

Require the current Definition parameter schema to match the recorded schema according to a documented comparison rule.

Any missing, added or type-changed field causes preparation failure.

This offers maximum fidelity.

## 16.2 `CopyCompatibleValuesUseCurrentDefaults`

Create a fresh request from the current Definition defaults, then copy recorded values for fields that still exist with compatible types.

New current fields retain current defaults.

Removed recorded fields are ignored with an observable compatibility report.

Type changes must not be silently coerced unless a specific safe conversion is intentionally implemented and tested.

## 16.3 No track mutation

Compatibility processing creates a new request snapshot for playback.

It must not rewrite the recorded Property Bag inside the source track.

## 16.4 Definition behavior compatibility

Parameter schema equality does not guarantee identical behavior.

Also compare recorded diagnostic snapshots where useful:

- ActionTag;
- Execution Locks;
- interruptibility-related Definition data if exposed;
- other execution-critical configuration.

A mismatch must be visible through validation or debug output.

Do not silently override `GameplayActions` internals with stale recorded lock data.

---

# 17. Playback scheduling

The first core milestone should replay entries according to their recorded accepted-submission timeline.

Required ordering:

```text
Recorded relative timestamp ascending
then track-local sequence ascending
```

## 17.1 Recorded timeline behavior

At each scheduled time, create and submit a fresh action request.

The request should preserve:

- Definition identity;
- compatible recorded parameters;
- recorded effective priority;
- recorded effective blocked policy;
- configured replay OriginTag;
- persistent RecordedIntentId correlation.

`GameplayActions` remains responsible for:

- Execution Lock conflicts;
- queueing;
- priority preemption;
- validation;
- runtime state transitions.

This allows concurrent or preemptive submissions to be reproduced when several recorded actions overlap in time.

## 17.2 No Tick by default

Do not add a permanently enabled Tick only to wait for the next entry.

Prefer scheduling the next due entry through the appropriate timer or clock mechanism.

When several entries share a timestamp, submit them in deterministic sequence order within one controlled scheduling pass.

## 17.3 Playback timing drift

Use the session-relative clock to determine which entries are due.

Do not repeatedly add delay intervals in a way that accumulates avoidable drift.

Calculate each due time relative to the playback start and explicit paused duration.

## 17.4 Future scheduling policies

Leave a clean extension point for future modes such as:

- sequential-by-terminal-result;
- immediate ordered submission;
- goal-driven adaptive execution;
- externally clocked playback.

Do not implement all of them in the first milestone.

---

# 18. Playback strategy

Define an execution abstraction conceptually equivalent to:

```cpp
IIntentReplayExecutionStrategy
```

The first plugin implementation must include a direct Gameplay Actions strategy.

## 18.1 Direct Gameplay Actions strategy

For each recorded intent it:

1. resolves the current Action Definition;
2. creates a new request through the authoritative Gameplay Actions request factory;
3. applies compatible recorded parameters;
4. applies recorded effective priority and blocked policy overrides;
5. sets the configured replay OriginTag;
6. writes the RecordedIntentId into generic correlation data;
7. submits through `UGameplayActionComponent`;
8. stores the returned runtime handle when accepted;
9. writes rejection details to the Execution Journal when rejected.

Do not manually instantiate `UGameplayActionInstance`.

## 18.2 Future GoalAgents strategy

A future optional module may translate a Recorded Intent into a higher-level Goal rather than immediately submitting the original action.

The core Intent Replay plugin must not depend on `GoalAgents`.

The strategy boundary must allow that future integration without changing Replay Track format or recording logic.

Do not implement project-specific fallback, replanning or alternative selection in this core milestone.

---

# 19. Recursive recording prevention

Playback-generated requests are still valid gameplay actions and should remain observable in the Execution Journal.

They must not automatically create duplicate entries in the source Replay Track.

Use all of the following safeguards:

1. apply the configured replay OriginTag;
2. preserve the persistent RecordedIntentId in correlation data;
3. default track eligibility excludes replay-originated requests;
4. never append to the source track during playback;
5. keep Recording Session and Playback Session storage separate.

Do not rely only on a boolean such as `bIsReplaying` hidden inside one component.

A replay request may pass through other systems before reaching Gameplay Actions. Its origin and correlation must travel with the request.

---

# 20. Playback submission and failure policies

Submitting a recorded entry can fail before an action starts.

Examples:

- Definition unavailable;
- request creation failure;
- current schema incompatibility;
- missing required journal;
- action validation rejection;
- blocked policy rejection;
- target-specific validation failure.

Provide an explicit Playback failure policy.

The first milestone should support at least:

```text
StopPlayback
SkipFailedEntry
```

## 20.1 `StopPlayback`

The first unrecoverable preparation or submission failure ends playback with a structured failure result.

Recommended generic default.

## 20.2 `SkipFailedEntry`

Record the failure in the Execution Journal, emit an entry-failed event and continue scheduling later records.

## 20.3 No unbounded retry in the core milestone

Do not implement indefinite automatic retry as a default.

Retry requires explicit timeout, backoff, cancellation and world-readiness semantics.

Leave it as a future strategy or policy.

## 20.4 Runtime action failure

A successfully submitted replay action may later fail, cancel, interrupt or abort.

Provide a separate policy for whether terminal action failure should stop future track scheduling.

Do not confuse submission rejection with terminal action failure.

---

# 21. Playback completion

Recorded-timeline submission is complete when every Replay Track entry has been processed for submission.

Overall Playback Session completion may use one of two clearly distinguished concepts:

```text
AllEntriesSubmitted
AllReplayOwnedActionsTerminal
```

The first milestone should expose both events or statuses.

Default final `Completed` state should occur only after:

1. every entry has been submitted or skipped according to policy;
2. every accepted replay-owned action has reached a terminal state;
3. no replay scheduling timer remains;
4. no pending Definition preparation remains.

This prevents a session from reporting completion while replay-owned actions are still running.

---

# 22. Pause and resume

## 22.1 Recording pause

Pausing recording:

- pauses the recording clock;
- preserves the current mutable track;
- keeps journal sink registration valid;
- does not finalize the session.

Define explicitly whether accepted actions during a paused recording are:

- rejected from the Replay Track;
- written only to the Execution Journal;
- accepted at the paused timestamp.

Recommended default:

```text
Execution Journal only
```

This avoids several actions collapsing onto one paused timestamp.

## 22.2 Playback pause

Pausing playback:

- stops scheduling new entries;
- pauses the playback clock;
- preserves next-entry state;
- preserves replay-owned action mappings.

Do not assume that pausing playback automatically pauses active Gameplay Actions.

Provide an explicit helper or option for coordinated pause with the bound Action Component, but keep the responsibilities distinguishable:

```text
Pause scheduling only
Pause scheduling and bound actions
```

## 22.3 Resume

Resume must compute remaining schedule from the original recorded timeline and accumulated paused duration.

Do not reschedule from “now plus original delay,” which would accumulate timing drift.

---

# 23. Stop and cancellation

Stopping playback must affect only work owned by that Playback Session unless explicitly configured otherwise.

The session must track every accepted runtime handle created from its records.

Default stop flow:

1. stop scheduling new entries;
2. cancel replay-owned queued and active actions through `GameplayActions`;
3. use a structured cancellation reason;
4. ignore late callbacks safely;
5. wait for controlled terminal events when required;
6. clear scheduling timers and async load handles;
7. enter `Cancelled` or `Failed` according to cause.

Do not call `AbortAllActions` on the Action Component by default because unrelated actions may be active.

A higher-level lifecycle coordinator may choose a wider abort operation separately.

---

# 24. Correlation model

Persistent replay identity and runtime execution identity must remain separate.

## 24.1 Persistent identity

Use:

```text
TrackId
RecordedIntentId
PlaybackSessionId
```

## 24.2 Runtime identity

Use:

```text
FGameplayActionHandle
```

A new action handle is produced on every playback attempt.

## 24.3 Mapping

The Playback Session maintains transient mappings such as:

```text
RecordedIntentId → current runtime action handle
Runtime action handle → RecordedIntentId
```

The source track stores no authoritative runtime handle.

## 24.4 Correlation propagation

The RecordedIntentId must be inserted into the generic request correlation field so journal events can be matched even when the request passes through adapters.

Do not encode IDs into Action Tags, object names or debug strings.

---

# 25. Blueprint API

The Blueprint API must support the safe high-level workflow without exposing mutable internals.

## 25.1 Recording workflow

Minimum Blueprint workflow:

```text
Start Intent Recording
    ↓
Gameplay Actions execute normally
    ↓
Stop Intent Recording
    ↓
On Recording Finalized
    ↓
Receive UIntentReplayTrack
```

Recommended functions:

```text
StartRecording
RequestStopRecording
CancelRecording
PauseRecording
ResumeRecording
GetRecordingState
GetCurrentRecordingEntryCount
GetPendingDrainCount
```

## 25.2 Playback workflow

Minimum Blueprint workflow:

```text
Prepare Replay Track
    ↓
Start Replay
    ↓
Pause / Resume / Stop as needed
    ↓
Receive structured completion result
```

Recommended functions:

```text
StartReplay
PauseReplay
ResumeReplay
StopReplay
GetPlaybackState
GetPlaybackProgress
GetNextRecordedIntent
GetReplayOwnedActionHandles
```

## 25.3 Track queries

Expose read-only queries such as:

```text
GetTrackId
GetEntryCount
GetEntryByIndex
FindEntryById
GetRecordedDuration
ValidateTrack
```

Do not expose a Blueprint-writable Entries array.

## 25.4 Events

Expose a small structured set of events:

```text
OnRecordingStarted
OnRecordingStateChanged
OnRecordingFinalized
OnRecordingFailed
OnReplayPrepared
OnReplayStarted
OnRecordedIntentSubmitted
OnRecordedIntentSubmissionFailed
OnReplayAllEntriesSubmitted
OnReplayCompleted
OnReplayFailed
OnReplayStopped
```

Avoid redundant event variants that can fire inconsistently.

---

# 26. Public results and failure data

Use structured result types instead of bool-only APIs.

Suggested concepts:

```cpp
FIntentRecordingStartResult
FIntentRecordingFinalizeResult
FIntentReplayStartResult
FIntentReplayResult
FIntentRecordabilityResult
FIntentTrackValidationResult
```

Failure reasons should distinguish at least:

```text
NotInitialized
MissingActionComponent
JournalRegistrationFailed
RecordingAlreadyActive
NoRecordingSession
TrackNotFinalized
TrackInvalid
DefinitionUnavailable
ParameterSchemaMismatch
UnrecordableParameter
PlaybackAlreadyActive
SubmissionRejected
ActionFailed
CancelledByRequester
OwnerEndPlay
WorldTeardown
InternalConsistencyFailure
```

Do not communicate these failures only through logs.

---

# 27. Lifecycle and Unreal ownership

## 27.1 Constructors

Do not perform world-dependent binding or asset loading in constructors.

## 27.2 Initialization

Initialization must establish:

- Action Component binding;
- journal sink registration;
- time source;
- default policies;
- debug state.

## 27.3 Symmetrical cleanup

Every:

- delegate binding;
- journal registration;
- timer;
- async load request;
- playback-owned action mapping;
- transient session;

must have a defined cleanup path.

## 27.4 EndPlay

During EndPlay:

1. stop accepting new recorder commands;
2. cancel pending playback preparation;
3. stop playback scheduling;
4. cancel replay-owned actions when safe and appropriate;
5. cancel or fail active recording sessions explicitly;
6. unregister the journal sink;
7. unbind delegates;
8. clear timers and transient mappings;
9. tolerate partial world teardown.

Late async callbacks must not access destroyed UObjects or revive terminal sessions.

---

# 28. Threading and persistence

The first milestone must keep state transitions and Property Bag processing on the Game Thread.

Do not add worker-thread UObject access.

Journal acceptance must be an in-memory synchronous operation.

Do not perform disk writes, save-game serialization or network transmission inside the required-journal acceptance path.

Future persistence should consume finalized immutable tracks asynchronously outside the Gameplay Actions submission transaction.

---

# 29. Debugging requirements

Provide local and global debug control according to the project-wide rules.

At minimum, debug output must expose:

```text
Owner
Bound Action Component
Journal registration state
No-session acceptance policy
Recording state
Current Track ID
Recorded entry count
Draining action count
Recording elapsed time
Playback state
Playback Session ID
Source Track ID
Next entry index
Next entry due time
Submitted/total entry count
Active replay-owned handles
Last submission failure
Last compatibility failure
Last recordability failure
Execution Journal size
```

## 29.1 Entry inspection

For a selected Recorded Intent, make it possible to inspect:

```text
RecordedIntentId
Definition identity
ActionTag
Relative timestamp
Sequence
Effective priority
Blocked policy
Execution Locks snapshot
OriginTag
Parameter names and types
Original result when present
Current replay result when correlated
```

## 29.2 Scheduling diagnostics

Debugging must explain:

- why an action snapshot entered the Replay Track;
- why it was journal-only;
- why recordability validation rejected it;
- why replay preparation failed;
- why an entry was submitted at a given time;
- why it was skipped;
- how it correlated to a runtime action handle;
- whether Definition or parameter compatibility changed.

Avoid per-frame logging.

Use state-transition logs and opt-in visualization or debug strings.

---

# 30. Logging

Use the module's primary log category and macros required by the root project rules.

Recommended state-transition logs include:

- journal sink registration/unregistration;
- recording start, drain, finalize, cancel and failure;
- playback prepare, start, pause, resume, stop and completion;
- track validation failure;
- recordability rejection;
- Definition resolution failure;
- parameter compatibility mismatch;
- replay submission rejection;
- unexpected duplicate or late journal event.

Do not log every timer check or parameter read by default.

Logs should include relevant context:

```text
Owner
TrackId
RecordedIntentId
PlaybackSessionId
Runtime action handle
Definition identity
Current state
Failure reason
```

---

# 31. Performance and profiling

Intent recording should add limited overhead to action lifecycle transitions.

Avoid:

- repeated full-track copies;
- repeated world searches;
- unnecessary string construction;
- per-frame scans of every record;
- rebuilding compatible Property Bags more than necessary;
- unbounded diagnostic history by default.

Make meaningful operations measurable in Unreal Insights where useful, including:

```text
IntentReplay_RecordAcceptedAction
IntentReplay_ValidatePropertyBag
IntentReplay_FinalizeTrack
IntentReplay_PreparePlayback
IntentReplay_BuildReplayRequest
IntentReplay_SubmitDueEntries
```

Do not instrument trivial getters.

Cache prepared replay requests or compatibility results only when invalidation and ownership are clear.

---

# 32. Suggested folder structure

Follow the root project folder rules without duplicating them inside implementation code.

Suggested structure:

```text
Plugins/IntentReplay/
├── IntentReplay.uplugin
├── CODEX/
│   └── IntentReplay.md
├── Docs/
└── Source/
    └── IntentReplay/
        ├── IntentReplay.Build.cs
        ├── Public/
        │   ├── Components/
        │   │   └── IntentReplayComponent.h
        │   ├── Recording/
        │   │   ├── IntentRecordingSession.h
        │   │   └── IntentReplayTrack.h
        │   ├── Playback/
        │   │   ├── IntentReplayPlaybackSession.h
        │   │   └── IntentReplayExecutionStrategy.h
        │   ├── Journal/
        │   │   └── IntentExecutionJournal.h
        │   ├── Policies/
        │   │   ├── IntentRecordabilityPolicy.h
        │   │   └── IntentReplayTimeSource.h
        │   ├── Types/
        │   │   ├── RecordedIntent.h
        │   │   ├── IntentReplayIds.h
        │   │   ├── IntentReplayResults.h
        │   │   ├── IntentReplayStates.h
        │   │   └── IntentExecutionEvent.h
        │   └── Blueprint/
        │       └── IntentReplayBlueprintLibrary.h
        ├── Private/
        │   ├── Components/
        │   ├── Recording/
        │   ├── Playback/
        │   ├── Journal/
        │   ├── Policies/
        │   ├── Types/
        │   ├── Blueprint/
        │   └── Tests/
        ├── CODEX/
        └── Docs/
```

Adjust the exact folder structure to real implementation needs.

Do not create empty architectural folders only to match the diagram.

---

# 33. Module dependencies

Keep dependencies minimal.

The runtime module will likely require modules providing:

- core Unreal types;
- UObject and Actor Component support;
- Gameplay Tags;
- Struct Utils / `FInstancedPropertyBag` support;
- asset management and soft references as actually used;
- `GameplayActions` public API.

Verify the exact Unreal module names and whether each dependency belongs in public or private dependencies.

Because public Intent Replay types contain or expose Gameplay Actions types and Property Bag snapshots, some dependencies may need to be public.

Do not guess module names from memory.

---

# 34. Initial implementation scope

The first core milestone should implement:

1. runtime plugin and module;
2. module log category and logging macros;
3. `UIntentReplayComponent`;
4. Gameplay Actions journal sink integration;
5. configurable no-recording-session policy;
6. `UIntentRecordingSession` state machine;
7. immutable `UIntentReplayTrack`;
8. stable Track and Record IDs;
9. `FRecordedIntent` with copied Property Bag data;
10. track eligibility filtering by OriginTag query;
11. default recordability validation;
12. relative timestamps and deterministic sequence ordering;
13. `UIntentExecutionJournal`;
14. `UIntentReplayPlaybackSession` state machine;
15. track validation and playback preparation;
16. strict and compatible parameter-schema policies;
17. recorded-timeline scheduling without permanent Tick;
18. direct Gameplay Actions playback strategy;
19. replay origin and persistent correlation propagation;
20. recursive track-recording prevention;
21. stop and skip playback failure policies;
22. pause/resume for recording and playback clocks;
23. cancellation of replay-owned action handles;
24. structured Blueprint APIs and lifecycle events;
25. debug inspection;
26. automated tests;
27. user-facing documentation in the plugin `Docs` folder.

## 34.1 Minimal reference workflow

Validate the plugin using the asynchronous reference action provided by `GameplayActions`.

The test workflow must demonstrate:

```text
1. Start a Recording Session.
2. Create a Gameplay Action request from a Definition.
3. Override dynamic Property Bag values.
4. Submit the action.
5. Journal acceptance appends one Recorded Intent.
6. The action starts and ends.
7. Stop and finalize the Recording Session.
8. Start playback on the finalized track.
9. A fresh request is created with copied parameters and recorded priority.
10. A new runtime action handle is returned.
11. Replay-generated events enter the Execution Journal.
12. The source Replay Track remains unchanged.
13. The replay action reaches its own terminal result.
14. Playback completes after every replay-owned action is terminal.
```

---

# 35. Required automated tests

Add focused automation tests for architecture and integration.

## Journal integration

1. The component registers and unregisters the journal sink symmetrically.
2. Required journaling accepts a valid recordable action before it starts.
3. Recordability rejection prevents action preemption and execution.
4. Journal-only mode accepts actions without an active Recording Session.
5. Reject-without-session mode rejects accepted-action journaling when no session exists.
6. Initial acceptance performs no partial mutation when it fails.
7. Late Started or Ended events do not create duplicate records.

## Property Bag snapshots

8. Recorded parameters are deep-copied from the Gameplay Action event.
9. Changing the original request after acceptance does not change the record.
10. Changing Definition defaults after recording does not change the track.
11. Two records never share mutable Property Bag state.
12. A hard runtime world-object reference is rejected by the default policy.
13. A valid soft object reference preserves its soft path.
14. Nested unsupported object data produces a structured parameter path failure.

## Recording lifecycle

15. Starting recording creates a unique Track ID and resets relative time.
16. Eligible Accepted events append exactly one record.
17. Excluded OriginTags remain journal-only.
18. Stop recording enters Draining while tracked actions remain non-terminal.
19. Draining finalizes after every tracked action ends.
20. Immediate-finalize mode leaves unresolved original results explicitly absent.
21. Finalized tracks reject mutation.
22. Record IDs are unique.
23. Equal timestamps preserve deterministic sequence order.

## Track validation

24. A track with duplicate Record IDs is invalid.
25. A track with negative timestamps is invalid.
26. A track with invalid Definition identity is invalid.
27. A non-finalized track cannot be replayed.
28. Validation does not mutate the track.

## Parameter compatibility

29. Strict mode rejects an added, removed or type-changed parameter.
30. Compatible mode copies existing compatible values.
31. Compatible mode preserves current defaults for newly added parameters.
32. Compatible mode reports removed recorded parameters without mutating the track.
33. Type changes are not silently coerced.
34. Execution Lock or ActionTag changes are visible in compatibility diagnostics.

## Playback

35. Playback creates a new request and a new runtime Action Instance.
36. Playback never reuses the original runtime handle.
37. Recorded effective priority is applied to the replay request.
38. Recorded blocked policy is applied to the replay request.
39. Replay requests receive the configured replay OriginTag.
40. Replay requests preserve RecordedIntentId correlation.
41. Replay-originated actions do not append to the source track.
42. Replay-originated lifecycle events do enter the Execution Journal.
43. Entries with equal timestamps submit in deterministic order.
44. Timer scheduling uses absolute session-relative due times without accumulating drift.
45. Pausing playback prevents new submissions.
46. Resuming playback preserves the original timeline offset.
47. Stop playback cancels only replay-owned actions by default.
48. Playback reports AllEntriesSubmitted before final completion when actions are still active.
49. Playback completes only after every replay-owned action is terminal.
50. Stop-on-failure ends the session on the first configured failure.
51. Skip-failed-entry records failure and continues.

## Lifecycle and reentrancy

52. Owner EndPlay cancels timers and async preparation safely.
53. Async Definition load completion after cancellation is ignored safely.
54. A journal observer starting a later recording does not corrupt the current event transaction.
55. An action ending during playback scheduling does not invalidate due-entry iteration.
56. Duplicate terminal callbacks do not complete playback twice.

---

# 36. Explicit non-goals for the first milestone

Do not implement the following as part of this core task:

- frame-by-frame transform recording;
- animation pose recording;
- physics-state replay;
- world snapshot or rewind;
- actor spawning or duplication;
- high-level goals, replanning or fallback selection;
- `GoalAgents` integration;
- grid-specific destination logic;
- target-ID registries for runtime-generated entities;
- save-game serialization;
- track asset creation in packaged builds;
- network replication or prediction;
- multiplayer authority rules;
- custom timeline editor UI;
- custom Blueprint nodes generated from Property Bag fields;
- indefinite retry policies;
- multi-actor recording into one track;
- project-specific OriginTag rules;
- project-specific track filtering;
- project-specific iteration orchestration.

Leave intentional extension points, but do not add speculative systems beyond the defined core.

---

# 37. Core invariants

The implementation is incorrect if any of these invariants can be violated:

1. `IntentReplay` depends on `GameplayActions`; `GameplayActions` never depends on `IntentReplay`.
2. A Replay Track contains copied semantic requests, not runtime Action Instances.
3. A finalized Replay Track is immutable.
4. An Execution Journal is not the authoritative Replay Track.
5. Every Recorded Intent has stable persistent identity distinct from runtime handles.
6. Every playback attempt creates new Gameplay Action requests and runtime handles.
7. Property Bag snapshots do not share mutable state with requests, Definitions or other records.
8. Required journal rejection occurs before action preemption, lock acquisition or execution.
9. Failed initial journal acceptance leaves no partial track mutation.
10. Replay-generated actions are observable but do not recursively append to their source track by default.
11. Recorded priority and blocked policy are preserved for direct replay.
12. Recorded Execution Locks are diagnostic/compatibility data; Gameplay Actions remains runtime lock authority.
13. Deterministic order uses timestamp plus explicit sequence, never pointer or hash iteration order.
14. Track compatibility processing never mutates the source track.
15. Runtime action results never overwrite recorded original intent.
16. Stopping playback cancels only replay-owned actions by default.
17. Playback completion is not reported while replay-owned actions remain non-terminal.
18. Late callbacks cannot revive or re-complete terminal Recording or Playback Sessions.
19. Component teardown leaves no journal registration, timers, async callbacks or replay-owned mappings behind.
20. The core contains no project-specific gameplay or iteration rules.

---

# 38. Definition of done for the core milestone

The core milestone is complete only when:

- the plugin compiles for the project's actual Unreal Engine version;
- the public dependency on `GameplayActions` is correct and no inverse dependency exists;
- journal sink registration and required acceptance work;
- valid action snapshots can be recorded into an isolated mutable session;
- unrecordable parameters produce structured rejection before execution;
- Recording Session draining and finalization work;
- finalized Replay Tracks are immutable;
- stable Track and Record IDs are implemented;
- direct playback creates fresh Gameplay Action requests;
- dynamic Property Bag values, effective priority and blocked policy are preserved;
- replay-originated actions do not recursively modify their source track;
- Execution Journals capture actual replay outcomes;
- recorded-timeline scheduling is deterministic and pause-safe;
- stop and failure policies behave correctly;
- replay-owned actions are tracked and cleaned up;
- parameter schema compatibility policies are implemented and tested;
- automated tests cover the listed critical cases;
- runtime debug information explains recording, filtering, preparation and playback decisions;
- human-facing documentation exists in the plugin `Docs` folder;
- no dependency on `GoalAgents`, `GridWorld`, project iteration logic or editor-only code has been introduced.
