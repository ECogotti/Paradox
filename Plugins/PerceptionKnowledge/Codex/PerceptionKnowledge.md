# PerceptionKnowledge — Milestone 1

## Task purpose

Implement a new UE5 runtime plugin named `PerceptionKnowledge`.

The plugin must provide a generic, independent infrastructure to:

- configure `AI Perception` listeners with sight and hearing;
- make generic Actors semantically observable;
- transform Unreal stimuli into structured observations;
- strictly distinguish between **observed states** and **perceived events**;
- retain the current world knowledge of each observer;
- notify other systems whenever a new observation is produced or a known fact changes;
- expose reusable data for both the future `IntentReplay` extension and the future GOAP system;
- provide a highly detailed visual and textual debugging system.

The plugin must not contain any **Paradox**-specific rule.

In particular, it must not know about:

- clones;
- Replay or Investigating;
- Observation Tracks or Observation Journals;
- Temporal Index;
- paradox generation;
- the dynamic visual-cone mesh;
- GOAP Goals, planners, or actions;
- Behavior Trees or StateTrees;
- concrete puzzle rules.

The plugin produces and retains information. It does not decide agent behavior.

---

# Mandatory preliminary rules

Before modifying the project, Codex must:

1. read the root `AGENTS.md` file;
2. identify the module or plugin that owns every piece of code being touched;
3. find and read all relevant `CODEX` instructions, prioritizing those closest to the modified files;
4. read the existing `Docs` for the systems involved;
5. inspect the actual UE API version used by the project and the relevant Engine headers;
6. never invent Unreal API signatures or behavior;
7. compile the appropriate editor target after every meaningful change;
8. update the plugin's user-facing documentation inside `Docs`;
9. not consider the task complete until the affected target compiles.

If the repository already contains a plugin or module with overlapping responsibilities, Codex must inspect it first and reuse compatible patterns instead of introducing unnecessary duplication.

---

# Binding architectural decisions

## 1. One shared information infrastructure, multiple future consumers

The plugin must provide the shared foundation for this chain:

```text
Real world
    ↓
AI Perception — sight and hearing
    ↓
PerceptionKnowledge
    ├── semantic observations
    ├── current observer knowledge
    └── public notifications
          ├── future IntentReplay extension
          └── future GOAP adapter
```

The same observation must later be usable:

- by `IntentReplay`, to record an `Observation Track` and compare it with the current execution;
- by GOAP, to build the Belief State used for planning.

Neither consumer must be implemented in this milestone.

---

## 2. Current knowledge and historical recording are different concepts

`PerceptionKnowledge` must retain only the **current knowledge** of each observer.

Example:

```text
(PC_04, State.Powered) = true
(Door_02, State.Open) = false
(Terminal_01, State.Broken) = Unknown
```

This memory answers:

> What is the latest world state known by this observer?

It must not retain a complete history such as:

```text
PC off → PC on → PC off → PC on
```

The temporal history will belong to the future `IntentReplay` extension.

The plugin must therefore provide:

```text
Current Knowledge Store
    latest known value for each fact

Recent Event Memory
    recent transient events with configurable expiration
```

It must not provide:

```text
Observation Track
Observation Journal
Expected / Unexpected matching
Timeline comparison
```

---

## 3. States and events must remain separate

The conceptual distinction is mandatory and must be explicit in both API and data design.

### Observable state

A state describes a condition with a current value.

Examples:

```text
State.Powered = true
State.Open = false
State.Broken = true
State.Active = false
State.Usable = true
```

States:

- remain valid until updated, invalidated, or forgotten;
- update the observer's `Current Knowledge Store`;
- must support `Unknown`, or an equivalent explicit status;
- must retain source, confidence, and last-observed time.

### Perceivable event

An event describes something that happens at a specific moment.

Examples:

```text
Event.Noise.Impact.Heavy
Event.Device.PoweredOn
Event.Door.Slammed
Event.Terminal.Malfunction
```

Events:

- must not automatically be treated as persistent states;
- are published to listeners;
- may be retained in recent memory for a configurable duration;
- must be recordable later by `IntentReplay` without modifying this plugin;
- must contain sufficient semantic identity, source, position, time, sense, and context.

Do not represent events and states through the same enum with implicit interpretation.

---

# Mandatory evaluation of native Unreal components

## Preferred source component

The preferred implementation path is:

