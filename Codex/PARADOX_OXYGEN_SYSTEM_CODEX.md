# Paradox Oxygen System — Codex Implementation Specification

## Purpose

Implement the **Oxygen System** for Paradox.

Oxygen is a per-Character gameplay resource used primarily to impose a maximum effective duration on a run. It belongs to each temporal Character independently, therefore both the current Player Character and every active temporal Clone own their own Oxygen state.

The system must support:

- regular oxygen consumption during an active run;
- instantaneous oxygen consumption;
- instantaneous oxygen restoration;
- temporary or persistent changes to oxygen consumption rate;
- source-aware blocking of oxygen consumption;
- continuous oxygen regeneration;
- optional maximum-capacity modification;
- clean reset at the beginning of a new timeline attempt;
- Player run failure when oxygen reaches zero;
- Clone death when oxygen reaches zero;
- preservation of the dead Clone's temporal identity for paradox detection;
- no navigation blocking and no Pawn blocking from dead Clone bodies;
- native, event-driven integration points for HUD and gameplay effects.

This specification supplements the existing project architecture and must be implemented using the actual repository APIs as the source of truth.

---

# 1. Mandatory repository investigation

Before modifying code, Codex must:

1. read the root `AGENTS.md`;
2. identify the actual Paradox runtime gameplay module that owns Player/Clone/time-loop integration;
3. search every affected module/plugin for relevant `CODEX` folders and read them before touching code;
4. read the relevant `Docs` folders;
5. inspect the current Player and Clone Character classes and determine whether they share a common Character base;
6. inspect the current temporal-identity / Temporal Index implementation;
7. inspect the current paradox detection path based on the dynamic Line-of-Sight mesh;
8. inspect the current Player run-failure / paradox-failure pipeline;
9. inspect the current time-loop authority, including failed-run cleanup, reset, reconstruction of consolidated Clones, Chrono Spawn release/reoccupation, and synchronized run start;
10. inspect the actual `IntentReplay` playback start/stop/cancel APIs used by Clones;
11. inspect the actual Gameplay Action execution/cancellation APIs used by Player and Clones;
12. inspect current GOAP/GoalAgent integration if it now exists in the repository;
13. inspect the current GridWorld rules for temporal Characters, especially the existing non-blocking behavior between Player and Clones;
14. inspect collision profiles/channels used by temporal Characters and the dynamic paradox LoS mesh;
15. inspect the current Character visual/physics representation before implementing ragdoll/death presentation;
16. inspect the current WorldState reset lifecycle and determine whether temporal Characters are restored through WorldState or reconstructed directly by the time-loop system;
17. inspect the current Inventory module integration with Player and Clones, including passive-effect cleanup and reset semantics;
18. inspect the current Gameplay HUD architecture, especially the existing `UParadoxHUDWidget`, its lifecycle, its Blueprint/UMG counterpart, how child widgets are bound, and how the HUD tracks the current Player Character;
19. inspect the actual TacticalPause/time-dilation implementation and verify how the selected game-time/timer API behaves under pause and simulation-speed changes;
20. inspect existing source-token / handle / modifier patterns before inventing a new one;
21. inspect module logging/debug conventions;
22. compile the appropriate target before implementation and after each meaningful milestone.

Do not invent Unreal Engine or project APIs.

When a required generic capability already exists, reuse it.

When a small capability is missing, add it to the module that owns the responsibility instead of duplicating state in the Oxygen System.

---

# 2. Module ownership

The Oxygen System is **Paradox-specific gameplay code**.

Preferred ownership is the existing Paradox runtime gameplay module already responsible for:

- Player/Clone integration;
- time-loop orchestration;
- Temporal Index;
- paradox rules;
- Chrono Spawn integration;
- project-specific Gameplay Actions;
- project-specific HUD integration.

Do not create a new generic Oxygen plugin unless the repository already has an established generic resource/stat plugin that is clearly the correct owner.

Do not add Paradox oxygen rules to generic plugins such as:

```text
GridWorld
WorldState
EntityRelations
IntentReplay
GameplayActions
TacticalPause
PuzzleSystem
```

Generic plugins must remain unaware of:

```text
oxygen death
oxygen run failure
clone corpse state
Temporal Index consequences of oxygen death
oxygen HUD presentation
```

Suggested paths, adapted to the actual module structure:

```text
Source/<ParadoxRuntimeModule>/Public/Oxygen/ParadoxOxygenComponent.h
Source/<ParadoxRuntimeModule>/Private/Oxygen/ParadoxOxygenComponent.cpp
Source/<ParadoxRuntimeModule>/Public/UI/ParadoxOxigenWidget.h
Source/<ParadoxRuntimeModule>/Private/UI/ParadoxOxigenWidget.cpp
Source/<ParadoxRuntimeModule>/Docs/OXYGEN_SYSTEM.md
```

Follow the existing module folder conventions instead of reorganizing unrelated files.

---

# 3. Core architectural rule

Oxygen belongs to the **Character**, not to the player controller, local player, GameMode, run manager, or replay track.

Required conceptual ownership:

```text
Temporal Character
└── UParadoxOxygenComponent
```

The same Oxygen component implementation must be usable by:

```text
Current Player Character
Temporal Clone Character
```

The component owns only oxygen-resource state and oxygen-resource operations.

It must not decide by itself whether depletion means:

```text
run failure
clone death
another future reaction
```

Instead it publishes a semantic depletion event and the Paradox Character/time-loop integration applies the correct owner-specific consequence.

This separation is mandatory.

---

# 4. Oxygen state model

Create or adapt one Actor Component equivalent to:

```text
UParadoxOxygenComponent
```

Required conceptual configuration/state:

```text
MaxOxygen
CurrentOxygen
BaseConsumptionRate
EffectiveConsumptionRate
EffectiveRegenerationRate
bConsumptionBlocked
bIsDepleted
```

Exact reflected fields may differ when the implementation can derive some values safely.

Required invariants:

```text
MaxOxygen >= 0
0 <= CurrentOxygen <= MaxOxygen
BaseConsumptionRate >= 0
```

`bIsDepleted` is an authoritative terminal resource state for the current Character life/run participation.

Once depletion has been reached, ordinary oxygen restoration must **not automatically resurrect a dead Clone or cancel an already accepted Player run failure**.

Recovery from depletion must occur only through an explicit reset/revival path owned by the appropriate gameplay lifecycle.

---

# 5. Oxygen uses simulation time

Oxygen consumption represents time passing inside the active Paradox simulation.

Expected semantics:

```text
Tactical Pause -> oxygen does not advance
x1             -> normal oxygen progression
x1.5           -> 1.5x progression relative to real time
x2             -> 2x progression relative to real time
x3             -> 3x progression relative to real time
```

Do not implement oxygen against platform real time.

Codex must verify the actual behavior of the project/engine timing mechanism under:

```text
pause
global time dilation
TacticalPause speed changes
world teardown
reset
```

Prefer an event/timer/time-integration solution over a permanent per-Character Tick.

Do not add Tick by default merely to subtract:

```text
Rate * DeltaTime
```

A recommended architecture is analytical/event-driven integration:

```text
materialized CurrentOxygen at last synchronization
+ last simulation-time sample
+ current effective net rate
+ one depletion timer when net consumption is positive
```

Before a state-changing oxygen operation:

```text
Synchronize current oxygen from elapsed simulation time
apply mutation
recompute effective rates
reschedule depletion if required
```

When the effective rate changes, consumption is blocked/unblocked, oxygen is restored/consumed, MaxOxygen changes, or reset occurs, resynchronize and reschedule.

If the verified Unreal timer/game-time behavior makes another implementation safer, use the smallest correct implementation while preserving these semantics.

No stale timer/deferred callback may apply after reset, depletion, owner destruction, or world teardown.

---

# 6. Base consumption

During an active run and while the Character is oxygen-operational:

```text
CurrentOxygen decreases at BaseConsumptionRate
```

The actual effective rate may be modified by active effects.

The component must expose designer-readable values for:

```text
BaseConsumptionRate
Current Effective Consumption Rate
Current Effective Regeneration Rate
Current Net Rate
```

Do not expose mutable internal modifier collections directly.

---

# 7. Instantaneous operations

Provide safe public operations equivalent to:

```text
ConsumeOxygen(Amount)
RestoreOxygen(Amount)
ConsumeOxygenPercent(PercentOfMax)
RestoreOxygenPercent(PercentOfMax)
FillOxygen()
```

Exact API names must follow project conventions.

## 7.1 Instant consumption

`ConsumeOxygen` must:

```text
synchronize current oxygen
validate/normalize Amount
subtract oxygen
clamp to zero
emit change notification only if value changed
trigger depletion exactly once if zero is reached
```

Instant oxygen consumption is allowed to deplete a Character immediately.

## 7.2 Instant restoration

`RestoreOxygen` must:

```text
synchronize current oxygen
validate/normalize Amount
add oxygen
clamp to MaxOxygen
emit change notification only if value changed
reschedule depletion timing when still operational
```

If `bIsDepleted == true`, ordinary restoration may change the stored value only if the chosen lifecycle needs it, but it must not automatically return the Character to operational state.

Prefer rejecting/ignoring ordinary restoration on a depleted Character unless the existing gameplay architecture has a concrete use for post-depletion resource mutation.

Do not create implicit resurrection semantics.

---

# 8. Continuous consumption modifiers

Effects must be able to increase or decrease oxygen consumption over time.

Do not implement temporary effects through paired calls such as:

```text
SetConsumptionRate(NewRate)
...
SetConsumptionRate(OldRate)
```

because overlapping effects would overwrite each other and cleanup order would become unsafe.

Use a source-aware or handle-based modifier model following an existing project pattern when one exists.

Conceptually:

```text
AddConsumptionModifier(...)
    -> ModifierHandle

RemoveConsumptionModifier(ModifierHandle)
```

Support at least:

```text
Additive modifiers
Multiplicative modifiers
```

Define and document one deterministic evaluation order.

Recommended conceptual order:

```text
EffectiveConsumptionRate = max(0, (BaseConsumptionRate + SumAdditive) * ProductMultipliers)
```

Do not assume this exact formula if an existing project stat/modifier convention already defines ordering; follow the established convention when suitable.

Required properties of the modifier system:

```text
multiple simultaneous modifiers coexist
removing one modifier does not remove another
invalid/stale handles fail predictably
repeated removal is safe/idempotent where practical
rate changes resynchronize oxygen before applying the new rate
modifier cleanup cannot resurrect a depleted Character
```

---

# 9. Consumption blocking

Effects must be able to stop oxygen consumption temporarily.

Do not use one externally mutable boolean when multiple independent effects can block consumption.

Use source-aware/token/handle semantics equivalent to:

```text
AddConsumptionBlock(Source/Reason)
    -> BlockHandle

RemoveConsumptionBlock(BlockHandle)
```

Effective rule:

```text
one or more valid blocks -> regular consumption is blocked
zero valid blocks        -> regular consumption may resume
```

Example:

```text
Effect A blocks consumption
Effect B blocks consumption
A ends -> still blocked by B
B ends -> consumption resumes
```

A consumption block stops **regular oxygen consumption**.

It does not automatically block:

```text
instant oxygen damage
explicit Set/Reset operations
continuous regeneration
```

unless a future effect explicitly defines different behavior.

Do not overload this mechanism into general oxygen invulnerability.

---

# 10. Continuous regeneration

Support continuous oxygen regeneration as a separate concept from negative consumption modifiers.

Conceptual API:

```text
AddRegenerationModifier(...)
    -> RegenerationHandle

RemoveRegenerationModifier(RegenerationHandle)
```

At minimum, support additive regeneration rate.

If the existing stat/effect architecture already supports additive and multiplicative modifiers consistently, it may be reused.

Conceptual net progression:

```text
NetRate = EffectiveConsumptionRate - EffectiveRegenerationRate
```

Therefore:

```text
NetRate > 0 -> oxygen decreases
NetRate = 0 -> oxygen remains stable
NetRate < 0 -> oxygen increases toward MaxOxygen
```

If consumption is blocked:

```text
Effective consumption contribution = 0
regeneration may continue
```

Continuous regeneration must never exceed `MaxOxygen`.

When oxygen reaches MaxOxygen while net regeneration is positive, stop unnecessary timers/work until state changes again.

---

# 11. Maximum oxygen operations

Support maximum-capacity changes only if the current project has or imminently needs effects that modify oxygen capacity.

Do not overbuild a generic attribute framework.

Provide a safe operation or modifier path equivalent to:

```text
SetMaxOxygen(NewMax, AdjustmentPolicy)
```

or reuse an existing project stat modifier system.

If exposed, define an explicit policy for CurrentOxygen when MaxOxygen changes.

Useful policies are conceptually:

```text
PreserveCurrentValue
PreservePercentage
FillAddedCapacity
```

Do not silently choose inconsistent behavior per caller.

For the first implementation, one clearly documented default policy is acceptable if no real use case requires all policies.

---

# 12. Administrative/reset operations

Provide lifecycle-owned operations equivalent to:

```text
ResetOxygen()
SetCurrentOxygenForLifecycle(...)
```

Do not expose unrestricted mutation to ordinary gameplay callers unless needed.

`ResetOxygen()` must conceptually:

```text
invalidate/cancel oxygen-owned timers and deferred callbacks
clear depletion state
clear transient oxygen modifiers and consumption blocks that are not intended to persist across timeline resets
restore MaxOxygen to the configured run-start value when appropriate
restore CurrentOxygen to the configured run-start value
recompute effective rates
leave consumption dormant until the Character is actually started for the run
emit only the necessary semantic notifications
```

The exact handling of persistent configuration vs runtime modifiers must follow the project's current effect/inventory model.

---

# 13. Run activation lifecycle

Oxygen should not begin draining merely because a Character Actor exists in the world.

This is important because the time-loop architecture can spawn/reconstruct Clones before the new run starts while the player is still selecting a Chrono Spawn.

Required semantics:

```text
Clone reconstructed for timeline selection
-> oxygen initialized
-> oxygen consumption NOT running

Player/Clones prepared
-> synchronized run-start barrier completes
-> oxygen consumption starts for every operational temporal Character at the same logical run-start moment
```

