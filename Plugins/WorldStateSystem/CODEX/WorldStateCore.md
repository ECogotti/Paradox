# World State Core

## Purpose

This document defines the architectural foundation of the `WorldState` Unreal Engine plugin.

The plugin must provide a generic, data-driven system for capturing, comparing and restoring the runtime state of world objects.

The plugin is responsible for:

- registering world-state participants;
- capturing an immutable baseline in memory;
- creating additional runtime snapshots;
- restoring complete or partial snapshot scopes;
- allowing designers to explicitly select which Actor and Component properties are captured;
- allowing designers to capture the relative transform of selected Scene Components through the Participant Component Details Panel;
- serializing selected reflected properties without relying on the Unreal `SaveGame` flag;
- supporting C++ `USTRUCT` and Blueprint User Defined Struct properties as complete values;
- preserving supported soft object and soft class references through their soft paths;
- restoring Actors that were destroyed after capture;
- removing Actors created after capture when required by policy;
- ordering restore operations through phases and explicit dependencies;
- exposing structured results, validation, diagnostics and formal subsystem-level restore lifecycle events;
- leaving project-specific iteration, rewind, clone and puzzle rules to external modules.

The plugin must not contain rules specific to Paradox.

---

# 1. Architectural boundaries

`WorldState` captures and restores world state. It does not own the live gameplay state of every Actor.

The authoritative runtime state remains owned by the Actor, Actor Component or UObject that implements the gameplay behavior.

Conceptual ownership:

```text
Door Actor
    owns current door state

WorldState Snapshot
    owns an independent captured copy

WorldState Subsystem
    coordinates capture and restore
```

The plugin must not become a second continuously synchronized gameplay database.

The normal flow is:

```text
World objects initialize
    ↓
Participants register
    ↓
Baseline is captured explicitly
    ↓
Gameplay changes the world
    ↓
External coordinator requests restore
    ↓
WorldState restores the selected snapshot
    ↓
Participants rebuild derived state
```

## 1.1 Runtime snapshots, not disk saves

The core milestone stores snapshots in memory.

It must not require:

- `USaveGame`;
- `SaveGameToSlot`;
- files on disk;
- the Unreal `SaveGame` property flag;
- the project's existing persistent save system.

Conceptual storage:

```text
UWorldStateSubsystem
├── BaselineSnapshot
└── RuntimeSnapshots
```

A future persistence adapter may export a snapshot to a save system, but persistent save compatibility is outside the core milestone.

## 1.2 Required dependency direction

`WorldState` is a foundational runtime plugin.

It must not depend directly on:

- `IterationLoop`;
- `IntentReplay`;
- `GoalAgents`;
- `GameplayActions`;
- `GridWorld`;
- puzzle systems;
- project-specific gameplay modules;
- UI or feedback systems.

Higher-level systems may depend on and coordinate `WorldState`.

Recommended direction:

```text
IterationLoop → WorldState
ParadoxGameplay → WorldState
PuzzleIntegration → WorldState
TacticalAnalysis → WorldState   optional read-only integration
```

Never introduce the inverse dependency only to make a reset notify a project-specific system.

## 1.3 WorldState does not stop gameplay systems

Before a restore, an external coordinator may need to:

- stop or abort gameplay actions;
- pause AI;
- stop replay execution;
- stop physics-driven gameplay;
- prevent new interactions.

`WorldState` exposes reset lifecycle events, but it does not know how those systems work.

The higher-level coordinator remains responsible for preparing the simulation before calling restore.

---

# 2. Core concepts

The architecture must keep the following concepts separate:

1. **World State Subsystem** — authoritative registry and snapshot coordinator for one `UWorld`.
2. **Participant** — one Actor that can be captured and restored.
3. **Participant Component** — designer-facing configuration and runtime bridge on the Actor.
4. **Capture Source** — the owner Actor or one selected Actor Component whose properties may be captured.
5. **Property Selection** — authored reference to one reflected root property.
6. **Resolved Property Descriptor** — validated runtime resolution of a Property Selection.
7. **Captured Property Record** — one independent serialized property value.
8. **Participant Snapshot** — structural state and captured values for one participant.
9. **World Snapshot** — immutable collection of participant snapshots.
10. **Restore Session** — transient runtime state while applying one snapshot.
11. **Spawn Descriptor** — information needed to recreate a missing participant.
12. **Restore Phase** — broad ordering category for state reconstruction.
13. **Restore Dependency** — explicit relation between participants when phase ordering is insufficient.
14. **Reference Resolution Policy** — rules for soft object and soft class references.
15. **Scene Component Structural Selection** — authored request to capture a selected `USceneComponent` relative transform.
16. **Global Restore Lifecycle Context** — immutable context broadcast by the subsystem for one accepted restore request.
17. **Snapshot Result** — structured success, warning or failure information.

Do not merge the immutable snapshot with mutable Restore Session state.

Do not store live `FProperty*`, Actor pointers or Component pointers as authoritative snapshot identity.

---

# 3. Runtime and editor modules

Create two modules from the first implementation:

```text
WorldState
WorldStateEditor
```

## 3.1 `WorldState`

The runtime module owns:

- `UWorldStateSubsystem`;
- `UWorldStateParticipantComponent`;
- participant IDs;
- property selection data;
- runtime property resolution;
- capture and restore;
- snapshot data;
- restore sessions;
- spawn strategies;
- validation results;
- Blueprint APIs and participant events;
- global subsystem restore lifecycle delegates and their immutable event contexts;
- logging and runtime debug inspection;
- automated runtime tests.

## 3.2 `WorldStateEditor`

The editor module owns:

- the property picker for `UWorldStateParticipantComponent`;
- Details Panel customization;
- property type filtering and warnings;
- validation of missing or incompatible properties;
- duplicate participant-ID validation;
- optional repair and regeneration tools;
- editor visualization of restore phases and dependencies;
- editor-only tests.

The runtime module must never depend on `WorldStateEditor`.

The editor module must not be required in a packaged build.

## 3.3 No Blueprint compiler extension in the core

The first implementation must not modify:

- Blueprint variable metadata;
- Blueprint variable flags;
- the Blueprint compiler pipeline;
- the Unreal `SaveGame` flag;
- engine property flag definitions.

The property selection is ordinary serialized configuration owned by the Participant Component.

---

# 4. Primary runtime owner

Create a `UWorldSubsystem` conceptually named:

```cpp
UWorldStateSubsystem
```

One subsystem instance coordinates one `UWorld`.

## 4.1 Responsibilities

The subsystem must:

- register and unregister participants;
- reject or report duplicate participant IDs;
- finalize registration before baseline capture;
- capture the immutable baseline;
- create optional runtime snapshots;
- retain snapshots through reflected ownership or value ownership;
- validate restore requests;
- construct the restore scope;
- order restore work;
- recreate missing participants;
- remove participants that should not exist;
- apply structural and selected property state;
- coordinate reference validation and post-restore callbacks;
- produce structured results;
- expose read-only debug and inspection data;
- clean up safely during world teardown.

## 4.2 Subsystem state machine

Use an explicit state machine.

Recommended states:

```text
Initializing
Registering
ReadyWithoutBaseline
Capturing
Ready
Restoring
Failed
ShuttingDown
```

Rules:

- baseline capture is rejected before registration is finalized;
- concurrent captures or restores are rejected by default;
- the baseline is immutable after successful capture;
- a failed restore must leave an observable result;
- teardown aborts active work safely;
- terminal teardown state cannot return to Ready.

## 4.3 Game Thread authority

The initial implementation performs:

- UObject discovery;
- property resolution;
- property serialization;
- Actor spawning and destruction;
- property restoration;
- Blueprint callbacks;

on the Game Thread.

Do not move UObject reflection or restoration to worker threads without a separately verified thread-safe design.

---

# 5. Participant Component

Create a Blueprint-spawnable Actor Component conceptually named:

```cpp
UWorldStateParticipantComponent
```

An Actor participates in snapshots by owning one component.

## 5.1 Component responsibilities

The component owns designer-facing configuration for:

```text
ParticipantId
Structural capture policies
Per-SceneComponent relative-transform capture selections
Captured property selections
Restore phase
Restore dependencies
Reset groups or tags
Existence policy
Spawn policy
Reference policy overrides
Debug enablement
```

It also provides lifecycle events and a controlled bridge between the subsystem and the owning Actor.

## 5.2 Stable participant identity

Create a value type conceptually named:

```cpp
FWorldStateParticipantId
```

An `FGuid`-backed implementation is acceptable.

Requirements:

- invalid by default;
- comparable and hashable;
- Blueprint-visible;
- serialized with the component;
- stable across Play sessions for world-authored participants;
- preserved when the participant is recreated from a snapshot;
- not derived only from pointer, array index, runtime spawn order or display label.

The editor module must detect duplicate IDs.

Duplicating an Actor in the editor must not silently create two valid participants with the same ID. Provide an explicit duplication repair policy or validation error.

## 5.3 Component lifecycle

The component should register during controlled runtime initialization and unregister symmetrically during EndPlay or destruction.

Do not rely on repeated world searches.

Registration must remain safe for:

- streamed levels;
- dynamically spawned Actors;
- Blueprint reconstruction in editor;
- world teardown;
- Actor destruction during gameplay.

---

# 6. Explicit property selection

The designer chooses captured values through the Participant Component.

Do not infer captured state from:

- `SaveGame` flags;
- custom metadata;
- property categories;
- property names;
- `EditAnywhere`;
- replication flags.

## 6.1 Authored selection type

Create a serialized value type conceptually named:

```cpp
FWorldStatePropertySelection
```

It should contain at least:

```text
CaptureSourceId
PropertyPath
Optional expected owner class
Optional expected property type signature
Enabled state
Optional per-property restore phase override
```

For the first milestone, `PropertyPath` contains exactly one root property name.

Example:

```text
CaptureSourceId = OwnerActor
PropertyPath    = [bIsOpen]
```

Component example:

```text
CaptureSourceId = DoorComponent
PropertyPath    = [OpeningState]
```

## 6.2 Capture Source

A Capture Source identifies which object contains the selected property.

The first milestone supports:

```text
Owner Actor
Actor Components owned by the participant Actor
```

Create a value type conceptually named:

```cpp
FWorldStateCaptureSourceId
```

Recommended source semantics:

```text
OwnerActor
Component:<StableComponentKey>
```

Do not identify a Component only by its current array index.

For default subobjects and Blueprint-authored components, a validated stable component name or equivalent subobject identity may be used.

Runtime-created Components require an explicit stable identity and reconstruction policy before they can be selected reliably.

## 6.3 Scene Component structural selection

The Participant Component must also store explicit structural selections for owned `USceneComponent` instances.

Create a serialized value type conceptually named:

```cpp
FWorldStateSceneComponentCaptureSelection
```

It should contain at least:

```text
CaptureSourceId
Capture Relative Transform
Enabled state
Optional strict parent-validation policy
```

Rules:

- the selected source must resolve to an owned `USceneComponent`;
- the relative transform is captured as one complete `FTransform` structural value;
- the transform selection is independent from reflected property selections;
- a Scene Component may capture only its relative transform, only selected properties, or both;
- the first milestone should expose relative-transform capture for non-root Scene Components;
- the Actor/root transform remains governed by `Capture Actor Transform` to avoid two competing transform authorities;
- missing or incompatible Scene Component sources produce structured validation errors;
- runtime-created Scene Components require stable identity and reconstruction support before their relative transform can be restored reliably.

This configuration is normal serialized Participant Component data. It does not use property metadata and it is available in cooked builds.

## 6.4 Root-property selection only

The first milestone selects complete root properties.

Examples:

```text
Supported selection:
Actor.DoorState

Not yet supported:
Actor.DoorState.bIsOpen
```

When `DoorState` is a struct, the entire struct is captured and restored as one property value.

This rule keeps the initial implementation predictable while still supporting arbitrarily complex structs and containers.

## 6.5 Per-class and per-instance configuration

Because selections are ordinary component properties:

- selections authored in a Blueprint class become defaults for all instances;
- a level instance may override the selection list;
- inherited Blueprint classes may extend or replace the list according to normal Unreal property inheritance behavior.

Do not add a second hidden source of selection truth.

---

# 7. Editor property picker

The `WorldStateEditor` module provides a custom Details Panel picker for `CapturedProperties`.

Recommended initial UI:

```text
Structural State

Capture Actor Transform                                      [✓]

Scene Component Relative Transforms
[DoorPivotComponent                    Capture Relative Transform ✓]
[SlidingPanelComponent                 Capture Relative Transform ✓]

Captured Properties

[Owner Actor.bIsOpen                     ▼] [Remove]
[Owner Actor.DoorState                   ▼] [Remove]
[DoorComponent.OpeningPercentage         ▼] [Remove]
[PowerComponent.bIsPowered               ▼] [Remove]

[Add Property]
```

The custom Details Panel may group relative-transform selection and reflected-property selection under the same Capture Source, but they remain distinct serialized policies.

The picker should group candidates by Capture Source:

```text
Owner Actor
├── bIsOpen
├── DoorState
└── Inventory

DoorComponent
├── OpeningPercentage
└── CurrentMode

PowerComponent
├── bIsPowered
└── PowerState
```

## 7.1 Picker requirements

The picker must:

- enumerate owned Scene Components that are eligible for relative-transform capture;
- enumerate reflected root properties from the selected source object class;
- show inherited properties when valid;
- show C++ and Blueprint-defined properties;
- show Blueprint User Defined Struct variables;
- show C++ `USTRUCT` variables;
- show containers when their complete nested type is supported;
- indicate property type;
- prevent duplicate property selections and duplicate Scene Component transform selections;
- report unsupported references before runtime;
- display missing selections clearly after rename or deletion;
- participate in Undo/Redo and normal transaction handling;
- avoid directly mutating Blueprint variable metadata;
- disable or warn about relative-transform capture on the root Scene Component when Actor transform capture is already authoritative;
- warn when a selected Scene Component may change parent but attachment capture is disabled.

## 7.2 Missing properties

If a selected property is renamed or removed, retain the invalid selection long enough to diagnose it.

Example editor display:

```text
Missing: OwnerActor.bIsOpen
```

Do not silently remove the selection or redirect it to another property with a similar name.

The designer may choose the replacement property explicitly.

## 7.3 Property renames

Automatic rename recovery is optional and not required for the core milestone.

The required behavior is:

- detect the missing path;
- report the owning Blueprint or class;
- report the participant and source;
- prevent an invalid baseline from being accepted when strict validation is enabled.

A future implementation may integrate verified engine property redirects.

---

# 8. Supported property model

The core serializer should not use a narrow primitive-type whitelist.

It should support any reflected root `FProperty` whose complete value graph passes the World State validation policy and can be serialized through the verified Unreal property serialization path.

This means “any data type” in the plugin refers to:

> any reflected and supported `UPROPERTY` value, not arbitrary non-reflected C++ memory.

## 8.1 Generally supported value categories

The architecture should support, when verified against the project's Unreal version:

- booleans;
- signed and unsigned numeric properties;
- floating-point properties;
- enums and bytes;
- names;
- strings;
- text;
- vectors, rotators, transforms and other reflected engine structs;
- Gameplay Tags and tag containers when present as reflected properties;
- C++ `USTRUCT` values;
- Blueprint User Defined Struct values;
- arrays;
- sets;
- maps;
- nested combinations of supported value properties;
- soft object references;
- soft class references;
- `FSoftObjectPath` and `FSoftClassPath` where appropriate.

Do not manually implement a serializer for every primitive type when Unreal's property system already owns the property's serialization behavior.

## 8.2 C++ `USTRUCT` support

A selected C++ `USTRUCT` property is represented by an `FStructProperty`.

The complete struct value must be captured as one property payload.

Example:

```cpp
USTRUCT(BlueprintType)
struct FDoorState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsOpen = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float OpeningPercentage = 0.0f;
};
```

Designer selection:

```text
OwnerActor.DoorState
```

Snapshot behavior:

```text
Capture and restore the complete FDoorState value
```

Do not require a dedicated World State adapter for every C++ struct.

## 8.3 Blueprint User Defined Struct support

A Blueprint User Defined Struct is also reflected through a struct property and script-struct type.

The complete value must be captured and restored as one property payload.

The serializer must not assume that only native C++ `UScriptStruct` types are valid.

The editor validator and automated tests must include at least one User Defined Struct.

## 8.4 Nested structs and containers

Even though selection stops at the root property, serialization may recurse internally through:

- nested structs;
- arrays of structs;
- maps containing structs;
- sets of supported values;
- structs containing supported containers.

The complete nested graph must be validated.

A root struct must not be approved merely because its top-level type is an `FStructProperty`.

## 8.5 Custom struct serialization

Use the actual Unreal property and script-struct serialization behavior rather than raw memory copying.

Do not use `memcpy` as the general snapshot format for structs because structs may contain:

- dynamic arrays;
- strings;
- names;
- maps and sets;
- soft references;
- custom serialization behavior;
- non-trivial initialization and destruction requirements.

