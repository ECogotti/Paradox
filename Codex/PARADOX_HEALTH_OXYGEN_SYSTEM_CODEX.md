# Paradox Health + Oxygen Systems — Codex Implementation Specification

## Purpose

Refactor the previously planned Oxygen System into **two separate gameplay systems** with explicit responsibilities:

```text
Health System
    = authoritative damage / healing / death system for temporal Characters

Oxygen System
    = independent timed resource
    = when oxygen reaches zero, it kills the Character through the Health System
```

This change is intentional because future gameplay may introduce additional damage or death sources besides oxygen.

The implementation must be split into **two milestones**:

```text
Milestone 1 — Health System
Milestone 2 — Oxygen System
```

Milestone 1 must compile and be usable independently before Milestone 2 is started.

The final architecture must support:

- Player and Clone health;
- generic future damage sources;
- healing;
- death;
- Player death causing the existing current-timeline failure/reset flow;
- Clone death producing the previously defined dead-body behavior;
- dead Clones remaining temporally relevant for paradox detection;
- dead Clone bodies remaining navigable / Pawn-overlappable and never becoming GridWorld/NavMesh blockers;
- reset/revival during the normal Paradox timeline lifecycle;
- Oxygen as a separate timed resource measured directly in **seconds**;
- oxygen depletion killing through Health rather than directly implementing death;
- native UI widget bases for Health and Oxygen;
- event-driven UI/gameplay hooks;
- future reuse of the widgets for selected Clones as well as the Player HUD.

This document supplements the repository root `AGENTS.md` and all local `CODEX` / `Docs` instructions.

The existing repository is the source of truth for actual APIs, type names, ownership and lifecycle.

---

# 0. Mandatory repository investigation

Before changing code, Codex must:

1. read the root `AGENTS.md`;
2. identify the actual Paradox runtime gameplay module;
3. search every affected module/plugin for relevant `CODEX` folders and read them;
4. read relevant `Docs`;
5. inspect the current Player and Clone Character classes and determine their common base, if one exists;
6. inspect how components are currently attached to Player and Clones;
7. inspect the current Player run-failure / paradox-failure pipeline;
8. inspect the current timeline reset/reconstruction lifecycle;
9. inspect the actual Temporal Index / temporal identity implementation;
10. inspect the paradox detection / dynamic Line-of-Sight implementation;
11. inspect how active Clones execute `IntentReplay`, Gameplay Actions, Goal/GOAP behavior and interaction;
12. inspect collision profiles used by Player and Clones;
13. inspect the current ragdoll implementation or closest existing Character ragdoll pattern;
14. inspect the current GridWorld integration and verify how temporal Characters are excluded from blocking navigation;
15. inspect the current `UParadoxHUDWidget` architecture;
16. inspect existing project widget base classes and UMG conventions;
17. inspect existing delegate/event patterns;
18. inspect existing reset/save/world-state participation patterns;
19. inspect TacticalPause / game-speed semantics;
20. inspect logging and debug conventions;
21. compile the appropriate target before implementation;
22. compile again after each milestone and after meaningful public-API changes.

Do not invent project APIs.

Do not redesign unrelated systems.

Use the smallest correct integration consistent with the repository.

---

# 1. High-level architecture

Required conceptual flow:

```text
                           ┌─────────────────────┐
Other future damage ─────>│                     │
Hazards ─────────────────>│  Health Component   │────> Death
Combat ──────────────────>│                     │        │
                          └─────────────────────┘        ├─ Player -> current timeline failure/reset
                                   ▲                    │
                                   │                    └─ Clone -> dead temporal body
                                   │
                          Oxygen depletion
                                   ▲
                                   │
                          ┌─────────────────────┐
                          │  Oxygen Component   │
                          │ timed resource only │
                          └─────────────────────┘
```

The responsibilities must remain separate.

## Health owns

```text
Current HP
Max HP
Damage
Healing
Alive / Dead state
Death transition
Revive/reset health state
Death-related gameplay notifications
```

## Oxygen owns

```text
Configured oxygen duration in seconds
Remaining oxygen time in seconds
Oxygen countdown progression
Consumption-speed modifiers
Consumption blockers
Optional restoration / removal of oxygen time
Oxygen depletion notification
Requesting lethal death through Health when depleted
```

## Oxygen does NOT own

```text
ragdoll
Player game over
timeline reset
Clone AI/replay shutdown
Temporal Index behavior
death state
health state
```

Those are consequences of **Health death**.

---

# 2. Module ownership

Both systems are currently **Paradox-specific gameplay systems** and should live in the existing Paradox runtime gameplay module unless repository inspection identifies an already-established generic stats/health module that is clearly the correct owner.

Suggested conceptual paths:

```text
Public/Health/ParadoxHealthComponent.h
Private/Health/ParadoxHealthComponent.cpp

Public/Oxygen/ParadoxOxygenComponent.h
Private/Oxygen/ParadoxOxygenComponent.cpp

Public/UI/ParadoxHealthWidget.h
Private/UI/ParadoxHealthWidget.cpp

Public/UI/ParadoxOxygenWidget.h
Private/UI/ParadoxOxygenWidget.cpp
```

Adapt paths to the real module structure.