Likewise, oxygen progression must stop when the run is no longer active because of:

```text
rewind transition
accepted run failure
world reset
level teardown
```

Do not allow one Clone to consume oxygen while the player is still choosing the next Chrono Spawn.

---

# 14. Player oxygen depletion

When the **current Player Character** reaches zero oxygen:

```text
UParadoxOxygenComponent
    -> OnOxygenDepleted
    -> Paradox Player/time-loop integration
    -> fail current run with reason OxygenDepleted
```

Do **not** create a second independent reset pipeline.

Player oxygen depletion must use the same authoritative failed-run path already used by a paradox, with a different failure reason/presentation.

Required consequences are equivalent to paradox failure:

```text
current partial recording is discarded
current run is NOT consolidated
no new Clone is created from the failed run
Chrono Spawn used by the failed run becomes available again
previously consolidated timelines remain consolidated
playback/paradox detection are stopped for reset
world returns to the same baseline/reset flow
consolidated Clones are reconstructed normally
player returns to Chrono Spawn selection according to existing loop rules
```

Add or extend the existing run-failure reason representation with a semantic reason equivalent to:

```text
Paradox
OxygenDepleted
```

Do not duplicate all failure code simply to change the message.

The game-over/failure widget must be able to display a different message for oxygen depletion.

The presentation text itself should remain data/Blueprint/UI configurable rather than hardcoded into the oxygen component.

---

# 15. Clone oxygen depletion

When a **Clone** reaches zero oxygen, the current run continues.

Clone depletion is not a Player run failure and does not automatically cause rewind/reset.

Required flow:

```text
Clone Oxygen reaches 0
    ↓
OnOxygenDepleted
    ↓
Clone enters dead/depleted state
    ↓
active execution is cancelled
    ↓
Clone loses active-agent gameplay capabilities
    ↓
runtime body enters ragdoll / physics-collapse death presentation
    ↓
body eventually settles/freeze according to current Character representation
    ↓
corpse remains in world until reset
```

The dead Clone becomes a passive temporal body, not an active agent.

---

# 16. Clone dead-state responsibilities

Codex must use or extend the existing Clone lifecycle/state representation rather than scattering independent booleans through unrelated systems.

Use one authoritative semantic state or existing death/operational-state mechanism equivalent to:

```text
Operational
OxygenDepleted / Dead
```

If the project already has a general death state/reason model, reuse it and represent:

```text
DeathReason = OxygenDepleted
```

Do not create a second competing death state.

On Clone death, disable/cancel active-agent behavior using verified APIs.

This includes, where present:

```text
IntentReplay playback
current Gameplay Action
queued/new Gameplay Actions
GOAP / GoalAgent planning and execution
locomotion requests
interaction requests
pickup/drop/special inventory actions
footstep/noise generation caused by locomotion
active input-like control paths
active observer perception / Clone vision processing
```

Do not disable the minimum temporal identity and collision/query state required for the corpse to remain a valid paradox target.

Do not automatically drop the Clone's equipped object unless the existing inventory/death design explicitly requires that behavior.

No new gameplay action should be manufactured merely because oxygen reached zero.

---

# 17. Ragdoll / physics-collapse requirement

The Clone must visually fall into a ragdoll/physics death state when oxygen is depleted.

However, Codex must first inspect the current Character representation.

Older design documentation describes Paradox Characters as voxel/static-mesh based rather than traditional skeletal Characters. Therefore:

- if the current implementation now has a verified skeletal ragdoll path, use it;
- if the current implementation remains static-mesh/voxel based, do not introduce an entire Skeletal Mesh + Animation Blueprint architecture solely to satisfy the word `ragdoll`;
- use the smallest existing/appropriate physics-collapse solution that produces the intended physical death/fall presentation;
- if the current representation cannot satisfy the requested result safely without an architectural change, report the conflict explicitly instead of inventing a fake engine API or silently redesigning the Character stack.

The gameplay requirement is:

```text
oxygen depleted Clone visibly collapses
body settles
body becomes a fixed/passive corpse for the rest of the current run
```

The final passive corpse must not continue simulating unnecessary physics forever.

Stop/freeze physics after a safe settle condition or use the project's established corpse-freeze behavior.

Do not use permanent Tick merely to monitor a sleeping body if the physics system provides a better lifecycle/event path.

---

# 18. CRITICAL CORPSE NAVIGATION RULE

This rule is mandatory and supersedes any previous interpretation that a dead Clone should become a GridWorld obstacle.

**A dead Clone corpse must remain navigable and must NOT block pathfinding or Pawn movement.**

Required behavior:

```text
corpse does not mark GridWorld cells blocked
corpse does not mark GridWorld cells occupied for navigation
corpse does not add a GridNavigationModifier
corpse does not force navmesh/grid recalculation
corpse does not invalidate paths merely because it exists
corpse does not block Player collision
corpse does not block Clone collision
Pawns may overlap/pass through the corpse
```

The GDD already establishes that temporal versions:

```text
do not block each other on the grid
are not considered pathfinding obstacles
may pass through each other
```

Death must preserve the non-blocking navigation philosophy.

Do not introduce a special corpse obstacle exception.

The corpse is a **persistent visual/temporal presence**, not a navigation obstacle.

---

# 19. Corpse collision profile

The corpse still needs enough collision/query presence to participate in paradox detection.

Configure death collision using the existing project collision-channel conventions.

Mandatory outcome:

```text
Pawn channel -> overlap or ignore blocking, never Block
Grid/navigation -> no obstacle contribution
Paradox dynamic LoS overlap query -> still detectable as a temporal target
```

During the brief physics-collapse phase, the body may need appropriate world collision to settle visually, but it must never become a blocking gameplay obstacle for temporal Pawns.

After settling, prefer the cheapest collision state that:

```text
keeps the corpse detectable by paradox overlap queries
keeps Pawn traversal non-blocking
avoids unnecessary physics cost
avoids navigation relevance
```

Do not make the corpse block another temporal version's line-of-sight geometry unless that is already the established temporal-character rule.

Temporal versions currently do not block one another's vision; death should not silently reverse that rule.

---

# 20. Dead Clone temporal identity

A dead Clone remains the **same temporal entity**.

Death must not remove or change:

```text
Temporal Index
temporal identity
EntityRelations identity data required by the project
paradox-target classification
```

Example:

```text
Clone T1 dies from oxygen depletion.

Living T0 later sees the corpse of T1.

0 < 1
-> T0 has observed a future version
-> paradox is generated through the normal rule
```

The corpse must therefore remain a valid **target** for the existing dynamic Line-of-Sight paradox detection path.

Do not create a special "corpse paradox" rule.

Reuse the normal temporal-index comparison:

```text
ObserverTemporalIndex < TargetTemporalIndex
```

The only special case is that the target happens to be dead.

---

# 21. Dead Clone is not an active observer

A dead Clone no longer behaves as a living observing agent.

Therefore its own active vision/perception/cone behavior should be disabled unless the current paradox system architecture requires a passive representation for unrelated reasons.

