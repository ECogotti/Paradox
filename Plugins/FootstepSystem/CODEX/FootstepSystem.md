# FootstepSystem Plugin — Implementation Specification

## Task purpose

Implement a new reusable UE5 runtime plugin named `FootstepSystem`.

The plugin must provide a generic, self-contained footstep feedback system that:

- receives animation-synchronized footstep notifications;
- determines which foot produced the step;
- traces the contacted floor beneath that foot;
- resolves the physical material and Unreal Physical Surface;
- creates a neutral semantic `FFootstepEvent`;
- selects surface-dependent audio and visual effects from data assets;
- plays footstep sounds;
- spawns Niagara effects;
- optionally spawns decals when enabled by configuration;
- exposes public Blueprint and C++ events for external integrations;
- provides detailed runtime debug visualization and logging.

The plugin must remain fully reusable outside the Paradox project.

It must not depend on:

- `ParadoxGameplay` or any other project module;
- `PerceptionKnowledge`;
- `IntentReplay` or `IntentReplayPerception`;
- AI Perception or `AIModule`;
- clone behavior;
- Replay, Investigating, or GOAP;
- crouch rules specific to Paradox;
- semantic AI noise;
- puzzle systems;
- Temporal Index or paradox rules.

The plugin owns only generic footstep detection and cosmetic feedback.

---

# Mandatory preliminary rules

Before modifying the repository, Codex must:

1. read the root `AGENTS.md`;
2. identify the actual project Unreal Engine version;
3. search the repository for existing footstep, surface-response, animation-notify, audio, Niagara, and physical-material systems;
4. locate and read every relevant `CODEX` folder before touching code;
5. inspect existing project naming, module, logging, data-asset, Blueprint, and debug conventions;
6. inspect the actual Engine headers before using animation notify, physical material, trace, audio, Niagara, decal, or component APIs;
7. avoid inventing Unreal API names or signatures;
8. preserve existing systems and assets unless an intentional migration is documented;
9. make the smallest correct implementation;
10. compile the appropriate editor target after every meaningful change;
11. validate the system in PIE;
12. add user-facing documentation under the plugin `Docs` directory;
13. review the final diff and remove unrelated edits;
14. not consider the task complete until the affected target compiles successfully.

If a compatible footstep system already exists, Codex must inspect it and either extend it safely or document why a separate plugin is necessary.

---

# Binding architectural decisions

## 1. The plugin is completely generic

Required dependency direction:

```text
Game or another plugin
    ↓ subscribes to public events
FootstepSystem
```

Forbidden dependency direction:

```text
FootstepSystem → ParadoxGameplay
FootstepSystem → PerceptionKnowledge
FootstepSystem → IntentReplay
FootstepSystem → AI Perception
```

The plugin must compile and remain useful in a project that does not contain any Paradox plugin or module.

Do not add optional compile-time dependencies on project systems.

---

## 2. A footstep is a neutral event, not an AI noise

The central runtime output is a generic event describing a physical footstep.

Conceptual flow:

```text
Animation Notify
    ↓
UFootstepComponent
    ↓
Floor trace and surface resolution
    ↓
FFootstepEvent
    ├── default audio response
    ├── default VFX response
    └── public delegate for external consumers
```

The plugin must not call:

- AI Hearing APIs;
- `ReportNoiseEvent`;
- `PerceptionKnowledge` semantic-noise APIs;
- `IntentReplay` recording APIs.

An external integration may subscribe to `FFootstepEvent` and convert it into gameplay-specific behavior.

---

## 3. Footstep timing is animation-driven

The primary trigger must be a native custom Animation Notify conceptually named:

```cpp
UAnimNotify_Footstep
```

Use project naming conventions if they require a different prefix.

The notify must represent the moment a foot contacts the floor.

The notify must contain only authoring data required to identify the event, such as:

- left foot, right foot, or unspecified;
- optional socket override;
- optional normalized intensity override;
- optional event tag when a generic tag system is already used by the project.

The notify must not:

- perform the floor trace itself;
- resolve surface response assets;
- play sound directly;
- spawn Niagara directly;
- know about crouch;
- generate AI noise;
- know about the Paradox project.

