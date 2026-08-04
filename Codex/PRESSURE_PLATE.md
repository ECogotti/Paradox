# Paradox Pressure Plate — Codex Implementation Specification

This file defines the project-specific native pressure-plate Actor built on top of the existing PuzzleSystem emitter templates.

It supplements, and must be read together with:

- the root `AGENTS.md`;
- `EMITTERS_TEMPLATES.md`;
- the existing `WorldState` documentation and local `CODEX` files;
- the existing `PerceptionKnowledge` documentation and local `CODEX` files;
- the local `CODEX` and `Docs` files of the Paradox runtime gameplay module that owns project-specific integration.

Do not duplicate or redesign rules already established by those documents.

---

# PURPOSE

Implement one reusable native project Actor:

```text
APressurePlate : public APuzzleSwitch
```

`APressurePlate` is the concrete overlap-driven pressure-plate implementation for Paradox.

It must:

- detect one valid overlapping occupant through a box overlap;
- expose one authoritative physical state, `Free` or `Occupied`;
- convert only `Free -> Occupied` and `Occupied -> Free` transitions into the inherited raw `Press()` and `Release()` requests;
- use the complete inherited `APuzzleSwitch` state machine for `Hold`, `Toggle`, `Latch`, and `Pulse` behavior;
- move its physical plate mesh according to the authoritative switch output state;
- play movement audio and Niagara feedback;
- publish movement noise through Paradox's existing perception integration;
- participate correctly in the existing WorldState reset flow;
- remain fully usable as a Blueprint parent and as a directly placed native Actor.

The pressure plate remains an Emitter-side gameplay Actor.

The puzzle flow must remain:

```text
APressurePlate
    -> inherited UPuzzleEmitterComponent
    -> APuzzleController
    -> UPuzzleReceiverComponent
```

`APressurePlate` must never activate Receivers directly and must never evaluate Controller conditions.

---

# MODULE OWNERSHIP

Create this class inside the existing Paradox project runtime gameplay module.

Preferred ownership is the module already responsible for project-specific integrations, named `ParadoxGameplay` in the architecture overview when that module exists in the repository.

If the actual runtime module has a different name:

1. inspect the repository;
2. identify the module that already owns Paradox-specific PuzzleSystem, WorldState, and PerceptionKnowledge integration;
3. place the class there;
4. do not create a new module merely for this Actor.

Do not place `APressurePlate` inside:

```text
PuzzleSystem plugin
WorldState plugin
PerceptionKnowledge plugin
any new PressurePlate plugin
```

The generic plugins must remain free of project-specific dependencies.

Suggested paths, adapted to the existing module structure:

```text
Source/<ParadoxRuntimeModule>/Public/Puzzles/PressurePlate.h
Source/<ParadoxRuntimeModule>/Private/Puzzles/PressurePlate.cpp
Source/<ParadoxRuntimeModule>/Docs/PRESSURE_PLATE.md
```

If the module intentionally keeps project-only Actor headers private, follow that convention instead of moving unrelated files.

---

# REQUIRED INVESTIGATION BEFORE IMPLEMENTATION

Before writing code, Codex must inspect the real implementation and public API of:

```text
APuzzleSwitch
UPuzzleEmitterComponent
WorldState participant component or interface
WorldState reset lifecycle delegates/hooks
PerceptionKnowledge noise publication path
Paradox module logging and debug controls
```

Also inspect:

- existing project Actors that participate in WorldState;
- existing project code that emits PerceptionKnowledge noise;
- existing component creation and Details-panel conventions;
- the exact project module names and `Build.cs` dependencies;
- local `CODEX` folders closest to every file that may be changed;
- the relevant human-facing `Docs` folders.

Do not invent API names for WorldState or PerceptionKnowledge.

The conceptual names in this document describe required behavior. Adapt them to the verified APIs already present in the repository.

Avoid modifying the generic plugins. A minimal generic API extension is allowed only when the required behavior cannot be implemented safely through the existing public or protected API. Any such change must be justified, documented, compiled, and kept generic rather than pressure-plate-specific.

---

# REQUIRED COMPONENT HIERARCHY

The final native component hierarchy must be equivalent to:

```text
APressurePlate : APuzzleSwitch
├── UBillboardComponent                 BillboardRoot                 [Root]
├── UStaticMeshComponent                FloorMesh                     [attached to BillboardRoot]
│   ├── UStaticMeshComponent            PlateMesh                     [attached to FloorMesh]
│   │   ├── UAudioComponent             MovementAudio                 [attached to PlateMesh]
│   │   └── UNiagaraComponent           MovementVFX                   [attached to PlateMesh]
│   └── UBoxComponent                   OccupancyVolume               [attached to FloorMesh]
├── inherited UPuzzleEmitterComponent
└── existing WorldState participant capability, when required by the real API
```

The WorldState capability may be an Actor Component, interface implementation, or another existing project pattern. Use the actual established architecture.

Do not create a second puzzle emitter. Use the `UPuzzleEmitterComponent` already owned by `APuzzleSwitch`.

---

# ROOT COMPONENT RULE

`BillboardRoot` must be the final Actor root component.

Required behavior:

```text
GetRootComponent() == BillboardRoot
```

Configure the billboard for editor selection and orientation assistance:

- hidden in game;
- no collision;
- no navigation relevance;
- optional project icon only if an existing appropriate asset is already available;
- safe fallback to Unreal's normal billboard behavior when no icon exists.

`APuzzleSwitch` may already create a generic scene root. Inspect its actual construction before replacing or reparenting anything.

Use the smallest safe component arrangement that produces one valid hierarchy. Do not leave two competing root hierarchies or an orphaned inherited scene component.

Do not redesign `APuzzleSwitch` only to satisfy an aesthetic component arrangement.

---

# STATIC MESH COMPONENT RULES