Required semantic distinction:

```text
Living T0 sees dead T1 -> paradox may occur
Dead T0 does NOT keep actively observing living T1
```

The corpse remains detectable.

The corpse does not continue to perform perception, investigation, replay, GOAP, or other active-agent behavior.

Do not preserve a live Clone LoS observer solely because the dead body must remain a paradox target.

---

# 22. Player/Clone collision compatibility

The oxygen-death implementation must preserve the existing project rule that Player and temporal Clones do not physically deadlock one another.

Validate at minimum:

```text
living Player vs living Clone
living Clone vs living Clone
living Player vs dead Clone corpse
living Clone vs dead Clone corpse
```

All must remain non-blocking for movement/pathfinding according to the established temporal-character rules.

A dead body must not create a new deadlock source.

---

# 23. Reset behavior for dead Clones

On a failed run, rewind reset, or any timeline reset that reconstructs temporal entities, dead Clones must return alive and fully operational for the next run.

Use the actual time-loop reconstruction lifecycle as the authority.

Required post-reset result for every reconstructed consolidated Clone:

```text
alive / operational state restored
ragdoll/physics death state disabled
original visual transform/state restored
normal non-blocking temporal collision restored
Temporal Index restored correctly
Replay Track restored correctly
oxygen reset to run-start value
oxygen modifiers/blocks cleared or reconstructed according to their real owning systems
observer/paradox behavior prepared but not yet authoritative
playback prepared but not yet started
```

The Clone must remain idle while the player chooses the next Chrono Spawn.

At the synchronized run-start barrier:

```text
Clone playback starts
Clone active observer behavior starts
Clone oxygen consumption starts
```

Do not let a stale corpse timer, physics callback, depletion callback, action callback, or perception callback mutate the reconstructed Clone later.

---

# 24. WorldState integration policy

Do not automatically register oxygen as ordinary WorldState snapshot data merely because a reset exists.

First inspect how the current time-loop treats temporal Characters.

Preferred rule when temporal Characters are already reconstructed explicitly by the loop:

```text
WorldState restores the world baseline
Paradox time-loop reconstructs temporal Characters
Paradox Character preparation resets Oxygen explicitly
```

This avoids snapshotting transient Character resource state that should always restart for a new timeline attempt.

If the actual current architecture manages temporal Characters differently, use the smallest integration that guarantees:

```text
new attempt -> full configured run-start oxygen for every temporal Character
no stale modifier/block/depletion state survives reset
```

Do not modify the generic WorldState plugin with oxygen-specific logic.

---

# 25. IntentReplay relationship

Oxygen is **runtime Character state**.

Do not record oxygen values into Replay Tracks frame by frame.

Do not encode historical oxygen drain as replay commands.

A Clone re-executes its recorded intentions while oxygen is simulated again in the current run.

Therefore the same Replay Track may produce a different oxygen outcome when current contextual effects differ.

Example:

```text
original player run:
normal oxygen rate -> interaction reached successfully

later Clone replay:
Clone crosses an effect that increases oxygen consumption
-> oxygen may deplete earlier
-> Clone dies
-> replay stops at that point
```

This is valid contextual runtime behavior.

Do not mutate the immutable Replay Track because oxygen changed the new execution result.

---

# 26. Inventory/effect integration

The Oxygen component must be usable by project effects such as pickupable passive effects, environment volumes, puzzle effects, hazards, or future status effects.

Effects should call controlled Oxygen APIs rather than mutate component properties directly.

Examples:

```text
hazard -> ConsumeOxygen(Amount)
helmet -> AddConsumptionModifier(...)
oxygen-rich zone -> AddRegenerationModifier(...)
stasis effect -> AddConsumptionBlock(...)
oxygen canister -> RestoreOxygen(Amount)
```

If an effect owns a modifier/block, it must also own cleanup of the returned handle/token.

Lifecycle cleanup must tolerate:

```text
item unequipped
Actor destroyed
Clone death
run reset
world teardown
```

Prefer existing passive-effect/source-handle patterns from the inventory architecture.

Do not let one effect remove another effect's modifier.

---

# 27. Mandatory HUD integration — `UParadoxHUDWidget` + `UParadoxOxigenWidget`

HUD integration is part of the **first implementation scope**, not a future-only extension.

The existing `UParadoxHUDWidget` must own/display one dedicated custom oxygen widget named:

```text
UParadoxOxigenWidget
```

Use this requested type name unless the repository already contains an equivalent oxygen widget with an established reflected class name that must be preserved for asset compatibility. Do not create two competing oxygen widgets only to resolve a spelling difference.

The standard HUD ownership flow is:

```text
UParadoxHUDWidget
    ↓ contains / binds
UParadoxOxigenWidget
    ↓ resolves through its Owning Player
Current Player Character
    ↓ owns
UParadoxOxygenComponent
```

However, `UParadoxOxigenWidget` must **not** be hardwired to the local Player Character. The same native widget class must support observing any Character's oxygen when explicitly initialized from code.

Required observation modes:

```text
1. Owning Player mode
   -> standard/default mode used by the gameplay HUD
   -> resolve the current Player Character/Pawn from the widget's Owning Player
   -> resolve that Character's UParadoxOxygenComponent

2. Manual component mode
   -> caller explicitly supplies a UParadoxOxygenComponent
   -> widget observes exactly that component
   -> intended for future Clone/entity inspection UI
```

This dual-source design is mandatory so future selectable Clones can reuse the same oxygen presentation widget without duplicating UI logic.

The Oxygen component remains completely independent from UMG. It must never create the HUD, locate widgets, or hold references to `UParadoxHUDWidget` / `UParadoxOxigenWidget`.

## 27.1 `UParadoxOxigenWidget`

Create a native `UUserWidget`-derived class equivalent to:

```text
UParadoxOxigenWidget
```

Codex must inspect the existing UI class hierarchy first and derive from the project's established base widget type when one exists and is appropriate. Do not force plain `UUserWidget` if `UParadoxHUDWidget` already establishes a project-specific base.

The widget must expose a native progress-bar member bound to its Blueprint widget tree through Unreal's `BindWidget` metadata.

Required conceptual declaration:

```cpp
UPROPERTY(meta = (BindWidget))
TObjectPtr<UProgressBar> OxygenProgressBar;
```

Use the exact reflected/property syntax valid for the project's Unreal Engine version and current UI conventions.

The Blueprint widget derived from `UParadoxOxigenWidget` must contain a `ProgressBar` widget with the exact designer variable name expected by the native `BindWidget` property.

Do not use runtime widget-tree searches by string/name when `BindWidget` can express the required contract.

Do not make the progress bar optional unless the existing HUD architecture has a concrete reason to support an oxygen widget without its primary bar. A missing required `BindWidget` child is an invalid widget setup and should be diagnosable.

## 27.2 Progress percentage

The progress bar represents the normalized oxygen of the **currently observed `UParadoxOxygenComponent`**.

In the standard HUD path, that observed component belongs to the current Player Character.

In manually initialized usages, the observed component may belong to a Clone or another valid temporal Character.

Required value:

```text
NormalizedOxygen = CurrentOxygen / MaxOxygen
```

