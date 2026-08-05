# Gameplay Tag declarations

## Paradox runtime module

All native Gameplay Tags owned by the Paradox module are declared as extern values in
`Source/Paradox/Public/Paradox.h` inside `ParadoxGameplayTags` and defined with their registered
strings in `Source/Paradox/Private/Paradox.cpp`.

| Native name | Registered tag |
| --- | --- |
| `Origin_Player` | `GameplayAction.Origin.Player` |
| `Action_SetCrouched` | `GameplayAction.Character.SetCrouched` |
| `Lock_Stance` | `GameplayAction.Lock.Stance` |
| `Action_TimeTravel` | `GameplayAction.Paradox.TimeTravel` |
| `Lock_TimeTravel` | `GameplayAction.Lock.TimeTravel` |
| `Relation_Outcome_FutureObserved` | `Paradox.Relation.Outcome.FutureObserved` |
| `Relation_Reason_FutureTemporalOrder` | `Paradox.Relation.Reason.FutureTemporalOrder` |
| `Relation_Reason_SafeTemporalOrder` | `Paradox.Relation.Reason.SafeTemporalOrder` |
| `Result_Interrupted_ByInvestigation` | `GameplayAction.Result.Interrupted.ByInvestigation` |
| `Result_Interrupted_InvestigationSuperseded` | `GameplayAction.Result.Interrupted.InvestigationSuperseded` |
| `Origin_Investigation` | `GameplayAction.Origin.Investigation` |
| `Action_InvestigationInspect` | `GameplayAction.Investigation.Inspect` |
| `State_Computer_Powered` | `Computer.State.Powered` |
| `Test_State_Active` | `Paradox.Test.State.Active` |
| `Test_Event_Noise` | `Paradox.Test.Event.Noise` |
| `Event_Noise_Character_Footstep` | `PerceptionKnowledge.Event.Noise.Character.Footstep` |
| `Cause_CharacterMovement_Footstep` | `PerceptionKnowledge.Cause.CharacterMovement.Footstep` |
| `Puzzle_Signal_Pressed` | `Puzzle.Signal.Pressed` |
| `Event_Noise_PressurePlate_Press` | `PerceptionKnowledge.Event.Noise.PressurePlate.Press` |
| `Event_Noise_PressurePlate_Release` | `PerceptionKnowledge.Event.Noise.PressurePlate.Release` |
| `Cause_PressurePlate_Movement` | `PerceptionKnowledge.Cause.PressurePlate.Movement` |
| `Result_Interrupted_ByBarrierLift` | `GameplayAction.Result.Interrupted.ByBarrierLift` |
| `State_Barrier_Open` | `Barrier.State.Open` |
| `State_Barrier_BlockingPassage` | `Barrier.State.BlockingPassage` |
| `State_Barrier_Moving` | `Barrier.State.Moving` |
| `State_Barrier_WaitingForClearance` | `Barrier.State.WaitingForClearance` |
| `State_Barrier_TransportingOccupants` | `Barrier.State.TransportingOccupants` |
| `Event_Noise_Barrier_Raise` | `PerceptionKnowledge.Event.Noise.Barrier.Raise` |
| `Event_Noise_Barrier_Lower` | `PerceptionKnowledge.Event.Noise.Barrier.Lower` |
| `Event_Noise_Barrier_Impact` | `PerceptionKnowledge.Event.Noise.Barrier.Impact` |
| `Cause_Barrier_Movement` | `PerceptionKnowledge.Cause.Barrier.Movement` |

## PuzzleSystem plugin

PuzzleSystem intentionally does not own fixed production signal names. `UPuzzleEmitterComponent`,
`APuzzleController`, and conditions consume designer-configured `FGameplayTag` values; the game or
owning module declares the concrete signal vocabulary.

The plugin's development automation fixtures declare these private native test tags at the top of
`Plugins/PuzzleSystem/Source/PuzzleSystem/Private/Tests/PuzzleSystemAutomationTests.cpp`:

- `Puzzle.Test.Pressed`;
- `Puzzle.Test.Powered`;
- `Puzzle.Test.Completed`.

Their root is `Puzzle`, matching Paradox's production `Puzzle.Signal.Pressed`. Automation test paths
and console variables still begin with `PuzzleSystem` because those strings identify the plugin and
are not Gameplay Tags.

## Configuration and legacy redirects

`Config/DefaultGameplayTags.ini` enables config import, registers the project's Gameplay Action tag
table, and redirects the former `PuzzleSystem.Test.*` names to `Puzzle.Test.*`. Redirects preserve
existing serialized assets while all current declarations and new selections use the `Puzzle` root.

Pressure Plate occupant filters are not Gameplay Tags. `RequiredOccupantActorTags` compares ordinary
`AActor::Tags` (`FName`) configured under **Actor → Tags**, so those names have no central registry.
`AParadoxVerticalBarrier::RequiredOccupantActorTags` uses the same ordinary Actor Tag convention.
