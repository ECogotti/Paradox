# TacticalPause Plugin Architecture

## Purpose

Implement a reusable Unreal Engine 5 runtime plugin named `TacticalPause`.

The plugin provides authoritative control over gameplay simulation time through:

- pause;
- resume/play;
- normal playback speed;
- configurable accelerated playback speeds;
- a default replaceable UMG control widget;
- public C++ and Blueprint APIs;
- observable state-change events;
- safe restoration of the previous temporal state.

This document is intended for Codex and defines the required architecture, scope, constraints, implementation priorities, and validation criteria.

The plugin replaces the earlier, broader `TacticalAnalysis` concept. It must remain focused exclusively on temporal control.

---

# 1. Mandatory scope

The first implementation must support:

- pausing the gameplay simulation;
- resuming the gameplay simulation;
- setting playback speed to `x1`;
- setting playback speed to configurable accelerated values;
- default speed presets of `x1.5`, `x2`, and `x3`;
- querying the current authoritative playback state;
- querying the current selected and applied playback multipliers;
- broadcasting pause, resume, and speed-change events;
- exposing safe C++ and Blueprint APIs;
- creating and managing a default playback-control widget;
- allowing the project to replace the default widget class;
- restoring the temporal state safely during shutdown and world teardown;
- coexistence with external systems that may already have paused or modified world time;
- optional registration of generic participants that require explicit temporal notifications.

The plugin must provide useful native default behavior without requiring Blueprint implementation.

---

# 2. Explicitly out of scope

Do not implement any of the following inside this plugin:

- entity or Actor selection;
- mouse picking;
- world traces for tactical selection;
- inspection panels;
- Goal inspection;
- action inspection;
- replay inspection;
- path inspection;
- grid visualization;
- path visualization;
- vision cones;
- sound or noise areas;
- Actor highlighting;
- red/blue clone classification;
- paradox rules;
- Temporal Index logic;
- tactical camera behavior;
- timeline scrubbing;
- frame-by-frame stepping;
- queued commands during pause;
- future-state prediction;
- gameplay-specific overlays;
- Paradox-specific input or presentation logic.

Those features belong to the game module, such as `ParadoxGameplay`, or to their owning generic plugin.

`TacticalPause` may expose events that allow the game module to show or hide its own tactical interface while the simulation is paused.

---

# 3. Core architectural principles

The implementation must follow these principles:

1. One authoritative owner controls temporal state.
2. UI never calls Unreal pause or time-dilation APIs directly.
3. Public commands validate every transition.
4. Pause state and playback speed are explicit and separate concepts.
5. External pause ownership must not be overwritten accidentally.
6. Speed presets are data-driven.
7. The runtime module must not depend on project gameplay plugins.
8. The plugin must not Tick continuously.
9. Presentation observes state and never becomes the source of truth.
10. Initialization and shutdown must be symmetrical.
11. Failures must remain observable through results and module logs.
12. The plugin must compile and include user-facing documentation before completion.

---

# 4. Recommended plugin structure

```text
Plugins/
└── TacticalPause/
    ├── TacticalPause.uplugin
    ├── Config/
    ├── Content/
    │   └── UI/
    │       └── WBP_TacticalPauseControls_Default
    ├── CODEX/
    │   └── ARCHITECTURE.md
    ├── Docs/
    │   └── README.md
    └── Source/
        └── TacticalPause/
            ├── TacticalPause.Build.cs
            ├── Public/
            │   ├── TacticalPause.h
            │   ├── Settings/
            │   ├── Subsystems/
            │   ├── Types/
            │   ├── Participants/
            │   └── Widgets/
            ├── Private/
            │   ├── TacticalPause.cpp
            │   ├── Settings/
            │   ├── Subsystems/
            │   ├── Types/
            │   ├── Participants/
            │   ├── Widgets/
            │   └── Tests/
            ├── CODEX/
            │   └── ARCHITECTURE.md
            └── Docs/
                └── README.md
```