with safe semantics:

```text
MaxOxygen > 0 -> clamp(CurrentOxygen / MaxOxygen, 0, 1)
MaxOxygen <= 0 -> 0
```

Prefer using the component query:

```text
GetNormalizedOxygen()
```

so normalization and edge-case semantics remain centralized in gameplay code.

Updating the visual bar is conceptually:

```cpp
OxygenProgressBar->SetPercent(OxygenComponent->GetNormalizedOxygen());
```

The progress bar is presentation only. Its percentage must never become authoritative oxygen state.

## 27.3 Oxygen Component binding — two mandatory initialization paths

`UParadoxOxigenWidget` must support **two explicit ways** to obtain the `UParadoxOxygenComponent` it observes.

### A. Owning Player resolution — standard HUD path

This is the default mode used when the widget is part of `UParadoxHUDWidget`.

The widget resolves the current controlled Character/Pawn through its **Owning Player** using the actual project/UMG ownership conventions verified in the repository, then resolves that Actor's:

```text
UParadoxOxygenComponent
```

Conceptually:

```text
UParadoxOxigenWidget
    -> Get/resolve Owning Player
    -> resolve current Player Character/Pawn
    -> Find/resolve UParadoxOxygenComponent
    -> BindObservedOxygenComponent(...)
```

Do not use:

```text
GetAllActorsOfClass
the first temporal Character found in the world
a global cached Player Character unrelated to widget ownership
a duplicate PlayerController oxygen value
```

The widget may expose an operation equivalent to:

```text
InitializeFromOwningPlayer()
```

or use an existing widget initialization/lifecycle hook if the repository already has a safer established pattern.

The important contract is that standard HUD usage works from the widget's Owning Player without requiring an external caller to manually pass the Player's Oxygen component every time.

When the Owning Player possesses/replaces a new Player Character after a timeline reset, the widget must be able to resolve and bind the new Character's component instead of remaining attached to the old instance.

### B. Manual initialization from code — generic inspection path

The widget must also expose a controlled initialization/binding operation equivalent to:

```text
SetObservedOxygenComponent(UParadoxOxygenComponent* InOxygenComponent)
```

and/or, if useful in the existing project architecture:

```text
SetObservedCharacter(ACharacter* InCharacter)
```

The component-based form is the core requirement.

This path must allow code to create/reuse `UParadoxOxigenWidget` for a Character that is **not** the Owning Player.

Primary future use case:

```text
Player selects Clone T2
    ↓
Clone information panel opens
    ↓
code gets T2.UParadoxOxygenComponent
    ↓
OxygenWidget->SetObservedOxygenComponent(T2OxygenComponent)
    ↓
same widget displays T2 oxygen
```

Do not create a separate Clone-only oxygen widget.

### Binding-source precedence

Manual initialization must not be silently overwritten by automatic Owning Player resolution.

Required semantics:

```text
default construction
    -> Owning Player mode

SetObservedOxygenComponent(Component)
    -> Manual component mode
    -> unbind previous observed component
    -> bind exactly Component
    -> refresh immediately
    -> automatic Owning Player refresh must not replace it

ReturnToOwningPlayer() / InitializeFromOwningPlayer()
    -> leave Manual component mode
    -> unbind manual component
    -> resolve current Owning Player Character
    -> bind its Oxygen component
    -> refresh immediately
```

Use the smallest state representation appropriate to the existing UI architecture. This may be an explicit observation-source enum or an equivalent internal invariant.

Do not scatter ambiguous booleans that allow both modes to believe they are authoritative simultaneously.

### Common rebind semantics

Every successful source change must perform:

```text
unbind delegates from previous Oxygen component
    ↓
store/validate new observed component
    ↓
bind new component notifications exactly once
    ↓
refresh progress bar immediately
```

A null manual component must be handled predictably. Prefer clearing the current observed target and presenting a safe empty/zero state unless the existing UI conventions define another explicit behavior.

Never leave delegate bindings pointing at a previous Character/component after switching Player, selecting another Clone, closing an inspection panel, reset, or teardown.

## 27.4 Event-driven immediate updates

Bind the Oxygen widget to the smallest useful set of component notifications needed to refresh immediately after discrete changes.

At minimum, oxygen value changes/reset/depletion must result in an immediate progress-bar refresh.

Conceptually:

```text
OnOxygenChanged -> RefreshOxygenPercent()
OnOxygenReset   -> RefreshOxygenPercent()
OnOxygenDepleted -> RefreshOxygenPercent()
```

If `OnOxygenChanged` already covers reset/depletion value transitions, do not add redundant delegate traffic only for the UI.

Immediate event-driven refresh is required for operations such as:

```text
instant oxygen consumption
instant restoration
FillOxygen
reset
MaxOxygen change
depletion
```

Do not broadcast unchanged values merely to drive the progress bar.

## 27.5 Continuous drain presentation

The Oxygen component may use analytical/event-driven time integration and therefore does not need to mutate/broadcast `CurrentOxygen` every frame.

The HUD still needs to visually represent continuous oxygen drain.

Use the project's existing HUD presentation-update mechanism if one already exists.

If no suitable mechanism exists, `UParadoxOxigenWidget` may perform a **presentation-only lightweight sample while visible** of:

```text
OxygenComponent->GetNormalizedOxygen()
```

and call `SetPercent()` on the bound progress bar.

This sampling may use the established widget update/tick path because there is only one current Player oxygen display and it is not gameplay authority. Keep it disabled when the widget is not active/visible where practical.

Do not introduce per-frame oxygen mutation into every Character merely to animate one HUD progress bar.

Do not use Blueprint UMG property-binding functions as the primary architecture when native controlled refresh/sampling is available; update the bound `UProgressBar` directly.

## 27.6 Integration inside `UParadoxHUDWidget`

`UParadoxHUDWidget` must contain the oxygen widget as a dedicated child widget.

Prefer the same native `BindWidget` contract for the child when this matches the existing HUD architecture.

Conceptually:

```cpp
UPROPERTY(meta = (BindWidget))
TObjectPtr<UParadoxOxigenWidget> OxygenWidget;
```

The corresponding Blueprint HUD widget must therefore contain the oxygen child widget with the exact designer variable name required by the native property.

Responsibilities are separated as follows:

```text
UParadoxHUDWidget
    -> owns overall HUD composition/visibility
    -> contains the OxygenWidget
    -> gives it the normal UMG Owning Player context

UParadoxOxigenWidget
    -> owns oxygen presentation
    -> defaults to resolving oxygen from its Owning Player
    -> can instead be manually bound from code to any UParadoxOxygenComponent
    -> observes exactly one component at a time
    -> updates OxygenProgressBar

UParadoxOxygenComponent
    -> owns oxygen gameplay state
    -> knows nothing about HUD/UMG
```

`UParadoxHUDWidget` may explicitly trigger the oxygen widget's Owning Player re-resolution when the existing HUD lifecycle provides the cleanest notification that the controlled Player Character changed, but it must not duplicate component-binding or percentage logic.

Do not put progress-bar update logic directly into unrelated sections of `UParadoxHUDWidget` when it belongs inside `UParadoxOxigenWidget`.

