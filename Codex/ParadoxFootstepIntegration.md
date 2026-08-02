# Paradox Footstep Perception and Crouch Integration — Project Module Specification

## Task purpose

Integrate the standalone `FootstepSystem` plugin into the Paradox project runtime module that owns player and clone gameplay behavior.

This milestone must implement project-specific functionality for:

- converting generic `FFootstepEvent` values into semantic hearing noise through `PerceptionKnowledge`;
- configuring AI-noise properties independently from the generic cosmetic footstep profile;
- implementing or completing the player crouch mechanic;
- suppressing AI footstep noise while crouched through a simple configurable boolean;
- preserving footstep audio and VFX while AI noise is suppressed;
- allowing the existing PerceptionKnowledge and IntentReplayPerception pipeline to record and compare footsteps that were actually heard;
- providing detailed debug visualization and tests.

The project integration belongs inside the existing Paradox runtime module, conceptually referred to as `ParadoxGameplay`.

Codex must identify and use the real module name rather than assuming that exact name.

Do not add Paradox-specific code to the generic `FootstepSystem` plugin.

---

# Mandatory preliminary rules

Before modifying code or content, Codex must:

1. read the root `AGENTS.md`;
2. identify the actual runtime module that owns the player Character, clone Character, input configuration, and AI integration;
3. locate and read all relevant `CODEX` instructions for:
   - the Paradox project module;
   - `FootstepSystem`;
   - `PerceptionKnowledge`;
   - `IntentReplay` and `IntentReplayPerception` when validating the downstream observation flow;
4. read the corresponding user-facing `Docs`;
5. inspect the actual public APIs implemented by the FootstepSystem milestone rather than assuming the architecture document is exact;
6. inspect the actual semantic-noise API implemented by PerceptionKnowledge;
7. inspect existing Character, movement, input, crouch, Gameplay Tags, logging, Data Asset, and debug conventions;
8. inspect the actual Unreal Engine headers for the project version before using Character crouch, CharacterMovement, input, AI Perception, or delegate APIs;
9. preserve Blueprint assets and serialized data;
10. make the smallest correct changes;
11. compile the appropriate editor target after every meaningful change;
12. validate behavior in PIE;
13. update user-facing documentation;
14. review the final diff and remove unrelated changes;
15. not consider the task complete until the affected target compiles successfully.

Do not invent or bypass the public contracts created by the previous FootstepSystem, PerceptionKnowledge, or IntentReplayPerception milestones.

---

# Source system contracts

## `FootstepSystem` owns

- animation-synchronized footstep requests;
- foot socket resolution;
- floor trace;
- Physical Material and Physical Surface resolution;
- `FFootstepEvent` production;
- surface-dependent audio;
- surface-dependent Niagara effects;
- optional generic decals;
- generic footstep debug.

It does not own:

- crouch gameplay;
- AI noise;
- PerceptionKnowledge;
- Observation Tracks;
- Replay or Investigating behavior.

## `PerceptionKnowledge` owns

- AI Perception sight and hearing listeners;
- semantic hearing events;
- structured state and event observations;
- the observer's Current Knowledge Store;
- public observation notifications;
- perception and current-knowledge debug.

It does not own:

- footstep animation;
- crouch input;
- project-specific footstep noise policy;
- Observation Tracks;
- clone behavior switching.

## `IntentReplayPerception` owns

- recording heard semantic observations into the original Observation Track;
- recording current replay observations into the Observation Journal;
- matching current hearing observations against expected observations;
- publishing comparison results.

It must not receive direct calls from the footstep adapter.

Required information flow:

```text
FootstepSystem
    ↓ FFootstepEvent
Paradox project adapter
    ↓ semantic noise emission
PerceptionKnowledge
    ↓ hearing observation
IntentReplayPerception
    ├── records Observation Track during the original run
    └── compares Observation Journal during replay
```

---

# Binding architectural decisions

## 1. Integration remains entirely in the project module

Create the Paradox-specific integration inside the runtime project module.

Conceptual classes:

```text
UParadoxFootstepNoiseComponent
UParadoxFootstepNoiseProfile
Player/clone crouch integration
Project-specific debug helpers
```

Use the repository naming conventions.

Do not create:

- a second plugin for this integration;
- a `FootstepPerception` module inside `FootstepSystem`;
- a dependency from FootstepSystem to PerceptionKnowledge;
- a dependency from PerceptionKnowledge to FootstepSystem;
- a direct FootstepSystem-to-IntentReplay integration.

Required dependency direction:

```text
Paradox project module
    ├── depends on FootstepSystem
    └── depends on PerceptionKnowledge

IntentReplayPerception
    depends on PerceptionKnowledge independently
```

---

## 2. Use a project adapter component

Create a project-specific Actor Component conceptually named:

```cpp
UParadoxFootstepNoiseComponent
```

The component must:

- locate or receive the associated `UFootstepComponent`;
- subscribe to its immutable `OnFootstepGenerated` event;
- unbind safely during teardown;
- validate the owning Actor;
- read the current crouch state from the actual owning Character when available;
- select project-specific noise settings;
- emit semantic noise through the public PerceptionKnowledge API;
- preserve the original footstep Actor as noise instigator/source;
- expose debug information;
- never play audio or spawn VFX itself;
- never call IntentReplay directly.

The component may be attached to:

- the player Character;
- clone Characters;
- other Characters that should produce AI-audible footsteps.

It must fail safely when attached to an unsupported Actor.

---

## 3. Crouch noise suppression is a boolean feature

Expose a simple editor- and Blueprint-visible configuration property conceptually named:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Footstep|Perception")
bool bIgnoreNoiseDuringCrouch = true;
```

Use project naming and metadata conventions.

Required semantics:

```text
bIgnoreNoiseDuringCrouch == true
AND owner is currently crouched
    → do not emit semantic footstep noise

bIgnoreNoiseDuringCrouch == false
    → emit semantic footstep noise normally even while crouched
```

There must be no crouch noise multiplier in this milestone.

Do not add:

```text
CrouchNoiseMultiplier
CrouchedLoudnessScale
CrouchedRangeScale
```

The feature is binary and intentionally simple.

The crouch rule affects only AI noise.

It must not prevent:

- `FFootstepEvent` generation;
- surface tracing;
- sound playback;
- Niagara playback;
- optional decals;
- generic FootstepSystem debug.

Required flow while crouched and suppression is enabled:

```text
Anim Notify
    ↓
FootstepSystem
    ├── FFootstepEvent broadcast
    ├── audio plays
    └── VFX spawns
          ↓
ParadoxFootstepNoiseComponent
    detects crouched owner
          ↓
semantic noise emission skipped
```

---

## 4. Check the actual crouch state, not merely input intent

The adapter must determine whether the Character is currently crouched using the actual runtime state supported by the project's Character implementation.

Do not infer crouch solely from:

- the most recent input press;
- a pending request;
- an animation state without authoritative gameplay state;
- an unrelated Blackboard key.

Codex must inspect the actual Character and CharacterMovement implementation.

If the project already exposes a stable stance interface or Gameplay Tag, reuse it.

Otherwise, use the authoritative Character crouch state supported by the Engine version.

The result must remain correct across:

- entering crouch;
- leaving crouch;
- possession changes;
- animation transition frames;
- component initialization order;
- clone Characters when they use crouch later.

---

## 5. Cosmetic and AI-noise configurations remain separate

The generic `UFootstepProfile` continues to control only audio, Niagara, and optional decals.

Create project-specific AI-noise configuration conceptually named:

```cpp
UParadoxFootstepNoiseProfile
```

or use an equivalent configuration struct if a Data Asset is unnecessary.

The project profile may map Physical Surface values to:

```text
Semantic Noise Tag
Base Loudness
Maximum Range
Whether this surface emits AI noise
Optional project-specific cause tag
```

Conceptual structure:

```cpp
USTRUCT(BlueprintType)
struct FParadoxFootstepNoiseResponse
{
    GENERATED_BODY()