Create only folders that contain real implementation or documentation. Do not create empty placeholders.

A separate editor module is not required for the first milestone.

---

# 5. Module boundaries and dependencies

The runtime module should depend only on the Unreal modules actually required for:

- core UObject functionality;
- engine and world access;
- subsystem lifetime;
- UMG;
- Slate dependencies required by UMG;
- Developer Settings, if used.

Do not add Enhanced Input unless the plugin directly implements optional generic input bindings.

The module must not depend on:

- `GridWorld`;
- `GameplayActions`;
- `GoalAgents`;
- `IntentReplay`;
- `WorldState`;
- `EntityRelations`;
- `GameplayFeedback`;
- `ParadoxGameplay`;
- the environmental puzzle system.

Any integration that knows both `TacticalPause` and another plugin must live in a separate project or bridge module.

Avoid circular dependencies.

---

# 6. Authoritative world subsystem

Create a world-scoped authoritative subsystem conceptually named:

```cpp
UTacticalPauseWorldSubsystem
```

A world-scoped subsystem is preferred because:

- pause and time dilation belong to a specific world;
- PIE may contain multiple worlds;
- teardown must follow world lifetime;
- the system must not behave as a process-wide singleton.

Before implementation, verify the exact subsystem base class and lifecycle callbacks available in the Unreal Engine version used by the project.

The subsystem owns:

- current playback state;
- current selected playback multiplier;
- current applied playback multiplier;
- previous external temporal state;
- ownership of the pause applied by this plugin;
- ownership of the time dilation applied by this plugin;
- registered participants;
- transition guards;
- state-change delegates;
- restoration during world teardown.

The subsystem is the only class allowed to apply authoritative temporal state.

Widgets, Blueprint libraries, and integration modules must route commands through it.

---

# 7. Playback state model

Do not model the system only with `bIsPaused`.

Use an explicit enum similar to:

```cpp
UENUM(BlueprintType)
enum class ETacticalPlaybackState : uint8
{
    Playing,
    Paused,
    TransitioningToPlay,
    TransitioningToPause
};
```

Exact names may be adjusted to existing project conventions.

The authoritative state must distinguish:

- logical requested state;
- state currently applied to the world;
- selected playback multiplier;
- applied playback multiplier.

Required invariants:

- `Paused` means gameplay simulation is effectively stopped;
- `Playing` means a positive valid multiplier is applied;
- changing speed while paused updates the next resume speed without resuming;
- `Play` resumes using the selected speed;
- repeated `Pause` is idempotent;
- repeated `Play` is idempotent;
- invalid multipliers never alter valid current state;
- a transition in progress cannot be re-entered unsafely.

---

# 8. Pause and speed semantics

The plugin must separate:

## Pause state

Whether gameplay simulation is stopped.

## Selected playback speed

The speed to use while playing and after the next resume.

## Applied playback speed

The speed currently applied to the world.

Example:

```text
Selected speed: x2
State: Paused
Applied speed: stopped

Press Play
    → State: Playing
    → Applied speed: x2
```

Changing from `x2` to `x3` while paused must update only the selected speed.

It must not implicitly resume the game.

This behavior must be documented and reflected by the widget.

---

# 9. Unreal time control

Use Unreal's supported pause and global time-dilation mechanisms.

Before implementation, inspect and verify the exact Unreal APIs available in the project engine version.

Do not invent APIs from memory.

The implementation must account for:

- world pause;
- global time dilation;
- pre-existing pause state;
- pre-existing time dilation;
- Actors or systems configured to tick while paused;
- real-time UI behavior;
- game-time timers versus real-time timers;
- physics behavior;
- animation behavior;
- audio behavior;
- world teardown while paused;
- map changes;
- multiple PIE worlds.

Do not use zero global time dilation as a substitute for proper world pause.

Use the actual pause mechanism for pause.

Use positive global time dilation for accelerated playback.

Reject:

- zero multipliers;
- negative multipliers;
- non-finite values;
- values above a configurable safety maximum.