Codex must inspect the actual engine APIs and headers for the project's Unreal version before selecting the concrete archive and `FProperty` serialization calls.

## 8.6 Unsupported property categories

Reject or explicitly require a custom handler for categories such as:

- delegates and multicast delegates;
- function references;
- latent execution state;
- timer handles as authoritative gameplay state;
- raw unreflected memory;
- runtime lambdas or callbacks;
- editor-only properties in packaged runtime snapshots;
- unsupported instanced object graphs;
- properties whose nested graph contains forbidden hard or weak UObject references.

A property must never be shown as safely supported merely because an archive can produce some bytes for it.

---

# 9. UObject and Actor reference policy

The default World State policy supports only path-based soft references inside captured properties.

Allowed reference forms should include, when verified:

```text
TSoftObjectPtr<T>
TSoftClassPtr<T>
FSoftObjectPath
FSoftClassPath
```

The default policy rejects:

```text
UObject*
AActor*
TObjectPtr<T>
TWeakObjectPtr<T>
TScriptInterface<T> containing a hard runtime object reference
raw Component pointers
hard object references nested inside structs or containers
```

## 9.1 Soft reference semantics

A `TSoftObjectPtr` represents a soft object path and does not require the snapshot to retain a hard reference to the object.

Conceptual capture:

```text
TSoftObjectPtr<AActor>
    ↓
FSoftObjectPath
    ↓
serialized property payload in RAM
```

Conceptual restore:

```text
serialized soft path
    ↓
restored TSoftObjectPtr
    ↓
optional later resolution
```

The serializer must preserve the soft path. It must not silently convert the reference into a hard pointer.

## 9.2 Recursive reference validation

Reference validation must inspect the complete property graph.

Examples:

```text
FMyStruct
└── TSoftObjectPtr<AActor>

Result: supported
```

```text
FMyStruct
└── TObjectPtr<AActor>

Result: rejected by default
```

```text
TArray<FMyStruct>
└── nested TSoftObjectPtr<UObject>

Result: supported
```

The validation error must identify the nested path that contains the unsupported reference.

## 9.3 Assets versus world objects

Soft references may point to:

- assets;
- classes;
- world-authored Actors;
- Components or subobjects with stable paths;
- runtime-created objects that can be recreated at the same path.

The existence of a soft path does not guarantee that the object can be resolved at restore time.

The restore result must distinguish:

```text
PathRestored
Resolved
UnresolvedAllowed
UnresolvedRequired
InvalidPath
```

## 9.4 Default loading policy

The core must not synchronously load every unresolved soft reference automatically.

Recommended defaults:

- asset soft references may be left unresolved unless a configured policy requests loading;
- world Actor soft references should be resolved only after the existence phase;
- missing world objects should produce a structured warning or failure according to property policy;
- no package load should occur unexpectedly during a hot restore unless explicitly configured.

## 9.5 Runtime-spawned Actors and path stability

A soft Actor reference can be restored only if the referenced Actor exists at a compatible object path when resolution occurs.

For Actors managed by World State, the Spawn Descriptor and Spawn Strategy must preserve a stable path whenever path-based references are expected to survive destruction and recreation.

Conceptual requirement:

```text
Captured Actor path:
PersistentLevel.RuntimeDoor_12

Actor destroyed
    ↓
WorldState recreates Actor
    ↓
Recreated Actor must use a compatible name and outer/level
    ↓
Soft path can resolve again
```

If the spawn strategy cannot preserve the path, the reference must remain unresolved and the restore result must report it.

Do not pretend that every runtime-generated object path is automatically stable.

## 9.6 Future stable-identity reference type

A future extension may add a World State-specific reference carrying a Participant ID and optional soft-path fallback.

That extension is not required for the initial milestone because the chosen default project policy uses `TSoftObjectPtr` and path serialization.

Do not implement it speculatively unless path stability proves insufficient in real use.

---

# 10. Property validation service

Create a centralized validation service conceptually named:

```cpp
FWorldStatePropertyValidator
```

or an equivalent runtime/editor shared service.

Do not scatter type checks across:

- the property picker;
- capture code;
- restore code;
- Blueprint libraries.

## 10.1 Validation inputs

Validation receives:

```text
Source object class
Property selection
Resolved FProperty
Capture and reference policies
Runtime/editor context
```

## 10.2 Validation result

Create a structured result conceptually named:

```cpp
FWorldStatePropertyValidationResult
```

Include at least:

```text
Status
Source identity
Property path
Property type
Nested failure path
Reason
Optional diagnostic message
```

Recommended broad statuses:

```text
Valid
MissingSource
MissingProperty
UnsupportedPropertyType
UnsupportedNestedType
HardObjectReferenceRejected
WeakObjectReferenceRejected
EditorOnlyPropertyRejected
TypeSignatureMismatch
InvalidSoftPathPolicy
```

## 10.3 Shared editor and runtime rules

The editor picker should hide clearly unsupported properties by default but allow a diagnostic view showing why they are unavailable.

Runtime capture must validate again.

Editor validation improves authoring but is not a runtime security boundary.

---

# 11. Resolved property descriptors

Authored selections use stable serialized names and source identifiers.

At runtime, resolve them into transient descriptors conceptually named:

```cpp
FResolvedWorldStateProperty
```

A resolved descriptor may cache:

```text
Source object weak reference
Resolved FProperty pointer
Value address access strategy
Validated property type signature
Serializer strategy
Reference policy result
```

## 11.1 Cache lifetime

Resolved `FProperty*` pointers are not snapshot identity.

They may be cached only while the owning class layout remains valid.

In editor builds, Blueprint recompilation or class reconstruction may invalidate cached fields.

The editor/runtime cache must be invalidated or rebuilt when required.

## 11.2 Snapshot identity

The snapshot stores durable descriptive identity such as:

```text
CaptureSourceId
PropertyPath
PropertyTypeSignature
Optional declaring class path
```

It must not store a raw `FProperty*` for later authoritative restore.

## 11.3 Type signature

Store enough type information to detect incompatible restore attempts.

The signature should distinguish at least:

- property category;
- struct or enum identity;
- container element/key/value types;
- soft object or class target type where useful.

Use verified engine type-name facilities when appropriate rather than inventing unstable display strings.

Do not silently coerce unrelated property types.

---

# 12. Property serialization pipeline

Create one centralized property serializer conceptually named:

```cpp
FWorldStatePropertySerializer
```

The serializer operates on one resolved root property at a time.

## 12.1 Capture flow

```text
Authored Property Selection
    ↓
Resolve Capture Source
    ↓
Resolve root FProperty
    ↓
Validate complete property graph
    ↓
Create memory archive
    ↓
Serialize the property value
    ↓
Store independent payload and type data
```

## 12.2 Restore flow

```text
Captured Property Record
    ↓
Resolve current Capture Source
    ↓
Resolve current root FProperty
    ↓
Validate type compatibility
    ↓
Create memory reader/archive
    ↓
Deserialize into the property value
    ↓
Record property-level result
```

## 12.3 Per-property payloads

Store each selected property as an independent record rather than serializing the complete Actor into one opaque blob.

Benefits:

- clear diagnostics;
- partial property failure reporting;
- easier schema validation;
- simpler debugging;
- optional future partial-property restore;
- no accidental serialization of unselected Actor state.

## 12.4 Archive requirements

The concrete archive must be verified against the project's Unreal Engine version.

It must correctly support the selected property types, including:

- names;
- strings and containers;
- C++ structs;
- User Defined Structs;
- soft object paths;
- soft class paths.

The archive must not set `ArIsSaveGame` merely to reuse the project's persistent save behavior.

The World State selection list is the capture filter.

The implementation may use an appropriate memory archive and proxy archive, but Codex must inspect the exact engine headers and serialization contracts before implementation.

## 12.5 No hidden hard-reference conversion

Even if a generic proxy archive is capable of writing a hard UObject reference as a string, the World State validator must reject forbidden hard references before serialization.

The archive is not the reference policy.

## 12.6 Snapshot isolation

Captured property payloads must own independent copied data.

Changing the live property after capture must not change the snapshot.

Two snapshots must not share mutable serialized buffers accidentally.

---

# 13. Snapshot data model

Create an immutable value hierarchy conceptually equivalent to the following.

## 13.1 Captured property record

```cpp
FWorldStateCapturedProperty
```

Recommended data:

```text
CaptureSourceId
PropertyPath
PropertyTypeSignature
SerializationFormatVersion
Payload
Optional capture diagnostic metadata
```

`Payload` may be a `TArray<uint8>` in the first implementation.

## 13.2 Capture source snapshot

```cpp
FWorldStateCaptureSourceSnapshot
```

Recommended data:

```text
CaptureSourceId
Expected source class path
Optional Scene Component structural record
Captured properties
```

For a selected `USceneComponent`, the optional structural record should contain at least:

```text
RelativeTransform
Captured parent CaptureSourceId or stable external parent identity when available
Captured socket name when relevant
Parent relationship validation status
```

The relative transform is structural snapshot data, not a generic reflected-property payload.

## 13.3 Participant snapshot

```cpp
FWorldStateParticipantSnapshot
```

Recommended data:

```text
ParticipantId
Existence state
Spawn Descriptor when applicable
Structural state
Capture source snapshots
Restore phase
Optional group metadata
```

## 13.4 World snapshot

```cpp
FWorldStateSnapshot
```

Recommended data:

```text
SnapshotId
FormatVersion
Capture sequence
Captured world identity or context
Participant snapshots
Snapshot tags or label
Creation status
```

The baseline is a `FWorldStateSnapshot` with immutable baseline semantics.

## 13.5 Snapshot ownership

The subsystem must own snapshot data safely for the world lifetime.

Do not retain participant Actors or Components through hard references solely because they appeared in a snapshot.

Soft paths and participant IDs may be stored as data.

---

# 14. Structural state

Structural state is managed separately from selected reflected gameplay properties.

The Participant Component exposes explicit policies such as:

```text
Capture Existence
Capture Actor Transform
Capture Attachment
Scene Component Relative Transform selections
Capture Actor Enabled/Hidden state, only if intentionally added
```

The first milestone should support at least:

- participant existence;
- Actor transform;
- Actor class identity;
- Actor path/name information needed by the spawn strategy;
- optional Actor attachment parent reference and socket information;
- relative transform capture for explicitly selected non-root Scene Components;
- stable identification of every selected Scene Component Capture Source.

Do not force designers to select Actor or Scene Component transforms through the generic property picker.

## 14.1 Actor transform versus Scene Component relative transform

Keep the two authorities distinct:

```text
Actor structural state
└── Actor transform

Selected child Scene Component structural state
└── Relative transform to its attach parent
```

`Capture Actor Transform` owns restoration of the Actor/root transform.

`Capture Relative Transform` on a selected child Scene Component owns restoration of that Component's complete relative `FTransform`, including:

```text
Relative Location
Relative Rotation
Relative Scale
```

This supports common authoring patterns such as a door leaf or pivot Component whose state is represented by relative rotation rather than world-space rotation.

The initial implementation should not capture both Actor transform and root Scene Component relative transform as competing values. The Details Panel must disable or clearly reject that conflicting configuration.

## 14.2 Scene Component capture behavior

For every enabled `FWorldStateSceneComponentCaptureSelection`:

```text
Resolve stable Capture Source
    ↓
Verify source is a USceneComponent
    ↓
Read complete relative transform
    ↓
Capture parent identity and socket context when available
    ↓
Store independent structural record in the Participant Snapshot
```

The relative transform must be copied as value data. The snapshot must not retain a live Component pointer.

A Scene Component may be selected for relative-transform capture even when none of its reflected properties are selected.

## 14.3 Scene Component restore behavior

Restore Scene Component relative transforms during the structural phase, after the owning Actor exists and after any required attachment relationship has been restored, but before selected gameplay properties and derived-state callbacks.

Required order inside one participant:

```text
1. Restore Actor transform
2. Restore Actor and Component attachment relationships when enabled
3. Restore selected Scene Component relative transforms
4. Restore selected reflected property values
5. Rebuild derived state
```

When multiple selected Scene Components form a hierarchy, restore them deterministically from parent to child.

Applying a relative transform must use the verified Unreal Scene Component API for the project's engine version and must not reconstruct the transform by manually assigning cached world-space values.

## 14.4 Parent and attachment semantics

A relative transform is meaningful relative to an attach parent.

If attachment capture is enabled:

- restore the captured parent and socket before the relative transform;
- fail or warn according to policy when the required parent cannot be resolved;
- preserve deterministic parent-before-child ordering.

If attachment capture is disabled:

- apply the captured relative transform against the Component's current parent;
- compare the current parent with captured diagnostic parent identity when available;
- produce a structured warning when they differ because the resulting world-space pose may not match the captured pose.

Do not silently reinterpret a captured relative transform as a world transform.

## 14.5 Derived state is not structural state

Do not automatically capture:

- collision caches;
- navigation overlays;
- active timers;
- animation instances;
- widget state;
- trace results;
- AI planning caches;
- delegate bindings.

Those systems should rebuild derived state after authoritative values and selected Scene Component transforms are restored.

# 15. Baseline capture

Baseline capture must be explicit.

Do not capture the baseline independently from each Actor's `BeginPlay` because initialization order may be incomplete.

Recommended flow:

```text
World loads
    ↓
Actors and Components initialize
    ↓
Participants register
    ↓
External coordinator declares world ready
    ↓
WorldState finalizes registration
    ↓
WorldState captures baseline
    ↓
Gameplay iteration begins
```

Conceptual API:

```text
FinalizeRegistration
CaptureBaseline
```

These may be separate operations or one controlled operation, but their semantics must remain explicit.

## 15.1 Immutable baseline

After successful capture:

- the baseline cannot be overwritten accidentally;
- a second baseline capture requires an explicit destructive policy or a new world/session;
- failed recapture must not destroy the valid existing baseline;
- runtime snapshots do not mutate the baseline.

## 15.2 Blueprint construction and editor changes

A baseline captured in Play is runtime data.

Editing a Blueprint or level after Play starts does not update the existing baseline automatically.

Editor hot reload or Blueprint recompile may invalidate active snapshots; the plugin should fail safely or invalidate them explicitly in editor builds.

---

# 16. Snapshot capture operation

Create a structured capture request conceptually named:

```cpp
FWorldStateCaptureRequest
```

It may include:

```text
Snapshot label or tag
Participant scope
Group query
Whether structural state is included
Failure policy
Whether warnings are accepted
```

## 16.1 Capture transaction

A snapshot should become available only after the requested capture transaction succeeds according to policy.

Recommended flow:

```text
Validate request
    ↓
Resolve scope
    ↓
Pre-capture callbacks
    ↓
Capture structural state
    ↓
Capture selected properties
    ↓
Validate records
    ↓
Publish immutable snapshot
```

Do not publish a supposedly valid snapshot before its required records are complete.

## 16.2 Failure policies

Support explicit policies such as:

```text
FailEntireSnapshot
SkipInvalidParticipant
SkipInvalidProperty
```

Recommended baseline default:

```text
FailEntireSnapshot
```

A baseline with silently omitted required values is unsafe.

For diagnostic runtime snapshots, a more permissive policy may be useful.

---

# 17. Restore Session

Create a transient runtime object or internal state holder conceptually named:

```cpp
UWorldStateRestoreSession
```

or a non-UObject value type if ownership and Blueprint observation do not require a UObject.

The Restore Session is mutable and exists only while one restore is executing.

It must not be confused with the immutable source snapshot.

## 17.1 Restore states

Recommended states:

```text
Created
Preflighting
Preparing
RestoringExistence
RestoringStructure
RestoringValues
ResolvingReferences
RebuildingDerivedState
Validating
Completed
CompletedWithWarnings
Failed
Cancelled
```

Terminal states must be immutable.

## 17.2 Restore request

Create a structured request conceptually named:

```cpp
FWorldStateRestoreRequest
```

It should specify:

```text
Source SnapshotId
Restore scope
Dependency expansion policy
Missing participant policy
Missing property policy
Soft reference resolution policy
Validation policy
Optional reason tag or context
```

---

# 18. Restore pipeline

Use a controlled multi-phase pipeline.

Recommended order:

```text
1. Accept request and create Restore Session
2. Notify global restore starting
3. Preflight
4. Build and expand restore scope
5. Notify global scope resolved
6. Restore participant existence
7. Restore Actor structural state and attachments
8. Restore selected Scene Component relative transforms
9. Restore non-reference and soft-reference property values
10. Resolve or validate soft references
11. Invoke derived-state rebuild callbacks
12. Validate final participant state
13. Freeze the terminal result and notify global completion or failure
```

## 18.1 Preflight

Preflight must verify:

- snapshot exists and is valid;
- subsystem is in a compatible state;
- selected participant IDs are valid;
- property selections can be resolved or handled according to policy;
- required spawn strategies exist;
- dependency graph is acyclic;
- required Capture Sources can be reconstructed;
- reference policies are coherent.

A preflight failure should prevent destructive restore work where possible.

## 18.2 Scope construction

The restore scope may be:

```text
Complete snapshot
Explicit participant set
Group or tag query
Changed/dirty participants
```