It must locate the appropriate `UFootstepComponent` on the animation owner and submit a lightweight footstep request.

Failure to find a valid component must be observable in debug builds without causing a runtime crash.

---

## 4. The component owns runtime resolution

Create a reusable Actor Component conceptually named:

```cpp
UFootstepComponent
```

It must be attachable to any suitable Pawn, Character, or Actor with a skeletal mesh.

The component owns:

- receiving requests from the Anim Notify;
- resolving the skeletal mesh and configured foot socket;
- computing the trace start and end;
- performing the floor query;
- resolving the hit Actor, component, physical material, and physical surface;
- constructing `FFootstepEvent`;
- selecting the response from the configured profile;
- playing audio when configured;
- spawning Niagara when configured;
- optionally spawning a decal when configured;
- broadcasting public delegates;
- debug visualization and diagnostic logging.

The component must not own character locomotion, stance, crouch, AI Perception, replay, or gameplay-state decisions.

---

## 5. Physical Surface is the primary response key

Use Unreal Physical Materials and Physical Surface Types as the default surface-classification mechanism.

Required conceptual resolution:

```text
Foot socket
    ↓ floor trace
FHitResult
    ↓
Physical Material
    ↓
EPhysicalSurface
    ↓
UFootstepProfile response
```

The trace must request the information required to resolve the physical material according to the actual Engine version.

The system must support:

- a configured response for a specific surface;
- a default fallback response;
- a safe no-feedback result when neither is available;
- diagnostic reporting for missing surface configuration.

Do not hardcode project-specific surface names in C++.

---

## 6. Cosmetic response data is data-driven

Create a data asset conceptually named:

```cpp
UFootstepProfile
```

It must map Unreal Physical Surface values to generic cosmetic responses.

Conceptual response structure:

```cpp
USTRUCT(BlueprintType)
struct FFootstepSurfaceResponse
{
    GENERATED_BODY()

    // One sound, sound selection asset, or another repository-compatible
    // audio representation selected after inspecting existing conventions.
    AudioAsset;

    NiagaraSystem;

    OptionalDecalMaterial;

    float VolumeMultiplier = 1.0f;
    float PitchMin = 1.0f;
    float PitchMax = 1.0f;
    float NiagaraScale = 1.0f;

    bool bSpawnAudio = true;
    bool bSpawnNiagara = true;
    bool bSpawnDecal = false;
};
```

The exact types must follow the actual repository and Engine conventions.

The profile must not contain:

- AI noise loudness;
- AI hearing range;
- Semantic Noise Tags;
- investigation priority;
- timeline matching settings;
- `bIgnoreNoiseDuringCrouch`;
- any dependency on PerceptionKnowledge.

Those fields belong to the Paradox integration milestone.

---

# Public data contracts

## Foot identifier

Create a Blueprint-visible foot identifier.

A simple enum is acceptable:

```cpp
UENUM(BlueprintType)
enum class EFootstepFoot : uint8
{
    Unspecified,
    Left,
    Right
};
```

Use a Gameplay Tag instead only if the repository already has a strong, consistent tag-based convention for this domain.

The solution must be deterministic and easy to author in an Anim Notify.

---

## Footstep request

The notify-to-component request should contain only the data known at animation time.

Conceptual structure:

```cpp
USTRUCT(BlueprintType)
struct FFootstepRequest
{
    GENERATED_BODY()

    EFootstepFoot Foot = EFootstepFoot::Unspecified;
    FName SocketOverride = NAME_None;
    float NormalizedIntensity = 1.0f;
};
```

Validate and clamp intensity according to documented semantics.

---

## Footstep event

The final resolved event must be immutable to subscribers.

Conceptual structure:

```cpp
USTRUCT(BlueprintType)
struct FFootstepEvent
{
    GENERATED_BODY()

    TObjectPtr<AActor> InstigatorActor = nullptr;
    TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;
    TObjectPtr<UPhysicalMaterial> PhysicalMaterial = nullptr;

    EFootstepFoot Foot = EFootstepFoot::Unspecified;
    TEnumAsByte<EPhysicalSurface> SurfaceType;

    FVector WorldLocation = FVector::ZeroVector;
    FVector SurfaceNormal = FVector::UpVector;
    FVector TraceStart = FVector::ZeroVector;
    FVector TraceEnd = FVector::ZeroVector;

    float NormalizedIntensity = 1.0f;
    float OwnerSpeed = 0.0f;

    bool bHadValidFloorHit = false;
};
```