Do not put Paradox-specific death consequences inside generic plugins such as:

```text
GridWorld
WorldState
EntityRelations
IntentReplay
GameplayActions
TacticalPause
PuzzleSystem
```

---

# MILESTONE 1 — HEALTH SYSTEM

# 3. Milestone 1 goal

Implement a complete Character health/death foundation that works **without the Oxygen System**.

After Milestone 1 it must already be possible for code or Blueprint to:

```text
Apply damage
Heal
Kill a Character
Observe health changes
Observe death
Reset/revive a Character through the intended lifecycle
Kill the Player and enter the existing run-failure flow
Kill a Clone and produce the required dead temporal body
Display health through a reusable Health widget base
```

Do not start Oxygen implementation until this milestone compiles and its relevant behavior has been validated.

---

# 4. `UParadoxHealthComponent`

Create or adapt one Actor Component equivalent to:

```text
UParadoxHealthComponent
```

It belongs to the Character.

Conceptual ownership:

```text
Temporal Character
└── UParadoxHealthComponent
```

The same implementation must work for:

```text
Player Character
Clone Character
```

The component must not encode "Player" and "Clone" through duplicated health implementations.

---

# 5. Health configuration and state

Required conceptual values:

```text
MaxHealth
CurrentHealth
bIsDead
```

Prefer:

```text
float MaxHealth
float CurrentHealth
```

unless the project already has a standard numeric/stat type.

Designer-facing `MaxHealth` must have safe validation.

Required invariants:

```text
MaxHealth > 0
0 <= CurrentHealth <= MaxHealth
bIsDead == true after accepted death transition
```

The component is the single authoritative owner of HP.

Do not duplicate current HP in:

```text
PlayerController
HUD
Character Blueprint variables
Oxygen component
Run manager
```

---

# 6. Damage API

Provide a controlled public API equivalent to:

```text
ApplyDamage(float Amount, optional damage context/source)
```

The exact signature should follow project conventions.

A lightweight future-proof damage context is acceptable if the repository already has a pattern for it.

Possible useful semantic information:

```text
Instigator / source Actor
Damage causer
Damage type / GameplayTag
Reason
```

Do not build a large damage framework before a concrete need exists.

The minimum required behavior is:

```text
Amount <= 0
    -> reject or no-op predictably

already Dead
    -> ordinary damage does nothing

valid damage
    -> clamp CurrentHealth
    -> notify health change
    -> notify damage received
    -> if CurrentHealth reaches 0:
           enter death exactly once
```

Repeated lethal calls must not execute death twice.

---

# 7. Healing API

Provide an operation equivalent to:

```text
Heal(float Amount)
```

Required behavior:

```text
Amount <= 0
    -> reject/no-op predictably

alive Character
    -> increase CurrentHealth up to MaxHealth
    -> notify healing / health change

dead Character
    -> ordinary Heal() does NOT revive
```

Death/revival must remain an explicit lifecycle transition.

Do not allow accidental resurrection because some generic healing volume overlaps a corpse.

---

# 8. Explicit Kill API

Provide an operation equivalent to:

```text
Kill(...)
```

This is important for systems such as Oxygen that need to cause death without manually calculating a giant damage value.

Required semantics:

```text
Kill
-> if alive:
      CurrentHealth = 0
      emit normal health-change semantics as appropriate
      enter normal death path
-> if already dead:
      no duplicate death transition
```

Oxygen depletion in Milestone 2 must use this or an equivalent authoritative Health API.

Do not let Oxygen invoke Clone ragdoll or Player reset directly.

---

# 9. Health events / delegates

Expose a useful native event set for gameplay and UI.

At minimum provide semantics equivalent to:

```text
OnHealthChanged
OnDamageTaken
OnHealed
OnDeath
OnRevived / OnHealthReset
```

`OnHealthChanged` should provide enough data for UI without requiring it to reconstruct state.

A useful conceptual payload is:

```text
OldHealth
NewHealth
MaxHealth
NormalizedHealth
```

`OnDamageTaken` may additionally expose:

```text
DamageAmount
Damage source/context when available
```

`OnDeath` must fire exactly once per life.

`OnRevived` / reset notification must fire when the component returns from the dead state through the explicit lifecycle.

Use dynamic multicast delegates where Blueprint/UMG observation needs them and native delegates/hooks where project conventions prefer them.

Do not broadcast high-frequency events when state has not actually changed.

---

# 10. Health queries

Provide safe read-only queries equivalent to:

```text
GetCurrentHealth()
GetMaxHealth()
GetNormalizedHealth()
IsDead()
IsAlive()
```

Normalized health semantics:

```text
CurrentHealth / MaxHealth
clamped to [0,1]
```

Handle invalid `MaxHealth` safely even though configuration validation should prevent it.

---

# 11. Health reset / revive

The Health System must support the timeline reset lifecycle.

Provide one explicit lifecycle operation equivalent to:

```text
ResetHealth()
```

or reuse the repository's reset interface if that is already the established architecture.

Required result:

```text
CurrentHealth = configured initial/full health
bIsDead = false
death guards reset
normal health events emitted in a deterministic documented order
Character can die again during the next run
```

Do not make arbitrary `Heal()` revive.

If the project has a more explicit `Revive()` plus `ResetHealth()`, use the smallest API that matches existing conventions.