The scope is expanded according to dependency policy before mutation begins.

## 18.3 Existence phase

Determine which participants should exist according to the snapshot.

Possible operations:

```text
Existing and required      → keep
Missing and required       → recreate
Existing and absent        → destroy when policy allows
Persistent/external object → leave under external ownership
```

## 18.4 Structural phase

Restore structural data such as:

- Actor transform;
- Actor and Component attachment relationships;
- selected Scene Component relative transforms;
- required Actor path/name relationship;
- other explicitly supported structural settings.

Attachment restoration must precede relative-transform restoration. Selected Scene Components in the same hierarchy must be restored parent-first.

## 18.5 Value phase

Restore selected property payloads.

Each property restore must produce a property-level result.

Do not skip type compatibility checks because the snapshot was captured earlier in the same Play session; editor recompilation and object replacement can still invalidate assumptions.

## 18.6 Reference phase

After required participants exist, evaluate restored soft references according to policy.

The path value may already have been deserialized during the value phase. The reference phase determines whether required references can now resolve.

Do not require every optional soft asset reference to be loaded merely to report restore success.

## 18.7 Derived-state phase

After authoritative properties are restored, participants rebuild dependent runtime state.

Examples:

- apply door mesh pose;
- refresh collision;
- update navigation modifiers;
- recalculate puzzle outputs;
- stop or restart visual effects according to restored values;
- rebuild cached queries.

World State does not know the domain-specific implementation.

## 18.8 Validation phase

Participants and the subsystem may validate:

- selected property values match the snapshot;
- selected Scene Component relative transforms match the snapshot within an explicit tolerance;
- required references resolved;
- recreated Actors have compatible paths;
- restore dependencies completed;
- participant-specific invariants hold.

A property byte comparison alone is not sufficient to prove that derived gameplay state is valid.

## 18.9 Global subsystem restore lifecycle events

A world “reset” is represented by restoring a baseline or another snapshot. The public API and delegate names should consistently use **Restore** rather than introducing a second overlapping Reset operation.

The `UWorldStateSubsystem` must expose global lifecycle delegates for systems that coordinate or observe the whole operation.

Required global events:

```text
OnWorldStateRestoreStarted
OnWorldStateRestoreScopeResolved
OnWorldStateRestoreCompleted
OnWorldStateRestoreFailed
```

Provide Blueprint-assignable dynamic multicast delegates and native C++ delegate access where appropriate. Both surfaces must follow the same ordering and exactly-once guarantees.

### `OnWorldStateRestoreStarted`

Broadcast once after a restore request has been accepted and a Restore Session has been created, but before preflight and before any participant mutation.

Use cases:

- globally block new interactions;
- suspend AI or replay orchestration through an external integration module;
- show reset presentation;
- record diagnostics for the accepted session.

A request rejected before session creation because the subsystem is busy, shutting down or otherwise unavailable does not broadcast this event. The synchronous request result must report that rejection.

### `OnWorldStateRestoreScopeResolved`

Broadcast once after preflight succeeds, dependency expansion is complete and deterministic restore ordering has been finalized, but before the existence phase mutates the world.

The event context must expose the final participant count and scope summary without exposing mutable internal arrays.

Use cases:

- prepare scope-specific presentation;
- inspect which reset groups are involved;
- collect diagnostics;
- allow an external coordinator to know the operation is ready to mutate the world.

Observers must not modify the resolved scope from this event.

### `OnWorldStateRestoreCompleted`

Broadcast exactly once when the Restore Session reaches a successful terminal state, including `SuccessWithWarnings` when that result category is supported.

It must run only after:

- existence, structural, value and reference phases complete;
- selected Scene Component relative transforms are applied;
- participant derived-state callbacks complete;
- final validation finishes;
- the immutable terminal result is finalized.

Use cases:

- resume external simulation;
- begin the next iteration;
- remove reset presentation;
- inspect warnings and timings.

### `OnWorldStateRestoreFailed`

Broadcast exactly once when an accepted Restore Session reaches a failed terminal state.

It may follow failure during:

```text
Preflight
Scope construction
Existence restoration
Structural restoration
Scene Component relative-transform restoration
Property restoration
Reference validation
Derived-state callback processing
Final validation
```

The failure result must identify the stage, affected participant or property when available, whether world mutation had begun, and whether the world may be partially restored.

### Global lifecycle context

Create an immutable Blueprint-visible context conceptually named:

```cpp
FWorldStateRestoreLifecycleContext
```

Include at least:

```text
RestoreSessionId
SnapshotId
Restore request type
Requested scope summary
Resolved participant count when available
Whether the snapshot is the baseline
Whether world mutation has begun
Current restore stage
```

Terminal events receive a finalized structured result conceptually named:

```cpp
FWorldStateRestoreResult
```

Include at least:

```text
RestoreSessionId
Terminal status
Failure stage when applicable
Requested and restored participant counts
Warning and error counts
Whether mutation began
Whether partial restoration occurred
Unresolved required and optional soft-reference summaries
Scene Component structural restore failures
Operation duration or trace correlation data when available
```

Do not expose mutable Restore Session internals through either structure.

### Ordering and exactly-once contract

For every accepted request, the valid sequences are:

```text
Started
    ↓
ScopeResolved
    ↓
Completed
```

or:

```text
Started
    ↓
Failed
```

or:

```text
Started
    ↓
ScopeResolved
    ↓
Failed
```

Rules:

- `Started` is emitted at most once;
- `ScopeResolved` is emitted at most once;
- exactly one terminal event is emitted;
- `Completed` and `Failed` are mutually exclusive;
- terminal events are emitted only after the result is frozen;
- no lifecycle event is emitted after world teardown has invalidated the subsystem;
- participant events remain local and do not replace global lifecycle events;
- global observers must not receive direct mutable access to participants, scope containers or snapshots.

### Reentrancy and observer safety

While broadcasting a global lifecycle event:

- a nested capture or restore request must be rejected or explicitly queued according to one documented policy;
- the active Restore Session must not be completed twice;
- removing or destroying observers must not invalidate subsystem iteration;
- Blueprint listener failure must remain observable without silently converting the restore into success;
- high-level coordinators should prepare critical gameplay systems before issuing restore rather than depending solely on observer order.

Global events are for whole-world orchestration. Actor-specific reconstruction remains in Participant Component callbacks.

---

# 19. Participant callbacks and Blueprint integration

The plugin must allow designers to react to capture and restore without writing C++.

Provide component-owned Blueprint events or delegates conceptually equivalent to:

```text
OnWorldStatePreCapture
OnWorldStateCaptured
OnWorldStatePreRestore
OnWorldStatePropertiesRestored
OnWorldStateRestored
OnWorldStateRestoreFailed
```

Recommended semantics:

## 19.1 `OnWorldStatePreCapture`

Called before values are read.

Use only for legitimate state preparation that must be represented in the snapshot.

Do not require designers to manually copy every variable into a second container.

## 19.2 `OnWorldStatePreRestore`

Called before the participant is mutated.

Useful for:

- stopping local timers;
- detaching transient callbacks;
- suspending local visual transitions.

## 19.3 `OnWorldStatePropertiesRestored`

Called after selected properties are deserialized but before global final validation.

Useful for applying restored authoritative values to derived local systems.

## 19.4 `OnWorldStateRestored`

Called after the participant's restore phases and dependency requirements are complete.

## 19.5 `OnWorldStateRestoreFailed`

Called for the affected participant when its local restore fails or is invalidated by a required dependency failure.

It receives a participant-scoped structured result and does not replace the subsystem-level `OnWorldStateRestoreFailed` terminal event.

## 19.6 Callback safety

Callbacks must not mutate subsystem collections recursively while a restore iteration is active.

If callbacks request another capture or restore, defer or reject the request according to explicit policy.

Late or duplicate completion notifications must not complete a Restore Session twice.

---

# 20. Restore phases and dependencies

Use both broad phases and explicit participant dependencies.

## 20.1 Broad phase

Expose an enum conceptually named:

```cpp
EWorldStateRestorePhase
```

Possible participant-level values:

```text
Early
Default
Late
```

The internal pipeline still owns existence, structure, values, references and validation stages.

The authored phase orders participants within applicable stages.

## 20.2 Explicit dependencies

A participant may declare:

```text
RestoreAfter Participant X
RestoreBefore Participant Y
```

Store dependencies through Participant IDs or another stable reference, not hard pointers.

## 20.3 Topological ordering

Build a deterministic dependency graph and topologically order the participants.

Tie-break participants deterministically, for example through stable participant ID ordering or explicit sequence.

Never depend on:

- pointer order;
- Actor iteration order;
- hash-map iteration order.

