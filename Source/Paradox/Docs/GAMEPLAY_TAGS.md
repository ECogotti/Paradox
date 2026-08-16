# Gameplay Tag declarations

## Paradox runtime module

All native Gameplay Tags owned by the Paradox module are declared as extern values in
`Source/Paradox/Public/Paradox.h` inside `ParadoxGameplayTags` and defined with their registered
strings in `Source/Paradox/Private/Paradox.cpp`.

| Native name | Registered tag |
| --- | --- |
| `Origin_Player` | `GameplayAction.Origin.Player` |
| `Action_SetCrouched` | `GameplayAction.Type.Paradox.Character.SetCrouched` |
| `Lock_Stance` | `GameplayAction.Lock.Stance` |
| `Action_TimeTravel` | `GameplayAction.Type.Paradox.TimeLoop.TimeTravel` |
| `Lock_TimeTravel` | `GameplayAction.Lock.Paradox.TimeTravel` |
| `Relation_Outcome_FutureObserved` | `Relation.Outcome.Paradox.FutureObserved` |
| `Relation_Reason_FutureTemporalOrder` | `Relation.Reason.Paradox.FutureTemporalOrder` |
| `Relation_Reason_SafeTemporalOrder` | `Relation.Reason.Paradox.SafeTemporalOrder` |
| `Result_Interrupted_ByInvestigation` | `GameplayAction.Result.Interrupted.Paradox.Investigation.Started` |
| `Result_Interrupted_InvestigationSuperseded` | `GameplayAction.Result.Interrupted.Paradox.Investigation.Superseded` |
| `Origin_Investigation` | `GameplayAction.Origin.Paradox.Investigation` |
| `Action_InvestigationInspect` | `GameplayAction.Type.Paradox.Investigation.Inspect` |
| `Action_Interaction_Receiver` | `GameplayAction.Type.Paradox.Interaction.Receiver.SetState` |
| `Action_Interaction_Emitter` | `GameplayAction.Type.Paradox.Interaction.Emitter.SetSignal` |
| `Lock_Interaction` | `GameplayAction.Lock.Interaction` |
| `Result_Failure_Interaction_InvalidRequest` | `GameplayAction.Result.Failure.Paradox.Interaction.InvalidRequest` |
| `Result_Failure_Interaction_TargetUnavailable` | `GameplayAction.Result.Failure.Paradox.Interaction.TargetUnavailable` |
| `Result_Failure_Interaction_InvalidPosition` | `GameplayAction.Result.Failure.Paradox.Interaction.InvalidPosition` |
| `Result_Failure_Interaction_SlotUnavailable` | `GameplayAction.Result.Failure.Paradox.Interaction.SlotUnavailable` |
| `Result_Failure_Interaction_ClaimFailed` | `GameplayAction.Result.Failure.Paradox.Interaction.ClaimFailed` |
| `Result_Failure_Interaction_NotImplemented` | `GameplayAction.Result.Failure.Paradox.Interaction.NotImplemented` |
| `Result_Failure_Interaction_EffectUnavailable` | `GameplayAction.Result.Failure.Paradox.Interaction.EffectUnavailable` |
| `Result_Failure_Interaction_Prerequisites` | `GameplayAction.Result.Failure.Paradox.Interaction.Prerequisites` |
| `Result_Failure_Interaction_GateClosed` | `GameplayAction.Result.Failure.Paradox.Interaction.GateClosed` |
| `Action_Interaction_Pickup` | `GameplayAction.Type.Paradox.Interaction.Pickup` |
| `Action_Interaction_Swap` | `GameplayAction.Type.Paradox.Interaction.Swap` |
| `Action_ItemSlot_Insert` | `GameplayAction.Type.Paradox.ItemSlot.Insert` |
| `Action_ItemSlot_Pickup` | `GameplayAction.Type.Paradox.ItemSlot.Pickup` |
| `Action_Inventory_Drop` | `GameplayAction.Type.Paradox.Inventory.Drop` |
| `Lock_Inventory` | `GameplayAction.Lock.Paradox.Inventory` |
| `Interaction_Inventory_Pickup` | `Interaction.Paradox.Inventory.Pickup` |
| `Interaction_Inventory_Swap` | `Interaction.Paradox.Inventory.Swap` |
| `Interaction_ItemSlot_Insert` | `Interaction.Paradox.ItemSlot.Insert` |
| `Interaction_ItemSlot_Pickup` | `Interaction.Paradox.ItemSlot.Pickup` |
| `Result_Failure_ItemSlot_InvalidRequest` | `GameplayAction.Result.Failure.Paradox.ItemSlot.InvalidRequest` |
| `Result_Failure_ItemSlot_Inactive` | `GameplayAction.Result.Failure.Paradox.ItemSlot.Inactive` |
| `Result_Failure_ItemSlot_Occupied` | `GameplayAction.Result.Failure.Paradox.ItemSlot.Occupied` |
| `Result_Failure_ItemSlot_Empty` | `GameplayAction.Result.Failure.Paradox.ItemSlot.Empty` |
| `Result_Failure_ItemSlot_Locked` | `GameplayAction.Result.Failure.Paradox.ItemSlot.Locked` |
| `Result_Failure_ItemSlot_Incompatible` | `GameplayAction.Result.Failure.Paradox.ItemSlot.Incompatible` |
| `Result_Failure_ItemSlot_OwnershipConflict` | `GameplayAction.Result.Failure.Paradox.ItemSlot.OwnershipConflict` |
| `State_ItemSlot_Active` | `PerceptionKnowledge.State.Paradox.ItemSlot.Active` |
| `State_ItemSlot_Occupied` | `PerceptionKnowledge.State.Paradox.ItemSlot.Occupied` |
| `State_ItemSlot_Locked` | `PerceptionKnowledge.State.Paradox.ItemSlot.Locked` |
| `State_ItemSlot_Removable` | `PerceptionKnowledge.State.Paradox.ItemSlot.Removable` |
| `Puzzle_Signal_ItemSlotSatisfied` | `Puzzle.Signal.Paradox.ItemSlot.Satisfied` |
| `Item_Type_Battery` | `Item.Type.Battery` |
| `Item_Battery_Voltage_12V` | `Item.Battery.Voltage.12V` |
| `Item_Type_Key` | `Item.Type.Key` |
| `Item_Access_Level_2` | `Item.Access.Level.2` |
| `State_Computer_Powered` | `PerceptionKnowledge.State.Paradox.Computer.Powered` |
| `Test_State_Active` | `PerceptionKnowledge.State.Test.Paradox.Active` |
| `Test_Event_Noise` | `PerceptionKnowledge.Event.Test.Paradox.Noise` |
| `Event_Noise_Character_Footstep` | `PerceptionKnowledge.Event.Paradox.Noise.Character.Footstep` |
| `Cause_CharacterMovement_Footstep` | `PerceptionKnowledge.Cause.Paradox.CharacterMovement.Footstep` |
| `Puzzle_Signal_Pressed` | `Puzzle.Signal.Pressed` |
| `Event_Noise_PressurePlate_Press` | `PerceptionKnowledge.Event.Paradox.Noise.PressurePlate.Press` |
| `Event_Noise_PressurePlate_Release` | `PerceptionKnowledge.Event.Paradox.Noise.PressurePlate.Release` |
| `Cause_PressurePlate_Movement` | `PerceptionKnowledge.Cause.Paradox.PressurePlate.Movement` |
| `Result_Interrupted_ByBarrierLift` | `GameplayAction.Result.Interrupted.Paradox.Barrier.Transport` |
| `State_Barrier_Open` | `PerceptionKnowledge.State.Paradox.Barrier.Open` |
| `State_Barrier_BlockingPassage` | `PerceptionKnowledge.State.Paradox.Barrier.BlockingPassage` |
| `State_Barrier_Moving` | `PerceptionKnowledge.State.Paradox.Barrier.Moving` |
| `State_Barrier_WaitingForClearance` | `PerceptionKnowledge.State.Paradox.Barrier.WaitingForClearance` |
| `State_Barrier_TransportingOccupants` | `PerceptionKnowledge.State.Paradox.Barrier.TransportingOccupants` |
| `Interaction_Barrier_Open` | `Interaction.Paradox.Barrier.Open` |
| `Interaction_Barrier_Close` | `Interaction.Paradox.Barrier.Close` |
| `Event_Noise_Barrier_Raise` | `PerceptionKnowledge.Event.Paradox.Noise.Barrier.Raise` |
| `Event_Noise_Barrier_Lower` | `PerceptionKnowledge.Event.Paradox.Noise.Barrier.Lower` |
| `Event_Noise_Barrier_Impact` | `PerceptionKnowledge.Event.Paradox.Noise.Barrier.Impact` |
| `Cause_Barrier_Movement` | `PerceptionKnowledge.Cause.Paradox.Barrier.Movement` |