---

# 12. Character death integration

The Health Component owns the **death transition**.

Project-specific Character/time-loop integration owns the **consequences**.

Conceptually:

```text
UParadoxHealthComponent
    -> OnDeath
       ↓
Temporal Character / ParadoxGameplay integration
       ↓
       determine Player vs Clone role
```

Do not put time-loop orchestration directly inside the reusable health state calculations.

---

# 13. Player death behavior

When the current Player Character dies:

```text
Health reaches zero / Kill()
    ↓
OnDeath
    ↓
Paradox run-failure authority
    ↓
Fail current timeline
    ↓
existing failure/reset pipeline
```

Death must use the **same underlying run-failure/reset path already used by paradox failure**, with a different reason.

Extend the existing failure-reason representation rather than creating another parallel reset pipeline.

Conceptually:

```text
EParadoxRunFailureReason
    Paradox
    PlayerDeath
```

If the repository already has a better equivalent type, extend that.

The Game Over / failure UI must receive the reason so it can display a death-specific message.

Do not duplicate:

```text
timeline cleanup
clone reconstruction
world reset
chrono spawn logic
run restart
```

---

# 14. Clone death behavior

When a Clone dies:

```text
Health death
    ↓
Clone enters dead state
```

A dead Clone:

```text
does NOT fail the current Player run
does NOT continue IntentReplay
does NOT continue Goal/GOAP behavior
does NOT execute Gameplay Actions
does NOT initiate normal interactions
does NOT continue ordinary movement
does NOT remain an active temporal observer
does retain its temporal identity / Temporal Index
does remain a valid temporal target for paradox detection
does become a passive physical corpse presentation
```

Cancel/disable the actual runtime systems using their real public APIs.

Do not merely set a boolean while those systems continue running underneath.

---

# 15. Clone ragdoll / corpse state

On Clone death, transition to ragdoll if supported by the current Character representation.

Conceptual sequence:

```text
Clone dies
    ↓
stop/cancel active gameplay behavior
    ↓
enter ragdoll / death physics
    ↓
optionally freeze/sleep the corpse once settled if appropriate
```

Use existing Character physics/collision patterns where possible.

The corpse is a passive world object, but it remains temporally identifiable.

---

# 16. DEAD CLONES MUST NOT BLOCK NAVIGATION

This is a hard requirement.

A dead Clone body must **not** become a GridWorld obstacle and must **not** carve/block NavMesh.

Forbidden behavior:

```text
register corpse as occupied GridWorld cell
make corpse cell non-navigable
add NavModifierVolume/component to corpse
force NavMesh rebuild because clone died
treat corpse as pathfinding obstacle
```

The corpse must remain navigable-through according to the same broad principle used for temporal Character overlap.

---

# 17. DEAD CLONES MUST OVERLAP PAWNS

This is also a hard requirement.

The dead body must not physically block Player or Clone Pawns.

Desired collision semantics:

```text
Corpse vs Pawn
    -> Overlap or Ignore
    -> never Block
```

Choose the exact collision response based on the repository's existing collision channels/profiles.

Do not globally disable all corpse collision if paradox LoS, floor collision or presentation still require specific channels.

The goal is specifically:

```text
Player and Clones can walk through / overlap the corpse
```

without the body becoming a movement or pathfinding obstruction.

---

# 18. Dead Clone temporal/paradox behavior

Death does not erase temporal identity.

A dead Clone must preserve:

```text
Temporal Index
temporal entity identity
whatever data EntityRelations/paradox policy needs to classify it
```

Required behavior:

```text
older living temporal entity sees later dead Clone
    -> normal temporal-order paradox rules still apply
```

Example:

```text
T0 alive sees corpse of T1
-> if ordinary T0 -> T1 visibility is paradoxical
-> paradox still triggers
```

However, the dead Clone itself is no longer an active observer.

Example:

```text
T0 corpse "sees" T1
-> no paradox produced from T0 as observer
```

Disable ordinary perception/LoS observation owned by the dead Clone while preserving the dead body as a valid target/entity for other observers.

Do not remove it from temporal registries merely because it died.

---

# 19. Clone reset after death

At timeline reset / reconstruction, the Clone must return to its normal operational state.

Required conceptual restoration:

```text
exit ragdoll
restore normal Character collision
restore normal movement state
restore health
restore replay/action/agent participation
restore normal temporal observation
restore transform/state according to existing reset/reconstruction architecture
```

Use the actual time-loop reconstruction path.

If Clones are destroyed and recreated instead of revived in place, do not force an unnecessary in-place revival architecture.

The important invariant is:

```text
new run -> reconstructed Clone is healthy and operational
```

---

# 20. `UParadoxHealthWidget`

Create a native reusable widget base equivalent to:

```text
UParadoxHealthWidget
```

Its job is to observe one `UParadoxHealthComponent` and expose presentation-ready health information/events.

It must **not** own authoritative HP.

---

# 21. Health widget observation sources

Support the same dual-source model intended for future selected-Clone UI.

## Default — Owning Player mode

Used by the normal Player HUD.

Conceptually:

```text
Widget Owning Player
    ↓
current possessed Player Character
    ↓
UParadoxHealthComponent
```