    FGameplayTag NoiseTag;
    FGameplayTag CauseTag;
    float Loudness = 1.0f;
    float MaxRange = 0.0f;
    bool bEmitNoise = true;
};
```

Use the exact semantic-noise contract provided by PerceptionKnowledge.

Do not duplicate the cosmetic assets from `UFootstepProfile`.

Do not store `bIgnoreNoiseDuringCrouch` per surface unless the user-facing design later requires that complexity. It must be a simple adapter-level feature switch for this milestone.

---

## 6. The adapter emits noise; listeners decide whether it is heard

A footstep event does not automatically become an observation.

Required semantics:

```text
Footstep generated
    ↓
Semantic noise emitted at source
    ↓
PerceptionKnowledge hearing listener evaluates range/configuration
    ↓
Only listeners that actually perceive it produce an observation
```

This distinction is mandatory.

Do not write directly to an Observation Track because a footstep occurred.

The Observation Track records what the original player listener actually heard, not every footstep generated in the world.

---

## 7. No direct IntentReplay dependency is required

The project footstep integration must not call:

- recording session APIs;
- Observation Track mutation APIs;
- Observation Journal APIs;
- comparison APIs;
- Replay mode APIs.

The existing pipeline handles this:

```text
Semantic noise
    ↓
PerceptionKnowledge observation
    ↓