## 20.4 Cycles

A dependency cycle is a validation error.

Report the complete cycle where practical.

Do not break a cycle by choosing an arbitrary order silently.

---

# 21. Partial restore

The system must support partial restore without assuming every snapshot operation is global.

Possible selection mechanisms:

```text
Explicit participant IDs
Participant group tags
Restore group name
Dirty participant set
External query policy
```

## 21.1 Dependency expansion policy

Provide an explicit policy such as:

```text
ExactSelection
IncludeRequiredDependencies
IncludeDependenciesAndDependents
RejectIncompleteScope
```

Recommended generic default:

```text
IncludeRequiredDependencies
```

A partial restore must not silently leave a required dependency in an incompatible state.

## 21.2 Property-level partial restore

Although snapshots store one record per selected property, arbitrary property-level restore is not required for the first milestone.

Keep the data model compatible with a future extension, but implement participant-level scopes first.

---

# 22. Actor creation and destruction

Each participant defines an existence policy conceptually equivalent to:

```text
ExistingOnly
RespawnIfMissing
DestroyIfAbsent
Persistent
ExternallyManaged
```

The exact enum may combine these dimensions differently, but the semantics must be explicit.

## 22.1 Spawn Descriptor

Create a value type conceptually named:

```cpp
FWorldStateSpawnDescriptor
```

Recommended data:

```text
ParticipantId
Actor class soft reference
Captured object path
Actor name/path segment
Level or outer context
Transform
Spawn policy identifier
Optional initialization payload
```

Use soft class references where persistent class identity is required.

## 22.2 Spawn Strategy

Not every Actor can be recreated safely through one generic spawn call.

Provide an extension contract conceptually named:

```cpp
IWorldStateSpawnStrategy
```

or a UObject strategy equivalent.

A strategy is responsible for:

- validating whether it can recreate the participant;
- choosing the correct world/level context;
- recreating required components;
- preserving Participant ID;
- preserving a compatible soft object path when required;
- returning a structured result.

## 22.3 Generic default strategy

A conservative generic strategy may support ordinary runtime Actors whose class and construction requirements are compatible with standard spawning.

Do not pretend it can recreate every Actor type.

World-authored level Actors may require different handling from runtime-spawned Actors.

## 22.4 Destruction during restore

Destruction must be deferred or ordered so it does not invalidate active registry iteration.

Update the participant registry through controlled commands.

Do not continue using Actor or Component pointers after destruction.

---

# 23. Capture Source reconstruction

If selected properties belong to Actor Components, those Components must exist before value restore.

The Spawn Strategy or participant reconstruction path must ensure that required authored Components are recreated with compatible stable identities.

If a selected Component cannot be found:

```text
MissingCaptureSource
```

must be reported.

Do not apply a component property payload to another Component merely because it has the same class.

---

# 24. Soft-reference resolution result

Create a structured reference result conceptually named:

```cpp
FWorldStateReferenceResolutionResult
```

Include:

```text
ParticipantId
CaptureSourceId
Property path
Nested value path when relevant
Soft object path
Resolution status
Resolved object class when available
Failure reason
```

The default policy may consider an unresolved optional asset reference valid while considering a missing required world Actor reference a restore failure.

Required versus optional semantics should be configurable at property selection or participant policy level rather than inferred from nullability alone.

---

# 25. Dirty tracking

Dirty tracking is optional for correctness but useful for performance and analysis.

Provide a controlled operation conceptually equivalent to:

```text
MarkParticipantDirty
```

Possible uses:

- optimized partial restore;
- baseline comparison;
- Tactical Analysis inspection;
- debugging unexpected mutations.

A complete restore must not depend exclusively on perfect dirty notifications.

Failure to mark a participant dirty must not make the baseline impossible to restore.

Do not add a per-frame scan of every selected property as the default dirty-tracking mechanism.

---

# 26. Snapshot comparison

Provide read-only comparison support as an optional core feature or planned extension.

Useful comparisons:

```text
Current value versus baseline
Snapshot A versus Snapshot B
Participants added or removed
Properties changed
References unresolved
```

Comparison must use the same property identity and type compatibility rules as restore.

Do not deserialize arbitrary payloads into live objects only to compare them.

The initial implementation may compare captured property payloads when format and type signatures match, while documenting that semantically equivalent custom serialization may require a specialized comparer later.

---

# 27. Error handling and results

Every public capture or restore operation must return a structured result.

Do not communicate failure only through logs.

Recommended broad result states:

```text
Success
SuccessWithWarnings
RejectedInvalidRequest
RejectedBusy
CaptureFailed
PreflightFailed
RestoreFailed
Cancelled
WorldTeardown
```

## 27.1 Result hierarchy

A top-level result should aggregate:

```text
Operation state
Snapshot or Restore Session ID
Participant results
Property results
Reference results
Spawn/destruction results
Dependency diagnostics
Warnings and errors
```

## 27.2 Failure policy consistency

If policy says one invalid required property fails the baseline, the operation must not return success because other participants were captured correctly.

If policy allows skipping an optional property, preserve the warning and exact property path.

## 27.3 No silent defaults

Do not restore a missing or incompatible property by writing a default value silently.

Preserve the existing runtime value or follow an explicit fallback policy and report what happened.

---

# 28. Blueprint API

Expose a small, controlled Blueprint API.

Recommended subsystem operations:

```text
Finalize World State Registration
Capture Baseline
Capture Runtime Snapshot
Restore Baseline
Restore Snapshot
Restore Participants
Get Baseline Status
Get Snapshot Summary
Get Participant State Summary
```

Required subsystem lifecycle delegates:

```text
On World State Restore Started
On World State Restore Scope Resolved
On World State Restore Completed
On World State Restore Failed
```

Recommended component operations:

```text
Get Participant ID
Mark Participant Dirty
Validate Captured Properties
Request Local State Refresh after restore, only if needed
```

Every operation that may fail must return observable success or a structured result.

Do not expose mutable internal snapshot arrays to Blueprint.

---

# 29. Runtime property lookup performance

Property resolution and validation should not be repeated unnecessarily.

Recommended strategy:

```text
Authored selections
    ↓ initialization or first use
Validated resolved descriptor cache
    ↓ capture/restore operations
Direct descriptor reuse while class layout remains valid
```

## 29.1 Cache invalidation

Invalidate and rebuild descriptors when:

- the source class changes;
- a Blueprint is recompiled in editor;
- the source Component is reconstructed;
- a selected property no longer matches its type signature;
- the participant is recreated.

## 29.2 No Tick

The core system must not require Tick for:

- participant registration;
- baseline storage;
- property capture;
- property restore;
- dependency processing;
- reference validation.

Use explicit operations and lifecycle callbacks.

## 29.3 Payload allocations

Avoid unnecessary repeated copies of large property payloads.

Still preserve snapshot immutability and ownership isolation.

Do not trade correctness for sharing mutable buffers.

## 29.4 Unreal Insights

Make meaningful operations measurable where useful:

```text
WorldState_CaptureSnapshot
WorldState_CaptureParticipant
WorldState_SerializeProperty
WorldState_PreflightRestore
WorldState_RestoreExistence
WorldState_RestoreParticipant
WorldState_DeserializeProperty
WorldState_ResolveReferences
WorldState_ValidateRestore
```

Do not instrument trivial getters.

---

# 30. Debugging and editor diagnostics

Provide both local and global debug control according to the project rules.

## 30.1 Runtime inspection

Make it possible to inspect:

```text
Subsystem state
Baseline availability
Registered participants
Participant IDs
Selected properties
Resolved or missing Capture Sources
Snapshot payload sizes
Restore phase and ordering
Dependency edges
Current Restore Session
Per-property restore result
Unresolved soft paths
Spawned and destroyed participants
```

## 30.2 Visual debugging

World State is not primarily spatial, but optional visual labels may identify participants and their restore status.

Examples:

```text
Participant ID
Dirty state
Included in current restore
Restore success/warning/failure
```

Visual debug must be disabled by default and controlled by global and local switches.

## 30.3 Logging

Use one module log category:

```text
LogWorldState
```

Provide scoped logging macros for Info, Warning and Error.

Logs should include relevant context:

```text
SnapshotId
RestoreSessionId
ParticipantId
Actor or Component name
CaptureSourceId
Property path
Soft object path
Restore phase
Failure reason
```

Avoid per-frame log spam.

---

# 31. Serialization format versioning

Store explicit format versions even though snapshots are in memory.

Recommended levels:

```text
World snapshot format version
Captured property record format version
Optional custom version GUID
```

This protects editor workflows, hot reload diagnostics and future persistence extensions.