```text
UPerceptionKnowledgeSourceComponent
```

extending:

```text
UAIPerceptionStimuliSourceComponent
```

The native component registers its owning Actor as a source of stimuli for configured senses. The derived class must add semantic exposure of states and events.

Before committing to inheritance, Codex must inspect the Engine headers for the exact project version and verify:

- whether the class can be safely extended without violating lifecycle or reflection constraints;
- which functions are virtual and which are not;
- when registration with the Perception System occurs;
- behavior during construction, registration, `BeginPlay`, `EndPlay`, and destruction;
- whether automatic registration works correctly for Blueprint and C++ components;
- how dynamically spawned Actors are handled;
- how enable, disable, and unregister are handled.

If inheritance is fragile or incompatible with the actual API, use composition:

```text
Actor
├── UAIPerceptionStimuliSourceComponent
└── UPerceptionKnowledgeObservableComponent
```

In either implementation, the plugin's public semantic API must remain equivalent.

The final choice and technical rationale must be documented in `Docs/ARCHITECTURE.md`.

---

## Smart Objects must not be the foundation of the plugin

Do not use `USmartObjectComponent` as the base observable component.

Architectural reason:

```text
Observable
    means an agent can perceive information about the Actor.

Smart Object
    means an agent can discover, claim, reserve, or use an activity offered by the Actor.
```

An Actor may be observable without being interactable or reservable:

- a light;
- an automatic door;
- a broken PC;
- a generator;
- decoration that produces noise;
- a visual indicator;
- a passive puzzle receiver.

For this milestone:

- `PerceptionKnowledge` must not depend on `SmartObjectsModule`;
- it must not derive from `USmartObjectComponent`;
- it must not require a Smart Object Definition;
- it must work on any generic Actor.

A future optional adapter module may be created, for example:

```text
PerceptionKnowledgeSmartObjects
```

That module may translate Activity Tags, slots, or affordances into perceivable information. It is out of scope here.

---

# Proposed plugin structure

The final layout may be adapted to existing repository conventions, but equivalent responsibilities must be preserved.

```text
Plugins/PerceptionKnowledge/
├── PerceptionKnowledge.uplugin
├── CODEX/
├── Docs/
│   ├── README.md
│   ├── ARCHITECTURE.md
│   ├── SETUP.md
│   └── DEBUGGING.md
└── Source/
    └── PerceptionKnowledge/
        ├── PerceptionKnowledge.Build.cs
        ├── Public/
        │   ├── Components/
        │   ├── Data/
        │   ├── Interfaces/
        │   ├── Settings/
        │   ├── Subsystems/
        │   └── Types/
        ├── Private/
        │   ├── Components/
        │   ├── Data/
        │   ├── Settings/
        │   ├── Subsystems/
        │   ├── Debug/
        │   └── Tests/
        ├── CODEX/
        └── Docs/
```

Do not create empty folders merely to anticipate unimplemented functionality.

Create a separate editor module only if it is genuinely required for:

- Data Validation;
- custom details panels;
- editor visualization;
- map validation tools.

The runtime module must not depend on editor code.

---

# Plugin dependencies

The plugin must be independent from all custom project plugins.

Allowed dependencies, when actually required:

```text
Core
CoreUObject
Engine
AIModule
GameplayTags
DeveloperSettings
GameplayDebugger — only if genuinely used and correctly guarded
```

Do not add dependencies on:

```text
IntentReplay
GoalAgents
GameplayActions
GridWorld
WorldState
EntityRelations
SmartObjectsModule
ParadoxGameplay
any concrete puzzle system
```

Dependencies should be private unless their types are exposed by public headers.

---

# Core data types

Names may be adapted to repository conventions, but responsibilities must remain equivalent.

## Persistent entity identity

Every observable source must have a stable identity.

Conceptual example:

```cpp
USTRUCT(BlueprintType)
struct FPerceptionKnowledgeEntityId
{
    GENERATED_BODY()

    FGuid PersistentId;
};
```

Requirements:

- identity must not rely only on a runtime pointer;
- map-placed Actors must retain a persistent ID;
- spawned Actors must receive a valid runtime ID according to a documented policy;
- duplicate IDs must be detected and reported;
- do not expose mutable structures that allow callers to break uniqueness;
- if the project already contains a suitable generic identity system, reuse it only after verifying that doing so does not introduce a dependency on Paradox-specific rules.