## `FloorMesh`

`FloorMesh` represents the static frame or surrounding floor section.

Required defaults:

- attached to `BillboardRoot`;
- static mobility unless the existing Actor construction requires otherwise;
- designer-assignable mesh and materials;
- collision profile configurable through the normal component Details panel;
- navigation relevance left configurable because the floor may legitimately contribute to navigation;
- no runtime movement performed by the pressure-plate logic.

The Actor must not assume a specific mesh asset.

## `PlateMesh`

`PlateMesh` is the moving visual plate.

Required defaults:

- attached to `FloorMesh`;
- movable mobility;
- designer-assignable mesh and materials;
- must never affect navigation;
- must not trigger runtime navigation rebuilds while moving;
- no accumulated transform offsets;
- no hard-coded content asset.

At construction, explicitly enforce the verified Unreal equivalent of:

```text
PlateMesh can never affect navigation = false
```

Also ensure that any dynamic-obstacle/navigation modifier behavior is disabled for `PlateMesh`.

Do not call navigation rebuild or dirty-area APIs from plate movement.

Default collision should avoid pushing or trapping occupants during the downward animation. Prefer the static `FloorMesh` as the walkable support and `OccupancyVolume` as the detector. Preserve designer configurability when the project has a clear collision convention.

---

# OCCUPANCY VOLUME

`OccupancyVolume` is the authoritative overlap detector.

Required defaults:

- attached to `FloorMesh`, not to the moving `PlateMesh`;
- query-only collision;
- overlap generation enabled;
- no physics simulation;
- no navigation relevance;
- designer-editable extent and relative transform;
- collision responses configurable so designers can choose which object channels may activate the plate.

Attaching the detector to `FloorMesh` prevents the overlap volume from moving downward with the animated plate and causing unstable enter/exit events.

Do not use the visual mesh overlap as the core detector.

---

# OCCUPANT FILTERING

The native implementation must provide a safe default filter and one deliberate Blueprint/C++ extension point equivalent to:

```text
CanOccupantActivatePlate(
    AActor* OccupantActor,
    UPrimitiveComponent* OccupantComponent
) -> bool
```

Use a `BlueprintNativeEvent` or the closest established project pattern so that:

- C++ provides complete default behavior;
- Blueprint children can specialize which Actors are accepted;
- designers do not have to replace the overlap state machine;
- the native tag filter remains available to every child.

Default validation must reject at least:

```text
null Actor
this pressure plate Actor
invalid or pending-kill objects
```

The `OccupancyVolume` collision responses are the primary coarse filter.

Do not hard-code that only `ACharacter`, `APawn`, the player, or a specific clone class may activate the plate. Paradox may need players, clones, movable objects, corpses, or future gameplay Actors to use the same plate.

## Required tag filter

Expose one designer-facing semantic tag container equivalent to:

```text
FGameplayTagContainer RequiredOccupantTags
```

Required semantics:

```text
RequiredOccupantTags is empty
    -> every otherwise valid Actor is accepted

RequiredOccupantTags is not empty
    -> the Actor must own every required tag
    -> missing even one required tag rejects the Actor
```

Example:

```text
RequiredOccupantTags = { Object.Trait.Heavy }
```

Only Actors semantically classified as heavy may occupy the plate.

Use the existing Paradox Gameplay Tag ownership/query convention. Inspect current project code before implementation. Prefer the project's established tag provider or `IGameplayTagAssetInterface` when that is the real convention. Do not invent a parallel tag component merely for this Actor.

When the filter is non-empty and the Actor exposes no compatible semantic tag source, reject it predictably.

Do not silently reinterpret `RequiredOccupantTags` as Unreal's raw `AActor::Tags` array unless the existing project explicitly uses Actor Tags for this classification. Do not request Gameplay Tags from strings during overlap callbacks; use registered tags and cached/configured containers.

The native filter order should be equivalent to:

```text
validate Actor and component
    -> apply collision/channel filtering already performed by OccupancyVolume
    -> evaluate RequiredOccupantTags
    -> run any additional native/project checks
    -> allow BlueprintNativeEvent specialization
```

Keep the exact extension order deterministic and documented. A Blueprint override must be able to call the parent implementation and retain the native tag behavior.

Do not add a generic weight value system or a new signal payload in this task. The `Heavy` example is a categorical tag requirement, not numeric mass simulation.

---

# OCCUPANT TRACKING

Rename this responsibility conceptually to **single-occupant state tracking**.

Define one explicit reflected enum equivalent to:

```text
enum class EPressurePlateOccupancyState : uint8
{
    Free,
    Occupied
};
```

This enum is the single authoritative physical occupancy state.

Required private runtime state is equivalent to:

```text
EPressurePlateOccupancyState OccupancyState = Free
TWeakObjectPtr<AActor> CurrentOccupant
TSet<TWeakObjectPtr<UPrimitiveComponent>> CurrentOccupantComponents
```

The exact component-set representation may adapt to existing project conventions, but these semantics are required:

- the plate owns at most one logical occupant;
- the first valid Actor acquired while `Free` becomes `CurrentOccupant`;
- multiple overlapping components from that same Actor still count as one occupant;
- duplicate begin-overlap notifications for an already tracked component do nothing;
- end overlap removes only the matching component;
- the Actor remains the occupant while at least one accepted component from it is still overlapping;
- a different Actor beginning overlap while the plate is `Occupied` is ignored and must not replace, stack with, or increment the current occupant;
- no permanent Tick and no periodic world polling are allowed.

Required state transitions:

```text
Free + first valid Actor begins overlap
    -> acquire Actor and component
    -> OccupancyState = Occupied
    -> Press() exactly once

Occupied + another component of CurrentOccupant begins overlap
    -> add component only
    -> no Press()

Occupied + different Actor begins overlap
    -> ignore
    -> no state change

Occupied + one CurrentOccupant component ends overlap
    -> remove component
    -> remain Occupied while another tracked component remains

Occupied + final CurrentOccupant component ends overlap
    -> try local replacement reconciliation inside OccupancyVolume
    -> if another valid Actor is already overlapping, transfer occupancy without a Free/Occupied edge
    -> otherwise clear CurrentOccupant, set Free, and call Release() exactly once
```

The local replacement reconciliation is a robustness path for edge cases where two Actors overlap the box momentarily. It must inspect only current overlaps from `OccupancyVolume`, apply the normal filter, and choose one valid replacement. It must not perform a global world search.

Because the gameplay contract says only one Actor can occupy the plate, replacement ordering is not intended as a puzzle mechanic. Use a stable existing project convention when practical; otherwise selecting the first valid current overlap is acceptable and must be documented.

Do not call `Press()` or `Release()` for every overlap callback.

Do not publish the PuzzleSystem signal directly from overlap handlers.

Do not expose mutable access to `CurrentOccupant`, `CurrentOccupantComponents`, or `OccupancyState`.

---

# DESTROYED OCCUPANT HANDLING

The current occupant may be destroyed without producing a reliable matching end-overlap event.

Use an event-driven cleanup pattern already accepted by the project, such as binding to the current Actor's destruction/end-play notification.

Required result:

- only the current occupant owns a destruction/end-play binding;
- destroying a different ignored overlap Actor has no effect;
- a destroyed current occupant is cleared safely;
- the component set is cleared;
- a local replacement reconciliation may acquire another valid Actor already inside `OccupancyVolume`;
- if no replacement exists, `OccupancyState` becomes `Free` and `Release()` occurs exactly once;
- bindings are removed whenever the current occupant changes;
- no stale delegate remains after reset or `EndPlay`;
- no world scan and no permanent Tick.

Do not store persistent raw UObject pointers without Unreal lifetime tracking.

---

# RELATIONSHIP WITH `APuzzleSwitch`

`APuzzleSwitch` remains authoritative for:

```text
EPuzzleSwitchMode
EPuzzleSwitchInputState
PressDelay
ReleaseDelay
PulseDuration
PulseRetriggerMode
bStartActive
bIsActive
OutputSignalTag
signal publication
Press / Release / ResetSwitch semantics
```

`APressurePlate` must not copy or fork that state machine.

The only normal physical-state calls into the inherited switch are:

```text
Free -> Occupied -> Press()
Occupied -> Free -> Release()
```

The inherited modes retain their existing meanings:

```text
Hold
    Occupied -> confirmed Press -> output Active
    Free -> confirmed Release -> output Inactive

Toggle
    each fully rearmed confirmed Press toggles output
    Release only rearms according to inherited delay semantics

Latch
    first confirmed Press activates permanently until explicit reset

Pulse
    confirmed Press starts or retriggers the inherited pulse policy
```

`PressDelay` and `ReleaseDelay` must work without pressure-plate-specific timer code.

Example required behavior:

```text
SwitchMode = Hold
PressDelay = 0
ReleaseDelay = 5

first valid occupant enters
    -> OccupancyState becomes Occupied
    -> Press()
    -> Active immediately

current occupant fully leaves and no replacement exists
    -> OccupancyState becomes Free
    -> ReleasePending
    -> remains Active

valid occupant is acquired before five seconds
    -> OccupancyState becomes Occupied
    -> Press()
    -> inherited pending release is cancelled
    -> remains logically Pressed and Active
    -> no duplicate confirmed Press
```

A same-Actor extra component or an ignored second Actor must never call `Press()` again.

---

# DEFAULT PUZZLE CONFIGURATION

Use inherited properties rather than shadowing them.

Recommended native defaults for `APressurePlate`:

```text
SwitchMode = Hold
OccupancyState = Free
RequiredOccupantTags = empty
bStartActive = false
PressDelay = 0
ReleaseDelay = 0
OutputSignalTag = Puzzle.Signal.Pressed
```

Set `OutputSignalTag` to the existing registered project/plugin gameplay-tag constant for `Puzzle.Signal.Pressed` when that tag already exists.

If the tag does not exist:

- add it through the project's established native-tag or config-tag mechanism;
- keep the change inside the appropriate generic/project ownership boundary;
- do not use an invalid fallback tag;
- do not silently publish another tag.

Do not create a pressure-plate-specific signal payload for the initial implementation.

---

# PLATE MOVEMENT MODEL

The plate's visual position represents the authoritative switch output state, not raw overlap state.

Required mapping:

```text
IsSwitchActive() == false -> plate target is Raised
IsSwitchActive() == true  -> plate target is Pressed
```

This ensures all inherited modes remain visually coherent:

```text
Hold   -> plate follows active/inactive output
Toggle -> plate stays in the toggled state
Latch  -> plate stays down until reset
Pulse  -> plate remains down for pulse duration
```

Do not animate from raw `OnPressed` / `OnReleased` input edges when that would diverge from `bIsActive`.

---

# MOVEMENT CONFIGURATION

Expose the smallest useful designer-facing configuration equivalent to:

```text
PressDepth
PressDuration
ReleaseDuration
MovementCurve
```

Required semantics:

## `PressDepth`

- non-negative distance;
- measured along local negative Z relative to `FloorMesh`;
- zero means no visual travel;
- must not be applied cumulatively.

## `PressDuration`

Duration from raised to fully pressed.

## `ReleaseDuration`

Duration from pressed to fully raised.

## `MovementCurve`

Optional normalized float curve used to shape interpolation from 0 to 1.

When no curve is assigned, use a stable built-in interpolation such as linear or smooth-step and document the chosen behavior.

Validate all durations as non-negative.

Do not add a generalized movement strategy framework to this project Actor.

---