## 31.1 In-session compatibility

The first milestone may reject snapshots captured against an incompatible class/property layout.

Do not implement broad automatic migration prematurely.

## 31.2 Future disk persistence

A future persistence adapter may:

- export supported snapshots to the existing save system;
- convert in-memory records into another persistent schema;
- persist only selected layers;
- migrate old snapshot versions.

The core runtime must not depend on that adapter.

---

# 32. Integration expectations

## 32.1 Iteration Loop

A future Iteration Loop coordinates:

```text
Stop or settle active gameplay
Finalize required recordings
Request WorldState restore
Wait for structured completion
Recreate or configure persistent agents
Begin the next iteration
```

World State does not know what a rewind or clone is.

## 32.2 Intent Replay

Replay Tracks remain independent runtime data.

World State must not mutate them during environmental reset.

## 32.3 Gameplay Actions

An external coordinator should abort or settle active actions before restore.

World State must not depend on the Gameplay Actions module merely to perform this orchestration.

## 32.4 Grid World

Restore the Actors and Components that author navigation modifiers.

Then allow Grid World to rebuild derived navigation state through participant callbacks or external observation.

Do not snapshot the entire dynamic navigation database by default.

## 32.5 Puzzle systems

Puzzle Actors add a Participant Component and select their authoritative properties.

Example:

```text
Door Actor
Selected:
- DoorState struct
- bIsPowered

Not selected:
- active timeline
- cached interactor
- last trace result
```

The puzzle system may react to `OnWorldStatePropertiesRestored` to rebuild outputs and visuals.

For a hinged or sliding element, the Participant Component may also capture the relative transform of the authored child `USceneComponent` that represents the moving leaf or panel. This avoids converting a locally authored door rotation into world-space state.

---

# 33. Suggested folder structure

Follow the root project folder rules without creating empty folders only to match this diagram.

```text
Plugins/WorldState/
├── WorldState.uplugin
├── CODEX/
│   └── WorldStateCore.md
├── Docs/
└── Source/
    ├── WorldState/
    │   ├── WorldState.Build.cs
    │   ├── Public/
    │   │   ├── Components/
    │   │   │   └── WorldStateParticipantComponent.h
    │   │   ├── Subsystems/
    │   │   │   └── WorldStateSubsystem.h
    │   │   ├── Capture/
    │   │   │   ├── WorldStateCaptureRequest.h
    │   │   │   ├── WorldStatePropertySelection.h
    │   │   │   └── WorldStateCaptureSource.h
    │   │   ├── Snapshots/
    │   │   │   ├── WorldStateSnapshot.h
    │   │   │   ├── WorldStateParticipantSnapshot.h
    │   │   │   └── WorldStateCapturedProperty.h
    │   │   ├── Restore/
    │   │   │   ├── WorldStateRestoreRequest.h
    │   │   │   ├── WorldStateRestoreSession.h
    │   │   │   └── WorldStateRestorePhase.h
    │   │   ├── Spawning/
    │   │   │   ├── WorldStateSpawnDescriptor.h
    │   │   │   └── WorldStateSpawnStrategy.h
    │   │   ├── Serialization/
    │   │   │   ├── WorldStatePropertySerializer.h
    │   │   │   ├── WorldStatePropertyValidator.h
    │   │   │   └── WorldStatePropertyTypeSignature.h
    │   │   ├── References/
    │   │   │   └── WorldStateReferenceResolution.h
    │   │   ├── Types/
    │   │   │   ├── WorldStateParticipantId.h
    │   │   │   ├── WorldStateSnapshotId.h
    │   │   │   ├── WorldStateResults.h
    │   │   │   └── WorldStatePolicies.h
    │   │   └── Blueprint/
    │   │       └── WorldStateBlueprintLibrary.h
    │   ├── Private/
    │   │   ├── Components/
    │   │   ├── Subsystems/
    │   │   ├── Capture/
    │   │   ├── Snapshots/
    │   │   ├── Restore/
    │   │   ├── Spawning/
    │   │   ├── Serialization/
    │   │   ├── References/
    │   │   ├── Blueprint/
    │   │   └── Tests/
    │   ├── CODEX/
    │   └── Docs/
    └── WorldStateEditor/
        ├── WorldStateEditor.Build.cs
        ├── Public/
        ├── Private/
        │   ├── Details/
        │   ├── PropertyPicker/
        │   ├── Validation/
        │   └── Tests/
        ├── CODEX/
        └── Docs/
```

Adjust exact folders to actual implementation responsibilities.

---

# 34. Module dependencies

Keep dependencies minimal and verify exact module names against the project's Unreal Engine version.

The runtime module will likely require modules providing:

- core Unreal types;
- CoreUObject reflection and serialization;
- Engine Actor, Component and World Subsystem support;
- soft object and soft class references;
- Gameplay Tags only if participant grouping uses them.

The editor module will likely require modules providing:

- Property Editor and Details customizations;
- Unreal Editor support;
- Blueprint inspection utilities where actually needed;
- editor validation and transactions.

Do not add runtime dependencies on editor modules.

Do not add `StructUtils` merely because structs are supported unless the concrete implementation actually uses a StructUtils type.

Do not add dependencies on the project's persistent save plugin.

---

# 35. Initial implementation scope

The first core milestone should implement:

1. runtime and editor plugin modules;
2. module log category and logging macros;
3. `UWorldStateSubsystem`;
4. `UWorldStateParticipantComponent`;
5. stable participant IDs and duplicate validation;
6. explicit baseline finalization and capture;
7. property selection stored directly on the component;
8. Details Panel property picker;
9. Owner Actor and authored Actor Component Capture Sources;
10. root-property selection only;
11. generic reflected property validation;
12. scalar, enum, name, string, text and common struct support;
13. complete C++ `USTRUCT` property capture;
14. complete Blueprint User Defined Struct property capture;
15. supported arrays, sets and maps;
16. recursive nested-type validation;
17. `TSoftObjectPtr`, `TSoftClassPtr` and soft-path support;
18. rejection of hard and weak UObject references, including nested references;
19. independent per-property memory payloads;
20. immutable baseline and runtime snapshot types;
21. participant existence and Actor transform capture;
22. explicit relative-transform capture for selected non-root Scene Components;
23. conservative spawn descriptor and default spawn strategy;
24. complete restore pipeline;
25. restore phases and explicit participant dependencies;
26. structured property, participant and operation results;
27. participant Blueprint capture and restore lifecycle events;
28. global subsystem restore lifecycle events with exactly-once terminal semantics;
29. partial participant restore with dependency expansion;
30. runtime debug inspection;
31. Unreal Insights scopes for meaningful operations;
32. automated tests;
33. user-facing documentation in `Docs`.

## 35.1 Minimal reference workflow

Validate the complete architecture using a reference Actor such as a generic test door.

The test Actor should contain:

```text
Owner Actor properties
- bIsOpen                      bool
- DoorState                    C++ USTRUCT
- DesignerState                Blueprint User Defined Struct
- ReferencedTarget             TSoftObjectPtr<AActor>
- HardReferencedTarget         TObjectPtr<AActor>  unsupported

Actor Component properties
- OpeningPercentage            float

Scene Component structural state
- DoorPivotComponent relative transform
```

Workflow:

```text
1. Add WorldStateParticipantComponent.
2. Select bIsOpen.
3. Select DoorState as one complete struct.
4. Select DesignerState as one complete User Defined Struct.
5. Select ReferencedTarget.
6. Select OpeningPercentage from the Component.
7. Enable relative-transform capture for DoorPivotComponent.
8. Verify HardReferencedTarget cannot be selected as valid.
9. Capture baseline.
10. Mutate every selected value and rotate DoorPivotComponent relative to its parent.
11. Destroy and recreate a referenced managed Actor when testing path stability.
12. Restore baseline.
13. Verify every selected value and the DoorPivotComponent relative transform are restored.
14. Verify unselected values remain unchanged.
15. Verify soft paths are preserved and required references resolve according to policy.
16. Verify participant and global lifecycle callbacks each run at their documented stage.
```

---

# 36. Required automated tests

Add focused tests for architecture, serialization and lifecycle.

## 36.1 Registration and identity

1. A participant registers exactly once.
2. Unregistration is symmetrical during EndPlay.
3. Duplicate Participant IDs are rejected or reported according to policy.
4. A recreated participant preserves its Participant ID.
5. World teardown leaves no active Restore Session or stale registry entry.

## 36.2 Property selection

6. Owner Actor root properties can be selected.
7. authored Actor Component root properties can be selected.
8. duplicate property selections are rejected.
9. a removed property remains visible as an invalid selection in editor.
10. a missing Capture Source produces a structured validation error.
11. an incompatible type change is detected before restore.