Do not introduce a second global identity system if a correct generic one already exists in the repository.

---

## Semantic value

Define a controlled, comparable value type for observable values.

Conceptual example:

```text
FPerceptionKnowledgeValue
├── Bool
├── Integer
├── Float
├── Name
├── GameplayTag
├── EntityId
├── Vector
└── any additional types that are actually required
```

Requirements:

- avoid persistent raw `UObject*` values;
- support deterministic comparison;
- support future serialization without requiring track recording in this milestone;
- avoid free-form strings where `FGameplayTag`, `FName`, or a typed ID is more appropriate;
- do not add speculative value types that are not required.

Evaluate `FInstancedStruct` or a custom variant only after verifying cost, reflection, serialization, and comparability.

---

## State key

Conceptual example:

```cpp
USTRUCT(BlueprintType)
struct FPerceptionKnowledgeStateKey
{
    GENERATED_BODY()

    FPerceptionKnowledgeEntityId EntityId;
    FGameplayTag StateTag;
};
```

Examples:

```text
(PC_04, State.Powered)
(Door_02, State.Open)
(Terminal_01, State.Broken)
```

Gameplay Tags identify semantic meaning, not the identity of a specific instance.

Do not create tags such as:

```text
State.PC.PC_04.Powered
```

---

## Observed state

Conceptual example:

```cpp
USTRUCT(BlueprintType)
struct FPerceptionKnowledgeStateObservation
{
    GENERATED_BODY()

    FPerceptionKnowledgeStateKey Key;
    FPerceptionKnowledgeValue Value;
    FGameplayTag SenseTag;
    float Confidence;
    double WorldTimestamp;
    FVector ObservationLocation;
};
```

The system must explicitly support:

```text
Known true
Known false
Unknown
Invalidated
```

Do not use `false` as a synonym for `Unknown`.

---

## Perceived event

Conceptual example:

```cpp
USTRUCT(BlueprintType)
struct FPerceptionKnowledgeEventObservation
{
    GENERATED_BODY()

    FGuid ObservationId;
    FGameplayTag EventTag;
    FGameplayTag SenseTag;

    FPerceptionKnowledgeEntityId SourceEntityId;
    FPerceptionKnowledgeEntityId InstigatorEntityId;

    FVector WorldLocation;
    float Strength;
    float Confidence;
    double WorldTimestamp;

    FGameplayTag CauseTag;
};
```

Recorded-action and timeline identifiers do not belong in this milestone's core. The format must nevertheless remain extensible so a future `IntentReplay` adapter can attach external correlations without changing the base event's meaning.

---

## Unified observation for consumers

The plugin may expose a discriminated wrapper:

```text
FPerceptionKnowledgeObservation
├── Type = State
│   └── StateObservation
└── Type = Event
    └── EventObservation
```

The distinction must be explicit and type-safe.

Do not rely on ambiguous optional fields to infer whether a record represents a state or an event.

---

## Current known state

Conceptual example:

```cpp
USTRUCT(BlueprintType)
struct FPerceptionKnowledgeKnownState
{
    GENERATED_BODY()

    FPerceptionKnowledgeStateKey Key;
    FPerceptionKnowledgeValue Value;

    FGameplayTag SourceSenseTag;
    float Confidence;
    double LastObservedWorldTime;

    EPerceptionKnowledgeFactStatus Status;
};
```

The structure must support:

- current value;
- knowledge source;
- confidence;
- age;
- `Known`, `Unknown`, `Invalidated`, or equivalent status;
- fact revision number and Knowledge Store revision number.

---

# Main components

## `UPerceptionKnowledgeSourceComponent`

Responsibilities:

- register the owning Actor as a source for configured senses;
- own or aggregate the entity's semantic identity;
- expose current observable states;
- publish changes to observable states;
- emit semantic events;
- support semantic noise emission through Unreal Hearing;
- register and unregister correctly with the plugin subsystem;
- provide bounds and descriptive data for debugging;
- work on both C++ and Blueprint Actors.

Conceptual API:

```text
SetObservableState(StateTag, Value)
RemoveObservableState(StateTag)
InvalidateObservableState(StateTag)
GetObservableStatesForSense(Observer, SenseTag)
EmitObservableEvent(EventData)
EmitSemanticNoise(NoiseData)
GetEntityId()
```

Public APIs must:

- validate Gameplay Tags;
- reject incompatible values;
- return explicit results;
- emit useful warnings or errors for invalid configuration;
- not require callers to know hidden assumptions.

### External providers

For Actors with dynamically computed state, support a generic interface such as:

```text
IPerceptionKnowledgeStateProvider
```

that can gather observable facts without reading arbitrary properties through runtime reflection.

Do not implement a generic automatic binding system for any `UPROPERTY` in this milestone.

Recommended paths:

```text
Gameplay Actor or component
    ↓ when state changes
UPerceptionKnowledgeSourceComponent.SetObservableState(...)
```

or:

```text
PerceptionKnowledge requests a snapshot
    ↓
IPerceptionKnowledgeStateProvider.GatherObservableStates(...)
```

---

## `UPerceptionKnowledgeListenerComponent`

The preferred path is deriving from:

```text
UAIPerceptionComponent
```

to directly add:

- sense configuration;
- semantic stimulus translation;
- per-observer Knowledge Store;
- public plugin events;
- listener debugging.

Codex must first inspect the Engine headers and verify that this extension is correct for the actual UE version.

If inheritance introduces limitations or unsafe behavior, use composition with a normal `UAIPerceptionComponent` while preserving an equivalent public API.

Responsibilities:

- configure Sight and Hearing through a reusable profile;
- receive AI Perception updates;
- resolve the source Actor and its observable component;
- transform stimuli into semantic observations;
- update the observer's Knowledge Store;
- maintain recent events;
- publish delegates for external consumers;
- retain the set of Actors currently perceived by each sense;
- handle loss, forgetting, destruction, and source invalidation;
- support Player Controllers, AI Controllers, and other valid listener Actors.

Conceptual API:

```text
GetKnowledgeSnapshot()
GetKnownState(EntityId, StateTag)
GetKnownStatesForEntity(EntityId)
GetRecentEvents()
IsEntityCurrentlyPerceived(EntityId, SenseTag)
ForgetEntity(EntityId)
InvalidateKnownState(EntityId, StateTag)
```

Conceptual delegates:

```text
OnObservationProduced
OnKnownStateChanged
OnKnownStateInvalidated
OnRecentEventAdded
OnEntityPerceptionChanged
```

The plugin must not expose mutable internal containers.

---

## `UPerceptionKnowledgeWorldSubsystem`

Possible responsibilities:

- registry of observable sources;
- resolving `EntityId` to runtime Actor;
- detecting duplicate IDs;
- maintaining runtime relationships between sources and listeners currently perceiving them;
- event-driven propagation of state changes to observers already seeing the Actor;
- temporary registry of semantic hearing events;
- global debug queries and data;
- cleanup during world teardown.

The subsystem must not become a god object or own every observer's Knowledge Store as the only authoritative source unless a strong technical reason is documented.

An observer's knowledge must have ownership and lifecycle consistent with that observer.

---

## Perception profile

Create a Data Asset or configurable object such as:

```text
UPerceptionKnowledgeProfile
```

that can be shared between the player recording listener and clone listeners:

```text
Sight Radius
Lose Sight Radius
Peripheral Vision Angle
Sight Max Age
Hearing Range
Hearing Max Age
Affiliation filters
Recent event lifetime
Optional visible-state validation interval
Sense enable/disable flags
```

Do not duplicate these values manually between Player Controller and AI Controller.

The plugin must not contain a hardcoded Paradox-specific profile.

---

# Player Controller listener support

The system must explicitly support a `UPerceptionKnowledgeListenerComponent` owned by an `APlayerController`.

This will be required by the next milestone to record what the player perceived during the original run.

Requirements:

1. verify the actual `Body Actor` used by `UAIPerceptionComponent`;
2. verify listener location and direction;
3. use the possessed Pawn as the physical reference when that is the correct project configuration;
4. do not automatically use the top-down camera position;
5. handle `OnPossess` and `OnUnPossess` correctly;
6. request a listener update when the Pawn changes;
7. suspend observation production when no valid body or viewpoint exists;
8. display body, position, and direction in debug;
9. provide a documented Pawn-owned fallback only if tests on the actual UE version prove that Player Controller ownership cannot provide the required viewpoint.

The plugin must not assume that every listener is an `AAIController`.

---

# Sight pipeline

AI Perception Sight must be used as an environmental scanner.

Required flow:

