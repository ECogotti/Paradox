#include "Actions/ParadoxTimeTravelAction.h"

#include "Characters/ParadoxCharacter.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "Controllers/ParadoxPlayerController.h"
#include "GameModes/ParadoxGameMode.h"
#include "GameplayActionTags.h"
#include "NiagaraComponent.h"
#include "Paradox.h"
#include "Perception/ParadoxTemporalVisionComponent.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"

bool UParadoxTimeTravelAction::CanStartAction_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	const UGameplayActionComponent* ActionComponent = GetOwningComponent();
	const AParadoxCharacter* Character = ActionComponent
		? Cast<AParadoxCharacter>(ActionComponent->GetOwner())
		: nullptr;
	const AController* Controller = Character
		? Character->GetController()
		: nullptr;
	if (!Character || !Character->GetTimeTravelNiagaraComponent())
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		OutDiagnostic =
			TEXT("Time Travel requires an AParadoxCharacter with its native Niagara component.");
		return false;
	}
	if (!Controller
		|| (!Controller->IsA<AParadoxPlayerController>()
			&& !Controller->IsA<AParadoxCloneController>()))
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		OutDiagnostic =
			TEXT("Time Travel requires either a Paradox Player Controller or a Paradox Clone Controller.");
		return false;
	}
	return true;
}

void UParadoxTimeTravelAction::OnActionStarted_Implementation()
{
	UGameplayActionComponent* ActionComponent = GetOwningComponent();
	AParadoxCharacter* Character = ActionComponent
		? Cast<AParadoxCharacter>(ActionComponent->GetOwner())
		: nullptr;
	UNiagaraComponent* NiagaraComponent = Character
		? Character->GetTimeTravelNiagaraComponent()
		: nullptr;
	if (!Character || !NiagaraComponent)
	{
		FailAction(
			GameplayActionTags::Result_Failure_CannotStart,
			TEXT("Time Travel lost its validated Character or Niagara component before execution."));
		return;
	}

	TimeTravelCharacter = Character;
	ActiveNiagaraComponent = NiagaraComponent;

	// Once a clone starts its recorded departure it cannot be diverted by a new observation.
	// It remains a perceivable source until the VFX finishes, matching the player's original run.
	if (AParadoxCloneCharacter* Clone = Cast<AParadoxCloneCharacter>(Character))
	{
		if (AParadoxCloneController* CloneController =
			Cast<AParadoxCloneController>(Clone->GetController()))
		{
			if (UPerceptionKnowledgeListenerComponent* Listener =
				CloneController->GetPerceptionKnowledgeListener())
			{
				const FPerceptionKnowledgeOperationResult DisableResult =
					Listener->SetListenerEnabled(false);
				if (!DisableResult.IsSuccess())
				{
					PARADOX_LOG_WARNING(
						TEXT("Clone '%s' started recorded Time Travel, but its listener could not be disabled: %s"),
						*GetNameSafe(Clone),
						*DisableResult.Message);
				}
			}
		}
		if (UParadoxTemporalVisionComponent* Vision =
			Clone->GetTemporalVisionComponent())
		{
			Vision->DisableTemporalDetection(true);
		}
	}

	if (!NiagaraComponent->GetAsset())
	{
		CompleteTimeTravel();
		return;
	}

	NiagaraComponent->OnSystemFinished.AddUniqueDynamic(
		this,
		&UParadoxTimeTravelAction::HandleTimeTravelSystemFinished);
	NiagaraComponent->Activate(true);
	if (!NiagaraComponent->IsActive() && !bCompletionRequested)
	{
		// A valid but non-activating system must not strand the time loop indefinitely.
		CompleteTimeTravel();
	}
}

void UParadoxTimeTravelAction::OnActionCleanup_Implementation()
{
	if (UNiagaraComponent* NiagaraComponent = ActiveNiagaraComponent.Get())
	{
		NiagaraComponent->OnSystemFinished.RemoveDynamic(
			this,
			&UParadoxTimeTravelAction::HandleTimeTravelSystemFinished);
		if (!bDepartureCommitted && NiagaraComponent->IsActive())
		{
			NiagaraComponent->DeactivateImmediate();
		}
	}

	if (!bDepartureCommitted)
	{
		if (AParadoxCharacter* Character = TimeTravelCharacter.Get())
		{
			if (AParadoxPlayerController* PlayerController =
				Cast<AParadoxPlayerController>(Character->GetController()))
			{
				PlayerController->ClearPendingRecordedTimeTravel();
			}
		}
	}

	ActiveNiagaraComponent.Reset();
	TimeTravelCharacter.Reset();
	Super::OnActionCleanup_Implementation();
}

void UParadoxTimeTravelAction::HandleTimeTravelSystemFinished(
	UNiagaraComponent* FinishedComponent)
{
	if (!bCompletionRequested
		&& FinishedComponent == ActiveNiagaraComponent.Get())
	{
		CompleteTimeTravel();
	}
}

void UParadoxTimeTravelAction::CompleteTimeTravel()
{
	if (bCompletionRequested)
	{
		return;
	}
	bCompletionRequested = true;

	AParadoxCharacter* Character = TimeTravelCharacter.Get();
	AController* Controller = Character ? Character->GetController() : nullptr;
	if (AParadoxPlayerController* PlayerController =
		Cast<AParadoxPlayerController>(Controller))
	{
		bDepartureCommitted = true;
		PlayerController->ScheduleRecordedTimeTravelExecution();
		SucceedAction(
			GameplayActionTags::Result_Success,
			TEXT("Player time-travel VFX completed; authoritative rewind scheduled."));
		return;
	}

	if (AParadoxCloneCharacter* Clone = Cast<AParadoxCloneCharacter>(Character))
	{
		AParadoxGameMode* GameMode = GetWorld()
			? GetWorld()->GetAuthGameMode<AParadoxGameMode>()
			: nullptr;
		UParadoxTimeLoopComponent* TimeLoop = GameMode
			? GameMode->GetTimeLoopComponent()
			: nullptr;
		FString DepartureDiagnostic;
		if (!TimeLoop
			|| !TimeLoop->CompleteCloneTimeTravelDeparture(
				*Clone,
				DepartureDiagnostic))
		{
			FailAction(
				GameplayActionTags::Result_Failure_CannotStart,
				DepartureDiagnostic.IsEmpty()
					? TEXT("Clone time-travel departure has no authoritative time loop.")
					: DepartureDiagnostic);
			return;
		}

		bDepartureCommitted = true;
		SucceedAction(
			GameplayActionTags::Result_Success,
			TEXT("Clone time-travel VFX completed; clone retired from the active run."));
		return;
	}

	FailAction(
		GameplayActionTags::Result_Failure_CannotStart,
		TEXT("Time Travel completed without a supported controller role."));
}
