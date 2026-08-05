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

namespace ParadoxGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Origin_Player, "GameplayAction.Origin.Player");
	UE_DEFINE_GAMEPLAY_TAG(
		Action_SetCrouched,
		"GameplayAction.Character.SetCrouched");
	UE_DEFINE_GAMEPLAY_TAG(Lock_Stance, "GameplayAction.Lock.Stance");
	UE_DEFINE_GAMEPLAY_TAG(
		Action_TimeTravel,
		"GameplayAction.Paradox.TimeTravel");
	UE_DEFINE_GAMEPLAY_TAG(
		Lock_TimeTravel,
		"GameplayAction.Lock.TimeTravel");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Outcome_FutureObserved,
		"Paradox.Relation.Outcome.FutureObserved");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Reason_FutureTemporalOrder,
		"Paradox.Relation.Reason.FutureTemporalOrder");
	UE_DEFINE_GAMEPLAY_TAG(
		Relation_Reason_SafeTemporalOrder,
		"Paradox.Relation.Reason.SafeTemporalOrder");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Interrupted_ByInvestigation,
		"GameplayAction.Result.Interrupted.ByInvestigation");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Interrupted_InvestigationSuperseded,
		"GameplayAction.Result.Interrupted.InvestigationSuperseded");
	UE_DEFINE_GAMEPLAY_TAG(
		Origin_Investigation,
		"GameplayAction.Origin.Investigation");
	UE_DEFINE_GAMEPLAY_TAG(
		Action_InvestigationInspect,
		"GameplayAction.Investigation.Inspect");
	UE_DEFINE_GAMEPLAY_TAG(
		State_Computer_Powered,
		"Computer.State.Powered");
	UE_DEFINE_GAMEPLAY_TAG(
		Test_State_Active,
		"Paradox.Test.State.Active");
	UE_DEFINE_GAMEPLAY_TAG(
		Test_Event_Noise,
		"Paradox.Test.Event.Noise");
	UE_DEFINE_GAMEPLAY_TAG(
		Event_Noise_Character_Footstep,
		"PerceptionKnowledge.Event.Noise.Character.Footstep");
	UE_DEFINE_GAMEPLAY_TAG(
		Cause_CharacterMovement_Footstep,
		"PerceptionKnowledge.Cause.CharacterMovement.Footstep");
	UE_DEFINE_GAMEPLAY_TAG(
		Puzzle_Signal_Pressed,
		"Puzzle.Signal.Pressed");
	UE_DEFINE_GAMEPLAY_TAG(
		Event_Noise_PressurePlate_Press,
		"PerceptionKnowledge.Event.Noise.PressurePlate.Press");
	UE_DEFINE_GAMEPLAY_TAG(
		Event_Noise_PressurePlate_Release,
		"PerceptionKnowledge.Event.Noise.PressurePlate.Release");
	UE_DEFINE_GAMEPLAY_TAG(
		Cause_PressurePlate_Movement,
		"PerceptionKnowledge.Cause.PressurePlate.Movement");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Interrupted_ByBarrierLift,
		"GameplayAction.Result.Interrupted.ByBarrierLift");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_Open, "Barrier.State.Open");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_BlockingPassage, "Barrier.State.BlockingPassage");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_Moving, "Barrier.State.Moving");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_WaitingForClearance, "Barrier.State.WaitingForClearance");
	UE_DEFINE_GAMEPLAY_TAG(State_Barrier_TransportingOccupants, "Barrier.State.TransportingOccupants");
	UE_DEFINE_GAMEPLAY_TAG(Event_Noise_Barrier_Raise, "PerceptionKnowledge.Event.Noise.Barrier.Raise");
	UE_DEFINE_GAMEPLAY_TAG(Event_Noise_Barrier_Lower, "PerceptionKnowledge.Event.Noise.Barrier.Lower");
	UE_DEFINE_GAMEPLAY_TAG(Event_Noise_Barrier_Impact, "PerceptionKnowledge.Event.Noise.Barrier.Impact");
	UE_DEFINE_GAMEPLAY_TAG(Cause_Barrier_Movement, "PerceptionKnowledge.Cause.Barrier.Movement");
}