```text
Sight acquires a source
    ↓
resolve PerceptionKnowledge source
    ↓
gather observable states for Sight
    ↓
produce State Observations
    ↓
update Current Knowledge Store
    ↓
OnObservationProduced
```

Sight must be able to acquire at least:

- entity identity;
- observed position;
- explicitly exposed state attributes;
- confidence;
- observation time;
- whether the entity is currently visible.

### Changes while an Actor remains visible

Do not rely only on perception acquisition events.

Required scenario:

```text
The listener sees a powered-off PC.
The PC remains continuously visible.
The PC is powered on.
The listener must produce a new State.Powered = true observation.
```

Required primary path:

1. the Source Component reports an observable-state change;
2. the World Subsystem knows which listeners currently perceive it through Sight;
3. only those listeners receive a semantic refresh request;
4. a new State Observation is produced;
5. the Knowledge Store is updated.

An optional lightweight periodic rescan may be used as a fallback, but:

- it must be configurable;
- it must only scan currently visible sources;
- it must not run every frame;
- it must be disabled when unnecessary;
- it must not replace the event-driven primary path.

### No paradox generation

`AI Perception` Sight:

- must not cause paradoxes;
- must not inspect Temporal Index;
- must not interact with the dynamic visual-cone mesh;
- must not change clone state;
- must not know about `ParadoxGameplay`.

In Paradox, paradox generation remains exclusively based on overlap with the dynamic visual-cone mesh and the project-specific Temporal Index policy, outside this plugin.

---

# Hearing pipeline

Hearing must produce semantic events, not merely generic notifications.

Required flow:

```text
Gameplay Actor emits a Semantic Noise Event
    ↓
plugin wrapper calls Unreal Hearing
    ↓
AI Perception determines which listeners hear it
    ↓
listener receives the stimulus
    ↓
resolve semantic event data
    ↓
produce Event Observation
    ↓
Recent Event Memory
    ↓
OnObservationProduced
```

Create a public wrapper such as:

```text
EmitSemanticNoise(...)
```

that supports at least:

```text
Event Tag
Source Entity
Instigator Entity
World Location
Loudness
Max Range
Strength
Cause Tag
```

The wrapper must actually use Unreal Hearing to determine which listeners perceive the sound.

Do not bypass AI Perception by broadcasting the event directly to every listener.

### Correlating `FAIStimulus` with semantic data

Do not assume that the native stimulus can contain arbitrary custom payloads. Codex must inspect the real API and implement robust correlation.

Strategies to evaluate:

- a short-lived World Subsystem registry indexed by event ID;
- verified use of the native stimulus `Tag` for correlation;
- controlled matching by instigator, position, time, and tag;
- a recent buffer on the Source Component.

The chosen strategy must:

- handle multiple noises emitted close together;
- avoid silently accepting ambiguous correlations;
- expire and clean up obsolete events;
- produce debug warnings when correlation fails;
- be documented.

Do not invent custom-payload support if the real API does not provide it.

---

# Current Knowledge Store

Each listener must own an authoritative Knowledge Store.

## Known states

Conceptually indexed by:

```text
EntityId + StateTag
```

Each update must:

1. compare the new value with the known value;
2. update value and metadata;
3. increment the fact revision when meaning changes;
4. increment a global Knowledge Store revision;
5. notify `OnKnownStateChanged` only when appropriate;
6. still produce `OnObservationProduced` for the current observation according to a configurable anti-spam policy.

## Recent events

Maintain a bounded buffer or ring buffer containing:

```text
ObservationId
EventTag
Source
Location
Time
Strength
Confidence
Expiration
```

Expired events must be removed without an expensive per-entry Tick.

Use timers, batch cleanup, or another measurable and documented strategy.

## Snapshots for future consumers

Expose a function that produces an immutable, consistent copy of current knowledge.

Conceptual example:

```text
BuildKnowledgeSnapshot(Filter)
```

The snapshot will be used by GOAP in a future milestone.

Requirements:

- do not expose mutable internal maps;
- allow filters by state, entity, sense source, or age;
- include the global revision;
- remain usable without later access to source UObjects;
- remain generic and independent from `GoalAgents`.

---

# Blueprint and C++ authoring

The plugin must be usable from both C++ and Blueprint.

## Minimum workflow for an observable Actor

A designer must be able to:

1. add `UPerceptionKnowledgeSourceComponent`;
2. select the senses for which the Actor is registered;
3. assign or validate the persistent identity;
4. expose initial states through configuration or Blueprint;
5. update states through controlled functions;
6. emit semantic events or noise;
7. enable local debug.

Example PC:

```text
BP_Computer
├── PerceptionKnowledgeSource
│   ├── State.Powered = false
│   ├── State.Broken = false
│   └── State.Usable = true
└── gameplay logic
```

When the PC is powered on:

```text
SetObservableState(State.Powered, true)
```

Example door:

```text
SetObservableState(State.Open, true)
EmitSemanticNoise(Event.Noise.Door.Open, ...)
```

## Minimum workflow for a listener

A designer or programmer must be able to:

1. add the listener to a Player Controller, AI Controller, or supported Actor;
2. assign a `PerceptionKnowledgeProfile`;
3. subscribe to public events;
4. query current knowledge;
5. enable local debug.

Blueprint properties must have:

- clear categories;
- complete tooltips;
- the narrowest practical access;
- `EditCondition` where useful;
- clamps for numeric ranges;
- no mutable exposure of internal containers.

---

# Detailed debugging

Debugging is a primary milestone requirement, not an optional add-on.

It must follow:

```text
Global Debug Enabled AND Local Debug Enabled
```

## Global control

Implement at least one mechanism consistent with repository conventions:

- Console Variable;
- Developer Settings;
- debug subsystem;
- console command.

Global control must immediately disable all visual debug output owned by the plugin.

## Local controls

Every Source and Listener must expose a local flag such as:

```text
bEnableDebug
```

It must be disabled by default.

Provide configurable filters for:

- a specific listener;
- a specific source;
- Sight;
- Hearing;
- states;
- events;
- known but not currently perceived entities;
- text labels;
- bounding boxes;
- lines;
- recent-event memory.

## Registered Actor debugging

Visualize every Actor registered as an observable source.

For each Actor, show:

- Actor bounds or configured component bounds;
- representative source point;
- abbreviated persistent ID;
- Actor name;
- senses for which it is registered;
- number of exposed states;
- registration state in the Perception System;
- any configuration errors.

Base color coding for this milestone:

```text
Blue
    registered observable source not perceived by the selected listener

Cyan
    source currently perceived through Sight

Yellow
    source or event perceived through Hearing / uncertain knowledge

Gray
    known entity not currently perceived

Magenta
    invalid configuration, duplicate ID, unresolved source, or semantic error

White
    listener, viewpoint, or neutral information
```

Do not use green and red in this milestone to represent timeline compliance or discrepancy.

That classification requires the Observation Track and belongs to the `IntentReplay` milestone.

## Listener debugging

For the selected listener, display:

- component owner;
- actual Body Actor;
- listener position;
- listener direction;
- Sight Radius;
- Lose Sight Radius;
- cone or directions useful for validating FOV;
- Hearing Range;
- number of known entities;
- Knowledge Store revision;
- number of recent events;
- Actors currently perceived by each sense.

## Observer-to-source lines

Draw a line from the listener viewpoint to each relevant source.

The line must communicate at least:

- the sense that produced the information;
- current perception state;
- last observation time;
- confidence;
- stimulus location when different from Actor location.

Lines must be independently toggleable.

Do not perform additional expensive traces solely for debug unless necessary.

## Known-state labels

Above or beside source bounds, show a configurable maximum number of states:

```text
State.Powered = true
State.Broken = false
State.Usable = true
```

Optionally include:

```text
Source = Sight
Confidence = 1.0
Age = 0.4 s
Revision = 3
```

Provide filters to prevent unreadable text in crowded scenes.

## Hearing debugging

For every recent hearing event, show:

- a sphere or circle at the stimulus location;
- a listener-to-stimulus line;
- Event Tag;
- Source Actor;
- Instigator;
- loudness;
- strength;
- age;
- semantic-correlation status;
- configurable visualization duration.

## Gameplay Debugger

Evaluate a dedicated Gameplay Debugger category such as:

```text
PerceptionKnowledge
```

The category should show:

- selected listener;
- currently perceived sources;
- Knowledge Store;
- recent events;
- Hearing correlation errors;
- revisions and statistics.

If this creates inappropriate dependencies, implement runtime visual debug first and document why Gameplay Debugger was excluded. It must not be mandatory in Shipping builds.

