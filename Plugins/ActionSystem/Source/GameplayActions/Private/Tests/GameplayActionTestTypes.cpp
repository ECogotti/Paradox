#include "Tests/GameplayActionTestTypes.h"

#include "GameplayActionTags.h"

int32 UGameplayActionTestInstance::InitCount = 0;
int32 UGameplayActionTestInstance::StartedCount = 0;
int32 UGameplayActionTestInstance::PausedCount = 0;
int32 UGameplayActionTestInstance::ResumedCount = 0;
int32 UGameplayActionTestInstance::CancelledCount = 0;
int32 UGameplayActionTestInstance::InterruptedCount = 0;
int32 UGameplayActionTestInstance::AbortedCount = 0;
int32 UGameplayActionTestInstance::CleanupCount = 0;
int32 UGameplayActionTestInstance::TickCount = 0;
float UGameplayActionTestInstance::LastTickDeltaSeconds = 0.0f;
EGameplayActionState UGameplayActionTestInstance::LastInitState = EGameplayActionState::Created;
bool UGameplayActionTestInstance::bSubmitDuringValidation = false;
FGameplayActionRequest UGameplayActionTestInstance::ValidationCallbackRequest;
EGameplayActionSubmissionStatus UGameplayActionTestInstance::ValidationReentrantSubmissionStatus = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
bool UGameplayActionTestInstance::bAttemptOperationsDuringInit = false;
FGameplayActionRequest UGameplayActionTestInstance::InitCallbackRequest;
EGameplayActionSubmissionStatus UGameplayActionTestInstance::InitReentrantSubmissionStatus = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
EGameplayActionOperationResult UGameplayActionTestInstance::InitReentrantCancelResult = EGameplayActionOperationResult::InvalidState;
EGameplayActionOperationResult UGameplayActionTestInstance::InitReentrantPauseResult = EGameplayActionOperationResult::InvalidState;

void UGameplayActionTestInstance::ResetCounters()
{
	InitCount = 0;
	StartedCount = 0;
	PausedCount = 0;
	ResumedCount = 0;
	CancelledCount = 0;
	InterruptedCount = 0;
	AbortedCount = 0;
	CleanupCount = 0;
	TickCount = 0;
	LastTickDeltaSeconds = 0.0f;
	LastInitState = EGameplayActionState::Created;
	bSubmitDuringValidation = false;
	ValidationCallbackRequest = FGameplayActionRequest();
	ValidationReentrantSubmissionStatus = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
	bAttemptOperationsDuringInit = false;
	InitCallbackRequest = FGameplayActionRequest();
	InitReentrantSubmissionStatus = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
	InitReentrantCancelResult = EGameplayActionOperationResult::InvalidState;
	InitReentrantPauseResult = EGameplayActionOperationResult::InvalidState;
}

void UGameplayActionTestInstance::CompleteForTest()
{
	SucceedAction(GameplayActionTags::Result_Success, TEXT("Completed by automation test."));
}

void UGameplayActionTestInstance::FailForTest()
{
	FailAction(GameplayActionTags::Result_Failure_Unspecified, TEXT("Failed by automation test."));
}

bool UGameplayActionTestInstance::CanStartAction_Implementation(FGameplayTag& OutFailureReason, FString& OutDiagnostic) const
{
	if (bSubmitDuringValidation && GetOwningComponent())
	{
		bSubmitDuringValidation = false;
		ValidationReentrantSubmissionStatus = GetOwningComponent()->SubmitAction(ValidationCallbackRequest).Status;
	}
	const TValueOrError<bool, EPropertyBagResult> RejectResult = GetParameters().GetValueBool(TEXT("RejectStart"));
	if (RejectResult.HasValue() && RejectResult.GetValue())
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		OutDiagnostic = TEXT("Rejected by the automation test action.");
		return false;
	}
	return true;
}

void UGameplayActionTestInstance::OnActionInit_Implementation()
{
	++InitCount;
	LastInitState = GetState();

	if (bAttemptOperationsDuringInit && GetOwningComponent())
	{
		bAttemptOperationsDuringInit = false;
		InitReentrantSubmissionStatus = GetOwningComponent()->SubmitAction(InitCallbackRequest).Status;
		InitReentrantCancelResult = GetOwningComponent()->CancelAction(GetHandle(), FGameplayTag());
		InitReentrantPauseResult = GetOwningComponent()->PauseActions();

		// Completion has no return value, so the automation test verifies that the outer action still
		// reaches Running and has no terminal result after this deliberately rejected request.
		SucceedAction(GameplayActionTags::Result_Success, TEXT("This completion must be rejected during Action Init."));
	}
}

void UGameplayActionTestInstance::OnActionStarted_Implementation()
{
	++StartedCount;
}

void UGameplayActionTestInstance::OnActionPaused_Implementation()
{
	++PausedCount;
}

void UGameplayActionTestInstance::OnActionResumed_Implementation()
{
	++ResumedCount;
}

void UGameplayActionTestInstance::OnActionTick_Implementation(const float DeltaSeconds)
{
	++TickCount;
	LastTickDeltaSeconds = DeltaSeconds;
	if (bCompleteOnNextTick)
	{
		bCompleteOnNextTick = false;
		CompleteForTest();
	}
}

void UGameplayActionTestInstance::OnActionCancelled_Implementation(FGameplayTag ReasonTag)
{
	++CancelledCount;
}

void UGameplayActionTestInstance::OnActionInterrupted_Implementation(FGameplayTag ReasonTag)
{
	++InterruptedCount;
}

void UGameplayActionTestInstance::OnActionAborted_Implementation(FGameplayTag ReasonTag)
{
	++AbortedCount;
}

void UGameplayActionTestInstance::OnActionCleanup_Implementation()
{
	++CleanupCount;
}

void UGameplayActionTestObserver::HandleActionEvent(const FGameplayActionEvent& Event)
{
	ObservedEvents.Add(Event);
	if (bSubmitOnEnded && Event.EventType == EGameplayActionEventType::Ended && Component)
	{
		bSubmitOnEnded = false;
		CallbackSubmissionResult = Component->SubmitAction(CallbackRequest);
	}
	if (bCancelOtherOnStarted && Event.EventType == EGameplayActionEventType::Started && Component && Event.Handle != HandleToCancel)
	{
		bCancelOtherOnStarted = false;
		CallbackCancelResult = Component->CancelAction(HandleToCancel, GameplayActionTags::Result_Cancelled_ByRequester);
	}
}

FGameplayActionJournalResult UGameplayActionTestObserver::WriteGameplayActionEvent_Implementation(const FGameplayActionEvent& Event)
{
	JournalEvents.Add(Event);
	if (bSubmitDuringJournal && Event.EventType == EGameplayActionEventType::Accepted && Component)
	{
		bSubmitDuringJournal = false;
		ReentrantJournalSubmissionStatus = Component->SubmitAction(CallbackRequest).Status;
	}

	FGameplayActionJournalResult Result;
	Result.Status = bAcceptJournal ? EGameplayActionJournalWriteStatus::Accepted : EGameplayActionJournalWriteStatus::Rejected;
	if (!bAcceptJournal)
	{
		Result.ReasonTag = GameplayActionTags::Result_Failure_JournalRejected;
		Result.DiagnosticMessage = TEXT("Rejected by the automation test journal.");
	}
	return Result;
}
