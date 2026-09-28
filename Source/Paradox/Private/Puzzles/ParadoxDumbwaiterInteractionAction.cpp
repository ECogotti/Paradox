#include "Puzzles/ParadoxDumbwaiterInteractionAction.h"

#include "GameplayActionTags.h"
#include "Paradox.h"
#include "Puzzles/ParadoxDumbwaiter.h"

namespace UE::Paradox::DumbwaiterInteraction::Private
{
	FGameplayTag FailureTagForStatus(const EParadoxTransferOperationStatus Status)
	{
		switch (Status)
		{
		case EParadoxTransferOperationStatus::InvalidRequester:
		case EParadoxTransferOperationStatus::RequesterBlocked:
			return ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		case EParadoxTransferOperationStatus::MissingLinkedEndpoint:
		case EParadoxTransferOperationStatus::SelfLinkedEndpoint:
		case EParadoxTransferOperationStatus::NonReciprocalPair:
		case EParadoxTransferOperationStatus::IncompatibleEndpoint:
		case EParadoxTransferOperationStatus::DifferentWorld:
			return ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable;
		default:
			return ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
		}
	}
}

bool UParadoxSendDumbwaiterInteractionAction::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	AActor* RequesterActor = GetInteractionRequester();
	AParadoxDumbwaiter* Dumbwaiter =
		Cast<AParadoxDumbwaiter>(GetInteractionTarget());
	if (!RequesterActor || !Dumbwaiter)
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		OutDiagnostic = TEXT("Send requires a valid requester and Dumbwaiter target.");
		return false;
	}
	const FParadoxTransferOperationResult Result =
		Dumbwaiter->EvaluateSendCargo(RequesterActor, const_cast<ThisClass*>(this));
	if (Result.IsSuccess())
	{
		return true;
	}
	OutFailureReason =
		UE::Paradox::DumbwaiterInteraction::Private::FailureTagForStatus(Result.Status);
	OutDiagnostic = Result.DiagnosticMessage;
	return false;
}

void UParadoxSendDumbwaiterInteractionAction::ExecuteInteraction_Implementation()
{
	AActor* RequesterActor = GetInteractionRequester();
	AParadoxDumbwaiter* Dumbwaiter =
		Cast<AParadoxDumbwaiter>(GetInteractionTarget());
	const FParadoxTransferOperationResult Result = Dumbwaiter
		? Dumbwaiter->TrySendCargo(RequesterActor, this)
		: FParadoxTransferOperationResult();
	if (Result.IsSuccess())
	{
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, Result.DiagnosticMessage);
		return;
	}
	CompleteInteractionFailure(
		UE::Paradox::DumbwaiterInteraction::Private::FailureTagForStatus(Result.Status),
		Result.DiagnosticMessage);
}
