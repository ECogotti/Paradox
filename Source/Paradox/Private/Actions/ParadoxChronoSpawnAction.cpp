#include "Actions/ParadoxChronoSpawnAction.h"

#include "Characters/ParadoxCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "GameModes/ParadoxGameMode.h"
#include "GameplayActionTags.h"
#include "Paradox.h"
#include "TimeLoop/ParadoxChronoSpawn.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"

namespace UE::Paradox::ChronoSpawnAction::Private
{
	UParadoxTimeLoopComponent* ResolveTimeLoop(const UObject& Context)
	{
		const UWorld* World = Context.GetWorld();
		const AParadoxGameMode* GameMode = World
			? World->GetAuthGameMode<AParadoxGameMode>()
			: nullptr;
		return GameMode ? GameMode->GetTimeLoopComponent() : nullptr;
	}
}

bool UParadoxChronoSpawnAction::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	const UGameplayActionComponent* Actions = GetOwningComponent();
	const AParadoxCharacter* Character = Actions
		? Cast<AParadoxCharacter>(Actions->GetOwner())
		: nullptr;
	AParadoxChronoSpawn* ChronoSpawn =
		Cast<AParadoxChronoSpawn>(GetInteractionTarget());
	UParadoxTimeLoopComponent* Loop =
		UE::Paradox::ChronoSpawnAction::Private::ResolveTimeLoop(*this);
	if (!Character || !ChronoSpawn || !Loop)
	{
		OutFailureReason =
			ParadoxGameplayTags::Result_Failure_ChronoSpawn_InvalidTarget;
		if (OutDiagnostic.IsEmpty())
		{
			OutDiagnostic =
				TEXT("Chrono Spawn requires a Paradox Character, valid target, and authoritative Time Loop.");
		}
		return false;
	}
	if (!Loop->CanStartChronoSpawnAction(
		*Character,
		*ChronoSpawn,
		GetOriginTag(),
		OutDiagnostic))
	{
		OutFailureReason =
			ParadoxGameplayTags::Result_Failure_ChronoSpawn_InvalidTarget;
		return false;
	}
	return Super::CanSatisfyInteractionPreconditions_Implementation(
		OutFailureReason,
		OutDiagnostic);
}

void UParadoxChronoSpawnAction::OnActionInit_Implementation()
{
	Super::OnActionInit_Implementation();
	UGameplayActionComponent* Actions = GetOwningComponent();
	TemporalAvatar = Actions
		? Cast<AParadoxCharacter>(Actions->GetOwner())
		: nullptr;
	if (AParadoxChronoSpawn* ChronoSpawn =
		Cast<AParadoxChronoSpawn>(GetInteractionTarget()))
	{
		TargetChronoSpawn = ChronoSpawn;
		ChronoSpawn->OnDestroyed.AddUniqueDynamic(
			this,
			&ThisClass::HandleChronoSpawnDestroyed);
	}
	TimeLoop = UE::Paradox::ChronoSpawnAction::Private::ResolveTimeLoop(*this);
}

void UParadoxChronoSpawnAction::ExecuteInteraction_Implementation()
{
	TryMaterialize();
}

void UParadoxChronoSpawnAction::OnActionCleanup_Implementation()
{
	if (AParadoxChronoSpawn* ChronoSpawn = TargetChronoSpawn.Get())
	{
		ChronoSpawn->OnChronoSpawnActivationChanged.RemoveDynamic(
			this,
			&ThisClass::HandleChronoSpawnActivationChanged);
		ChronoSpawn->OnDestroyed.RemoveDynamic(
			this,
			&ThisClass::HandleChronoSpawnDestroyed);
	}
	if (!bMaterialized && bWaitingForActivation)
	{
		if (UParadoxTimeLoopComponent* Loop = TimeLoop.Get())
		{
			if (AParadoxCharacter* Character = TemporalAvatar.Get())
			{
				Loop->CancelPendingChronoSpawnAction(*Character);
			}
		}
	}
	bWaitingForActivation = false;
	TargetChronoSpawn.Reset();
	TemporalAvatar.Reset();
	TimeLoop.Reset();
	PausedReplay.Reset();
	Super::OnActionCleanup_Implementation();
}

void UParadoxChronoSpawnAction::TryMaterialize()
{
	AParadoxCharacter* Character = TemporalAvatar.Get();
	AParadoxChronoSpawn* ChronoSpawn = TargetChronoSpawn.Get();
	UParadoxTimeLoopComponent* Loop = TimeLoop.Get();
	if (!Character || !ChronoSpawn || !Loop)
	{
		CompleteInteractionFailure(
			ParadoxGameplayTags::Result_Failure_ChronoSpawn_InvalidTarget,
			TEXT("Chrono Spawn action lost its avatar, target, or Time Loop."));
		return;
	}

	FString Diagnostic;
	const EParadoxChronoSpawnExecutionResult Result =
		Loop->TryExecuteChronoSpawnAction(
			*Character,
			*ChronoSpawn,
			Diagnostic);
	if (Result == EParadoxChronoSpawnExecutionResult::Failed)
	{
		CompleteInteractionFailure(
			ParadoxGameplayTags::Result_Failure_ChronoSpawn_Materialization,
			Diagnostic);
		return;
	}
	if (Result == EParadoxChronoSpawnExecutionResult::PendingActivation)
	{
		if (!bWaitingForActivation)
		{
			bWaitingForActivation = true;
			ChronoSpawn->OnChronoSpawnActivationChanged.AddUniqueDynamic(
				this,
				&ThisClass::HandleChronoSpawnActivationChanged);
			if (UIntentReplayComponent* Replay =
				Character->GetIntentReplayComponent();
				Replay
				&& Replay->GetPlaybackState()
					== EIntentReplayPlaybackState::Playing)
			{
				const FIntentReplayOperationResult PauseResult =
					Replay->PauseReplay();
				if (!PauseResult.Succeeded())
				{
					CompleteInteractionFailure(
						ParadoxGameplayTags::Result_Failure_ChronoSpawn_Materialization,
						PauseResult.Failure.DiagnosticMessage);
					return;
				}
				PausedReplay = Replay;
			}
		}
		return;
	}

	bMaterialized = true;
	bWaitingForActivation = false;
	UIntentReplayComponent* ReplayToResume = PausedReplay.Get();
	CompleteInteractionSuccess(GameplayActionTags::Result_Success, Diagnostic);
	if (ReplayToResume
		&& ReplayToResume->GetPlaybackState()
			== EIntentReplayPlaybackState::Paused)
	{
		const FIntentReplayOperationResult ResumeResult =
			ReplayToResume->ResumeReplay();
		if (!ResumeResult.Succeeded())
		{
			PARADOX_LOG_ERROR(
				TEXT("Chrono Spawn materialized '%s' but failed to resume replay: %s"),
				*GetNameSafe(Character),
				*ResumeResult.Failure.DiagnosticMessage);
		}
	}
}

void UParadoxChronoSpawnAction::HandleChronoSpawnActivationChanged(
	AParadoxChronoSpawn* ChronoSpawn,
	const bool bIsActive)
{
	if (bWaitingForActivation
		&& bIsActive
		&& ChronoSpawn == TargetChronoSpawn.Get())
	{
		TryMaterialize();
	}
}

void UParadoxChronoSpawnAction::HandleChronoSpawnDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == TargetChronoSpawn.Get())
	{
		CompleteInteractionFailure(
			ParadoxGameplayTags::Result_Failure_ChronoSpawn_InvalidTarget,
			TEXT("Chrono Spawn target was destroyed while materialization was pending."));
	}
}