## 27.7 HUD visibility lifecycle

The existing `UParadoxHUDWidget` visibility rules remain authoritative for whether gameplay HUD is visible.

Do not create a second oxygen-specific global HUD visibility manager.

When the gameplay HUD is hidden because of an existing game state, the oxygen widget follows the parent HUD unless the current HUD architecture intentionally supports independent child visibility.

The oxygen widget must safely handle these states:

```text
HUD constructed before Player Character is ready
Player Character replaced/reconstructed while in Owning Player mode
Player Character temporarily unavailable during reset
manual binding to a Clone Oxygen component
manual rebind from one Clone to another
manual target destroyed/unavailable
switch from Manual component mode back to Owning Player mode
Oxygen component reset
HUD/inspection widget removed or destructed
world teardown
```

On widget destruction/removal, unbind any long-lived Oxygen component delegates according to Unreal lifecycle rules.

## 27.8 Blueprint designer workflow

The intended designer workflow is:

```text
WBP_ParadoxHUD (derived from UParadoxHUDWidget)
└── OxygenWidget (derived from / instance of UParadoxOxigenWidget)
    └── OxygenProgressBar (UProgressBar, Is Variable)
```

The exact Blueprint asset names may follow the project's existing naming conventions; the native `BindWidget` variable contracts must match the actual designer widget names.

Designers may customize:

```text
progress-bar style
fill image
background
size/layout
animations
oxygen warning visuals
```

but they must not reimplement oxygen arithmetic or component-binding logic in Blueprint just to make the base widget work.

The same Blueprint child of `UParadoxOxigenWidget` should be reusable in:

```text
main Player HUD
future selected-Clone information panel
other Character inspection UI
```

unless different presentation genuinely requires a separate visual Blueprint while still reusing the same native widget class.

## 27.9 HUD validation

Validate at least:

```text
UParadoxHUDWidget resolves/binds its UParadoxOxigenWidget child
UParadoxOxigenWidget resolves its OxygenProgressBar through BindWidget
Owning Player mode resolves the current Player Character/Pawn and its UParadoxOxygenComponent
Manual component mode accepts a specific UParadoxOxygenComponent supplied from code
Manual mode is not silently overwritten by automatic Owning Player resolution
returning to Owning Player mode re-resolves the current Player Character
old component delegates are removed on every rebind
new component delegates are added exactly once
null/destroyed observed components are handled safely
progress percentage is clamped to [0,1]
MaxOxygen <= 0 cannot divide by zero
widget teardown leaves no stale delegate binding
```

Use the module's established log category/macros for invalid runtime setup.

Do not use `LogTemp`.

---

# 28. Optional useful query: time to depletion

If it can be provided cheaply and accurately from the chosen analytical model, expose a read-only query equivalent to:

```text
GetEstimatedTimeToDepletion()
```

Semantics:

```text
positive net consumption -> CurrentOxygen / NetConsumptionRate
zero/negative net consumption -> no finite depletion time
already depleted -> 0
```

Use simulation-time units.

Do not expose misleading values when regeneration/modifiers make depletion non-finite.

This query is optional for the first implementation if it would complicate the architecture.

---

# 29. Blueprint extension model

Core resource invariants must remain native.

Blueprint may observe and present semantic events.

Useful Blueprint-facing hooks may include:

```text
OnOxygenChanged
OnOxygenDepleted
OnOxygenConsumptionBlocked
OnOxygenConsumptionResumed
OnOxygenReset
```

Do not require Blueprint to:

```text
subtract oxygen every frame
maintain modifiers
track block ownership
decide whether depletion already fired
cancel depletion timers
restore Clone operational state
perform Player failed-run orchestration
maintain Temporal Index on corpses
```

Blueprint presentation must not be required for correctness.

---

# 30. Validation and error handling

Validate at least:

```text
MaxOxygen is not negative
run-start oxygen is inside valid range
BaseConsumptionRate is not negative
instant operation amounts are valid
percent operations use a documented range
modifier handles belong to this component
block handles belong to this component
reset cannot leave stale active timers
component owner is valid for Character integration when required
```

Invalid runtime requests must fail predictably and preserve valid state.

Do not silently manufacture oxygen or modifiers from invalid inputs.

Do not use `check()` for ordinary content/runtime errors.

Use the Paradox module's established logging category/macros.

No committed `LogTemp`.

---

# 31. Debug state

Add lightweight debug inspection following the module's local/global debug rules where visual debug is applicable.

The Oxygen component should make the following state inspectable through logs/debug UI/Details runtime inspection as appropriate:

```text
owner Character
Player or Clone role when the project can identify it safely
Temporal Index when applicable
CurrentOxygen
MaxOxygen
normalized oxygen
BaseConsumptionRate
active consumption modifiers
EffectiveConsumptionRate
active regeneration modifiers
EffectiveRegenerationRate
active consumption-block count
net oxygen rate
run-consumption active/inactive
bIsDepleted
scheduled depletion state / estimated remaining time
```

For a dead Clone also make it possible to diagnose:

```text
death reason
active-agent systems disabled
corpse paradox-target capability retained
corpse observer capability disabled
Pawn blocking disabled
navigation blocking disabled
physics collapse settled/frozen state
```

Do not draw persistent debug geometry by default.

---

# 32. Required behavior scenarios — Oxygen core

Validate at least these cases.

## 32.1 Normal consumption

```text
Current = 100
Base rate = 1/s
run active
10 simulation seconds pass
-> Current = 90
```

## 32.2 Tactical Pause

```text
oxygen consuming
pause simulation
real time passes
-> oxygen unchanged
resume
-> oxygen continues from previous value
```

## 32.3 Faster simulation

```text
simulation x2
-> oxygen progresses twice as fast relative to real time
```

## 32.4 Instant consumption

```text
Current = 50
ConsumeOxygen(20)
-> 30
```

## 32.5 Instant depletion

```text
Current = 10
ConsumeOxygen(20)
-> 0
-> depletion event exactly once
```

## 32.6 Instant restoration

```text
Current = 50
RestoreOxygen(20)
-> 70
```

## 32.7 Restore clamp

```text
Current = 90
Max = 100
RestoreOxygen(20)
-> 100
```

## 32.8 Additive consumption modifier

Two modifiers coexist and are removed independently.

## 32.9 Multiplicative consumption modifier

Rate recomputes deterministically and oxygen is synchronized before the rate changes.

## 32.10 Consumption block stacking

```text
A blocks
B blocks
remove A -> still blocked
remove B -> resumes
```

## 32.11 Instant damage while blocked

Consumption block is active.

```text
ConsumeOxygen(10)
-> still consumes 10 instantly
```

## 32.12 Regeneration slower than drain

Net oxygen still decreases at the correct reduced rate.

## 32.13 Regeneration equal to drain

Oxygen remains stable.

## 32.14 Regeneration greater than drain

Oxygen increases and clamps at MaxOxygen.

## 32.15 Reset during pending depletion

Reset invalidates old timer/callback and stale depletion does not fire later.

## 32.16 Character exists before run starts

Clone is visible during Chrono Spawn selection but oxygen does not drain.