The exact reflected pointer representation must respect Unreal ownership and garbage collection.

Subscribers must receive the event by const reference or an equivalent immutable public contract.

Do not expose mutable access to internal response state.

---

# Component configuration

Expose editor-friendly configuration for at least:

```text
Footstep Profile
Skeletal Mesh source or resolution policy
Left foot socket
Right foot socket
Default socket
Trace start offset
Trace length
Trace shape or query mode
Trace channel or object-query configuration
Whether to ignore the owner
Whether audio is enabled
Whether Niagara is enabled
Whether decals are enabled
Whether public events are broadcast when no floor hit exists
Debug enable flags
```

Prefer a compact configuration struct when it improves organization without hiding critical values.

Defaults must be safe and documented.

The component must validate its configuration during initialization and when edited where appropriate.

Do not silently use arbitrary socket names when required configuration is missing.

---

# Trace and surface-resolution requirements

## Trace origin

Resolve the socket based on the request:

```text
Left request  → left socket unless overridden
Right request → right socket unless overridden
Unspecified   → default socket or documented fallback
```

The trace must start slightly above the socket and extend downward by a configurable distance.

The system must handle:

- missing socket;
- missing skeletal mesh;
- invalid owner;
- no floor hit;
- floor hit without a physical material;
- surface type with no configured response;
- component disabled during teardown.

Every recoverable failure must return a meaningful result or produce an appropriate diagnostic when debug logging is enabled.

Do not use `check()` for normal runtime configuration failures.

---

## Trace policy

The implementation may use a line trace or a small shape trace.

Codex must inspect the project collision conventions before choosing defaults.

Required priorities:

1. deterministic behavior;
2. low cost;
3. reliable floor contact on grid-based and ordinary walkable geometry;
4. easy debug visualization;
5. no dependency on character-specific floor-caching internals.

Do not query every frame. A trace occurs only when a footstep request is received.

---

# Feedback execution

## Audio

When the selected response contains valid audio and audio feedback is enabled:

- play the configured asset at the resolved footstep location;
- apply configured volume and pitch variation;
- preserve repository audio conventions;
- avoid spawning invalid or empty audio components;
- ensure random pitch selection is bounded and safe;
- make repeated footsteps efficient.

Do not add a project-specific audio middleware dependency unless the repository already requires it and the plugin can remain reusable under that convention.

If the project uses an abstraction layer, reuse it after inspecting its public API.

---

## Niagara

When the selected response contains a valid Niagara system and VFX feedback is enabled:

- spawn it at the hit location;
- orient it using the surface normal or a configurable policy;
- apply configured scale;
- avoid spawning it without a valid floor hit unless explicitly supported;
- keep Niagara as a declared module dependency only when required by implementation.

---

## Decals

Decals are optional.

When enabled by both component and response configuration:

- spawn at the hit location;
- align to the surface;
- use configurable size and lifetime if implemented;
- avoid introducing pooling complexity unless an existing project system already provides it.

If decals are not implemented in the first pass, remove decal fields rather than leaving a nonfunctional public contract. Audio and Niagara are mandatory; decals are optional.

---

# Public notifications

Expose at least one native multicast event and a Blueprint-assignable equivalent when compatible with project conventions.

Conceptual contract:

```text
OnFootstepGenerated(const FFootstepEvent& Event)
```

Broadcast timing must be documented.

Preferred order:

```text
1. resolve floor and surface;
2. construct final FFootstepEvent;
3. broadcast the immutable event;
4. execute default cosmetic feedback;
```

Alternatively, broadcasting after feedback is acceptable if existing repository patterns require it, but the order must be deterministic and documented.

External subscribers must not be able to cancel or mutate the plugin's default feedback unless an explicit, generic policy is intentionally designed and documented.