The widget should resolve and observe the current Player Character's Health Component using the actual project/UMG ownership pattern.

## Manual component mode

Allow code to explicitly initialize/rebind the widget to:

```text
UParadoxHealthComponent*
```

Conceptual API:

```text
SetObservedHealthComponent(UParadoxHealthComponent* InHealth)
ReturnToOwningPlayer()
```

Use project naming conventions.

Manual mode is intended for future UI such as:

```text
selected Clone information panel
```

Manual binding must not be silently overwritten by Player possession changes until explicitly returned to Owning Player mode.

Every rebind must:

```text
unbind previous delegates
store/validate new component
bind exactly once
refresh presentation immediately
```

---

# 22. Health widget — Resident Evil style presentation support

The intended visual direction is a **Resident Evil-style ECG / heartbeat health display**.

Codex does **not** need to author the final art or a sophisticated waveform renderer.

Codex must prepare the native logic so Blueprint/UMG can implement that presentation cleanly.

The widget should expose:

```text
CurrentHealth
MaxHealth
NormalizedHealth
Health display state
Alive/Dead state
```

Define a small presentation state enum equivalent to:

```text
EParadoxHealthDisplayState

Fine
Caution
Danger
Dead
```

Thresholds should be designer-configurable either on the widget or an appropriate UI config object.

Example conceptual thresholds:

```text
Fine    >= 0.60
Caution >= 0.30
Danger  > 0
Dead    == 0 / HealthComponent.IsDead()
```

Do not hard-code these sample numbers if project/design data should own them.

The enum is for UI presentation only.

It must not alter gameplay Health behavior.

---

# 23. Health waveform data

To support an ECG-style widget without forcing one rendering technique, expose presentation-ready values/events rather than hard-coding Slate drawing prematurely.

Provide Blueprint-readable/queryable values equivalent to:

```text
GetNormalizedHealth()
GetHealthDisplayState()
GetSuggestedHeartbeatRate()
GetSuggestedWaveAmplitude()
```

`SuggestedHeartbeatRate` and `SuggestedWaveAmplitude` are presentation parameters derived from health state/percentage.

Their exact mapping should be designer-tunable.

For example, low health may drive:

```text
faster heartbeat
stronger/more urgent waveform
```

but the native system should not assume final colors, images, materials or animations.

Prefer data/functions/hooks that a Widget Blueprint can use to animate:

```text
material parameters
a custom waveform widget later
UMG animation playback rate
image transforms
audio heartbeat timing
```

Do not build an unnecessarily complex real ECG simulation.

---

# 24. Health widget events

Expose Blueprint-friendly hooks/events equivalent to:

```text
OnObservedHealthComponentChanged
OnHealthDisplayUpdated
OnHealthDisplayStateChanged
OnDamageFeedbackRequested
OnHealFeedbackRequested
OnDeathFeedbackRequested
OnReviveFeedbackRequested
```

`OnHealthDisplayUpdated` should provide enough data to update the entire widget in one place, e.g.:

```text
CurrentHealth
MaxHealth
NormalizedHealth
DisplayState
```

Presentation hooks must not be required for gameplay correctness.

A Blueprint child with no overrides must not break the Health System.

---

# 25. Health widget and `UParadoxHUDWidget`

Integrate the health widget into the existing HUD architecture in the smallest way consistent with the repository.

Conceptually:

```text
UParadoxHUDWidget
└── UParadoxHealthWidget
```

If existing HUD child widgets use:

```cpp
UPROPERTY(meta=(BindWidget))
```

follow that convention.

Do not require a specific visual sub-widget such as `UProgressBar` for Health: the intended presentation is an ECG/heartbeat display.

The native widget is the logic bridge; Blueprint owns final presentation.

---

# 26. Health debug support

Expose useful health debug state through the module's existing debug conventions:

```text
Actor
CurrentHealth / MaxHealth
NormalizedHealth
Alive / Dead
last damage amount/source when practical
```

Do not spam logs every frame.

Log meaningful state transitions:

```text
death
revive/reset
invalid damage request where useful
```

---

# 27. Milestone 1 validation scenarios

Validate at least:

### 27.1 Basic damage

```text
100 HP
ApplyDamage(25)
-> 75 HP
-> OnDamageTaken
-> OnHealthChanged
```

### 27.2 Lethal damage

```text
25 HP
ApplyDamage(30)
-> 0 HP
-> one death transition
```

### 27.3 Repeated lethal calls

After death:

```text
ApplyDamage(...)
Kill()
```

must not emit duplicate death.

### 27.4 Healing

```text
50 / 100
Heal(30)
-> 80
```

### 27.5 Healing clamp

```text
90 / 100
Heal(50)
-> 100
```

### 27.6 Heal does not revive

Dead Character:

```text
Heal(100)
-> remains dead
```

### 27.7 Player death

```text
Player Health -> 0
-> existing timeline failure/reset pipeline
-> failure reason = PlayerDeath or project equivalent
```

### 27.8 Clone death

```text
Clone Health -> 0
-> run continues
-> replay/actions stop
-> Clone ragdolls
```

### 27.9 Corpse Pawn overlap

Player and other Clones can cross/overlap corpse without being blocked.

### 27.10 Corpse navigation

