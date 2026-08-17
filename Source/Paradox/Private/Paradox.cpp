// Copyright Epic Games, Inc. All Rights Reserved.

#include "Paradox.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, Paradox, "Paradox" );

DEFINE_LOG_CATEGORY(LogParadox)

namespace
{
	TAutoConsoleVariable<int32> CVarParadoxTimeLoopDebug(
		TEXT("Paradox.TimeLoop.Debug"),
		0,
		TEXT("Draw Paradox temporal overlap authority and deduplication state.\n")
		TEXT("0: disabled (default)\n")
		TEXT("1: enabled for Temporal Vision components whose local debug flag is enabled"),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarParadoxCloneBehaviorDebug(
		TEXT("Paradox.CloneBehavior.Debug"),
		0,
		TEXT("Enables Paradox clone behavior diagnostics when the owning component local flag is enabled."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarParadoxFootstepDebug(
		TEXT("Paradox.Footsteps.Debug"),
		0,
		TEXT("Enables project footstep-perception diagnostics when the owning adapter local flag is enabled."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarParadoxPressurePlateDebug(
		TEXT("Paradox.PressurePlate.Debug"),
		0,
		TEXT("Enables pressure-plate occupancy and movement diagnostics when the inherited local debug flag is enabled."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarParadoxVerticalBarrierDebug(
		TEXT("Paradox.VerticalBarrier.Debug"),
		0,
		TEXT("Enables vertical-barrier passage, passenger, and movement diagnostics when local debug is enabled."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarParadoxInteractionDebug(
		TEXT("Paradox.Interaction.Debug"),
		0,
		TEXT("Enables event-driven Smart Object slot and GridWorld interaction diagnostics when local debug is enabled."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarParadoxPuzzleOverlayDebug(
		TEXT("Paradox.PuzzleOverlay.Debug"),
		0,
		TEXT("Enables selected puzzle-circuit routing diagnostics when the renderer's local debug flag is enabled."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarParadoxInventoryDebug(
		TEXT("Paradox.Inventory.Debug"),
		0,
		TEXT("Enables event-driven inventory and drop diagnostics when the owning object's local flag is enabled."),
		ECVF_Default);
}

bool IsParadoxTimeLoopDebugEnabled()
{
	return CVarParadoxTimeLoopDebug.GetValueOnGameThread() != 0;
}

bool IsParadoxCloneBehaviorDebugEnabled()
{
	return CVarParadoxCloneBehaviorDebug.GetValueOnGameThread() != 0;
}

bool IsParadoxFootstepDebugEnabled()
{
	return CVarParadoxFootstepDebug.GetValueOnGameThread() != 0;
}

bool IsParadoxPressurePlateDebugEnabled()
{
	return CVarParadoxPressurePlateDebug.GetValueOnGameThread() != 0;
}

bool IsParadoxVerticalBarrierDebugEnabled()
{
	return CVarParadoxVerticalBarrierDebug.GetValueOnGameThread() != 0;
}

bool IsParadoxInteractionDebugEnabled()
{
	return CVarParadoxInteractionDebug.GetValueOnGameThread() != 0;
}

bool IsParadoxPuzzleOverlayDebugEnabled()
{
	return CVarParadoxPuzzleOverlayDebug.GetValueOnGameThread() != 0;
}

bool IsParadoxInventoryDebugEnabled()
{
	return CVarParadoxInventoryDebug.GetValueOnGameThread() != 0;
}

namespace ParadoxGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Origin_Player, "GameplayAction.Origin.Player");
	UE_DEFINE_GAMEPLAY_TAG(
		Action_SetCrouched,
		"GameplayAction.Type.Paradox.Character.SetCrouched");
	UE_DEFINE_GAMEPLAY_TAG(Lock_Stance, "GameplayAction.Lock.Stance");
	UE_DEFINE_GAMEPLAY_TAG(
		Action_TimeTravel,
		"GameplayAction.Type.Paradox.TimeLoop.TimeTravel");
	UE_DEFINE_GAMEPLAY_TAG(
		Lock_TimeTravel,
		"GameplayAction.Lock.Paradox.TimeTravel");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Outcome_FutureObserved,
		"Relation.Outcome.Paradox.FutureObserved");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Reason_FutureTemporalOrder,
		"Relation.Reason.Paradox.FutureTemporalOrder");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Reason_SafeTemporalOrder,
		"Relation.Reason.Paradox.SafeTemporalOrder");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Interrupted_ByInvestigation,
		"GameplayAction.Result.Interrupted.Paradox.Investigation.Started");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Interrupted_InvestigationSuperseded,
		"GameplayAction.Result.Interrupted.Paradox.Investigation.Superseded");
	UE_DEFINE_GAMEPLAY_TAG(
		Origin_Investigation,
		"GameplayAction.Origin.Paradox.Investigation");
	UE_DEFINE_GAMEPLAY_TAG(
		Action_InvestigationInspect,
		"GameplayAction.Type.Paradox.Investigation.Inspect");
	UE_DEFINE_GAMEPLAY_TAG(Action_Interaction_Receiver, "GameplayAction.Type.Paradox.Interaction.Receiver.SetState");
	UE_DEFINE_GAMEPLAY_TAG(Action_Interaction_Emitter, "GameplayAction.Type.Paradox.Interaction.Emitter.SetSignal");
	UE_DEFINE_GAMEPLAY_TAG(Lock_Interaction, "GameplayAction.Lock.Interaction");
	UE_DEFINE_GAMEPLAY_TAG(Action_Interaction_Pickup, "GameplayAction.Type.Paradox.Interaction.Pickup");
	UE_DEFINE_GAMEPLAY_TAG(Action_Interaction_Swap, "GameplayAction.Type.Paradox.Interaction.Swap");
	UE_DEFINE_GAMEPLAY_TAG(Action_ItemSlot_Insert, "GameplayAction.Type.Paradox.ItemSlot.Insert");
	UE_DEFINE_GAMEPLAY_TAG(Action_ItemSlot_Pickup, "GameplayAction.Type.Paradox.ItemSlot.Pickup");
	UE_DEFINE_GAMEPLAY_TAG(Action_Inventory_Drop, "GameplayAction.Type.Paradox.Inventory.Drop");
	UE_DEFINE_GAMEPLAY_TAG(Lock_Inventory, "GameplayAction.Lock.Paradox.Inventory");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Inventory_Pickup, "Interaction.Paradox.Inventory.Pickup");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Inventory_Swap, "Interaction.Paradox.Inventory.Swap");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_ItemSlot_Insert, "Interaction.Paradox.ItemSlot.Insert");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_ItemSlot_Pickup, "Interaction.Paradox.ItemSlot.Pickup");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_ItemSlot_InvalidRequest, "GameplayAction.Result.Failure.Paradox.ItemSlot.InvalidRequest");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_ItemSlot_Inactive, "GameplayAction.Result.Failure.Paradox.ItemSlot.Inactive");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_ItemSlot_Occupied, "GameplayAction.Result.Failure.Paradox.ItemSlot.Occupied");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_ItemSlot_Empty, "GameplayAction.Result.Failure.Paradox.ItemSlot.Empty");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_ItemSlot_Locked, "GameplayAction.Result.Failure.Paradox.ItemSlot.Locked");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_ItemSlot_Incompatible, "GameplayAction.Result.Failure.Paradox.ItemSlot.Incompatible");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_ItemSlot_OwnershipConflict, "GameplayAction.Result.Failure.Paradox.ItemSlot.OwnershipConflict");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Inventory_InvalidRequest, "GameplayAction.Result.Failure.Paradox.Inventory.InvalidRequest");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Inventory_SlotOccupied, "GameplayAction.Result.Failure.Paradox.Inventory.SlotOccupied");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Inventory_SlotEmpty, "GameplayAction.Result.Failure.Paradox.Inventory.SlotEmpty");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Inventory_ItemUnavailable, "GameplayAction.Result.Failure.Paradox.Inventory.ItemUnavailable");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Inventory_OwnershipConflict, "GameplayAction.Result.Failure.Paradox.Inventory.OwnershipConflict");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Inventory_InvalidCell, "GameplayAction.Result.Failure.Paradox.Inventory.InvalidCell");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Inventory_NoReachableExecutionCell, "GameplayAction.Result.Failure.Paradox.Inventory.NoReachableExecutionCell");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Inventory_TargetInvalidated, "GameplayAction.Result.Failure.Paradox.Inventory.TargetInvalidated");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Interaction_InvalidRequest,
		"GameplayAction.Result.Failure.Paradox.Interaction.InvalidRequest");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Interaction_TargetUnavailable,
		"GameplayAction.Result.Failure.Paradox.Interaction.TargetUnavailable");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Interaction_InvalidPosition,
		"GameplayAction.Result.Failure.Paradox.Interaction.InvalidPosition");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Interaction_SlotUnavailable,
		"GameplayAction.Result.Failure.Paradox.Interaction.SlotUnavailable");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Interaction_ClaimFailed,
		"GameplayAction.Result.Failure.Paradox.Interaction.ClaimFailed");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Interaction_NotImplemented,
		"GameplayAction.Result.Failure.Paradox.Interaction.NotImplemented");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Interaction_EffectUnavailable, "GameplayAction.Result.Failure.Paradox.Interaction.EffectUnavailable");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Interaction_Prerequisites, "GameplayAction.Result.Failure.Paradox.Interaction.Prerequisites");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Interaction_GateClosed, "GameplayAction.Result.Failure.Paradox.Interaction.GateClosed");
	UE_DEFINE_GAMEPLAY_TAG(
		State_Computer_Powered,
		"PerceptionKnowledge.State.Paradox.Computer.Powered");
	UE_DEFINE_GAMEPLAY_TAG(State_ItemSlot_Active, "PerceptionKnowledge.State.Paradox.ItemSlot.Active");
	UE_DEFINE_GAMEPLAY_TAG(State_ItemSlot_Occupied, "PerceptionKnowledge.State.Paradox.ItemSlot.Occupied");
	UE_DEFINE_GAMEPLAY_TAG(State_ItemSlot_Locked, "PerceptionKnowledge.State.Paradox.ItemSlot.Locked");
	UE_DEFINE_GAMEPLAY_TAG(State_ItemSlot_Removable, "PerceptionKnowledge.State.Paradox.ItemSlot.Removable");
	UE_DEFINE_GAMEPLAY_TAG(
		Test_State_Active,
		"PerceptionKnowledge.State.Test.Paradox.Active");
	UE_DEFINE_GAMEPLAY_TAG(
		Test_Event_Noise,
		"PerceptionKnowledge.Event.Test.Paradox.Noise");
	UE_DEFINE_GAMEPLAY_TAG(
		Event_Noise_Character_Footstep,
		"PerceptionKnowledge.Event.Paradox.Noise.Character.Footstep");
	UE_DEFINE_GAMEPLAY_TAG(
		Cause_CharacterMovement_Footstep,
		"PerceptionKnowledge.Cause.Paradox.CharacterMovement.Footstep");
	UE_DEFINE_GAMEPLAY_TAG(
		Puzzle_Signal_Pressed,
		"Puzzle.Signal.Pressed");
	UE_DEFINE_GAMEPLAY_TAG(Puzzle_Signal_ItemSlotSatisfied, "Puzzle.Signal.Paradox.ItemSlot.Satisfied");
	UE_DEFINE_GAMEPLAY_TAG(Item_Type_Battery, "Item.Type.Battery");
	UE_DEFINE_GAMEPLAY_TAG(Item_Type_KeyCard_Red, "Item.Type.KeyCard.Red");
	UE_DEFINE_GAMEPLAY_TAG(Item_Type_KeyCard_Blue, "Item.Type.KeyCard.Blue");
	UE_DEFINE_GAMEPLAY_TAG(Item_Type_KeyCard_Green, "Item.Type.KeyCard.Green");
	UE_DEFINE_GAMEPLAY_TAG(
		Event_Noise_PressurePlate_Press,
		"PerceptionKnowledge.Event.Paradox.Noise.PressurePlate.Press");
	UE_DEFINE_GAMEPLAY_TAG(
		Event_Noise_PressurePlate_Release,
		"PerceptionKnowledge.Event.Paradox.Noise.PressurePlate.Release");
	UE_DEFINE_GAMEPLAY_TAG(
		Cause_PressurePlate_Movement,
		"PerceptionKnowledge.Cause.Paradox.PressurePlate.Movement");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Interrupted_ByBarrierLift,
		"GameplayAction.Result.Interrupted.Paradox.Barrier.Transport");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_Open, "PerceptionKnowledge.State.Paradox.Barrier.Open");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_BlockingPassage, "PerceptionKnowledge.State.Paradox.Barrier.BlockingPassage");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_Moving, "PerceptionKnowledge.State.Paradox.Barrier.Moving");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_WaitingForClearance, "PerceptionKnowledge.State.Paradox.Barrier.WaitingForClearance");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_TransportingOccupants, "PerceptionKnowledge.State.Paradox.Barrier.TransportingOccupants");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Barrier_Open, "Interaction.Paradox.Barrier.Open");
	UE_DEFINE_GAMEPLAY_TAG(Interaction_Barrier_Close, "Interaction.Paradox.Barrier.Close");
	UE_DEFINE_GAMEPLAY_TAG(Event_Noise_Barrier_Raise, "PerceptionKnowledge.Event.Paradox.Noise.Barrier.Raise");
	UE_DEFINE_GAMEPLAY_TAG(Event_Noise_Barrier_Lower, "PerceptionKnowledge.Event.Paradox.Noise.Barrier.Lower");
	UE_DEFINE_GAMEPLAY_TAG(Event_Noise_Barrier_Impact, "PerceptionKnowledge.Event.Paradox.Noise.Barrier.Impact");
	UE_DEFINE_GAMEPLAY_TAG(Cause_Barrier_Movement, "PerceptionKnowledge.Cause.Paradox.Barrier.Movement");
}