Do not silently accept invalid values.

---

# 10. External ownership and restoration

The plugin must not assume it is the only system that can pause or alter world speed.

Before taking control, capture the relevant previous temporal state.

Track explicitly:

- whether the world was already paused;
- whether non-default time dilation was already active;
- whether `TacticalPause` applied the current pause;
- whether `TacticalPause` applied the current dilation.

A restoration structure may conceptually resemble:

```cpp
struct FTacticalTemporalSnapshot
{
    bool bWasPaused;
    float PreviousGlobalTimeDilation;
    bool bPauseOwnedByPlugin;
    bool bDilationOwnedByPlugin;
};
```

The exact structure must be designed after inspecting real engine APIs.

During resume or shutdown:

- remove only temporal state owned by this plugin;
- do not unpause a world paused externally;
- do not overwrite a newer external time change without detecting the conflict;
- log ambiguous ownership;
- leave the world in the safest valid state.

---

# 11. Commands and result types

Public mutation APIs must return meaningful results.

Use a result enum or result struct similar to:

```cpp
UENUM(BlueprintType)
enum class ETacticalPauseRequestResult : uint8
{
    Succeeded,
    AlreadyInRequestedState,
    InvalidWorld,
    InvalidPlaybackSpeed,
    UnknownPreset,
    TransitionInProgress,
    ParticipantRejected,
    ApplyFailed
};
```

Suggested commands:

```cpp
RequestPause();
RequestPlay();
TogglePause();
SetPlaybackSpeed(float InMultiplier);
SetPlaybackPreset(FName InPresetId);
```

Suggested queries:

```cpp
GetPlaybackState();
IsPaused();
IsPlaying();
GetSelectedPlaybackSpeed();
GetAppliedPlaybackSpeed();
CanPause();
CanPlay();
CanSetPlaybackSpeed(float InMultiplier);
```

Do not expose mutable internal state as `BlueprintReadWrite`.

---

# 12. Configurable speed presets

Speed presets must not be hardcoded in the widget.

Define a structure similar to:

```cpp
USTRUCT(BlueprintType)
struct FTacticalPlaybackSpeedPreset
{
    FName Id;
    FText DisplayName;
    float Multiplier;
    int32 SortOrder;
    bool bIsDefault;
};
```

Default presets:

```text
Normal    → x1
Fast      → x1.5
Faster    → x2
Fastest   → x3
```

Configuration must support:

- adding presets;
- removing presets;
- changing values;
- changing labels;
- changing order;
- choosing the default speed;
- defining a maximum permitted speed;
- optionally deciding whether speed can be changed while paused.

Reject duplicate preset identifiers or resolve them deterministically with a clear warning.

Use the smallest data-driven solution that satisfies these requirements.

Prefer Developer Settings when a separate data asset is unnecessary.

---

# 13. Plugin settings

Create settings conceptually named:

```cpp
UTacticalPauseSettings
```

Suggested settings:

- available speed presets;
- default preset;
- maximum allowed multiplier;
- whether speed may be selected while paused;
- whether the default widget is created automatically;
- default widget class as a soft class reference;
- widget Z-order;
- whether the widget is created for every local player or only the primary player;
- widget visibility while playing;
- widget visibility while paused.

Use soft class references for configurable widget classes.

Validate settings and report invalid configuration.

Do not hardcode project-specific assets.

---

# 14. Default UMG widget

Provide a default replaceable widget conceptually named:

```cpp
UTacticalPauseControlsWidget
```

The plugin must include a usable default UMG asset.

Required controls:

- Play;
- Pause;
- x1;
- x1.5;
- x2;
- x3.

Speed buttons must be generated from configured presets rather than permanently hardcoded as fixed native properties.

Responsibility split:

```text
UTacticalPauseControlsWidget
    presentation and user interaction

UTacticalPauseWorldSubsystem
    authoritative temporal state
```

The widget must:

- retrieve the authoritative subsystem;
- build speed controls from configuration;
- route commands through subsystem APIs;
- subscribe to state and speed delegates;
- update enabled and selected button states;
- remain responsive while gameplay is paused;
- unsubscribe safely during destruction;
- avoid per-frame polling;
- never call pause or time-dilation APIs directly;
- provide Blueprint extension hooks for presentation.

Expected behavior:

## Playing

- Play appears active or disabled;
- Pause is enabled;
- the current speed appears selected.

## Paused

- Pause appears active or disabled;
- Play is enabled;
- the selected resume speed remains visible.

## Transitioning

- mutation controls are disabled;
- the widget waits for authoritative state events.

A project-defined widget subclass must be able to replace the default presentation without replacing core temporal logic.

---

# 15. Local-player presentation ownership

Do not make the world subsystem own local-player UI.

Use a player-scoped owner conceptually named:

```cpp
UTacticalPauseLocalPlayerSubsystem
```

Preferred responsibility split:

```text
UTacticalPauseWorldSubsystem
    authoritative simulation state

UTacticalPauseLocalPlayerSubsystem
    local widget creation and ownership
```

The local-player subsystem handles:

- loading the configured widget class;
- creating the widget with the correct owning player;
- adding it to the viewport;
- preventing duplicate creation;
- removing it during teardown;
- rebinding when the world changes;
- future split-screen support.

Primary-local-player-only support is acceptable for the first milestone if documented explicitly.

The architecture must not prevent future support for multiple local players.

---

# 16. Optional participants and adapters

Most systems should respect Unreal pause and time dilation automatically.

Some systems may require explicit notifications.

Provide an optional generic participant mechanism without adding dependencies.

Possible callbacks:

```text
BeforePause
AfterPause
BeforeResume
AfterResume
BeforeSpeedChange
AfterSpeedChange
```

Possible architecture:

```cpp
UTacticalPauseParticipantInterface
ITacticalPauseParticipantInterface
```

Use an interface for lightweight participation.

Use adapter UObjects only when integration requires substantial logic or persistent state.

Participants may:

- prepare internal state;
- suspend custom real-time behavior;
- restore state;
- report recoverable failure.

Participants must not:

- become a second source of temporal truth;
- change global pause independently;
- set global time dilation independently;
- introduce dependencies from `TacticalPause` to their owning systems.

Specific bridges must live outside this plugin.

Example:

```text
ParadoxGameplay
└── IntentReplayTacticalPauseAdapter
```

---

# 17. Participant registration

Registration must be explicit and lifecycle-safe.

Suggested APIs:

```cpp
RegisterParticipant(UObject* InParticipant);
UnregisterParticipant(UObject* InParticipant);
```

Requirements:

- validate interface support;
- prevent duplicates;
- use weak references when the subsystem does not own participants;
- remove invalid references safely;
- avoid world-wide searches;
- unregister during participant teardown when possible;
- never keep destroyed UObjects alive accidentally;
- do not iterate participants every frame.

Participants are processed only during temporal transitions and teardown.

---

# 18. Transition order

Pause:

```text
Validate request
    ↓
Set TransitioningToPause
    ↓
Notify BeforePause
    ↓
Apply world pause
    ↓
Notify AfterPause
    ↓
Set Paused
    ↓
Broadcast authoritative state
```

Resume:

```text
Validate request
    ↓
Set TransitioningToPlay
    ↓
Notify BeforeResume
    ↓
Apply selected playback speed
    ↓
Remove plugin-owned pause
    ↓
Notify AfterResume
    ↓
Set Playing
    ↓
Broadcast authoritative state
```

Speed change while playing:

```text
Validate speed
    ↓
Notify BeforeSpeedChange
    ↓
Apply global time dilation
    ↓
Update selected and applied speed
    ↓
Notify AfterSpeedChange
    ↓
Broadcast speed change
```

Speed change while paused:

```text
Validate speed
    ↓
Update selected resume speed
    ↓
Keep the world paused
    ↓
Broadcast selected-speed change
```