Clone death causes no GridWorld occupancy and no NavMesh blocking/rebuild requirement.

### 27.11 Corpse paradox target

Older live temporal entity can still see the later corpse and trigger the normal paradox rule.

### 27.12 Corpse is not observer

Dead Clone no longer produces paradox because of what it would have seen.

### 27.13 Reset/reconstruction

Next run restores/reconstructs a healthy operational Clone.

### 27.14 Health widget Player mode

Widget resolves Player Health from Owning Player and updates immediately.

### 27.15 Health widget manual mode

Widget can observe Clone T2 Health via `SetObservedHealthComponent`.

### 27.16 Health UI state

Crossing designer health thresholds produces expected `Fine/Caution/Danger/Dead` presentation state changes.

---

# 28. Milestone 1 Definition of Done

Milestone 1 is complete only when:

- `UParadoxHealthComponent` exists and owns HP;
- damage/heal/kill APIs work;
- dead state is authoritative and transition-safe;
- Player death uses the existing timeline failure/reset flow;
- Clone death produces the required passive corpse state;
- corpse does not block Pawn movement;
- corpse does not block GridWorld/NavMesh;
- corpse remains a valid temporal paradox target;
- corpse is no longer an active observer;
- reset/reconstruction restores healthy operational Clones;
- `UParadoxHealthWidget` exists;
- Health widget supports Owning Player and manual component binding;
- Health widget exposes presentation data suitable for ECG/heartbeat presentation;
- Health widget exposes useful update/feedback events;
- documentation is updated;
- affected target compiles successfully;
- relevant behavior is validated.

Only after this milestone is stable should Codex continue to Milestone 2.

---

# MILESTONE 2 — OXYGEN SYSTEM

# 29. Milestone 2 goal

Implement Oxygen as a **parallel timed resource** that depends on the completed Health System only for its lethal consequence.

Oxygen must not recreate Health/death logic.

Required conceptual dependency:

```text
UParadoxOxygenComponent
    ↓ on depletion
UParadoxHealthComponent::Kill(...)
```

or the verified project-equivalent Health API.

---

# 30. Oxygen tuning is expressed directly in seconds

This is a mandatory design change.

Designers must tune oxygen primarily as **time remaining**, not as arbitrary oxygen points.

Use a designer-facing value equivalent to:

```text
OxygenDurationSeconds
```

Example:

```text
OxygenDurationSeconds = 180
```

means:

```text
the Character has approximately 180 seconds of oxygen
at normal x1 consumption
```

Runtime state should expose an equivalent:

```text
RemainingOxygenSeconds
```

The system may also expose:

```text
NormalizedOxygen
```

for future UI convenience, but normalized percentage is **derived**:

```text
RemainingOxygenSeconds / OxygenDurationSeconds
```

It is not the primary tuning model.

This allows future UI to choose freely between:

```text
03:00 countdown
180 s
progress bar
circular timer
no visible oxygen UI
```

without changing gameplay tuning.

---

# 31. Oxygen component

Create or adapt:

```text
UParadoxOxygenComponent
```

Conceptual Character ownership:

```text
Temporal Character
├── UParadoxHealthComponent
└── UParadoxOxygenComponent
```

Required conceptual state/configuration:

```text
OxygenDurationSeconds
RemainingOxygenSeconds

BaseConsumptionSpeed = 1.0
EffectiveConsumptionSpeed

bConsumptionBlocked
bIsDepleted
```

At normal operation:

```text
1 real simulation second at x1
-> RemainingOxygenSeconds decreases by approximately 1 second
```

The component should normally initialize:

```text
RemainingOxygenSeconds = OxygenDurationSeconds
```

at run/life reset.

---

# 32. Oxygen uses simulation time

Oxygen is a run timer inside the gameplay simulation.

Expected behavior:

```text
Tactical Pause -> countdown stops
x1             -> normal countdown
x1.5           -> oxygen countdown advances 1.5x relative to wall-clock time
x2             -> 2x
x3             -> 3x
```

Codex must verify actual timer/time-dilation behavior in the engine/project.

Do not use platform wall-clock time.

Avoid permanent Character Tick when a timer/analytical solution is sufficient.

A good architecture is:

```text
Remaining seconds materialized at last synchronization
last simulation-time sample
effective consumption-speed multiplier
predicted depletion time
depletion timer
```

When modifiers or direct oxygen operations occur:

```text
synchronize elapsed oxygen time
apply operation
recompute effective speed
reschedule depletion
```

The exact implementation may differ if repository APIs make another approach safer.

---

# 33. Oxygen consumption-speed modifiers

Effects may increase or decrease how quickly oxygen time runs out.

Because designers think in seconds, modifiers should conceptually alter **countdown speed**, not an arbitrary "oxygen units per second" value.

Examples:

```text
Base speed = 1.0

x2 modifier
-> 60 seconds remaining are consumed in ~30 simulation seconds

x0.5 modifier
-> 60 seconds remaining last ~120 simulation seconds
```

Support source-aware modifiers so overlapping effects do not overwrite one another.

Use a handle/source pattern consistent with the repository.

At minimum support multiplicative modification.

Additive modification may be supported only if it has clear semantics and does not make designer behavior confusing.