# AUTHORITATIVE MOVEMENT STATE

Use a normalized internal plate alpha:

```text
0.0 = fully raised
1.0 = fully pressed
```

Cache the authored raised relative transform exactly once during controlled runtime initialization, after Blueprint component defaults have been applied and before the first gameplay animation.

Compute the pressed target from that stable baseline:

```text
PressedRelativeLocation
    = RaisedRelativeLocation
    + FVector(0, 0, -PressDepth)
```

Preserve authored relative rotation and scale.

Do not use repeated `AddLocalOffset` calls.

Do not infer a new baseline from an already animated transform after every reset or construction rerun.

---

# MOVEMENT EXECUTION

The implementation must support:

- activation while idle;
- deactivation while idle;
- reversal while moving;
- repeated identical target requests without restarting movement;
- zero-duration snapping;
- completion without overshoot;
- disabling update cost while idle.

A short per-frame update is acceptable only while the plate is moving.

Preferred semantics:

```text
Actor Tick disabled by default
state target changes and alpha differs -> enable Tick
interpolate from current alpha to target alpha
movement completes -> snap exact target -> disable Tick
```

If an existing project movement helper or timeline pattern already provides the same lifecycle safely, reuse it instead.

Do not leave Tick permanently enabled.

Do not perform world searches, allocations, gameplay-tag requests, or verbose logging inside the movement update.

When reversing mid-motion:

- start from the current alpha;
- do not snap to the previous endpoint;
- scale remaining duration by the remaining alpha distance when practical so speed remains consistent;
- treat the reversal as a new movement start for presentation and noise, unless reset suppression is active.

---

# MOVEMENT START AND COMPLETION BOUNDARIES

Provide internal operations equivalent to:

```text
StartPlateMovement(bool bMoveToPressed)
UpdatePlateMovement(float DeltaSeconds)
CompletePlateMovement()
ApplyPlateAlpha(float Alpha)
SnapPlateToAuthoritativeState()
```

All visual transform writes must pass through `ApplyPlateAlpha` or an equivalent single boundary.

A movement begins only when:

```text
requested target differs from current target or current final state
and
there is a meaningful remaining alpha delta
```

Repeated activation/deactivation callbacks for the same state must not replay movement feedback.

---

# SWITCH HOOK INTEGRATION

Use the inherited native/Blueprint extension points without duplicating signal publication.

Required result:

```text
inherited switch output changes Active
    -> APressurePlate starts movement toward Pressed

inherited switch output changes Inactive
    -> APressurePlate starts movement toward Raised

inherited switch Reset
    -> APressurePlate reconciles to restored authoritative state
```

Prefer protected C++ overrides from `APuzzleSwitch` when they exist.

If the base currently exposes only Blueprint events and no safe C++ override path, add the smallest generic protected virtual hook to `APuzzleSwitch` only when necessary. The hook must be useful to any native switch subclass and must not mention pressure plates.

Never manually republish `OutputSignalTag` from `APressurePlate`.

---

# AUDIO COMPONENT

`MovementAudio` is one reusable `UAudioComponent`.

Required defaults:

- attached to `PlateMesh`;
- auto activation disabled;
- not playing during construction or BeginPlay initialization;
- designer-configurable attenuation, concurrency, spatialization, and other normal component settings.

Expose optional per-direction assets only when compatible with the established project style:

```text
PressSound
ReleaseSound
```

A single preconfigured component sound may be used as fallback.

Required behavior:

- activate/play once when a meaningful press movement begins;
- activate/play once when a meaningful release movement begins;
- a mid-motion reversal may start the opposite movement sound;
- repeated identical target requests do not replay audio;
- reset/init synchronization does not play audio;
- WorldState reset stops currently playing pressure-plate movement audio;
- no audio logic is required for the switch state machine to work.

Use verified `UAudioComponent` APIs for the engine version.

---

# NIAGARA COMPONENT

`MovementVFX` is one reusable `UNiagaraComponent`.

Required defaults:

- attached to `PlateMesh`;
- auto activation disabled;
- designer-configurable system asset and component parameters;
- no effect during construction or initial state synchronization.

Expose optional per-direction systems only when compatible with project conventions:

```text
PressNiagaraSystem
ReleaseNiagaraSystem
```

A single preconfigured system may be used as fallback.

Required behavior mirrors audio:

- activate/reinitialize once when meaningful movement begins;
- allow Blueprint/designers to tune or replace the effect;
- no duplicate activation for repeated identical state requests;
- stop/deactivate during WorldState reset and EndPlay;
- no Niagara logic is required for gameplay correctness.

Use the real Niagara component APIs verified in the installed Unreal version.

---

# PERCEPTIONKNOWLEDGE NOISE INTEGRATION

The pressure plate must emit movement noise through the existing Paradox perception integration.

Do not bypass the established architecture with a new local perception system.

Do not publish directly to Receivers or IntentReplay.

Do not call Unreal's generic `MakeNoise` as a parallel path unless the existing PerceptionKnowledge integration intentionally uses it internally.

Before implementation, inspect the actual project pattern and reuse its real:

- noise event type;
- source identity representation;
- position/origin rules;
- loudness/radius/profile data;
- gameplay tags or semantic classification;
- subsystem/component/interface used for publication;
- optional bridge into IntentReplay observation recording.

The pressure plate should expose project-appropriate configuration equivalent to:

```text
bEmitNoiseOnPressMovement = true
bEmitNoiseOnReleaseMovement = true
PressNoiseSettings/Profile
ReleaseNoiseSettings/Profile
```

Use the existing noise-settings type instead of inventing another duplicate struct.

Required noise semantics:

```text
meaningful movement toward Pressed begins
    -> optionally publish one press-movement noise event

meaningful movement toward Raised begins
    -> optionally publish one release-movement noise event
```

