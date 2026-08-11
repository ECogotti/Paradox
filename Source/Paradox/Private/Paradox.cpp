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