IntentReplayPerception subscriber
```

A direct dependency on `IntentReplay` or `IntentReplayPerception` is permitted only if inspection proves a small adapter is absolutely necessary and no public PerceptionKnowledge observation path exists.

If that unexpected condition occurs, Codex must stop and document the architectural incompatibility before adding the dependency. It must not silently create one.

---

# Crouch mechanic requirements

## Scope

Implement or complete the basic player crouch mechanic inside the project module.

At minimum it must support:

- an input action or existing input binding;
- entering crouch;
- leaving crouch;
- CharacterMovement crouch eligibility/configuration;
- a clearly observable runtime crouch state;
- compatibility with the character's current animation setup;
- correct interaction with footstep-noise suppression.

Codex must inspect whether crouch is already partially implemented before adding anything.

Do not replace an existing working crouch system.

---

## Input behavior

Follow existing Enhanced Input or project input conventions.

Use either hold or toggle according to existing project design. If no design is encoded in the repository, prefer the smallest implementation consistent with the current input framework and document the choice.

Do not hardcode keys in C++ when the project uses input actions and mapping contexts.

---

## Movement configuration

Use the existing CharacterMovement implementation and animation conventions.

The task may configure or expose:

- whether the Character can crouch;
- crouched movement speed;
- capsule transition behavior already supported by the Engine;
- relevant Blueprint events or animation state access.

Do not redesign locomotion, grid movement, collision, or animation systems beyond what crouch requires.

Do not implement prone, stealth meters, visibility modifiers, or generalized stance frameworks.

---

# Footstep noise resolution

When `OnFootstepGenerated` is received:

```text
1. Validate the event and owner.
2. Resolve the project noise profile.
3. Determine the current Physical Surface.
4. Resolve a surface-specific or default noise response.
5. Check bEmitNoise.
6. Check bIgnoreNoiseDuringCrouch and actual crouch state.
7. If suppressed, record a debug result and return successfully.
8. Build the semantic noise request.
9. Preserve instigator, location, surface, and cause metadata.
10. Emit through PerceptionKnowledge.
11. Publish project debug information.
```

Noise location should normally use the resolved footstep contact location from `FFootstepEvent`, unless the PerceptionKnowledge API or project rules require an Actor origin.

Document the chosen policy.

---

# Semantic noise metadata

Use the actual PerceptionKnowledge event structures.

The semantic noise should preserve enough information for later observation matching.

Conceptually include:

```text
NoiseTag = Noise.Character.Footstep or a surface-specific child tag
CauseTag = Cause.CharacterMovement.Footstep
Instigator = Actor that generated the footstep
SourceEntity = persistent semantic identity when supported
Location = footstep contact location
Loudness = profile value
MaxRange = profile value
PhysicalSurface = optional semantic metadata when supported
```

Do not add a flag such as:

```text
bUnexpectedForClone
```

Expectedness depends on the observing clone's Observation Track and must be determined by IntentReplayPerception.

Do not mark crouched footsteps as expected or ignored observations. When suppression is enabled, no semantic noise is emitted at all.

---

# Self-generated footsteps

The adapter must always provide the correct instigator.

This permits downstream systems to distinguish:

```text
Observer heard its own footstep
Observer heard another Actor's footstep
```

Do not hardcode self-noise filtering into FootstepSystem.

Do not silently remove the instigator in the adapter.

The existing PerceptionKnowledge or project observation policy should decide whether an observer's own noise is ignored, retained as knowledge, or excluded from investigation matching.

If no such policy exists yet, document the observed Engine behavior and add only the smallest project-level filter required by the current design.

---

# Player and clone setup

## Player Character

The player Character should contain or receive:

```text
UFootstepComponent
UParadoxFootstepNoiseComponent
```

The player footstep component produces audio/VFX and neutral events.

The project adapter produces semantic noise unless crouch suppression is active.

The Player Controller's PerceptionKnowledge listener records only noises the player actually perceives according to the Milestone 1 and 2 architecture.

---

## Clone Character

Clone Characters should use the same generic FootstepSystem setup and the same project adapter when they are intended to produce audible footsteps.

This ensures identical source behavior:

```text
Player standing footstep → semantic noise
Clone standing footstep  → semantic noise
Player crouched footstep with suppression → no semantic noise
Clone crouched footstep with suppression  → no semantic noise
```

Do not assume clones already support crouch commands. The adapter only reads crouch state when available.

Do not implement clone crouch planning or replay behavior unless already required by existing recorded Gameplay Actions.

---

# Debug system

The project integration must provide detailed debug information without duplicating FootstepSystem's surface-trace debug.

## Debug classifications

For every received footstep event, classify the AI-noise result as one of:

```text
Emitted
SuppressedByCrouch
DisabledBySurface
MissingNoiseProfile
MissingSurfaceResponse
InvalidEvent
InvalidOwner
EmissionFailed
```

Use a stable enum or equivalent result type when it improves tests and logging.

---

## Visual debug

When enabled, draw:

- a marker at the semantic-noise origin;
- a circle or sphere representing the configured maximum noise range;
- a line from the owner to the noise origin when useful;
- a short-lived text label.

The label should include:

```text
Surface
Noise Tag
Loudness
Max Range
Owner crouched: true/false
bIgnoreNoiseDuringCrouch: true/false
Result classification
```

Suggested semantic visualization:

```text
Noise emitted              → normal hearing-debug color
Suppressed by crouch       → muted/gray or project-configured suppression color
Disabled by surface        → distinct disabled color
Configuration/error result → warning/error color
```

Reuse project debug color and CVar conventions when available.

Do not add the generic hearing-radius cylinder renderer here; that belongs to PerceptionKnowledge according to Milestone 3.

---

## Logging

Create or reuse a project-specific log category.

Log at appropriate verbosity:

- adapter initialization and binding;
- missing FootstepComponent;
- missing noise profile;
- resolved surface noise settings;
- crouch suppression;
- semantic-noise emission result;
- invalid lifecycle or owner state.

Per-step verbose logs must be explicitly enabled to avoid spam.

---

# Runtime ownership and lifecycle

The adapter must:

- bind after both components are initialized;
- tolerate component initialization order;
- avoid duplicate delegate bindings;
- unbind during `EndPlay` or the correct repository lifecycle point;
- validate weak or tracked UObject references before use;
- not retain transient `FHitResult` data longer than required;
- handle possession changes when the component lives on a possessed Character;
- avoid Tick;
- avoid allocating a UObject per step.

If components may be added dynamically, support explicit rebinding or document static-component requirements.

---

# Blueprint authoring requirements

The integration must be configurable in the editor.

Required setup should resemble:

```text
1. Add/configure FootstepSystem's UFootstepComponent.
2. Add UParadoxFootstepNoiseComponent.
3. Assign UParadoxFootstepNoiseProfile.
4. Set bIgnoreNoiseDuringCrouch.
5. Configure crouch input and CharacterMovement.
6. Configure PerceptionKnowledge listener and semantic hearing.
7. Test standing and crouched footsteps in PIE.
```

Expose safe Blueprint APIs and read-only debug state where useful.

Do not expose direct Observation Track mutation or internal PerceptionKnowledge stores.

---

# Proposed project structure

Use the actual module name and layout, but the conceptual files may resemble:

```text
Source/<ParadoxRuntimeModule>/
├── Public/
│   ├── Footsteps/
│   │   ├── ParadoxFootstepNoiseComponent.h
│   │   ├── ParadoxFootstepNoiseProfile.h
│   │   └── ParadoxFootstepNoiseTypes.h
│   └── Characters/
│       └── existing player character headers as required
├── Private/
│   ├── Footsteps/
│   │   ├── ParadoxFootstepNoiseComponent.cpp
│   │   ├── ParadoxFootstepNoiseProfile.cpp
│   │   └── related tests/debug implementation
│   └── Characters/
│       └── minimal crouch integration changes
├── CODEX/
│   └── ParadoxFootstepIntegration.md
└── Docs/
    └── FOOTSTEP_INTEGRATION.md