The event must be broadcast even when feedback assets are missing, provided the floor observation itself is valid. This permits gameplay integrations to remain independent from cosmetic configuration.

Document the behavior when there is no valid floor hit.

---

# Blueprint authoring requirements

The plugin must be straightforward to use without additional C++ work.

Required authoring flow:

```text
1. Add UFootstepComponent to a Character or Pawn.
2. Assign a UFootstepProfile.
3. Configure left and right foot sockets.
4. Add UAnimNotify_Footstep to foot-contact frames.
5. Assign Left or Right in each notify.
6. Configure Physical Materials and Surface Types.
7. Test in PIE with debug enabled.
```

The component, profile, event data, notify settings, and useful debug operations must be Blueprint-visible where appropriate.

Do not expose unsafe lifecycle or mutable internal-state operations merely for Blueprint convenience.

---

# Debug system

The plugin must include detailed domain-specific debug support.

## Debug categories

Provide independently configurable visualization for:

- notify received;
- resolved foot socket;
- trace start and end;
- successful hit point;
- hit normal;
- failed trace;
- resolved Physical Material;
- resolved Physical Surface;
- selected response;
- audio spawn;
- Niagara spawn;
- decal spawn when supported;
- missing profile;
- missing surface response;
- missing socket;
- duplicate or unexpectedly rapid notify calls.

Use the project's existing debug settings and console-variable conventions when available.

---

## Visual debug

At minimum support:

```text
Trace line
Foot socket marker
Hit point marker
Surface-normal arrow
Short-lived text label at the contact point
```

Suggested semantic colors may be used, but do not hardcode a project-wide palette when configurable debug colors already exist.

The text label should be able to show:

```text
Foot
Surface Type
Physical Material
Response asset/profile entry
Intensity
Owner speed
```

Debug drawing must be disabled by default in shipping configurations and must not create persistent runtime Actors.

---

## Logging

Create a dedicated log category.

Useful logs include:

- initialization and configuration validation;
- notify received;
- surface resolution;
- missing response fallback;
- invalid socket;
- failed floor trace;
- feedback spawn failures.

Avoid log spam during ordinary gameplay. Verbose per-step logging must require an explicit debug flag or verbosity level.

---

# Performance and lifetime requirements

- Do not Tick solely to support footsteps.
- Perform floor queries only when a request arrives.
- Avoid repeatedly searching for the skeletal mesh when it can be cached safely.
- Validate cached UObject references before use.
- Unbind delegates during teardown where applicable.
- Do not retain transient hit Actors or components longer than needed.
- Do not create a UObject per footstep.
- Prefer value structs for transient events.
- Avoid synchronous asset loading during a footstep.
- Do not introduce pooling unless profiling proves it necessary or a compatible pool already exists.
- Ensure dedicated-server behavior is explicit; cosmetic feedback may be disabled or skipped according to project conventions.

---

# Proposed plugin structure

Use the repository's actual layout and naming conventions, but the conceptual structure should resemble:

```text
Plugins/FootstepSystem/
├── FootstepSystem.uplugin
├── Source/
│   └── FootstepSystem/
│       ├── FootstepSystem.Build.cs
│       ├── Public/
│       │   ├── Animation/
│       │   │   └── AnimNotify_Footstep.h
│       │   ├── Components/
│       │   │   └── FootstepComponent.h
│       │   ├── Data/
│       │   │   └── FootstepProfile.h
│       │   └── Types/
│       │       └── FootstepTypes.h
│       └── Private/
├── CODEX/
│   └── FootstepSystem.md
├── Docs/
│   ├── README.md
│   ├── ARCHITECTURE.md
│   ├── AUTHORING.md
│   └── DEBUGGING.md
└── Content/
    └── optional generic example assets
```

Do not create an editor module unless a verified authoring requirement justifies it.

Do not add a second module for Paradox integration inside this plugin.

---

# Build dependencies

Codex must determine the exact dependencies from actual includes and Engine headers.

Expected categories may include:

```text
Core
CoreUObject
Engine
GameplayTags only if actually used
Niagara when Niagara spawning is implemented
PhysicsCore or other physical-material dependency only when required
```