## 32.17 Synchronized run start

Player and all operational Clones begin oxygen progression at the same logical start barrier.

---

# 33. Required behavior scenarios — Player depletion

## 33.1 Player reaches zero naturally

```text
Player oxygen -> 0
-> one OxygenDepleted failure request
-> current recording discarded
-> no new Clone from failed run
-> same reset path as paradox
-> previous consolidated timelines preserved
-> failed run Chrono Spawn available again
-> oxygen-specific failure presentation available
```

## 33.2 Player instant depletion

Instant oxygen damage reaching zero produces the same failed-run result.

## 33.3 Duplicate depletion callbacks

Only one failed-run transition is accepted.

No duplicate reset/session starts.

## 33.4 Depletion during already-started failure/reset

Must not start a second failure pipeline.

---

# 34. Required behavior scenarios — Clone depletion

## 34.1 Clone reaches zero

```text
Clone oxygen -> 0
-> Clone dies
-> current Player run continues
-> other Clones continue
```

## 34.2 Active replay cancellation

Clone dies during a Replay action.

```text
running action cancelled/stopped safely
Replay does not continue
Replay Track remains immutable
```

## 34.3 GOAP/Goal execution cancellation

If GOAP exists, it stops for the dead Clone and does not resume until a lifecycle reset returns the Clone operational.

## 34.4 Ragdoll/physics collapse

Clone visibly collapses and eventually becomes passive/fixed without permanent unnecessary simulation.

## 34.5 Corpse is navigable

Pathfinding route may pass through the corpse's location exactly as it can pass through living temporal versions.

No GridWorld blocked/occupied navigation state is added.

## 34.6 Pawn overlaps corpse

Player walks through dead Clone body without being blocked.

Another Clone walks through dead Clone body without being blocked.

## 34.7 Corpse does not force nav rebuild

Death and corpse settling do not trigger navigation rebuild/recalculation merely because a corpse exists.

## 34.8 Corpse remains paradox target

```text
T0 alive
T1 dead corpse
T0 LoS mesh overlaps T1 corpse
0 < 1
-> paradox
```

## 34.9 Corpse is not observer

```text
T0 dead corpse
T1 alive
```

T0 no longer runs its active observer/vision behavior and does not create a paradox by actively seeing T1.

## 34.10 Temporal identity preserved

Death does not change T1 into an unindexed generic world prop.

Temporal Index and required relation identity remain queryable.

## 34.11 Reset after Clone death

Next timeline attempt reconstructs T1 alive, full oxygen, no corpse physics, correct Replay Track, correct Temporal Index, idle until synchronized start.

## 34.12 Stale corpse callback

A physics/timer/depletion callback from the previous attempt cannot affect the reconstructed Clone.

---

# 35. Required behavior scenarios — HUD integration

## 35.1 Initial binding through Owning Player

```text
Player Character owns OxygenComponent
HUD initializes with its normal Owning Player
-> UParadoxHUDWidget contains UParadoxOxigenWidget
-> UParadoxOxigenWidget resolves current Player Character/Pawn through Owning Player
-> widget resolves that Character's OxygenComponent
-> OxygenProgressBar Percent == GetNormalizedOxygen()
```

## 35.2 Instant consumption UI update

```text
Current Oxygen = 100 / 100
ConsumeOxygen(25)
-> gameplay Current Oxygen = 75
-> OxygenProgressBar updates immediately to 0.75
```

## 35.3 Instant restoration UI update

```text
Current Oxygen = 25 / 100
RestoreOxygen(25)
-> OxygenProgressBar updates immediately to 0.50
```

## 35.4 Continuous drain UI update

During an active run, the visible Player oxygen bar follows `GetNormalizedOxygen()` smoothly enough for normal gameplay presentation without requiring every Character Oxygen component to Tick solely for HUD purposes.

## 35.5 Tactical Pause UI

```text
Tactical Pause active
-> authoritative oxygen stops progressing
-> progress bar remains at the same normalized value
```

The widget must not fake continued depletion while simulation oxygen is paused.

## 35.6 Timeline reset / Player reconstruction

```text
old Player Character removed/replaced
new Player Character becomes authoritative
-> old OxygenComponent delegate bindings removed
-> new OxygenComponent bound exactly once
-> bar immediately reflects new run-start oxygen
```

No stale previous-run component may continue updating the bar.

## 35.7 Invalid MaxOxygen

If runtime/content error produces `MaxOxygen <= 0`, progress-bar percentage resolves safely to `0` and does not divide by zero or produce NaN.

## 35.8 Missing required BindWidget

A Blueprint oxygen widget missing the required `OxygenProgressBar` child is treated as invalid UI setup and is diagnosable through the project logging/validation conventions.

## 35.9 HUD teardown

Removing/destructing the HUD cleans long-lived delegate bindings and leaves no callbacks into a dead widget.


## 35.10 Manual Clone binding

```text
Clone T2 owns OxygenComponent
inspection UI code selects T2
-> UParadoxOxigenWidget.SetObservedOxygenComponent(T2.OxygenComponent)
-> previous observed component delegates removed
-> T2 component bound exactly once
-> OxygenProgressBar immediately reflects T2 normalized oxygen
```

The widget does not require T2 to be the Owning Player's Pawn.

## 35.11 Manual rebind between Clones

```text
widget manually observes T2
player selects T3
-> SetObservedOxygenComponent(T3.OxygenComponent)
-> T2 delegates removed
-> T3 delegates added exactly once
-> only T3 can update this widget
```

## 35.12 Manual mode survives Player changes

While manually observing a Clone, a Player possession/timeline reconstruction event must **not** silently switch the widget back to Player oxygen.

The widget remains in Manual component mode until explicitly told to return to Owning Player mode or given another manual component.

## 35.13 Return to Owning Player

```text
widget manually observes Clone T2
-> ReturnToOwningPlayer() / InitializeFromOwningPlayer()
-> T2 delegates removed
-> current Owning Player Character resolved
-> Player OxygenComponent bound
-> bar immediately reflects Player normalized oxygen
```

## 35.14 Manual target becomes invalid

If the manually observed Clone/component is destroyed or becomes invalid:

```text
widget must not dereference stale state
widget must stop receiving callbacks from the old component
presentation falls back to a documented safe empty/zero state
```

Do not silently switch to Owning Player mode unless that fallback behavior is explicitly chosen and documented by the existing UI architecture.

---

# 36. Collision acceptance tests

Explicitly validate collision-channel behavior for:

```text
Player <-> living Clone
Clone <-> living Clone
Player <-> dead Clone
living Clone <-> dead Clone
Dynamic LoS paradox mesh <-> dead Clone
```

Expected gameplay result:

```text
movement pairs -> non-blocking
pathfinding -> corpse ignored as obstacle
paradox LoS overlap -> corpse detected as target
```

Also validate that the corpse itself does not become a vision occluder when temporal Characters are configured not to block one another's cones.

---

# 37. Performance requirements

Expected idle cost should be near zero.

Do not:

```text
add permanent Tick to every temporal Character without proving it is required
poll every oxygen effect each frame
rebuild navigation when a Clone dies
scan the world for oxygen owners
scan the world for modifier sources
log oxygen every frame
broadcast unchanged oxygen/rate state repeatedly
keep corpse physics simulating forever
```