Never allow effective countdown speed to become negative and silently regenerate oxygen unless regeneration is an explicit supported operation.

---

# 34. Oxygen consumption blockers

Support overlapping source-aware blockers.

Conceptual API:

```text
AddConsumptionBlock(Source) -> Handle
RemoveConsumptionBlock(Handle)
```

Semantics:

```text
one or more active blockers
-> RemainingOxygenSeconds does not decrease
```

Example:

```text
Block A active
Block B active
remove A
-> still blocked by B
remove B
-> countdown resumes
```

Do not model this as one global boolean that unrelated systems overwrite.

---

# 35. Direct oxygen-time operations

Expose operations expressed in **seconds**.

Examples:

```text
ConsumeOxygenSeconds(float Seconds)
RestoreOxygenSeconds(float Seconds)
SetRemainingOxygenSeconds(float Seconds)
RefillOxygen()
```

Optional percentage helpers are acceptable:

```text
ConsumeOxygenPercent(float Fraction)
RestoreOxygenPercent(float Fraction)
```

but seconds remain the primary API/tuning language.

Required invariants:

```text
0 <= RemainingOxygenSeconds <= OxygenDurationSeconds
```

Do not let ordinary restoration after accepted depletion resurrect a Character.

Death/revival remains a Health/lifecycle responsibility.

---

# 36. Optional continuous oxygen regeneration

If retained from the previous specification, represent it coherently in time terms.

For example:

```text
OxygenRestorationSecondsPerSimulationSecond
```

or another clear designer-facing naming.

However, do not overcomplicate Milestone 2 if no current gameplay requires continuous regeneration.

The required initial capabilities are:

```text
normal countdown
direct consume
direct restore
speed modifiers
consumption blockers
depletion
reset
```

Continuous regeneration may be implemented if it is already part of the intended immediate design, otherwise document it as a straightforward extension point.

---

# 37. Oxygen depletion

When:

```text
RemainingOxygenSeconds <= 0
```

the Oxygen Component must:

```text
clamp remaining time to 0
mark depletion exactly once
stop/suspend further oxygen progression
broadcast oxygen depletion
request lethal death from the same Character's Health Component
```

Conceptually:

```text
OnOxygenDepleted
    ↓
HealthComponent->Kill(OxygenDepletionReason)
    ↓
normal Health death pipeline
```

This means:

```text
Player oxygen depleted
-> Health kills Player
-> Health death integration triggers normal Player failure/reset

Clone oxygen depleted
-> Health kills Clone
-> Health death integration triggers Clone corpse behavior
```

Oxygen must not branch on Player vs Clone itself.

That is one of the core reasons for this refactor.

---

# 38. Health dependency handling

At controlled initialization, Oxygen must resolve/validate the Character's Health Component.

Do not repeatedly search every frame.

If a valid Health Component is mandatory for temporal Characters, treat absence as a configuration/developer error according to project conventions.

Do not silently implement an alternate death path if Health is missing.

One authoritative death system must exist.

---

# 39. Oxygen reset

At normal timeline/life reset:

```text
RemainingOxygenSeconds = OxygenDurationSeconds
bIsDepleted = false
remove/reset transient runtime modifiers according to ownership semantics
clear stale timers/callbacks
recalculate effective consumption speed
resume normal countdown only when the run lifecycle says simulation is active
```

Codex must inspect whether runtime effects/modifiers are reconstructed naturally with the Character or need explicit cleanup.

Do not allow stale modifier handles from the previous life/run to affect the next run.

---

# 40. Oxygen events / delegates

Expose a UI/gameplay-friendly event set.

At minimum provide semantics equivalent to:

```text
OnOxygenChanged
OnOxygenDepleted
OnOxygenReset
OnConsumptionSpeedChanged
OnConsumptionBlockedChanged
```

Because oxygen continuously decreases, avoid broadcasting a delegate every frame by default.

Provide useful discrete updates for countdown UI.

Recommended additional event:

```text
OnWholeSecondChanged
```

Conceptual payload:

```text
WholeSecondsRemaining
RemainingSeconds
NormalizedOxygen
```

This allows a simple countdown:

```text
03:00
02:59
02:58
```

without forcing UMG to poll every frame.

If the implementation uses analytical time/depletion timers, schedule lightweight second-boundary updates only while something is actually observing/needed if the project architecture makes that worthwhile.

Alternatively, the widget may own a low-frequency presentation timer while bound.

Choose the smallest clean design after inspecting existing UI patterns.

The core gameplay depletion logic must remain accurate independently of UI update frequency.

---

# 41. Oxygen queries

Expose:

```text
GetOxygenDurationSeconds()
GetRemainingOxygenSeconds()
GetNormalizedOxygen()
GetWholeSecondsRemaining()
IsOxygenDepleted()
IsConsumptionBlocked()
GetEffectiveConsumptionSpeed()
```

Optional convenience formatting should generally stay in UI rather than gameplay component.

---

# 42. `UParadoxOxygenWidget`

Create/retain a reusable native widget base equivalent to:

```text
UParadoxOxygenWidget
```

Important: **do not require a `UProgressBar` with `BindWidget` anymore.**

The final Oxygen presentation is intentionally undecided.

The same native widget logic must support future Blueprint presentations such as:

```text
countdown text
progress bar
circular progress
icons
warning animation
```

The native widget should therefore be presentation-agnostic.

---

# 43. Oxygen widget observation sources

Keep the previously requested dual-source behavior.

## Default — Owning Player mode

Normal Player HUD path:

```text
Widget Owning Player
    ↓
current Player Character
    ↓
UParadoxOxygenComponent
```

## Manual component mode

Allow code to bind a specific:

```text
UParadoxOxygenComponent*
```

for future selected-Clone UI.

Conceptual API:

```text
SetObservedOxygenComponent(UParadoxOxygenComponent* InOxygen)
ReturnToOwningPlayer()
```

Manual mode must not be overwritten automatically by Player possession/reconstruction.

Rebinding must clean old delegates and bind new delegates exactly once.

---

# 44. Oxygen widget presentation data

Expose Blueprint-ready values:

```text
RemainingOxygenSeconds
WholeSecondsRemaining
OxygenDurationSeconds
NormalizedOxygen
IsConsumptionBlocked
EffectiveConsumptionSpeed
IsDepleted
```

Provide convenience query/formatting support for countdown presentation, for example:

```text
GetMinutesRemaining()
GetSecondsRemainder()
```

or one formatting helper if consistent with project UI conventions.

Do not hard-code final text style or visual layout.

---

# 45. Oxygen widget events

Expose Blueprint-friendly hooks/events equivalent to:

```text
OnObservedOxygenComponentChanged
OnOxygenDisplayUpdated
OnOxygenWholeSecondChanged
OnOxygenWarningStateChanged
OnOxygenDepletedFeedbackRequested
OnOxygenResetFeedbackRequested
```

`OnOxygenDisplayUpdated` should give Blueprint enough data to implement either a progress bar or countdown.

A useful conceptual payload:

```text
RemainingSeconds
DurationSeconds
NormalizedOxygen
WholeSecondsRemaining
```

---

# 46. Optional Oxygen warning states

To help presentation without dictating it, the widget may expose a small UI-only warning state:

```text
Normal
Low
Critical
Depleted
```

Thresholds should be designer-configurable in **remaining seconds and/or normalized fraction**, whichever best matches existing UI data patterns.

Because Oxygen is fundamentally tuned in seconds, seconds-based warning thresholds are especially useful:

```text
Low at <= 30 seconds
Critical at <= 10 seconds
```

These are examples only.

Do not make these warning states gameplay-authoritative.

---

# 47. `UParadoxHUDWidget` integration

Integrate the two widgets with the existing gameplay HUD using existing conventions.

Conceptually:

```text
UParadoxHUDWidget
├── UParadoxHealthWidget
└── UParadoxOxygenWidget
```

Both default to Owning Player observation mode in the gameplay HUD.

Both remain reusable independently in future selected-Clone information UI by manual component binding.

Do not make `UParadoxHUDWidget` duplicate resource calculations.

Its responsibility is composition and normal Player-HUD lifecycle.

---

# 48. UI lifecycle rules

Both Health and Oxygen widgets must safely handle:

```text
widget created before Player Character is available
Player Character replaced after timeline reset
manual bind to Clone
manual rebind from Clone T2 to T3
manual target destruction
return from manual mode to Owning Player mode
widget removal
world teardown
```

Common rule:

```text
unbind previous observed component
bind new component exactly once
refresh presentation immediately
```

Do not leave stale delegate bindings.

Use GC-safe UObject references consistent with Unreal ownership.

---

# 49. Milestone 2 validation scenarios

Validate at least:

### 49.1 Designer duration

```text
OxygenDurationSeconds = 180
normal x1 consumption
-> depletion occurs after ~180 seconds of active simulation time
```

within expected timer precision.

### 49.2 Tactical pause

While paused:

```text
RemainingOxygenSeconds does not decrease
```

### 49.3 Accelerated simulation

At x2 simulation speed:

```text
60 oxygen seconds
-> approximately 30 wall-clock seconds until depletion
```

assuming TacticalPause implements speed through normal game-time dilation as verified.

### 49.4 Direct consume

```text
120 s remaining
ConsumeOxygenSeconds(20)
-> 100 s
```

### 49.5 Direct restore

```text
100 / 180
RestoreOxygenSeconds(30)
-> 130
```

### 49.6 Restore clamp

Cannot exceed configured duration/capacity.

### 49.7 Consumption-speed modifier

```text
speed x2
-> countdown drains twice as fast in simulation terms
```

### 49.8 Multiple blockers

Two blockers active; removing one does not resume countdown until the second is removed.

### 49.9 Player oxygen depletion

```text
Oxygen -> 0
-> Oxygen calls Health Kill
-> Health Player death integration
-> current timeline fails through existing pipeline
```

Oxygen does not directly call the run manager.

### 49.10 Clone oxygen depletion

```text
Clone Oxygen -> 0
-> Oxygen calls Health Kill
-> Health Clone death path
-> passive ragdoll corpse
-> no Player run failure
```

### 49.11 Oxygen reset

New run/restored Character starts with full configured duration and no stale depletion callback.

### 49.12 Widget Player mode

`UParadoxOxygenWidget` resolves the Player Oxygen Component through Owning Player.

### 49.13 Widget manual Clone mode

