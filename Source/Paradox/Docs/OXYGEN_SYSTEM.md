# Paradox Oxygen System

## Modes, ownership, and per-map setup

Every `AParadoxCharacter` still owns one `UParadoxOxygenComponent`; existing gameplay, canisters,
widgets, modifiers, and blockers always call that component. The component is either the original
per-Pawn resource authority or a facade for one World-owned reservoir.

Oxygen mode is selected per map with one placeable `AParadoxWorldInitializer`:

- no initializer: `PerPawn`, preserving existing maps and assets;
- one initializer with `Mode = PerPawn`: explicit legacy behavior;
- one initializer with `Mode = SharedGlobal`: the World subsystem owns the shared resource;
- more than one initializer, or non-finite/non-positive shared duration or base speed: invalid
  configuration. The diagnostic is logged and Time Loop initialization is rejected.

`AParadoxWorldInitializer` does not Tick and does not participate in World State. It is the common
per-map configuration root for future World-scoped systems. Its Oxygen configuration defaults to
180 seconds, base rate 1.0, and `FixedWorldRate`.

`UParadoxOxygenWorldSubsystem` resolves this configuration at World begin play. Blueprint can query
mode, validity and diagnostic, shared duration/current/normalized/whole-second values, effective
rate, blocked/depleted state, current run checkpoint, and active participant count. A new World,
including Restart Level, constructs a new subsystem and starts from the configured duration.

In `PerPawn`, `OxygenDurationSeconds` and `BaseConsumptionSpeed` remain designer-facing component
defaults. The persistent Player resets on activation and reconstructed Clones start full. Leaving
`ActiveRun`, retiring a Clone, or dying stops that Pawn's resource.

In `SharedGlobal`, all component value queries and existing mutation APIs transparently target the
same reservoir. Component-local duration/speed tuning is ignored without being removed from the
framework, so switching the map back to `PerPawn` restores the original behavior.

## Shared consumption and participants

The time loop marks participants active only while they are live temporal avatars in `ActiveRun`:

- a Player counts only after it has selected a spawn and materialized;
- replaying Clones count;
- Clones parked in terminal GOAP count;
- hidden Players, dead Clones, and `RetireInPlace` Clones do not count.

`FixedWorldRate` consumes `SharedBaseConsumptionSpeed` once while at least one participant is
active. `PerActiveAvatar` multiplies that rate by the active participant count. Runtime speed
modifiers multiply the resulting shared rate; any shared blocker suspends regular consumption.

## Run checkpoints

The configured starting value is the first shared checkpoint. Successful Time Travel synchronizes
the current reservoir and promotes that exact value to the next run's checkpoint. Oxygen therefore
persists across successful timelines instead of refilling.

Any failed run, including player death, temporal paradox, or `GlobalOxygenDepleted`, restores the
exact checkpoint captured at the beginning of that run. Consumption and canister refills performed
inside the failed attempt are both rolled back. Checkpoint promotion and rollback clear active
participants, speed modifiers, blockers, and their handles without changing the stored checkpoint
to full capacity.

## Simulation-time countdown

Oxygen uses `UWorld::GetTimeSeconds()`, which is paused by Unreal gameplay pause and includes global
time dilation. Therefore Tactical Pause stops the countdown without a separate Oxygen pause mode,
while x1.5, x2, and x3 simulation speeds accelerate it consistently with the rest of gameplay.

Neither authority Ticks. It keeps a materialized remaining value and the last simulation-time
sample, then uses one-shot timers for the predicted depletion and next whole-second boundary.
Queries project the current value analytically. Every direct operation, modifier change, blocker
change, or lifecycle transition synchronizes elapsed time and reschedules those timers.

## Direct operations

All public resource operations use seconds:

- `ConsumeOxygenSeconds` and `RestoreOxygenSeconds` return the amount actually changed;
- `SetRemainingOxygenSeconds` assigns a clamped value;
- `RefillOxygen` restores the configured duration;
- `ResetOxygen` starts a fresh resource lifetime and clears all transient effects.

Values always remain between zero and the active duration. Non-finite and invalid amounts are
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
invalidates all handles from the previous run. In `SharedGlobal`, the same APIs create World-owned
handles and checkpoint promotion/rollback invalidates them for every facade.

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
records only the generic Use intent and soft canister reference. In `PerPawn`, a Clone applies the
fixed tuning to its own live resource and may diverge when already full. In `SharedGlobal`, Player
and Clone Use both mutate the same reservoir through the unchanged component API.

No Oxygen or Health widget is created by this feature. The existing Inventory widget discovers
the canister's authored `/Game/Data/Inventory/DA_ParadoxUsePickupableAction` descriptor and uses
normal Gameplay Action preflight to enable or disable it.

## Depletion and run failure

Depletion commits exactly once: remaining time becomes zero, progression stops,
`OnOxygenChanged`/`OnWholeSecondChanged` publish zero, and `OnOxygenDepleted` fires. In `PerPawn`,
the component then calls:

```cpp
HealthComponent->Kill(
    nullptr,
    Character,
    UParadoxOxygenDepletionDamageType::StaticClass());
```

This remains the `PerPawn` lethal integration. Player depletion produces the existing
`PlayerDeath` run failure, while Clone depletion reaches the passive temporal-corpse path.

Shared depletion is different: the World authority commits zero and broadcasts exactly once. An
enabled Time Loop accepts one `GlobalOxygenDepleted` run failure immediately, even when no Player
spawn has been selected, and restores the run checkpoint through normal recovery. It does not call
`Kill` reentrantly on every facade. Without an enabled Time Loop, each living facade retains the
safe standalone fallback of killing its own owner through the same depletion damage type.

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

If presentation binds before the observed Character begins play, the component publishes its
authoritative shared-reservoir snapshot during `BeginPlay`. The widget therefore replaces the
temporary per-Pawn construction value before the first rendered gameplay tick; no Blueprint delay,
poll, or spawn selection is required.

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

The suite covers legacy fallback, initializer validation, both shared policies, participant counts,
facade sharing, checkpoint promotion/rollback, time dilation, pause, modifiers, overlapping
blockers, stale handles, event order, native Health death classification, Clone death, explicit
widget rebinding, source destruction, countdown formatting, and HUD composition.

Run `Paradox.OxygenCanister` for Pickup/Drop/Swap separation, restore/clamp/consumption,
configuration and ownership failures, reentrancy, exactly-once passive cleanup, Inventory widget
discovery, semantic recording, Clone replay divergence, and World State baseline restoration.