Do not include:

```text
AIModule
PerceptionKnowledge
IntentReplay
IntentReplayPerception
ParadoxGameplay
SmartObjectsModule
```

Remove unused dependencies before completion.

---

# Testing requirements

## Automated tests

Add practical automation tests where repository conventions permit.

At minimum verify pure or mostly pure behavior for:

- surface-response lookup;
- default-response fallback;
- foot socket selection;
- request validation;
- intensity clamping;
- missing profile behavior;
- event construction from a synthetic hit result or isolated resolver;
- deterministic response selection when deterministic mode is configured.

Do not build fragile tests around exact audio playback internals if the Engine does not expose reliable assertions. Test the selection and request path instead.

---

## PIE validation map or scenario

Provide a simple test scenario with:

- one Character containing `UFootstepComponent`;
- an animation with left and right footstep notifies;
- at least two floor materials mapped to distinct Surface Types;
- visibly different Niagara responses;
- audibly different sound responses;
- debug visualization enabled.

Validate:

1. left and right notify timing;
2. correct socket selection;
3. correct trace position;
4. correct surface detection;
5. profile fallback behavior;
6. sound playback;
7. Niagara playback;
8. no per-frame trace or Tick requirement;
9. no dependency on any Paradox module.

If binary asset creation cannot be performed safely, provide exact editor-authoring instructions and all required native types so the test assets can be created without additional C++ work.

---

# Documentation requirements

Create or update:

```text
Docs/README.md
Docs/ARCHITECTURE.md
Docs/AUTHORING.md
Docs/DEBUGGING.md
```

Documentation must explain:

- plugin scope and non-goals;
- dependency-free architecture;
- notify-to-component flow;
- surface setup;
- profile authoring;
- audio and Niagara setup;
- public `FFootstepEvent` integration point;
- debug controls;
- common configuration failures;
- how another project module can subscribe without modifying the plugin.

Explicitly document that AI noise, crouch policy, PerceptionKnowledge, and IntentReplay integration are outside this plugin.

---

# Explicit non-goals

Do not implement in this task:

- crouch input or crouch movement;
- `bIgnoreNoiseDuringCrouch`;
- AI Hearing events;
- semantic noise;
- PerceptionKnowledge integration;
- IntentReplay integration;
- Observation Track recording;
- Replay or Investigating state changes;
- GOAP behavior;
- foot-placement IK;
- procedural animation;
- generalized impact effects for weapons or physics objects;
- network prediction redesign;
- a centralized gameplay-feedback framework;
- project-specific surface definitions hardcoded in C++.

---

# Acceptance criteria

The milestone is complete only when all of the following are true:

1. `FootstepSystem` exists as a standalone reusable runtime plugin.
2. The plugin has no dependency on Paradox, PerceptionKnowledge, IntentReplay, or AI Perception.
3. A custom Animation Notify can submit left, right, or unspecified footstep requests.
4. `UFootstepComponent` resolves the configured foot socket and performs one floor query per request.
5. Physical Material and Physical Surface information are resolved safely.
6. A finalized immutable `FFootstepEvent` is broadcast through public C++ and Blueprint-compatible APIs.
7. A data asset maps surfaces to cosmetic responses.
8. Surface-dependent audio works.
9. Surface-dependent Niagara effects work.
10. Missing profile, socket, hit, material, or response conditions fail safely and visibly in debug.
11. The plugin does not require Tick for normal operation.
12. Detailed visual and textual debug tools are available.
13. Automated tests cover the data-selection and validation logic where practical.
14. A PIE validation scenario or exact asset-authoring guide is provided.
15. User-facing documentation is complete.
16. The appropriate Unreal target compiles successfully.
17. The final diff contains no unrelated changes.

---

# Final implementation report

At completion, Codex must report:

- files added and modified;
- plugin and module dependencies;
- public Blueprint and C++ APIs;
- exact Animation Notify authoring workflow;
- surface and profile setup;
- debug commands and controls;
- tests added and executed;
- PIE scenarios validated;
- compilation command and result;
- known limitations;
- any binary assets that still require manual editor creation.
