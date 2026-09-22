# Paradox Oxygen System

## Ownership and tuning

Every `AParadoxCharacter` owns one `UParadoxOxygenComponent`, so Player and Clones use the same
resource and depletion path. Access it with `GetOxygenComponent()`.

`OxygenDurationSeconds` is the designer-facing capacity and defaults to `180`. It means three
minutes at the default `BaseConsumptionSpeed` of `1.0`; Oxygen is not expressed in arbitrary
points. Runtime queries provide remaining seconds, whole seconds, normalized Oxygen, effective
speed, blocked state, and depletion state.

The component starts full but inactive. The time loop authorizes consumption only during
`ActiveRun`. The persistent Player is reset when a new run is activated; reconstructed Clones are
new instances and also start full. Leaving `ActiveRun`, retiring a Clone, or dying stops its Oxygen
timers.

## Simulation-time countdown

Oxygen uses `UWorld::GetTimeSeconds()`, which is paused by Unreal gameplay pause and includes global
time dilation. Therefore Tactical Pause stops the countdown without a separate Oxygen pause mode,
while x1.5, x2, and x3 simulation speeds accelerate it consistently with the rest of gameplay.

The component does not Tick. It keeps a materialized remaining value and the last simulation-time
sample, then uses one-shot timers for the predicted depletion and next whole-second boundary.
Queries project the current value analytically. Every direct operation, modifier change, blocker
change, or lifecycle transition synchronizes elapsed time and reschedules those timers.

## Direct operations

All public resource operations use seconds:

- `ConsumeOxygenSeconds` and `RestoreOxygenSeconds` return the amount actually changed;
- `SetRemainingOxygenSeconds` assigns a clamped value;
- `RefillOxygen` restores the configured duration;
- `ResetOxygen` starts a fresh resource lifetime and clears all transient effects.

Values always remain between zero and `OxygenDurationSeconds`. Non-finite and invalid amounts are
rejected or treated as no-ops as appropriate. Once depletion is accepted, consume, restore, set,
and refill are inert until `ResetOxygen`. Resetting Oxygen does not reset or revive Health.

## Speed modifiers and blockers

`AddConsumptionSpeedModifier(Source, Multiplier)` returns an
`FParadoxOxygenSpeedModifierHandle`. Effective speed is the base speed multiplied by every active
modifier. Multipliers must be finite and strictly positive; use a blocker rather than a zero-speed
modifier.

`AddConsumptionBlock(Source)` returns an `FParadoxOxygenBlockHandle`. Any active blocker suspends
regular countdown. Removing one of two blockers leaves the other authoritative.

The effect owner must retain and remove its exact handle. Multiple effects from the same source can
coexist and do not overwrite one another. Handles are transient: `ResetOxygen` clears both maps and
invalidates all handles from the previous run.

Continuous regeneration is intentionally not part of this milestone. Model immediate recovery
with `RestoreOxygenSeconds`.

## Oxygen Canister

`AParadoxOxygenCanister` is a normal single-slot pickupable consumable. Its designer-facing
`OxygenRestoreSeconds` is measured in seconds and defaults to `30`. Create a Blueprint child only
to author mesh, materials, icon, sound, VFX, or a different valid restore amount; the native class
is placeable and already exposes the generic Inventory `Use` action.

Pickup, Drop, and Swap never change Oxygen. Use is enabled only while the authoritative holder is
alive, the Oxygen component exists and is not depleted, the configured amount is finite and
positive, and remaining Oxygen is below capacity. The canister calls only
`RestoreOxygenSeconds` and succeeds only when that API reports a positive actual delta. Capacity
clamping remains inside the Oxygen component: a 30-second canister at 170/180 restores 10 seconds
and is consumed. At full Oxygen, with invalid tuning, or after depletion, the action fails and the
item remains equipped.

Successful Use enters the pickupable `Consumed` state instead of destroying the Actor. The Actor
is hidden and removed from collision, GridWorld, and navigation for the rest of the run. World
State restores it through the ordinary `Consumed -> RestorePending -> World` path. Intent Replay
records only the generic Use intent and soft canister reference, so a Clone applies the fixed
canister tuning to its own live Oxygen value; replay may legitimately diverge when that value is
already full.

No Oxygen or Health widget is created by this feature. The existing Inventory widget discovers
the canister's authored `/Game/Data/Inventory/DA_ParadoxUsePickupableAction` descriptor and uses
normal Gameplay Action preflight to enable or disable it.

## Depletion and Health

Depletion commits exactly once: remaining time becomes zero, progression stops,
`OnOxygenChanged`/`OnWholeSecondChanged` publish zero, and `OnOxygenDepleted` fires. The component
then calls:

```cpp
HealthComponent->Kill(
    nullptr,
    Character,
    UParadoxOxygenDepletionDamageType::StaticClass());
```

This is the only lethal integration. `UParadoxHealthComponent::Kill` submits the remaining HP
through Unreal's native damage pipeline. Oxygen never branches on Player versus Clone and never
calls the time loop, presentation, ragdoll, or behavior systems.

Player depletion consequently produces the existing `PlayerDeath` run failure. Its
`FParadoxRunFailureContext::DamageTypeClass` identifies Oxygen depletion for presentation. Clone
depletion reaches the existing passive temporal-corpse path without failing the Player's run.

## Events and UI

The component exposes `OnOxygenChanged`, `OnOxygenDepleted`, `OnOxygenReset`,
`OnConsumptionSpeedChanged`, `OnConsumptionBlockedChanged`, and `OnWholeSecondChanged`. Updates are
discrete and at whole-second boundaries; gameplay does not broadcast every frame.

`UParadoxOxygenWidget` observes only an explicitly supplied component:

```cpp
OxygenWidget->SetObservedOxygenComponent(Character->GetOxygenComponent());
OxygenWidget->ClearObservedOxygenComponent();
```

It never searches an owning Player, Pawn, or Controller. Rebinding removes old delegates, binds the
new weak source once, and refreshes immediately. Destroying the observed owner clears the source.
This same contract supports the possessed Player and a future selected-Clone panel.

The widget exposes countdown, duration, normalized value, blocker, speed, depletion, and formatted
`MM:SS` data. Presentation-only states are `Normal`, `Low`, `Critical`, and `Depleted`; the default
thresholds are 30 and 10 seconds. The native class creates no visual tree and declares no
`BindWidget` fields. Blueprint owns the complete layout and may use a timer, bar, circular
indicator, icons, or animations without changing gameplay.

Add a `UParadoxOxygenWidget` anywhere below the authored Gameplay HUD root to opt into Oxygen
presentation; the root never creates it automatically. The coordinator explicitly binds every
discovered Health/Oxygen presentation widget when the possessed `AParadoxCharacter` changes and
clears those bindings during teardown. Status visibility is applied directly to the discovered
widgets rather than to a specially named container.

## Verification

After building `ParadoxEditor`, run:

```text
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -nosplash -NullRHI -NoSound -ExecCmds="Automation RunTests Paradox.Oxygen; Quit" -TestExit="Automation Test Queue Empty" -log
```

The suite covers seconds-based operations, time dilation, pause, modifiers, overlapping blockers,
reset and stale handles, event order, native Health death classification, Clone death, explicit
widget rebinding, source destruction, countdown formatting, and HUD composition.

Run `Paradox.OxygenCanister` for Pickup/Drop/Swap separation, restore/clamp/consumption,
configuration and ownership failures, reentrancy, exactly-once passive cleanup, Inventory widget
discovery, semantic recording, Clone replay divergence, and World State baseline restoration.
