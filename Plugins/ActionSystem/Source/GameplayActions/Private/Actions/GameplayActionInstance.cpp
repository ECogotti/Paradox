#include "Actions/GameplayActionInstance.h"

#include "Actions/GameplayActionDefinition.h"
#include "Components/GameplayActionComponent.h"
#include "GameplayActionTags.h"

UWorld* UGameplayActionInstance::GetWorld() const
{
	return OwningComponent ? OwningComponent->GetWorld() : nullptr;
}

bool UGameplayActionInstance::CanStartAction_Implementation(FGameplayTag& OutFailureReason, FString& OutDiagnostic) const
{
	return true;
}

void UGameplayActionInstance::OnActionInit_Implementation()
{
}

void UGameplayActionInstance::OnActionStarted_Implementation()
{
}

void UGameplayActionInstance::OnActionPaused_Implementation()
{
}

void UGameplayActionInstance::OnActionResumed_Implementation()
{
}

void UGameplayActionInstance::OnActionTick_Implementation(float DeltaSeconds)
{
}

void UGameplayActionInstance::OnActionCancelled_Implementation(FGameplayTag ReasonTag)
{
}

void UGameplayActionInstance::OnActionInterrupted_Implementation(FGameplayTag ReasonTag)
{
}

void UGameplayActionInstance::OnActionAborted_Implementation(FGameplayTag ReasonTag)
{
}

void UGameplayActionInstance::OnActionCleanup_Implementation()
{
}

void UGameplayActionInstance::SucceedAction(FGameplayTag ReasonTag, const FString& DiagnosticMessage)
{
	if (OwningComponent)
	{
		OwningComponent->FinishActionFromInstance(this, EGameplayActionState::Succeeded,
			ReasonTag.IsValid() ? ReasonTag : GameplayActionTags::Result_Success, DiagnosticMessage);
	}
}

void UGameplayActionInstance::FailAction(FGameplayTag ReasonTag, const FString& DiagnosticMessage)
{
	if (OwningComponent)
	{
		OwningComponent->FinishActionFromInstance(this, EGameplayActionState::Failed,
			ReasonTag.IsValid() ? ReasonTag : GameplayActionTags::Result_Failure_Unspecified, DiagnosticMessage);
	}
}

void UGameplayActionInstance::SetActionTickEnabled(const bool bEnabled)
{
	if (bActionTickEnabled == bEnabled)
	{
		return;
	}

	bActionTickEnabled = bEnabled;
	if (OwningComponent)
	{
		OwningComponent->NotifyActionTickStateChanged(this);
	}
}

void UGameplayActionInstance::InitializeInstance(
	UGameplayActionComponent* InOwningComponent,
	UGameplayActionDefinition* InDefinition,
	const FGameplayActionRequest& Request,
	const FGameplayActionHandle InHandle,
	const int64 InSubmissionSequence,
	const int32 InPriority,
	const EGameplayActionBlockedPolicy InBlockedPolicy,
	const double InAcceptedTimeSeconds)
{
	OwningComponent = InOwningComponent;
	Definition = InDefinition;
	Handle = InHandle;
	State = EGameplayActionState::Created;
	Parameters = Request.Parameters;
	ActionTag = InDefinition->ActionTag;
	Priority = InPriority;
	BlockedPolicy = InBlockedPolicy;
	ExecutionLocks = InDefinition->ExecutionLocks;
	bInterruptible = InDefinition->bInterruptible;
	OptionalTimeout = InDefinition->OptionalTimeout;
	MaxQueueTimeSeconds = InDefinition->MaxQueueTimeSeconds;
	QueueElapsedSeconds = 0.0;
	JournalRequirement = InDefinition->JournalRequirement;
	OriginTag = Request.OriginTag;
	Correlation = Request.Correlation;
	Requester = Request.Requester;
	SubmissionSequence = InSubmissionSequence;
	bHasInitialized = false;
	bHasStarted = false;
	AcceptedTimeSeconds = InAcceptedTimeSeconds;
}