## Extensibility for future comparative debugging

Expose data or hooks so the next milestone can apply:

```text
Green
    observation matches the timeline

Red
    observation differs and may trigger Investigation

Orange
    ambiguous comparison or outside tolerance

Purple
    change differs from the run but was caused by the observer itself
```

`PerceptionKnowledge` must not compute those classifications.

---

# Logging

The module must own one primary category:

```text
LogPerceptionKnowledge
```

Provide macros for at least:

```text
PERCEPTIONKNOWLEDGE_LOG_INFO
PERCEPTIONKNOWLEDGE_LOG_WARNING
PERCEPTIONKNOWLEDGE_LOG_ERROR
```

Do not leave `LogTemp` in committed code.

Logs must include useful context:

- listener;
- source Actor;
- Entity ID;
- sense;
- operation;
- current state;
- failure reason.

Avoid spam from Tick, scans, or repeated updates.

Repeated observations must be rate-limited or guarded by verbose debug settings.

---

# Performance

The system must be event-driven.

Do not add Tick by default to Source, Listener, or Subsystem.

Use:

- AI Perception events;
- observable-state change delegates;
- low-frequency timers only when required;
- batched recent-event cleanup;
- indexed registries;
- caches with clear invalidation rules.

Potentially expensive paths that should be instrumented with Unreal Insights:

```text
PerceptionKnowledge_ProcessSightStimulus
PerceptionKnowledge_ProcessHearingStimulus
PerceptionKnowledge_RefreshVisibleSourceStates
PerceptionKnowledge_BuildKnowledgeSnapshot
PerceptionKnowledge_DrawDebug
```

Do not instrument trivial getters or wrappers.

Measure at least:

- registered source count;
- listener count;
- produced observation count;
- updates discarded as duplicates;
- visible-Actor refresh time;
- debug cost when enabled;
- negligible debug cost when disabled.

---

# Lifecycle and cleanup

Explicitly handle:

- component construction;
- registration;
- initialization;
- `BeginPlay`;
- runtime spawning;
- enable/disable;
- possession changes;
- Sight acquired/lost;
- stimulus forgotten;
- source destruction;
- listener destruction;
- `EndPlay`;
- world teardown;
- module shutdown.

Every delegate binding must have symmetrical cleanup.

Every timer must be canceled.

Every registry must remove invalid references.

Use UObject references tracked correctly by Unreal's Garbage Collector.

Do not retain persistent raw UObject pointers without a clear lifetime guarantee.

---

# Validation and Data Validation

Implement validation for at least:

- invalid Entity ID;
- duplicate Entity ID;
- source with no registered senses;
- invalid state tag;
- invalid event tag;
- value incompatible with its semantic definition, when such a definition exists;
- listener without a profile;
- listener without a valid body/viewpoint;
- Source Component not registered with the system;
- Hearing events that cannot be correlated with semantic data;
- negative or inconsistent range configuration;
- invalid or excessively low rescan interval.

Errors must remain observable through:

- Data Validation, if an editor module is created;
- logs;
- magenta debug visualization;
- explicit API results.

---

# Mandatory tests

Add automated tests or Functional Tests according to repository patterns.

## Source registration tests

- map-placed Actor;
- runtime-spawned Actor;
- enable/disable;
- unregister;
- destruction;
- duplicate ID.

## AI Controller listener tests

- Sight acquires a source;
- states are produced correctly;
- sight is lost;
- knowledge is retained according to policy;
- forgetting or invalidation works.

## Player Controller listener tests

- component registers as a listener;
- correct Body Actor;
- correct viewpoint;
- direction follows the Pawn, not the top-down camera;
- possession updates correctly;
- no observation is produced without a valid Pawn.

## State-change-while-visible test

Scenario:

```text
PC visible and powered off
→ State.Powered = false acquired
→ PC remains visible
→ PC is powered on
→ new State.Powered = true observation
→ Knowledge Store updated
```

The test must verify that losing and reacquiring Sight is not required.

## Hearing tests

- semantic noise emission;
- listener inside range;
- listener outside range;
- two closely timed noises;
- different source and instigator;
- semantic-data correlation;
- event-registry cleanup;
- Recent Event Memory and expiration.

## State/event separation tests

- a state updates the Knowledge Store;
- an event does not automatically overwrite a state;
- a recent event expires;
- a state remains until updated or invalidated.