Noise origin should be the plate/mechanism location using the existing project convention, normally `PlateMesh` world location unless a current helper requires a different source component.

Do not emit noise:

- every movement Tick;
- on movement completion unless the existing design explicitly requires an impact event;
- for repeated identical state requests;
- during construction;
- during initial state synchronization;
- while applying WorldState reset;
- merely because an overlap begins if no switch output movement starts.

The noise source must identify this Actor through the existing PerceptionKnowledge entity/source model so AI observation and any downstream Paradox recording remain consistent.

---

# WORLDSTATE INTEGRATION

`APressurePlate` must participate in the existing WorldState architecture through the actual project component/interface pattern.

Do not create a custom pressure-plate save system.

Do not add pressure-plate knowledge to the generic WorldState plugin.

The integration must restore the pressure plate deterministically during full or partial Paradox world reset.

## State ownership rule

The semantic source of truth is the inherited switch state.

The plate mesh transform is derived presentation state.

Prefer restoring semantic state and rebuilding the visual transform instead of treating an in-flight interpolated transform as an independent authoritative value.

## State that belongs to reset/baseline restoration

At minimum, reset must restore or deterministically reconstruct:

```text
inherited APuzzleSwitch authoritative output state
inherited input state according to APuzzleSwitch reset contract
configured initial/start state
plate visual alpha/relative transform matching the restored switch state
current emitted PuzzleSystem signal matching the restored switch state
```

Use the existing WorldState participant Details-panel/property selection model when that is how the project captures semantic state.

If the established WorldState implementation supports selected relative transforms for Scene Components, use it only where it helps validation or baseline restoration. Do not create two conflicting sources of truth between serialized `PlateMesh` transform and switch active state.

## Transient state that must not be persisted as authoritative snapshot data

Do not serialize or restore as long-lived authoritative gameplay state:

```text
overlap delegate handles
CurrentOccupant
CurrentOccupantComponents
OccupancyState
current-occupant destruction binding
movement Tick enabled state
movement elapsed time
timer handles
currently playing audio state
currently active Niagara state
bIsApplyingWorldState guard
stale deferred callbacks
```

These values must be cleared and rebuilt safely.

---

# WORLDSTATE RESET LIFECYCLE

Use the real reset lifecycle events and ordering exposed by WorldState.

The required behavior is equivalent to the following phases.

## Before reset/application

```text
set internal bIsApplyingWorldState guard
stop plate movement and disable movement Tick
invalidate pressure-plate-owned deferred callbacks
stop MovementAudio
stop/deactivate MovementVFX
unbind and clear the current occupant safely; set OccupancyState to Free without gameplay feedback
suppress movement feedback and perception noise
allow inherited APuzzleSwitch/WorldState logic to cancel its timers safely
```

Do not let stale overlap, movement, pulse, press-delay, or release-delay callbacks apply after reset.

## During semantic restoration

Use the existing WorldState API and inherited `APuzzleSwitch` reset/restore contract.

For the normal Paradox baseline reset, the final state should normally be equivalent to:

```text
InputState = Released
bIsActive = bStartActive
emitted signal = bStartActive
```

If the existing WorldState system supports restoring a different captured semantic snapshot, use that established mechanism rather than forcibly replacing it with `bStartActive`.

Do not synthesize gameplay overlap input while properties are being restored.

## After semantic restoration

```text
snap PlateMesh to the alpha implied by restored IsSwitchActive()
verify inherited emitter state matches restored switch output
leave audio and Niagara stopped
clear stale transient movement state
clear reset suppression only at the correct WorldState completion phase
```

After all relevant Actors have been restored, perform one lifecycle-safe deferred overlap reconciliation if the established reset order requires it.

This reconciliation may rebuild one valid occupant already standing inside the volume and then use the normal single-occupant state-transition path.

It must not:

- world-scan for Actors;
- run every frame;
- emit reset-caused noise or movement feedback while reset suppression is active;
- directly publish the switch signal.

---

# RESET VERSUS GAMEPLAY FEEDBACK

WorldState restoration is not a physical gameplay interaction.

Therefore:

```text
reset changes state/transform
    -> no pressure-plate audio
    -> no pressure-plate Niagara activation
    -> no PerceptionKnowledge noise
```

A later genuine `Free <-> Occupied` transition after reset uses normal gameplay feedback.

If post-reset overlap reconciliation immediately acquires a valid Actor already on the plate, apply the project-established policy consistently. Prefer one suppressed reconciliation during reset completion, followed by normal feedback only for later gameplay transitions.

---

# PUBLIC BLUEPRINT API

Expose only controlled operations and queries that are useful to designers or project gameplay code.

Required pressure-plate-specific conceptual API:

```text
GetOccupancyState() -> EPressurePlateOccupancyState
IsOccupied() -> bool
GetCurrentOccupant() -> AActor*
RefreshOccupantFromVolume() -> bool
GetPlateMovementAlpha() -> float
IsPlateMoving() -> bool
```

`GetCurrentOccupant()` is a read-only observation query and may return null.

`RefreshOccupantFromVolume()` must:

- inspect only `OccupancyVolume`'s current overlaps;
- apply the normal validity and `RequiredOccupantTags` filter;
- preserve the current valid occupant when it is still overlapping;
- otherwise select at most one valid Actor;
- rebuild the current occupant component set;
- apply at most one `Free -> Occupied` or `Occupied -> Free` edge;
- avoid a synthetic Release/Press pair when replacing one overlapping occupant with another;
- fail predictably when the volume is unavailable;
- never perform a global world search.

Keep mutation of occupant references, component sets, occupancy state, movement alpha, and reset guards private/protected.

Do not expose a public setter for `OccupancyState`, `CurrentOccupant`, inherited `bIsActive`, or the emitted puzzle signal.

