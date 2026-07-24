#include "Playback/IntentReplayExecutionStrategy.h"

#include "Components/GameplayActionComponent.h"

FGameplayActionSubmissionResult UIntentReplayExecutionStrategy::SubmitPreparedRequest(
	UGameplayActionComponent* ActionComponent,
	const FGameplayActionRequest& Request) const
{
	// Abstract policy misuse remains observable instead of silently pretending a request was sent.
	FGameplayActionSubmissionResult Result;
	Result.Status = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
	Result.DiagnosticMessage = TEXT("The abstract Intent Replay execution strategy cannot submit requests.");
	return Result;
}

FGameplayActionSubmissionResult UIntentReplayDirectExecutionStrategy::SubmitPreparedRequest(
	UGameplayActionComponent* ActionComponent,
	const FGameplayActionRequest& Request) const
{
	if (!IsValid(ActionComponent))
	{
		FGameplayActionSubmissionResult Result;
		Result.Status = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
		Result.DiagnosticMessage = TEXT("The target Gameplay Action Component is invalid.");
		return Result;
	}
	// SubmitAction creates a new recipient-local action instance and handle; the recorded source
	// action is never resumed or transferred.
	return ActionComponent->SubmitAction(Request);
}