## Snapshot tests

- snapshot is consistent;
- revision is correct;
- no mutable reference to internal containers;
- snapshot remains valid after a source is destroyed.

## Debug tests

- global off prevents all drawing;
- local off prevents drawing for that instance;
- registered source shows bounds;
- listener shows viewpoint;
- base colors are correct;
- no expensive debug computation when debug is disabled.

---

# Required documentation

Create or update at least:

```text
Docs/README.md
Docs/ARCHITECTURE.md
Docs/SETUP.md
Docs/DEBUGGING.md
```

Documentation must explain:

- plugin purpose;
- state/event distinction;
- current knowledge versus historical timeline;
- how to add an observable source;
- how to update states;
- how to emit events and noise;
- how to configure a Player Controller listener;
- how to configure an AI Controller listener;
- how to query knowledge;
- how to build a snapshot;
- how to enable and filter debug;
- milestone limitations;
- why Smart Objects are not a core dependency;
- future extension toward IntentReplay and GOAP.

User documentation must not contain Codex instructions.

---

# Required deliverables

At completion, the following must exist:

1. a compiling `PerceptionKnowledge` runtime plugin;
2. a semantic Source Component integrated with or composed alongside the native Stimuli Source;
3. a Listener Component integrated with or composed alongside `UAIPerceptionComponent`;
4. Sight and Hearing support;
5. type-safe distinction between states and events;
6. per-observer Current Knowledge Store;
7. Recent Event Memory;
8. immutable snapshots for future consumers;
9. Player Controller and AI Controller support;
10. registry and identity management;
11. detailed debugging system;
12. module logging;
13. relevant tests;
14. documentation in `Docs`;
15. successful compilation of the appropriate target.

---

# Out of scope

Do not implement in this milestone:

- Observation Track;
- Observation Journal;
- IntentReplay relative clock;
- Expected / Unexpected matching;
- green/red timeline comparison color coding;
- Replay → Investigating transition;
- Investigation behavior;
- clone Behavior Tree;
- GOAP;
- planner;
- Goals;
- action costs;
- autonomous exploration;
- clone coordination;
- maintained actions;
- Smart Object integration;
- paradox rules;
- dynamic-cone overlap;
- Temporal Index;
- persistent on-disk knowledge saving;
- multiplayer replication, unless already-existing unavoidable project requirements demand it.

---

# Acceptance criteria

The milestone is accepted when all scenarios below work.

## Scenario A — Seen state

```text
A listener sees a PC.
The PC exposes State.Powered = false.
The listener produces a State Observation.
The Current Knowledge Store contains PC.State.Powered = false.
```

## Scenario B — State changes while continuously visible

```text
The PC remains in sight.
The PC is powered on.
The Source Component reports the change.
The listener produces State.Powered = true.
The Knowledge Store updates the value and revision.
```

## Scenario C — Heard event

```text
A door emits Event.Noise.Door.Open through the semantic wrapper.
A listener within range receives the Hearing stimulus.
An Event Observation is produced with source, instigator, position, and strength.
The event appears in Recent Event Memory.
```

## Scenario D — No timeline behavior

```text
The plugin creates no tracks or journals.
It classifies no observation as Expected or Unexpected.
It changes no AI state.
```

## Scenario E — Future IntentReplay consumer

```text
An external consumer can subscribe to OnObservationProduced
without modifying PerceptionKnowledge.
```

## Scenario F — Future GOAP consumer

```text
An external consumer can obtain an immutable snapshot
containing the observer's latest known states.
```

## Scenario G — Debug

```text
With global and local debug enabled:
- all registered sources display bounds and labels;
- the listener displays viewpoint, radii, and ranges;
- visible sources are cyan;
- hearing information is yellow;
- known but not perceived Actors are gray;
- configuration errors are magenta.

Disabling global debug prevents all plugin drawing.
```

---

# Note for the next milestone

The future `IntentReplay` extension must implement:

```text
PerceptionKnowledge.OnObservationProduced
    ↓
record Observation Track during the player's run

PerceptionKnowledge.OnObservationProduced
    ↓
record Observation Journal during clone Replay

Observation Track + Observation Journal
    ↓
Expected / Unexpected comparison
    ↓
green / red color coding
    ↓
event consumed by the ParadoxGameplay module
```

This note exists only to preserve the correct extension points.

Do not implement those features in this milestone.