The inherited `Press`, `Release`, `ResetSwitch`, mode, delay, and signal APIs remain available according to `APuzzleSwitch`, but ordinary occupancy must drive them through the native transition logic rather than Blueprint manually calling both paths.

---

# BLUEPRINT EXTENSION HOOKS

Provide presentation/observation hooks only where they add value beyond the inherited switch hooks.

Useful pressure-plate-specific hooks may be equivalent to:

```text
OnOccupantAccepted(AActor* Occupant)
OnOccupantReleased(AActor* Occupant)
OnOccupantReplaced(AActor* PreviousOccupant, AActor* NewOccupant)
OnOccupancyStateChanged(EPressurePlateOccupancyState PreviousState,
                        EPressurePlateOccupancyState NewState)
OnPlateMovementStarted(bool bMovingDown)
OnPlateMovementCompleted(bool bIsPressed)
```

Use the smallest set consistent with existing module style. `OnOccupantReplaced` is optional when the project prefers representing replacement as release/accept notifications, but replacement must not produce `Free` or duplicate switch input edges.

These events must not be required to maintain native state.

Blueprint children may use them for presentation or project scripting, but must not be required to republish puzzle state, maintain occupant/component tracking, evaluate tags, or drive the authoritative plate transform.

Do not duplicate inherited `OnSwitchActivated`, `OnSwitchDeactivated`, or delay events under new names without a clear pressure-plate-specific meaning.

---

# DETAILS PANEL ORGANIZATION

Follow existing project naming and categories.

Suggested categories:

```text
Pressure Plate | Components
Pressure Plate | Occupancy
Pressure Plate | Movement
Pressure Plate | Feedback
Pressure Plate | Noise
Pressure Plate | Debug
```

Inherited switch properties should remain under their established Puzzle/Switch categories.

Use:

- clear tooltips;
- `ClampMin` for non-negative numeric values;
- edit conditions for direction-specific audio/VFX/noise settings;
- advanced display for rarely changed technical options;
- narrow property access such as `EditDefaultsOnly`, `EditAnywhere`, or `VisibleAnywhere` based on actual designer needs;
- `BlueprintReadOnly` for observable state unless mutation is intentionally supported.

Do not expose transient runtime maps, timers, or weak references in the Details panel.

---

# INITIALIZATION

The initialization order must not depend on Blueprint child events.

Required runtime result:

```text
components are valid and registered
OccupancyVolume delegates are bound
raised PlateMesh transform is cached once
OccupancyState is `Free`, CurrentOccupant is null, and the component set is empty
inherited APuzzleSwitch initializes authoritative state and signal
PlateMesh snaps to IsSwitchActive() without feedback
MovementAudio remains inactive
MovementVFX remains inactive
PerceptionKnowledge noise is not emitted
movement Tick remains disabled
WorldState participation is registered through the existing lifecycle
```

If Controllers initialize later, they must query the correct current signal from the inherited `UPuzzleEmitterComponent`.

Do not use the constructor for world-dependent initialization.

---

# ENDPLAY AND CLEANUP

During `EndPlay`:

- unbind `OccupancyVolume` overlap delegates when required by the binding type;
- unbind the current occupant destruction/end-play delegate;
- clear the current occupant, component set, and occupancy state;
- stop movement and disable Tick;
- stop audio;
- deactivate Niagara;
- invalidate pressure-plate-owned callbacks;
- unregister from project services only when the real integration requires explicit unregistration;
- call the inherited cleanup in the correct order.

Do not publish new gameplay noise or manufacture a final `Release()` solely because the world is tearing down.

---

# DEBUGGING

The class belongs to the Paradox project module, so its own logs must use that module's established log category and macros.

Do not use `LogTemp`.

Do not create a second PuzzleSystem log category.

Provide local debug control equivalent to:

```text
bEnableDebug = false
```

Visual debug must also respect the Paradox module's existing global visual-debug kill switch.

Effective visual debug follows:

```text
Global Paradox debug enabled AND bEnableDebug
```

When enabled, make it possible to inspect:

```text
OccupancyVolume bounds
OccupancyState: Free or Occupied
CurrentOccupant name or None
tracked component count for CurrentOccupant
RequiredOccupantTags
whether CurrentOccupant satisfies the tag filter
inherited SwitchMode
inherited InputState
inherited IsSwitchActive()
current plate alpha
target plate alpha
movement direction and remaining duration
reset suppression state
configured noise behavior
```

Suggested visualization:

- box for `OccupancyVolume`;
- compact world-space text near the billboard/plate;
- one line to `CurrentOccupant` only when useful.

Debug drawing must be disabled by default and have negligible cost when disabled.

Do not enumerate every ignored overlapping Actor every frame.

Do not log every Tick.

---

# VALIDATION

Provide editor/runtime validation using the established project pattern.

Validate at least:

```text
BillboardRoot exists and is root
FloorMesh exists
PlateMesh exists
OccupancyVolume exists
MovementAudio exists
MovementVFX exists
inherited UPuzzleEmitterComponent exists
OutputSignalTag is valid
RequiredOccupantTags contains only valid registered Gameplay Tags
PressDepth >= 0
PressDuration >= 0
ReleaseDuration >= 0
PlateMesh cannot affect navigation
OccupancyVolume generates overlaps
OccupancyVolume is query-capable
WorldState participant/integration is valid
PerceptionKnowledge integration settings are valid when noise emission is enabled
```

An empty `RequiredOccupantTags` container is valid and means accept all otherwise valid Actors.

Invalid critical configuration must fail predictably.

A missing optional sound, Niagara system, or noise profile may disable only that feedback path; it must not break occupancy, switch activation, or signal publication.

Do not silently substitute unrelated assets, tags, or profiles.

---

# BUILD DEPENDENCIES

Inspect exact module names before editing `Build.cs`.

Expected dependencies may include:

```text
PuzzleSystem module that owns APuzzleSwitch
Niagara
WorldState
PerceptionKnowledge
GameplayTags
Engine
```

Add only dependencies actually required by the implementation.

Choose Public versus Private dependency according to the real header boundary:

- a dependency used by public headers must be public;
- implementation-only dependencies should remain private;
- use forward declarations where valid;
- avoid leaking WorldState or PerceptionKnowledge implementation details into public headers when private integration is sufficient;
- do not create circular dependencies.

---

# DOCUMENTATION DELIVERABLE

Create or update human-facing documentation inside the owning Paradox runtime module's `Docs` folder.

At minimum, document:

```text
purpose and class location
component hierarchy
how to assign floor and plate meshes
how to size/configure OccupancyVolume
Free/Occupied single-occupant semantics
first-valid-Actor acquisition and same-Actor multi-component handling
RequiredOccupantTags semantics: empty accepts all, otherwise all tags are required
which project tag-provider convention is used
how Hold, Toggle, Latch, and Pulse behave on a pressure plate
how PressDelay and ReleaseDelay affect occupancy edges
movement properties
PlateMesh navigation rule
audio and Niagara setup
PerceptionKnowledge noise setup
WorldState reset and post-reset overlap reconciliation
Blueprint extension hooks
debug controls
common failure cases
```

Update the module README/index when one exists.

Do not put Codex instructions inside the human `Docs` folder.

If any generic `APuzzleSwitch` API changes are required, update its own plugin documentation in the same task.

---

# REQUIRED BEHAVIOR SCENARIOS

Validate all scenarios below before considering the implementation complete.

## 1. Basic Hold plate

```text
Free
first valid occupant enters
-> Occupied
-> Press()
-> inherited Confirmed Press
-> switch Active
-> plate moves down
-> audio/VFX start once
-> one PerceptionKnowledge movement noise event

final component of current occupant leaves
-> no valid replacement overlap
-> Free
-> Release()
-> inherited Confirmed Release
-> switch Inactive
-> plate moves up
-> audio/VFX start once
-> one release movement noise event when enabled
```

## 2. Multiple components on the current Actor

One Actor overlaps with two components.

It remains one logical occupant. Leaving with only one component does not free or release the plate. Leaving with the final tracked component does.

## 3. Second Actor while occupied

Actor A occupies the plate. Actor B begins overlap.

```text
CurrentOccupant remains Actor A
OccupancyState remains Occupied
Actor B is not stacked or counted
no duplicate Press()
```

If Actor A leaves while Actor B is still overlapping, the local replacement reconciliation may acquire Actor B without producing a `Free` edge or a Release/Press pair.

## 4. Empty tag filter

```text
RequiredOccupantTags = empty
```

Every otherwise valid Actor accepted by collision and the native extension filter can occupy the plate.

## 5. Required tag accepted

```text
RequiredOccupantTags = { Object.Trait.Heavy }
```

An Actor that owns `Object.Trait.Heavy` is accepted and occupies the plate.

## 6. Required tag rejected

An Actor missing one or more entries from `RequiredOccupantTags`, or exposing no supported semantic tag provider while the filter is non-empty, is rejected.

It does not change `CurrentOccupant`, `OccupancyState`, or switch input.

## 7. Multiple required tags use ALL semantics

With two required tags, the Actor must own both. Owning only one is insufficient.

## 8. Destroyed current occupant

Destroying the current occupant clears it safely. A valid replacement already overlapping may be acquired; otherwise the state becomes `Free` and one `Release()` occurs. No Tick or stale delegate binding is used.

## 9. Hold release delay cancellation

```text
Hold, ReleaseDelay = 5
current occupant leaves and no replacement exists
-> OccupancyState becomes Free
-> ReleasePending, plate remains down
same valid Actor or another valid Actor is acquired before completion
-> OccupancyState becomes Occupied
-> inherited pending release is cancelled
-> no new Confirmed Press
-> plate remains down
```

## 10. Press delay cancellation

```text
PressDelay > 0
valid Actor enters -> Occupied -> PressPending
same occupant fully leaves before completion -> Free
-> inherited PressDelay cancelled
-> no activation or movement
```

## 11. Toggle mode

Each complete `Free -> Occupied` edge may toggle only after inherited logical release rearming. Extra components and ignored second Actors do not create extra toggles.

## 12. Latch mode

The first accepted `Free -> Occupied` edge latches output active. Returning to `Free` does not raise the plate until reset according to inherited behavior.

## 13. Pulse mode

A complete accepted occupancy cycle starts/retriggers the inherited pulse according to `PulseRetriggerMode`. Leaving does not end the pulse early.

## 14. Start active

With `bStartActive = true` and physical state initially `Free`:

- the emitted signal initializes active;
- the plate initializes pressed;
- no initialization audio, VFX, or noise is produced;
- later occupancy still follows inherited mode semantics.

## 15. Mid-motion reversal

A genuine switch output reversal during movement changes target without a transform jump and restarts direction-specific movement feedback only as specified.

## 16. Repeated identical output

Repeated callbacks or state requests for the current target do not restart movement, audio, Niagara, or noise.

## 17. Navigation safety

Moving `PlateMesh` never dirties or rebuilds navigation.

## 18. WorldState reset while idle

Reset restores switch signal and plate transform, clears `CurrentOccupant` and returns physical occupancy to `Free`, without audio, Niagara, or noise.

## 19. WorldState reset during movement

Movement stops, transient callbacks are invalidated, restored state is applied, and no reset feedback occurs.

## 20. WorldState reset during PressDelay/ReleaseDelay/Pulse

Inherited timers are safely cancelled/restored according to `APuzzleSwitch` and WorldState contracts.

No stale timer changes state after reset.

## 21. Post-reset occupant reconciliation

After reset completion, current overlaps are reconciled once through the normal single-occupant and tag-filter logic without a world search or permanent Tick.

