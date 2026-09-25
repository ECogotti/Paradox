# Paradox Health System

## Ownership and native damage contract

Every `AParadoxCharacter` owns one `UParadoxHealthComponent`, so the same implementation is used by
the Player and all Clones. Access it with `GetHealthComponent()`.

Damage must enter through Unreal's native Actor damage pipeline:

- `UGameplayStatics::ApplyDamage`;
- `UGameplayStatics::ApplyPointDamage`;
- `UGameplayStatics::ApplyRadialDamage`;
- `AActor::TakeDamage`.

The component observes its owning Character's `OnTakeAnyDamage` delegate. There is intentionally no
project-specific `ApplyDamage` function and no damage-cause tag. Add future damage classifications
as `UDamageType` subclasses and pass the native instigator Controller and damage-causing Actor.

`AParadoxCharacter::TakeDamage` calls the inherited implementation and returns the HP actually
removed. Damage is clamped to the remaining health: requesting 30 damage from a Character with 25
HP returns 25. `SetCanBeDamaged(false)` remains the native invulnerability gate.

## Health state and operations

`MaxHealth` defaults to 100 and is editable on the inherited component. Runtime state is available
through `GetCurrentHealth`, `GetMaxHealth`, `GetNormalizedHealth`, `IsAlive`, and `IsDead`.

- `Heal(Amount)` restores only a living Character and returns the amount actually restored. It
  clamps at `MaxHealth` and never revives.
- `Kill(Instigator, Causer, DamageType)` submits exactly the remaining HP through
  `UGameplayStatics::ApplyDamage`. It does not edit health or death consequences directly.
- `ResetHealth()` restores `MaxHealth`, clears death, and is the only Health operation that revives.

Zero or negative damage, repeated damage after death, repeated `Kill`, and healing after death are
no-ops. Event order is stable and state is already committed before observers run:

- damage: `OnDamageTaken`, `OnHealthChanged`, then `OnDeath` when lethal;
- healing: `OnHealed`, then `OnHealthChanged`;
- reset: `OnHealthChanged` when the value changed, `OnHealthReset`, then `OnRevived` when the prior
  state was dead.

`OnDamageTaken` reports the amount actually removed. `OnDeath` preserves the native
`UDamageType`, instigator Controller, and damage causer.

Oxygen depletion is classified by `UParadoxOxygenDepletionDamageType` and calls `Kill`; it does not
introduce a parallel damage or death system. See [Paradox Oxygen System](OXYGEN_SYSTEM.md).

## Player death and run recovery

Player death during `ActiveRun` is a recoverable run failure. The time loop emits an
`FParadoxRunFailureContext` with reason `EParadoxRunFailureReason::PlayerDeath` and the native damage
context, stops the unconsolidated run, presents `LIFE SIGNS LOST`, restores the World State
baseline, reconstructs existing Clones, releases the failed Chrono Spawn, and returns to
`ChronoSpawnSelection`.

Temporal paradoxes use the same generic failure/recovery path with reason `TemporalParadox`.
`OnParadoxAccepted`, `ContinueParadoxRecovery`, and `OnParadoxRecoveryCompleted` remain compatibility
wrappers for paradox-specific consumers. New presentation code can use `OnRunFailureAccepted`,
`ContinueRunFailureRecovery`, `OnRunFailureRecoveryCompleted`, and `GetLastRunFailureContext`.

The persistent Player always calls `ResetHealth` when a new run activates. In legacy `PerPawn`
Oxygen mode it also calls `ResetOxygen`, and reconstructed Clones begin full. In `SharedGlobal`
mode the World reservoir instead persists across successful Time Travel and is restored from the
failed run's checkpoint; see [Paradox Oxygen System](OXYGEN_SYSTEM.md).

## Clone death

A dead Clone remains a temporal entity and keeps its `EntityIdentity`, Temporal Index, and temporal
target registration. It is no longer an observer and cannot start new behavior. Death idempotently:

- stops Intent Replay, observation comparison, investigation, Behavior Tree, and the behavior
  coordinator;
- aborts Gameplay Actions, movement, traffic reservations, and GridWorld occupancy;
- disables temporal observation and semantic perception listening;
- enables ragdoll while preventing the mesh and capsule from blocking Pawns or affecting NavMesh.

The capsule remains a Pawn query target for temporal detection. Normal time-loop reset still
destroys and reconstructs Clones; there is no in-place Clone revival path.

## Health widget and HUD integration

`UParadoxHealthWidget` observes only an explicitly supplied component:

```cpp
HealthWidget->SetObservedHealthComponent(Character->GetHealthComponent());
HealthWidget->ClearObservedHealthComponent();
```

It never searches `GetOwningPlayer`, `GetOwningPlayerPawn`, or an owning Controller. This makes the
same widget usable for the possessed Player and for a future selected-Clone panel. Rebinding first
disconnects the old source, stores weak references, connects once, and immediately refreshes the
cached presentation. Destroying the observed owner clears the source.

The Gameplay HUD coordinator explicitly binds its embedded Health widget whenever it creates the
HUD or the possessed Pawn changes. Add a `UParadoxHealthWidget` anywhere below the authored root to
opt into this presentation; the root never creates one automatically. The Status visibility setting
targets the discovered Health and Oxygen descendants directly.

Presentation states are `Fine`, `Caution`, `Danger`, and `Dead`. `FineThreshold` defaults to 0.60
and `CautionThreshold` to 0.30. Blueprint layouts can use the display queries plus the source,
display, state, death-feedback, and reset-feedback events. The native class creates no visual tree
and declares no `BindWidget` fields: progress bars, text, icons, animations, and ECG styling belong
entirely to the derived Widget Blueprint.

## Verification

Run the focused suite after building `ParadoxEditor`:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -NoSound -ExecCmds="Automation RunTests Paradox.Health; Quit" -TestExit="Automation Test Queue Empty" -log
```

The suite covers native generic/point/radial damage, actual-damage return values, damage gates,
death idempotence, healing/reset/revival, `Kill`, explicit widget rebinding and destroyed sources,
and the passive temporal-corpse contract for Clones.