Widget can observe Clone T2 Oxygen Component without changing widget class.

### 49.14 Countdown update

Whole-second event/update can drive:

```text
03:00 -> 02:59 -> 02:58
```

without gameplay correctness depending on per-frame UI polling.

### 49.15 Progress representation remains possible

`GetNormalizedOxygen()` continues to provide:

```text
RemainingSeconds / DurationSeconds
```

so a future Blueprint can use a progress bar without gameplay refactoring.

---

# 50. Milestone 2 Definition of Done

Milestone 2 is complete only when:

- Oxygen exists as a separate Character component;
- its primary designer tuning is expressed in seconds;
- remaining oxygen is represented/queryable in seconds;
- normal consumption respects active simulation time;
- consumption speed can be modified safely;
- consumption can be blocked by multiple independent sources;
- direct consume/restore operations use seconds;
- depletion happens exactly once;
- depletion kills through Health;
- Oxygen contains no separate Player-vs-Clone death implementation;
- reset clears stale oxygen state/timers/effects;
- `UParadoxOxygenWidget` is presentation-agnostic;
- Oxygen widget supports Owning Player and manual component binding;
- Oxygen widget exposes countdown-ready and percentage-ready data;
- useful UI update events exist without requiring per-frame gameplay broadcasts;
- HUD integration exists;
- documentation is updated;
- affected target compiles successfully;
- relevant runtime scenarios are validated.

---

# 51. Recommended implementation order

Follow this order unless repository architecture gives a strong reason to adjust it.

## Milestone 1

```text
1. investigate Character / reset / failure / paradox architecture
2. create UParadoxHealthComponent
3. implement damage / heal / kill / queries
4. implement death transition and events
5. integrate Player death with existing failure pipeline
6. integrate Clone death
7. implement ragdoll/passive corpse transition
8. enforce Pawn-overlap corpse collision
9. verify corpse does not affect GridWorld/NavMesh
10. preserve corpse temporal target identity
11. disable corpse observer behavior
12. integrate reset/reconstruction
13. create UParadoxHealthWidget
14. implement Owning Player + manual component observation
15. expose ECG/heartbeat presentation state/data/events
16. integrate Health widget with UParadoxHUDWidget
17. tests/validation
18. documentation
19. compile and review diff
```

## Milestone 2

```text
1. create/refactor UParadoxOxygenComponent
2. replace arbitrary oxygen-unit tuning with seconds-based duration
3. implement simulation-time countdown
4. implement direct seconds consume/restore
5. implement source-aware countdown-speed modifiers
6. implement source-aware blockers
7. add depletion event
8. integrate depletion -> Health Kill
9. integrate reset
10. create/refactor UParadoxOxygenWidget as presentation-agnostic
11. implement Owning Player + manual component observation
12. expose countdown/normalized presentation data and events
13. integrate Oxygen widget with UParadoxHUDWidget
14. tests/validation
15. documentation
16. compile and review diff
```

---

# 52. Forbidden shortcuts

Do not:

```text
make Oxygen the owner of death consequences
duplicate Player death logic inside Oxygen
duplicate Clone corpse logic inside Oxygen
make Oxygen directly reset the timeline
use "999999 damage" instead of an explicit Health Kill path
let ordinary Heal revive dead Characters
let oxygen restoration revive dead Characters
create separate Health implementations for Player and Clone
create separate Oxygen implementations for Player and Clone
store HP in the HUD
store oxygen state in the HUD
make corpse block Pawns
make corpse occupy GridWorld cells
make corpse carve/block NavMesh
remove dead Clone temporal identity
leave dead Clone as an active paradox observer
require a ProgressBar for the Oxygen widget
hard-code ECG art/rendering into the Health gameplay component
broadcast oxygen gameplay updates every frame solely for UI
use permanent Tick by default when timers/events are sufficient
leave delegate bindings on old Player/Clone components
allow manual widget observation to be silently overwritten by Owning Player mode
create a second run-reset pipeline for Player death
```

---

# 53. Documentation requirements

Update/create user-facing documentation for both systems.

Suggested structure:

```text
Docs/HEALTH_SYSTEM.md
Docs/OXYGEN_SYSTEM.md
```

Health documentation should cover:

```text
purpose
component setup
damage/heal/kill API
death semantics
Player death
Clone death
corpse collision/navigation behavior
temporal paradox behavior
reset/revive
Health widget usage
Owning Player vs manual widget binding
ECG presentation hooks
debugging
```

Oxygen documentation should cover:

```text
purpose
seconds-based tuning
simulation-time behavior
consume/restore operations
speed modifiers
blockers
depletion -> Health dependency
reset
Oxygen widget usage
Owning Player vs manual widget binding
countdown/progress presentation options
debugging
```

---

# 54. Final architectural invariant

The final design must make this statement true:

```text
Health answers:
"Is this Character alive, how much HP does it have, and what happens when it dies?"

Oxygen answers:
"How much breathable time does this Character have left?"

When Oxygen reaches zero:
Oxygen does not invent a new death path.
It asks Health to kill the Character.

Everything that follows from death is owned by Health / Paradox death integration.
```

This separation is required so future systems can independently damage or kill temporal Characters without coupling themselves to Oxygen.