At most one Actor becomes `CurrentOccupant`.

## 22. Missing optional presentation assets

The plate still detects one occupant, uses `APuzzleSwitch`, and publishes its signal when no sound or Niagara system is assigned.

## 23. Blueprint child without overrides

The full native class works without Blueprint event implementations.

## 24. Controller integration

A configured `APuzzleController` observes the inherited `OutputSignalTag` and controls a Receiver through the normal PuzzleSystem path.

The plate never accesses the Receiver.

---

# AUTOMATED TESTS

Add focused automation tests when the existing module/test architecture supports them without excessive harness work.

Prioritize tests for:

```text
Free/Occupied authoritative state transitions
single-current-occupant enforcement
same-Actor multi-component overlap set behavior
second Actor ignored while occupied
replacement acquisition without Release/Press churn
empty RequiredOccupantTags accepts all
non-empty RequiredOccupantTags requires all tags
Actor without a supported tag source is rejected when tags are required
destroyed current occupant cleanup
Hold delayed-release cancellation
Toggle/Latch/Pulse delegation to APuzzleSwitch
movement target deduplication
mid-motion reversal
reset suppression of audio/VFX/noise
reset cancellation of transient callbacks
PlateMesh navigation relevance disabled
idle Tick disabled
```

Use test doubles or the smallest existing PerceptionKnowledge test hook to verify noise publication without depending on full AI behavior.

Do not replace runtime validation with tests; both are useful.

---

# PERFORMANCE REQUIREMENTS

The expected idle cost is near zero.

Required constraints:

- no permanent Tick;
- no periodic overlap polling;
- no world searches;
- no per-frame navigation updates;
- no per-frame noise publication;
- no per-frame logging;
- no repeated allocation in movement update;
- event-driven occupant tracking;
- movement update enabled only while alpha changes;
- weak/lifecycle-safe occupant references.

Only add an Unreal Insights CPU scope if the actual implementation contains a non-trivial reconciliation or update path worth measuring. Do not instrument trivial getters or a tiny interpolation solely to satisfy a checkbox.

---

# FORBIDDEN SHORTCUTS

Do not:

```text
create APressurePlate inside PuzzleSystem
create a new pressure-plate plugin
copy APuzzleSwitch's mode or delay state machine
publish signals directly from overlap callbacks
activate Receivers directly
place conditions inside APressurePlate
use PlateMesh overlap as the only detector
attach OccupancyVolume to moving PlateMesh
track or aggregate multiple logical occupants at once
treat another component of the same Actor as another logical occupant
replace CurrentOccupant merely because another Actor begins overlap
use raw AActor::Tags as a silent substitute for Gameplay Tags
hard-code player-only activation
add a mandatory weight system
add a new generic payload without a concrete condition need
let PlateMesh affect navigation
force runtime navmesh rebuilds
use AddLocalOffset cumulatively
leave Tick enabled while idle
emit noise every frame
play feedback during initialization or WorldState restoration
serialize timer handles, occupant references/component sets, or active audio/VFX as authoritative state
use a global Actor search after reset
leave stale occupant delegates bound
use LogTemp
silently invent WorldState or PerceptionKnowledge APIs
modify generic plugins with project-specific logic
```

---

# IMPLEMENTATION ORDER

Use this order after completing required repository investigation:

1. identify owning Paradox runtime module and local instructions;
2. inspect `APuzzleSwitch` implementation and native extension hooks;
3. inspect established WorldState participant/reset patterns;
4. inspect established PerceptionKnowledge noise publication pattern;
5. add module dependencies only where required;
6. create `APressurePlate` component hierarchy;
7. implement tag-aware filtering and single-occupant/component-set tracking;
8. connect `Free/Occupied` transitions to inherited `Press()` / `Release()`;
9. implement plate alpha movement with idle Tick disabled;
10. connect inherited output transitions to plate movement;
11. add audio and Niagara movement-start feedback;
12. add PerceptionKnowledge noise through the existing project integration;
13. add WorldState lifecycle suppression, cleanup, restoration, and overlap reconciliation;
14. add validation and debug inspection;
15. update human-facing module documentation;
16. add focused tests where practical;
17. compile the affected editor/game target;
18. fix all errors and recompile until successful;
19. validate all required behavior scenarios;
20. review the complete diff and remove unrelated changes.

---

# DEFINITION OF DONE

The task is complete only when:

- `APressurePlate` derives from the real `APuzzleSwitch`;
- the class lives in the existing Paradox project runtime module;
- `BillboardRoot` is the actual root;
- `FloorMesh`, `PlateMesh`, `OccupancyVolume`, `MovementAudio`, and `MovementVFX` exist with the required hierarchy;
- `PlateMesh` never affects navigation;
- exactly one current occupant is tracked at a time;
- empty `RequiredOccupantTags` accepts every otherwise valid Actor;
- non-empty `RequiredOccupantTags` requires the Actor to own all configured tags;
- `Free/Occupied` edges use only inherited `Press()` and `Release()`;
- all `APuzzleSwitch` modes and delays retain their established behavior;
- plate movement follows inherited authoritative output state;
- movement reverses cleanly and has no idle Tick cost;
- audio and Niagara start only on meaningful gameplay movement;
- movement noise uses the existing PerceptionKnowledge integration;
- WorldState resets semantic and visual state without feedback/noise or stale callbacks;
- the normal PuzzleSystem Controller path remains intact;
- invalid configuration fails predictably;
- local/global debug rules are respected;
- module logging conventions are respected;
- human-facing `Docs` are updated;
- the affected target compiles successfully;
- the required scenarios have been validated;
- no project-specific logic was pushed into generic plugins without a justified generic API need;
- the final diff contains no unrelated changes.

If the affected target does not compile, the task is not finished.