The shared private interaction fixtures declare `ParadoxInteractionTestTags::Primary`,
`ParadoxInteractionTestTags::Secondary`, and `ParadoxInteractionTestTags::Rejected` in
`Private/Tests/ParadoxInteractionTestTypes.h`; they register
`Interaction.Test.Catalog.Primary`, `Interaction.Test.Catalog.Secondary`, and
`Interaction.Test.Catalog.Rejected` only in development automation builds. Test vocabulary is not
part of the public runtime API.

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
table, and contains one-to-one redirects for the legacy names that have an unambiguous semantic
successor. Redirects preserve external serialized data while all project assets and current
declarations use the canonical roots. The legacy `Barrier.State.Open`, `Barrier.State.BlockingPassage`,
and `Barrier.State.Moving` names deliberately have no permanent redirect because those names had been
serialized both as observable state and as interaction identity. The known Blueprint and Data Asset
usages were migrated contextually to `Interaction.Paradox.Barrier.*` instead.

Pressure Plate occupant filters are not Gameplay Tags. `RequiredOccupantActorTags` compares ordinary
`AActor::Tags` (`FName`) configured under **Actor → Tags**, so those names have no central registry.
`AParadoxVerticalBarrier::RequiredOccupantActorTags` uses the same ordinary Actor Tag convention.