```

Do not move unrelated Character code merely to match this example.

---

# Module dependencies

The project runtime module may depend on:

```text
FootstepSystem
PerceptionKnowledge
```

and its existing Engine/project dependencies.

Do not add a direct dependency on:

```text
IntentReplay
IntentReplayPerception
```

unless inspection demonstrates that the existing observation subscription architecture is incomplete and a direct bridge is unavoidable.

Remove unused dependencies before completion.

---

# Testing requirements

## Unit and automation tests

Add tests for the project-level decision logic where practical.

At minimum test:

### Crouch suppression enabled

```text
Owner crouched = true
bIgnoreNoiseDuringCrouch = true
Valid surface response
Expected result = SuppressedByCrouch
Semantic noise emission count = 0
```

### Crouch suppression disabled

```text
Owner crouched = true
bIgnoreNoiseDuringCrouch = false
Valid surface response
Expected result = Emitted
Semantic noise emission count = 1
```

### Standing

```text
Owner crouched = false
bIgnoreNoiseDuringCrouch = true or false
Valid response
Expected result = Emitted
```

### Surface disabled

```text
Response bEmitNoise = false
Expected result = DisabledBySurface
```

### Missing configuration

Verify safe, observable results for:

- missing FootstepComponent;
- missing noise profile;
- unmapped surface without fallback;
- invalid semantic-noise request;
- teardown before a delayed callback if any exists.

Prefer isolating the policy in a pure or mostly pure resolver so behavior can be tested without full PIE when practical.

---

## PIE integration scenarios

Create or document a validation scenario containing:

- player Character with FootstepSystem and project noise adapter;
- AI or clone listener configured through PerceptionKnowledge Hearing;
- at least two physical surfaces;
- crouch input;
- audio and visible Niagara responses;
- PerceptionKnowledge debug;
- IntentReplayPerception recording/comparison debug when Milestone 2 is available.

Validate these scenarios:

### Scenario A — standing footstep

1. Player walks on a configured surface.
2. Footstep audio plays.
3. Footstep VFX spawns.
4. Semantic noise is emitted.
5. An in-range listener receives a hearing observation.
6. An out-of-range listener does not receive it.

### Scenario B — crouched with suppression enabled

1. Set `bIgnoreNoiseDuringCrouch = true`.
2. Player crouches.
3. Player moves and generates animation footstep notifies.
4. Audio still plays.
5. VFX still spawns.
6. No semantic noise is emitted.
7. No hearing observation is produced from that footstep.
8. Debug reports `SuppressedByCrouch`.

### Scenario C — crouched with suppression disabled

1. Set `bIgnoreNoiseDuringCrouch = false`.
2. Player crouches and moves.
3. Audio and VFX work.
4. Semantic noise is emitted normally.
5. In-range listeners receive the observation.

### Scenario D — Observation Track recording

1. Begin an original player recording session.
2. Generate one audible standing footstep within listener range.
3. Confirm that PerceptionKnowledge produces the semantic hearing observation.
4. Confirm that IntentReplayPerception records the observation through its normal subscription.
5. Confirm that the footstep adapter never calls IntentReplay directly.

### Scenario E — replay comparison

1. Replay a run containing an expected heard footstep.
2. Confirm the current hearing observation is compared against the source Observation Track.
3. Confirm a conforming footstep is classified as expected.
4. Generate an extra audible footstep not present in the source run.
5. Confirm IntentReplayPerception classifies it through its normal discrepancy pipeline.
6. Confirm the adapter itself does not switch clone behavior.

---

# Documentation requirements

Create or update project documentation describing:

```text
Docs/FOOTSTEP_INTEGRATION.md
```

or the repository-equivalent location.

Documentation must explain:

- why FootstepSystem remains generic;
- why AI-noise conversion lives in the project module;
- component setup for player and clones;
- noise profile authoring;
- exact semantics of `bIgnoreNoiseDuringCrouch`;
- crouch input and movement setup;
- semantic-noise metadata;
- indirect IntentReplayPerception flow;
- debug controls;
- test scenarios;
- common configuration failures.

Explicitly document:

> When `bIgnoreNoiseDuringCrouch` is enabled, crouched footsteps still generate generic FootstepSystem events, audio, and VFX, but no semantic AI noise is emitted.

---

# Explicit non-goals

Do not implement in this task:

- a crouch loudness multiplier;
- a crouch range multiplier;
- noise attenuation curves based on stance;
- stealth visibility modifiers;
- detection meters;
- generalized sound propagation or occlusion;
- direct Observation Track writes;
- direct IntentReplay recording calls;
- Replay/Investigating Behavior Tree changes beyond using existing observation results;
- GOAP behavior;
- clone planning for crouch;
- changes to the generic FootstepSystem profile for AI noise;
- a FootstepPerception plugin or module;
- a new centralized feedback framework;
- footstep IK or procedural locomotion.

---

# Acceptance criteria

The milestone is complete only when all of the following are true:

1. FootstepSystem remains unchanged in dependency direction and has no PerceptionKnowledge or Paradox dependency.
2. The Paradox runtime module subscribes to generic `FFootstepEvent` values through a project adapter component.
3. The adapter emits semantic noise only through the public PerceptionKnowledge API.
4. AI-noise settings are stored in project-specific configuration, not the generic footstep profile.
5. `bIgnoreNoiseDuringCrouch` exists as a simple configurable boolean.
6. When that boolean is true and the owner is crouched, no semantic footstep noise is emitted.
7. When that boolean is false, crouched footsteps emit noise normally.
8. Audio and VFX continue during crouch regardless of AI-noise suppression.
9. No crouch noise multiplier exists.
10. The adapter does not call IntentReplay or mutate Observation Tracks directly.
11. Heard noises reach IntentReplayPerception only through PerceptionKnowledge observations.
12. Correct instigator, source, location, tag, loudness, and range metadata are emitted.
13. The player crouch mechanic is functional or existing crouch behavior is correctly integrated.
14. The adapter works for player and clone Characters where configured.
15. Detailed project-level noise debug is available.
16. Automated tests cover crouch suppression and configuration results.
17. PIE tests validate standing, suppressed crouch, unsuppressed crouch, observation recording, and replay comparison paths.
18. User-facing documentation is complete.
19. The appropriate Unreal editor target compiles successfully.
20. The final diff contains no unrelated changes.

---

# Final implementation report

At completion, Codex must report:

- project files added and modified;
- actual runtime module name;
- dependencies added;
- public adapter and profile APIs;
- crouch input and Character changes;
- exact `bIgnoreNoiseDuringCrouch` behavior;
- semantic-noise metadata and emission path;
- proof that FootstepSystem remains independent;
- proof that no direct IntentReplay calls were added;
- debug controls;
- tests added and executed;
- PIE scenarios validated;
- compilation command and result;
- known limitations;
- any assets that still require manual editor authoring.