Prefer:

```text
event-driven modifier changes
source handles/tokens
analytical simulation-time integration
one depletion timer when required
no timer when oxygen is stable/full/depleted/not in active run
presentation-only sampling for smooth HUD
```

Instrument a meaningful non-trivial hot path with Unreal Insights only if the actual implementation warrants it.

Do not add profiling noise to trivial getters.

---

# 38. Forbidden shortcuts

Do not:

```text
store oxygen on PlayerController instead of Character
use one global oxygen pool for all temporal versions
record oxygen frame-by-frame into IntentReplay
create a second independent Player reset pipeline for oxygen failure
consolidate a failed oxygen run into a new Clone
make a dead Clone block GridWorld cells
make a dead Clone a navigation modifier
make a dead Clone block Pawns
force a navmesh/grid rebuild because of corpse death
remove Temporal Index from a dead Clone
remove corpse detectability from paradox LoS
keep dead Clone active as a vision observer
continue Replay after Clone death
continue GOAP/Goal execution after Clone death
implicitly resurrect from RestoreOxygen()
let one consumption blocker cancel all blockers
let one rate modifier overwrite unrelated modifiers
mutate modifier arrays directly from Blueprint
leave stale timers active across reset
use real/platform time for run oxygen
let oxygen drain during Chrono Spawn selection
hardcode HUD/widget references inside the Oxygen component
make the Oxygen component create/find UMG widgets
make `UParadoxHUDWidget` duplicate oxygen arithmetic that belongs in the component/widget
search the widget tree by name instead of using the required `BindWidget` contract
use a Blueprint UMG property binding as the authoritative oxygen source
keep `UParadoxOxigenWidget` bound to a stale Character/component after timeline reconstruction
hardcode failure text inside the Oxygen component
add a Skeletal Mesh/Animation Blueprint stack blindly without inspecting the current voxel Character implementation
use LogTemp in committed code
modify generic plugins with Paradox-specific oxygen rules
```

---

# 39. Suggested implementation order

After repository investigation:

1. identify the shared Player/Clone Character integration point;
2. define/adapt `UParadoxOxygenComponent` and core state invariants;
3. implement simulation-time synchronization and start/stop lifecycle;
4. implement instant consume/restore operations;
5. implement source-aware consumption modifiers;
6. implement source-aware consumption blocks;
7. implement continuous regeneration;
8. implement depletion exactly-once semantics;
9. integrate synchronized run start/stop/reset;
10. integrate Player depletion with the existing run-failure reason/pipeline;
11. integrate Clone depletion with authoritative Clone dead state;
12. cancel Replay/Actions/GOAP/active-agent systems through verified APIs;
13. implement ragdoll/physics-collapse presentation using the actual Character representation;
14. configure corpse collision so Pawns overlap/pass through and navigation remains unaffected;
15. preserve corpse temporal identity and paradox-target overlap behavior;
16. disable dead Clone observer behavior;
17. integrate reset/reconstruction back to operational Clone state;
18. integrate inventory/passive-effect cleanup as required by existing ownership rules;
19. expose the Oxygen component queries/delegates required by presentation;
20. implement `UParadoxOxigenWidget` with its required `UProgressBar` `meta=(BindWidget)` property and dual observation-source support (Owning Player resolution + manual `UParadoxOxygenComponent` binding);
21. integrate `UParadoxOxigenWidget` as a bound child of the existing `UParadoxHUDWidget`, using Owning Player mode as the standard Player HUD path;
22. implement safe rebinding for both observation modes: automatic Owning Player resolution for the Player HUD and explicit manual `UParadoxOxygenComponent` binding for Clone/entity inspection, updating the progress percentage immediately after every source change;
23. validate continuous drain presentation without making UI authoritative gameplay state;
24. add debug inspection;
25. add/update human-facing `Docs`, including HUD setup and required Blueprint widget names;
26. compile the affected target;
27. validate all scenarios in this specification;
28. review the final diff and remove unrelated changes.

---

# 40. Completion criteria

The task is complete only when all relevant conditions below are true.

## Oxygen resource

- Player and Clones use the same Character-owned oxygen component.
- Oxygen drains in simulation time only during an active run.
- Tactical Pause stops oxygen progression.
- Simulation speed changes affect oxygen progression consistently with the simulation.
- Instant consume/restore operations work and clamp safely.
- Multiple rate modifiers coexist safely.
- Multiple consumption blocks coexist safely.
- Continuous regeneration works and composes with consumption.
- Depletion fires exactly once per operational lifecycle.
- Reset invalidates all stale oxygen timers/callbacks.

## Player

- Player depletion uses the existing failed-run/reset path.
- Oxygen depletion has a distinct semantic failure reason.
- The failed run is not consolidated.
- No new Clone is created from the failed run.
- Previous consolidated timelines remain intact.
- The UI can show an oxygen-specific failure message without duplicating reset logic.

## Clone

- Clone depletion does not fail the current Player run.
- Clone Replay/Actions/GOAP/active-agent behavior stops safely.
- Clone enters the requested ragdoll/physics-collapse death presentation.
- Corpse eventually becomes passive/fixed.
- Corpse does not block Player or Clones.
- Corpse does not affect GridWorld/pathfinding/navigation.
- Corpse does not force nav rebuilds.
- Corpse preserves Temporal Index and temporal identity.
- Corpse remains a valid paradox target.
- Dead Clone no longer acts as an active observer.
- A living earlier Clone seeing a dead later Clone can still generate paradox through the normal Temporal Index rule.
- Reset reconstructs the Clone alive and operational with full run-start oxygen.

## HUD

- `UParadoxHUDWidget` contains/integrates one dedicated `UParadoxOxigenWidget`.
- `UParadoxOxigenWidget` owns a `UProgressBar` bound with `meta=(BindWidget)`.
- In default/Owning Player mode, the progress bar displays the normalized oxygen of the current Player Character's `UParadoxOxygenComponent`.
- The same `UParadoxOxigenWidget` can be manually initialized from code with a specific `UParadoxOxygenComponent`, including a Clone's component.
- Manual component binding has explicit precedence and is not overwritten by automatic Owning Player resolution until the widget is explicitly returned to Owning Player mode.
- Instant oxygen changes refresh the bar immediately.
- Continuous drain is visually updated through the existing HUD presentation-update path or a lightweight presentation-only widget sample.
- Timeline reconstruction/replacement rebinds Owning Player-mode widgets away from stale Player Character/component instances.
- Manual rebinds cleanly detach from the previous observed Character/component and attach exactly once to the new target.
- UI never becomes authoritative for oxygen state or depletion.
- Widget teardown removes any long-lived delegate bindings.

## Architecture

- Generic plugins contain no Paradox-specific oxygen/death rules.
- Oxygen is not recorded as frame-by-frame replay data.
- Component state is not mutated directly by HUD or Blueprint effects.
- Documentation is updated.
- The affected target compiles successfully.
- Final diff contains no unrelated changes.

If the affected target does not compile, the task is not finished.