If participant rejection is not required initially, prefer notification-only participants and avoid premature transaction complexity.

---

# 19. Failure and rollback

Transitions must not leave the system partially updated.

If a pre-transition participant fails:

- do not apply temporal changes;
- restore already-prepared participants where necessary;
- return failure;
- remain in the previous authoritative state.

If applying Unreal pause or dilation fails:

- restore participant state;
- restore previous world state;
- return failure;
- log exact context.

If a post-transition notification fails:

- do not fake success;
- preserve the valid world temporal state;
- report degraded participant synchronization;
- identify the failing participant.

Define rollback behavior before implementing participant-rejectable transitions.

---

# 20. Delegates and Blueprint events

Expose delegates for at least:

- playback state changed;
- paused;
- resumed;
- selected playback speed changed;
- applied playback speed changed;
- request failed.

Suggested names:

```text
OnPlaybackStateChanged
OnPaused
OnResumed
OnSelectedPlaybackSpeedChanged
OnAppliedPlaybackSpeedChanged
OnRequestFailed
```

Do not broadcast `OnPaused` until the world is actually paused.

Do not broadcast `OnResumed` until the world is actually playing.

Delegate payloads should include useful context:

- previous state;
- new state;
- previous speed;
- new speed;
- request result;
- preset identifier when relevant.

Avoid vague no-argument events when consumers need immediate context.

---

# 21. Blueprint function library

A Blueprint function library may provide convenience access:

```cpp
UTacticalPauseBlueprintLibrary
```

Suggested nodes:

- Get Tactical Pause Subsystem;
- Pause Simulation;
- Play Simulation;
- Toggle Pause;
- Set Playback Speed;
- Set Playback Preset;
- Is Simulation Paused;
- Get Selected Playback Speed;
- Get Applied Playback Speed.

The library must only forward commands to the subsystem.

It must not duplicate state or temporal logic.

Verify the correct World Context metadata pattern before implementation.

---

# 22. Input integration

The first milestone must not require plugin-owned input assets.

Projects may bind their own input to:

```text
Pause input
    → RequestPause or TogglePause

Play input
    → RequestPlay

Speed input
    → SetPlaybackPreset
```

Do not hardcode:

- keyboard keys;
- controller buttons;
- Mapping Context assets;
- Paradox-specific controls.

The default widget is the built-in control surface.

Enhanced Input support may be added later as an optional extension.

---

# 23. Runtime-system considerations

Codex must validate behavior for:

- Character Movement;
- AI controllers;
- physics simulation;
- animation;
- Niagara;
- audio;
- game-time timers;
- real-time timers;
- widgets;
- Actors configured to tick while paused;
- systems using real-world delta time.

The plugin controls global temporal state but does not own every subsystem-specific response.

Systems that intentionally ignore global time must be configured by their owner or integrated through an external adapter.

The default widget must remain interactive during pause.

Do not add global Tick to synchronize UI.

---

# 24. Networking scope

The first milestone is single-player.

Explicit assumptions:

- the world subsystem controls the local gameplay world;
- no replication contract is provided;
- multiplayer is unsupported until designed separately.

Do not add speculative replicated properties or RPCs.

Document that server-authoritative pause and per-client pause require a dedicated multiplayer design.

---

# 25. Logging

Define one module log category:

```cpp
LogTacticalPause
```

Provide macros for:

- Info;
- Warning;
- Error.

Log meaningful events:

- subsystem initialization;
- pause applied;
- simulation resumed;
- speed changed;
- invalid preset requested;
- restoration during teardown;
- participant failure;
- ambiguous external ownership.

Do not use `LogTemp` in committed code.

Do not log every frame.

Include context:

- world name;
- current state;
- requested operation;
- multiplier;
- preset ID;
- participant name;
- failure reason.

---

# 26. Debugging

The plugin does not need world-space visual debugging.

Provide diagnostic support through:

- module logs;
- Blueprint queries;
- automated tests;
- optional console commands.

Potential console commands:

```text
TacticalPause.Status
TacticalPause.Pause
TacticalPause.Play
TacticalPause.SetSpeed <Multiplier>
```

Console commands must call public APIs and must not bypass validation.

Do not create a second temporal-control implementation for debugging.

---

# 27. Performance

The plugin must not Tick continuously.

Use:

- explicit commands;
- delegates;
- subsystem lifecycle callbacks;
- participant notifications;
- widget event bindings.

Avoid:

- repeated world searches;
- per-frame polling;
- rebuilding speed controls on every state change;
- unnecessary allocations;
- high-frequency logs.

Unreal Insights instrumentation is optional for the first milestone.

Add trace scopes only if transition or participant processing becomes non-trivial.

---

# 28. Lifecycle

Initialization must:

- validate settings;
- initialize state from the actual world;
- avoid capturing restoration state until control is taken;
- initialize participant storage;
- expose delegates;
- create UI only through the local-player subsystem.

Shutdown and teardown must:

- reject new transitions;
- unbind delegates;
- remove widgets;
- clear participant references;
- restore plugin-owned temporal state safely;
- avoid callbacks on invalid UObjects;
- avoid unsafe world mutation during teardown;
- leave no static state shared between PIE worlds.

Every delegate binding, registration, or created widget must have a matching cleanup path.

---

# 29. Suggested public types

Suggested names:

```text
ETacticalPlaybackState
ETacticalPauseRequestResult
FTacticalPlaybackSpeedPreset
FTacticalPauseStateChange
FTacticalPauseSpeedChange
FTacticalTemporalSnapshot
UTacticalPauseSettings
UTacticalPauseWorldSubsystem
UTacticalPauseLocalPlayerSubsystem
UTacticalPauseControlsWidget
UTacticalPauseBlueprintLibrary
UTacticalPauseParticipantInterface
```

Do not create all suggested types automatically.

Create only types required by the implemented milestone.

Avoid generic `Manager`, `Helper`, and `Utility` classes.

---

# 30. Suggested first-milestone API

Conceptual API:

```cpp
ETacticalPauseRequestResult RequestPause();
ETacticalPauseRequestResult RequestPlay();
ETacticalPauseRequestResult TogglePause();

ETacticalPauseRequestResult SetPlaybackSpeed(float InMultiplier);
ETacticalPauseRequestResult SetPlaybackPreset(FName InPresetId);

ETacticalPlaybackState GetPlaybackState() const;
bool IsPaused() const;
bool IsPlaying() const;

float GetSelectedPlaybackSpeed() const;
float GetAppliedPlaybackSpeed() const;

TArray<FTacticalPlaybackSpeedPreset> GetAvailablePresets() const;
```

Delegates:

```text
OnPlaybackStateChanged
OnPaused
OnResumed
OnSelectedPlaybackSpeedChanged
OnAppliedPlaybackSpeedChanged
OnRequestFailed
```

Finalize signatures only after inspecting existing project conventions and Unreal reflection requirements.

---

# 31. Default widget workflow

Expected designer workflow:

1. Enable `TacticalPause`.
2. Configure speed presets in Project Settings.
3. Assign the default or custom widget class.
4. Start PIE.
5. The plugin creates the controls for the configured local player.
6. Press Pause to suspend gameplay.
7. Select a speed preset.
8. Press Play to resume at the selected speed.
9. Replace the widget subclass when project-specific presentation is required.

The default widget must function without requiring a Blueprint subclass.

---

# 32. Integration with Paradox

`ParadoxGameplay` may listen to `TacticalPause`:

```text
TacticalPause enters Paused
    ↓
ParadoxGameplay shows tactical inspection UI
    ↓
ParadoxGameplay enables clone selection
    ↓
ParadoxGameplay displays paths and classifications

TacticalPause resumes
    ↓
ParadoxGameplay hides tactical inspection UI
    ↓
ParadoxGameplay clears selection and overlays
```

Dependency direction:

```text
ParadoxGameplay
    depends on TacticalPause
```

Never:

```text
TacticalPause
    depends on ParadoxGameplay
```

These remain entirely inside `ParadoxGameplay`:

- clone selection;
- Temporal Index presentation;
- paradox danger evaluation;
- red/blue highlighting;
- Goal and action panels;
- path and grid visualization;
- vision and noise overlays.

---

# 33. Automated tests

Add focused automation tests.

Minimum cases:

## State transitions

- Playing → Paused;
- Paused → Playing;
- repeated Pause is idempotent;
- repeated Play is idempotent;
- Toggle behaves correctly;
- invalid re-entry during transitions is rejected.

## Speed

- x1 applies correctly;
- x1.5 applies correctly;
- x2 applies correctly;
- x3 applies correctly;
- custom valid speed applies;
- zero is rejected;
- negative speed is rejected;
- non-finite speed is rejected;
- speed above the maximum is rejected;
- changing speed while paused preserves pause;
- Play resumes with the selected paused speed.

## Presets

- preset lookup succeeds;
- unknown preset fails predictably;
- duplicate IDs are diagnosed;
- sort order is stable;
- default preset is deterministic.

## Ownership and teardown

- pre-existing pause is not removed incorrectly;
- previous time dilation is restored when owned;
- teardown while paused restores safely;
- teardown while accelerated restores safely;
- invalid participants do not crash transitions.

## UI

Where practical:

- widget reflects authoritative state;
- active preset updates;
- buttons route through subsystem commands;
- widget creation does not duplicate;
- widget destruction unbinds delegates.

Do not write pixel-based UI tests.

---

# 34. Documentation requirements

Create user-facing documentation in:

```text
Plugins/TacticalPause/Docs/README.md
```

It must explain:

- plugin purpose;
- supported playback states;
- difference between pause and selected speed;
- setup;
- settings;
- default widget;
- custom widget replacement;
- Blueprint API;
- C++ API;
- optional participants;
- restoration behavior;
- limitations;
- single-player scope;
- troubleshooting.

Do not place Codex implementation rules inside `Docs`.

Keep `CODEX` and `Docs` separate.

---

# 35. Implementation phases

## Phase 1 — Runtime core

Implement:

- module;
- log category and macros;
- settings;
- playback state;
- request result types;
- world subsystem;
- pause;
- play;
- x1;
- configurable speed changes;
- restoration;
- delegates;
- Blueprint library;
- tests;
- documentation.

## Phase 2 — Default UI

Implement:

- local-player subsystem;
- native widget base;
- default UMG asset;
- dynamic speed controls;
- state synchronization;
- custom widget setting;
- UI tests and documentation.

## Phase 3 — Optional participants

Implement only after a concrete integration requires it:

- participant interface;
- explicit registration;
- ordered notifications;
- failure reporting;
- rollback behavior;
- tests.

Do not over-engineer Phase 1 for hypothetical integrations.

---

# 36. Definition of done

The `TacticalPause` task is complete only when:

- root and local `CODEX` instructions were read;
- project conventions were inspected;
- the plugin owns only temporal-control responsibilities;
- no tactical-analysis or Paradox-specific logic was added;
- pause works;
- resume works;
- default speeds x1, x1.5, x2, and x3 work;
- custom configured presets work;
- changing speed while paused behaves consistently;
- the world subsystem is the single source of truth;
- the widget never manipulates world time directly;
- the default widget works without Blueprint implementation;
- custom widget replacement is supported;
- external pause and dilation ownership are handled safely;
- initialization and cleanup are symmetrical;
- world teardown leaves no invalid temporal state;
- C++ and Blueprint APIs validate requests;
- failures are observable;
- the module uses `LogTacticalPause`;
- no committed `LogTemp` remains;
- no unnecessary Tick was introduced;
- tests cover state, speed, presets, restoration, and invalid inputs;
- the affected Unreal target compiles successfully;
- `Docs/README.md` exists and is current;
- the final diff contains no unrelated changes.

If the affected target does not compile, the task is not finished.