## 36.3 Primitive and common values

12. bool values capture and restore.
13. integer and floating-point values capture and restore.
14. enum, name, string and text values capture and restore.
15. vector, rotator and transform properties capture and restore when selected as values.

## 36.4 Structs

16. a native C++ `USTRUCT` captures and restores as one complete property.
17. a Blueprint User Defined Struct captures and restores as one complete property.
18. nested native and Blueprint structs capture and restore.
19. a struct containing arrays, sets and maps of supported values restores correctly.
20. changing an unselected member is impossible because struct selection restores the entire struct by design.
21. a selected struct containing a forbidden hard UObject reference is rejected with the nested failure path.

## 36.5 Containers

22. arrays of supported values restore correctly.
23. arrays of supported structs restore correctly.
24. maps and sets of supported values restore correctly.
25. a container containing a nested forbidden reference is rejected.

## 36.6 Soft references

26. a `TSoftObjectPtr` to an asset preserves its soft path.
27. a `TSoftClassPtr` preserves its soft class path.
28. a `TSoftObjectPtr` to a world-authored Actor restores its path.
29. an optional unresolved soft reference produces a warning or allowed status according to policy.
30. a required unresolved soft reference fails restore according to policy.
31. a managed runtime Actor recreated at the same compatible path allows the soft reference to resolve again.
32. recreation at a different path produces an observable unresolved-reference result.
33. raw UObject, Actor, `TObjectPtr` and weak pointer properties are rejected by the default policy.
34. forbidden references nested inside a User Defined Struct are rejected.

## 36.7 Snapshot isolation

35. mutating a live property after capture does not mutate the snapshot.
36. two snapshots do not share mutable payloads.
37. baseline capture remains unchanged after runtime snapshot creation.
38. failed runtime snapshot creation does not corrupt an existing valid snapshot.

## 36.8 Restore pipeline

39. existence restore occurs before value restore.
40. structural restore occurs before participant post-restore callbacks.
41. soft-reference validation occurs after required managed Actors are recreated.
42. dependency order is deterministic.
43. dependency cycles fail preflight.
44. partial restore includes required dependencies when configured.
45. unselected properties remain unchanged.
46. a property restore failure follows the configured operation failure policy.
47. a participant callback requesting nested restore does not corrupt the active session.
48. one Restore Session reaches one terminal state exactly once.

## 36.9 Actor creation and destruction

49. an Actor absent from the snapshot is destroyed when policy requires it.
50. a required missing Actor is recreated when a valid strategy exists.
51. a missing spawn strategy produces preflight failure when required.
52. Component Capture Sources exist before their properties are restored.
53. participant registry mutation during destruction does not invalidate iteration.

## 36.10 Scene Component structural state

54. a selected non-root Scene Component captures and restores its complete relative transform.
55. relative rotation restores correctly for a door-pivot test Component.
56. Actor transform restoration occurs before child Scene Component relative-transform restoration.
57. attachment restoration occurs before relative-transform restoration.
58. selected Scene Components in one hierarchy restore parent-first.
59. a missing selected Scene Component produces a structured validation or restore error.
60. a changed parent with attachment capture disabled produces the configured warning or failure.
61. root Scene Component relative-transform capture conflicts with Actor transform capture and is rejected or disabled.

## 36.11 Participant Blueprint events

62. PreCapture executes before property serialization.
63. PreRestore executes before structural or property mutation for that participant.
64. PropertiesRestored executes after selected values are applied.
65. Restored executes once after dependency completion.
66. participant RestoreFailed exposes the structured participant failure reason.

## 36.12 Global restore lifecycle events

67. an accepted request broadcasts Started exactly once before preflight mutation.
68. ScopeResolved broadcasts exactly once after dependency expansion and before existence mutation.
69. a successful session broadcasts Completed exactly once and never broadcasts Failed.
70. a failed accepted session broadcasts Failed exactly once and never broadcasts Completed.
71. a request rejected before session creation broadcasts no lifecycle event and returns a structured rejection.
72. terminal event context contains the same RestoreSessionId as Started.
73. Completed runs after Scene Component relative transforms, participant callbacks and final validation.
74. Failed identifies the failure stage and whether world mutation began.
75. a nested restore request from a global listener does not corrupt or double-complete the active session.

---

# 37. Explicit non-goals for the first milestone

Do not implement the following as part of the core milestone:

- disk save slots;
- integration with the existing persistent save system;
- use of the Unreal `SaveGame` flag;
- custom Blueprint variable metadata;
- Blueprint compiler extensions;
- engine modifications or custom `EPropertyFlags`;
- selection of individual nested struct members;
- automatic migration for every renamed Blueprint property;
- arbitrary instanced UObject graph cloning;
- support for raw or weak world-object references;
- automatic synchronous loading of all soft references;
- project-specific clone, rewind or Temporal Index rules;
- direct cancellation of Gameplay Actions;
- snapshotting Grid World's complete derived navigation database;
- network replication or rollback prediction;
- multithreaded UObject serialization;
- continuous per-frame property comparison;
- a generic solution for every possible Actor construction pattern;
- disk format backward compatibility.

Leave intentional extension points, but do not add speculative systems beyond the defined core.

---

# 38. Core invariants

The implementation is incorrect if any of these invariants can be violated:

1. Live gameplay state remains owned by the participant Actor or Component.
2. The World State Subsystem is the sole authority for snapshot registration and restore-session transitions.
3. The baseline is immutable after successful capture.
4. Captured properties are selected explicitly through Participant Component data.
5. The World State core does not use the Unreal `SaveGame` flag as its selection mechanism.
6. Runtime behavior does not depend on editor-only metadata.
7. A selected struct is captured and restored as one complete root property.
8. C++ `USTRUCT` and Blueprint User Defined Struct properties use the same generic reflected-property pipeline.
9. Snapshot data owns independent copied payloads.
10. A snapshot never relies on live `FProperty*`, Actor or Component pointers as authoritative identity.
11. Forbidden hard or weak object references are rejected recursively, including when nested inside structs and containers.
12. Supported object and class references preserve soft paths.
13. A restored soft path is not reported as resolved unless resolution actually succeeds.
14. Runtime-spawned path-based references are considered reliable only when recreation preserves a compatible object path.
15. Existence restoration occurs before property restoration.
16. Required Capture Sources exist before their property payloads are applied.
17. Restore ordering is deterministic.
18. Dependency cycles never resolve through arbitrary ordering.
19. Unselected properties are not modified by generic property restore.
20. Selected Scene Component relative transforms are structural state and are restored after attachment but before gameplay properties and derived-state callbacks.
21. Actor transform capture and root Scene Component relative-transform capture never act as competing authorities.
22. Derived state is rebuilt through explicit callbacks or external systems, not guessed by the serializer.
23. Every accepted Restore Session emits at most one Started event and exactly one mutually exclusive terminal global event.
24. A Restore Session reaches one terminal state at most once.
25. Capture or restore failures remain observable through structured results.
26. World teardown leaves no live sessions, cached invalid object references or stale registrations.
27. Runtime code never depends on the editor module.
28. The core contains no project-specific iteration or puzzle rules.

---

# 39. Definition of done for the core milestone

The core milestone is complete only when:

- the plugin compiles for the project's actual Unreal Engine version;
- the runtime and editor module boundary is correct;
- participant registration and stable identity work;
- the Details Panel property picker works for Actors and Components;
- the Details Panel can enable relative-transform capture for eligible Scene Components;
- selections persist in Blueprint defaults and level instances;
- no metadata or `SaveGame` property flag is required;
- scalar and container properties capture and restore correctly;
- native C++ `USTRUCT` values capture and restore as complete values;
- Blueprint User Defined Struct values capture and restore as complete values;
- nested unsupported references are detected before capture;
- only supported soft object/class reference forms are accepted by default;
- soft paths are preserved in snapshot payloads;
- managed Actor recreation supports compatible path restoration where required;
- the baseline is immutable;
- runtime snapshots own isolated data;
- complete and partial restore work deterministically;
- Actor transforms, attachments and selected Scene Component relative transforms restore in the intended structural order;
- existence, structure, values, references, callbacks and validation execute in the intended order;
- subsystem global lifecycle events obey their ordering and exactly-once contract;
- failures return structured results;
- automated tests cover the listed critical cases;
- runtime diagnostics explain missing properties, invalid types, unresolved paths and restore ordering;
- performance-sensitive operations are visible in Unreal Insights where useful;
- user-facing documentation exists and matches the implemented behavior;
- no dependency on the existing persistent save system or project-specific gameplay code has been introduced.
