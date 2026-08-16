// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/** Main log category used across the project */
DECLARE_LOG_CATEGORY_EXTERN(LogParadox, Log, All);

#define PARADOX_LOG_INFO(Format, ...) UE_LOG(LogParadox, Log, Format, ##__VA_ARGS__)
#define PARADOX_LOG_WARNING(Format, ...) UE_LOG(LogParadox, Warning, Format, ##__VA_ARGS__)
#define PARADOX_LOG_ERROR(Format, ...) UE_LOG(LogParadox, Error, Format, ##__VA_ARGS__)

/** Returns whether module-wide time-loop visual debugging is enabled. */
PARADOX_API bool IsParadoxTimeLoopDebugEnabled();
/** Global half of the clone-behavior debug gate. */
PARADOX_API bool IsParadoxCloneBehaviorDebugEnabled();
/** Global half of the project footstep-perception debug gate. */
PARADOX_API bool IsParadoxFootstepDebugEnabled();
/** Global half of the project pressure-plate debug gate. */
PARADOX_API bool IsParadoxPressurePlateDebugEnabled();
/** Global half of the vertical-barrier visual debug gate. */
PARADOX_API bool IsParadoxVerticalBarrierDebugEnabled();
/** Global half of the Smart Object/GridWorld interaction-query debug gate. */
PARADOX_API bool IsParadoxInteractionDebugEnabled();
/** Global half of the selected puzzle-circuit overlay debug gate. */
PARADOX_API bool IsParadoxPuzzleOverlayDebugEnabled();
/** Global half of the single-slot inventory and drop-targeting debug gate. */
PARADOX_API bool IsParadoxInventoryDebugEnabled();

namespace ParadoxGameplayTags
{
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Origin_Player);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_SetCrouched);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lock_Stance);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_TimeTravel);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lock_TimeTravel);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Outcome_FutureObserved);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Reason_FutureTemporalOrder);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Relation_Reason_SafeTemporalOrder);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Interrupted_ByInvestigation);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Interrupted_InvestigationSuperseded);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Origin_Investigation);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_InvestigationInspect);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Interaction_Receiver);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Interaction_Emitter);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lock_Interaction);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Interaction_Pickup);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Interaction_Swap);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_ItemSlot_Insert);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_ItemSlot_Pickup);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Inventory_Drop);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lock_Inventory);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Inventory_Pickup);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Inventory_Swap);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_ItemSlot_Insert);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_ItemSlot_Pickup);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_ItemSlot_InvalidRequest);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_ItemSlot_Inactive);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_ItemSlot_Occupied);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_ItemSlot_Empty);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_ItemSlot_Locked);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_ItemSlot_Incompatible);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_ItemSlot_OwnershipConflict);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Inventory_InvalidRequest);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Inventory_SlotOccupied);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Inventory_SlotEmpty);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Inventory_ItemUnavailable);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Inventory_OwnershipConflict);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Inventory_InvalidCell);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Inventory_NoReachableExecutionCell);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Inventory_TargetInvalidated);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_InvalidRequest);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_TargetUnavailable);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_InvalidPosition);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_SlotUnavailable);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_ClaimFailed);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_NotImplemented);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_EffectUnavailable);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_Prerequisites);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Interaction_GateClosed);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Computer_Powered);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ItemSlot_Active);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ItemSlot_Occupied);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ItemSlot_Locked);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ItemSlot_Removable);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Test_State_Active);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Test_Event_Noise);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Noise_Character_Footstep);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cause_CharacterMovement_Footstep);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Puzzle_Signal_Pressed);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Puzzle_Signal_ItemSlotSatisfied);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Item_Type_Battery);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Item_Battery_Voltage_12V);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Item_Type_Key);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Item_Access_Level_2);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Noise_PressurePlate_Press);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Noise_PressurePlate_Release);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cause_PressurePlate_Movement);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Interrupted_ByBarrierLift);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Barrier_Open);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Barrier_BlockingPassage);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Barrier_Moving);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Barrier_WaitingForClearance);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Barrier_TransportingOccupants);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Barrier_Open);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Interaction_Barrier_Close);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Noise_Barrier_Raise);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Noise_Barrier_Lower);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Noise_Barrier_Impact);
	PARADOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cause_Barrier_Movement);
}
