#include "Components/GameplayActionComponent.h"

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayActionInstance.h"
#include "GameplayActionsModule.h"
#include "GameplayActionTags.h"
#include "Interfaces/GameplayActionJournalSink.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	bool IsRuntimeState(const EGameplayActionState State)
	{
		return State == EGameplayActionState::Queued
			|| State == EGameplayActionState::Starting
			|| State == EGameplayActionState::Running
			|| State == EGameplayActionState::Paused
			|| State == EGameplayActionState::Ending;
	}
}

UGameplayActionComponent::UGameplayActionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

void UGameplayActionComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameplayActions_ActionTick);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bActionsPaused || bShuttingDown || (GetWorld() && GetWorld()->IsPaused()))
	{
		RefreshComponentTickEnabled();
		return;
	}

	// Capture running handles before timeout events are delivered. Actions started by an Ended observer
	// therefore begin receiving Action Tick on the next frame, matching normal lifecycle reentrancy.
	TArray<FGameplayActionHandle> Handles = ActiveHandles;
	UpdateQueuedTimeouts(DeltaTime);
	FlushEventQueue();

	// A snapshot makes tick callbacks free to finish, cancel, or submit actions without invalidating iteration.
	SortHandlesBySchedulerOrder(Handles);
	for (const FGameplayActionHandle Handle : Handles)
	{
		UGameplayActionInstance* Instance = GetActionInstance(Handle);
		if (Instance && Instance->State == EGameplayActionState::Running && Instance->bActionTickEnabled)
		{
			Instance->OnActionTick(DeltaTime);
		}
	}

	RefreshComponentTickEnabled();
}

FGameplayActionSubmissionResult UGameplayActionComponent::SubmitAction(const FGameplayActionRequest& Request)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameplayActions_Submit);

	if (!IsInGameThread())
	{
		return MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedInvalidRequest,
			GameplayActionTags::Result_Failure_InvalidRequest, TEXT("Gameplay Actions can only be submitted on the Game Thread."));
	}

	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedReentrant,
			GameplayActionTags::Result_Failure_InvalidRequest,
			TEXT("Submission is not allowed during initial journal processing, action validation, or Action Init."));
		QueueEvent(BuildRequestEvent(Request, nullptr, EGameplayActionEventType::Rejected, Rejected));
		FlushEventQueue();
		return Rejected;
	}

	if (!bAcceptingSubmissions || bShuttingDown)
	{
		FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedNotAccepting,
			GameplayActionTags::Result_Failure_InvalidRequest, TEXT("The component is not accepting submissions."));
		QueueEvent(BuildRequestEvent(Request, nullptr, EGameplayActionEventType::Rejected, Rejected));
		FlushEventQueue();
		return Rejected;
	}

	FValidatedRequest Validated;
	FString ValidationDiagnostic;
	if (!ValidateRequest(Request, Validated, ValidationDiagnostic))
	{
		FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedInvalidRequest,
			GameplayActionTags::Result_Failure_InvalidRequest, MoveTemp(ValidationDiagnostic));
		QueueEvent(BuildRequestEvent(Request, nullptr, EGameplayActionEventType::Rejected, Rejected));
		FlushEventQueue();
		return Rejected;
	}

	const FGameplayActionHandle CandidateHandle(NextHandleValue++);
	const int64 CandidateSequence = NextSubmissionSequence++;
	UGameplayActionInstance* Instance = NewObject<UGameplayActionInstance>(this, Validated.Definition->InstanceClass, NAME_None, RF_Transient);
	if (!Instance)
	{
		FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedInvalidRequest,
			GameplayActionTags::Result_Failure_InvalidRequest, TEXT("Failed to create the action instance."));
		QueueEvent(BuildRequestEvent(Request, &Validated, EGameplayActionEventType::Rejected, Rejected));
		FlushEventQueue();
		return Rejected;
	}

	const double AcceptedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Instance->InitializeInstance(this, Validated.Definition, Request, CandidateHandle, CandidateSequence,
		Validated.Priority, Validated.BlockedPolicy, AcceptedTime);

	FGameplayTag CanStartReason;
	FString CanStartDiagnostic;
	if (!ValidateInstanceCanStart(*Instance, CanStartReason, CanStartDiagnostic))
	{
		FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedValidation,
			CanStartReason.IsValid() ? CanStartReason : GameplayActionTags::Result_Failure_CannotStart,
			CanStartDiagnostic.IsEmpty() ? TEXT("The action instance rejected the request during validation.") : MoveTemp(CanStartDiagnostic));
		QueueEvent(BuildRequestEvent(Request, &Validated, EGameplayActionEventType::Rejected, Rejected));
		FlushEventQueue();
		return Rejected;
	}
	if (!bAcceptingSubmissions || bShuttingDown)
	{
		FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedNotAccepting,
			GameplayActionTags::Result_Failure_InvalidRequest,
			TEXT("The component stopped accepting submissions during action validation."));
		QueueEvent(BuildRequestEvent(Request, &Validated, EGameplayActionEventType::Rejected, Rejected));
		FlushEventQueue();
		return Rejected;
	}

	TArray<FGameplayActionHandle> Conflicts;
	FGameplayActionSubmissionResult Submission = EvaluateSchedule(*Instance, Conflicts);
	if (!Submission.IsAccepted())
	{
		QueueEvent(BuildRequestEvent(Request, &Validated, EGameplayActionEventType::Rejected, Submission));
		FlushEventQueue();
		return Submission;
	}

	Submission.Handle = CandidateHandle;
	FGameplayActionEvent AcceptedEvent = BuildEvent(*Instance, EGameplayActionEventType::Accepted);
	AcceptedEvent.bHasSubmissionResult = true;
	AcceptedEvent.SubmissionResult = Submission;

	if (Validated.Definition->JournalRequirement != EGameplayActionJournalRequirement::Disabled)
	{
		if (!JournalSink.GetObject())
		{
			if (Validated.Definition->JournalRequirement == EGameplayActionJournalRequirement::Required)
			{
				FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedJournal,
					GameplayActionTags::Result_Failure_JournalRejected, TEXT("A required journal sink is not registered."));
				QueueEvent(BuildRequestEvent(Request, &Validated, EGameplayActionEventType::Rejected, Rejected), true);
				FlushEventQueue();
				return Rejected;
			}
		}
		else
		{
			FGameplayActionJournalResult JournalResult;
			{
				TGuardValue<bool> JournalGuard(bInInitialJournalTransaction, true);
				JournalResult = WriteJournalEvent(AcceptedEvent);
			}
			if (!JournalResult.IsAccepted() && Validated.Definition->JournalRequirement == EGameplayActionJournalRequirement::Required)
			{
				FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedJournal,
					JournalResult.ReasonTag.IsValid() ? JournalResult.ReasonTag : GameplayActionTags::Result_Failure_JournalRejected,
					JournalResult.DiagnosticMessage.IsEmpty() ? TEXT("The required journal sink rejected the Accepted event.") : JournalResult.DiagnosticMessage);
				QueueEvent(BuildRequestEvent(Request, &Validated, EGameplayActionEventType::Rejected, Rejected), true);
				FlushEventQueue();
				return Rejected;
			}
			if (!JournalResult.IsAccepted())
			{
				GAMEPLAYACTIONS_LOG_WARNING(TEXT("Optional journal rejected action %lld: %s"),
					CandidateHandle.GetValue(), *JournalResult.DiagnosticMessage);
			}
		}
	}
	if (!bAcceptingSubmissions || bShuttingDown)
	{
		FGameplayActionSubmissionResult Rejected = MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedNotAccepting,
			GameplayActionTags::Result_Failure_InvalidRequest,
			TEXT("The component stopped accepting submissions during initial journal processing."));
		QueueEvent(BuildRequestEvent(Request, &Validated, EGameplayActionEventType::Rejected, Rejected), true);
		FlushEventQueue();
		return Rejected;
	}

	ActionsByHandle.Add(CandidateHandle, Instance);
	QueueEvent(MoveTemp(AcceptedEvent), true);

	if (Submission.Status == EGameplayActionSubmissionStatus::AcceptedQueued)
	{
		Instance->State = EGameplayActionState::Queued;
		QueuedHandles.Add(CandidateHandle);
		InitializeAcceptedAction(*Instance);
		RecordSchedulerDecision(FString::Printf(TEXT("Queued action %lld."), CandidateHandle.GetValue()));
	}
	else
	{
		Instance->State = EGameplayActionState::Starting;
		InitializeAcceptedAction(*Instance);
		SortHandlesBySchedulerOrder(Conflicts);
		for (const FGameplayActionHandle ConflictHandle : Conflicts)
		{
			if (UGameplayActionInstance* Conflict = GetActionInstance(ConflictHandle))
			{
				FinishActionInternal(*Conflict, EGameplayActionState::Interrupted,
					GameplayActionTags::Result_Interrupted_HigherPriority,
					FString::Printf(TEXT("Preempted by action %lld."), CandidateHandle.GetValue()), false);
			}
		}
		StartAction(*Instance);
		RecordSchedulerDecision(FString::Printf(TEXT("Started action %lld."), CandidateHandle.GetValue()));
		EvaluateQueuedActions();
	}

	FlushEventQueue();
	return Submission;
}

FGameplayActionSubmissionResult UGameplayActionComponent::PreflightAction(const FGameplayActionRequest& Request)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameplayActions_Preflight);

	if (!IsInGameThread() || bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		return MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedReentrant,
			GameplayActionTags::Result_Failure_InvalidRequest, TEXT("Preflight is unavailable in the current callback or thread."));
	}
	if (!bAcceptingSubmissions || bShuttingDown)
	{
		return MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedNotAccepting,
			GameplayActionTags::Result_Failure_InvalidRequest, TEXT("The component is not accepting submissions."));
	}

	FValidatedRequest Validated;
	FString Diagnostic;
	if (!ValidateRequest(Request, Validated, Diagnostic))
	{
		return MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedInvalidRequest,
			GameplayActionTags::Result_Failure_InvalidRequest, MoveTemp(Diagnostic));
	}
	if (Validated.Definition->JournalRequirement == EGameplayActionJournalRequirement::Required && !JournalSink.GetObject())
	{
		return MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedJournal,
			GameplayActionTags::Result_Failure_JournalRejected, TEXT("A required journal sink is not registered."));
	}

	UGameplayActionInstance* Instance = NewObject<UGameplayActionInstance>(this, Validated.Definition->InstanceClass, NAME_None, RF_Transient);
	if (!Instance)
	{
		return MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedInvalidRequest,
			GameplayActionTags::Result_Failure_InvalidRequest, TEXT("Failed to create a temporary action instance."));
	}
	Instance->InitializeInstance(this, Validated.Definition, Request, FGameplayActionHandle(), 0,
		Validated.Priority, Validated.BlockedPolicy, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);

	FGameplayTag Reason;
	if (!ValidateInstanceCanStart(*Instance, Reason, Diagnostic))
	{
		return MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedValidation,
			Reason.IsValid() ? Reason : GameplayActionTags::Result_Failure_CannotStart, MoveTemp(Diagnostic));
	}

	TArray<FGameplayActionHandle> Conflicts;
	return EvaluateSchedule(*Instance, Conflicts);
}

EGameplayActionOperationResult UGameplayActionComponent::CancelAction(const FGameplayActionHandle Handle, FGameplayTag ReasonTag)
{
	if (!IsInGameThread())
	{
		return EGameplayActionOperationResult::InvalidState;
	}
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		return EGameplayActionOperationResult::RejectedReentrant;
	}
	UGameplayActionInstance* Instance = GetActionInstance(Handle);
	if (!Instance)
	{
		return EGameplayActionOperationResult::HandleNotFound;
	}
	if (!IsRuntimeState(Instance->State) || Instance->State == EGameplayActionState::Ending)
	{
		return EGameplayActionOperationResult::InvalidState;
	}

	FinishActionInternal(*Instance, EGameplayActionState::Cancelled,
		ReasonTag.IsValid() ? ReasonTag : GameplayActionTags::Result_Cancelled_ByRequester,
		TEXT("Action cancelled by request."), true);
	FlushEventQueue();
	return EGameplayActionOperationResult::Succeeded;
}

int32 UGameplayActionComponent::AbortAllActions(FGameplayTag ReasonTag)
{
	if (!IsInGameThread())
	{
		return 0;
	}
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		GAMEPLAYACTIONS_LOG_WARNING(TEXT("%s rejected reentrant Abort All Actions during Action Init, validation, or initial journal processing."),
			*GetNameSafe(this));
		return 0;
	}

	TArray<FGameplayActionHandle> Handles;
	ActionsByHandle.GetKeys(Handles);
	Handles.RemoveAll([this](const FGameplayActionHandle Handle)
	{
		const UGameplayActionInstance* Instance = GetActionInstance(Handle);
		return !Instance || !IsRuntimeState(Instance->State) || Instance->State == EGameplayActionState::Ending;
	});
	SortHandlesBySchedulerOrder(Handles);

	int32 AbortedCount = 0;
	for (const FGameplayActionHandle Handle : Handles)
	{
		if (UGameplayActionInstance* Instance = GetActionInstance(Handle))
		{
			AbortedCount += FinishActionInternal(*Instance, EGameplayActionState::Aborted,
				ReasonTag.IsValid() ? ReasonTag : GameplayActionTags::Result_Aborted_SystemReset,
				TEXT("Action aborted by component."), false) ? 1 : 0;
		}
	}
	if (!bShuttingDown)
	{
		EvaluateQueuedActions();
	}
	FlushEventQueue();
	return AbortedCount;
}

EGameplayActionOperationResult UGameplayActionComponent::PauseActions()
{
	if (!IsInGameThread())
	{
		return EGameplayActionOperationResult::InvalidState;
	}
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		return EGameplayActionOperationResult::RejectedReentrant;
	}
	if (bActionsPaused)
	{
		return EGameplayActionOperationResult::InvalidState;
	}
	bActionsPaused = true;

	TArray<FGameplayActionHandle> Handles = ActiveHandles;
	SortHandlesBySchedulerOrder(Handles);
	for (const FGameplayActionHandle Handle : Handles)
	{
		if (UGameplayActionInstance* Instance = GetActionInstance(Handle); Instance && Instance->State == EGameplayActionState::Running)
		{
			Instance->State = EGameplayActionState::Paused;
			QueueEvent(BuildEvent(*Instance, EGameplayActionEventType::Paused));
			Instance->OnActionPaused();
		}
	}
	RecordSchedulerDecision(TEXT("Component paused; running actions retain their locks."));
	RefreshComponentTickEnabled();
	FlushEventQueue();
	return EGameplayActionOperationResult::Succeeded;
}

EGameplayActionOperationResult UGameplayActionComponent::ResumeActions()
{
	if (!IsInGameThread())
	{
		return EGameplayActionOperationResult::InvalidState;
	}
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		return EGameplayActionOperationResult::RejectedReentrant;
	}
	if (!bActionsPaused)
	{
		return EGameplayActionOperationResult::InvalidState;
	}
	bActionsPaused = false;

	TArray<FGameplayActionHandle> Handles = ActiveHandles;
	SortHandlesBySchedulerOrder(Handles);
	for (const FGameplayActionHandle Handle : Handles)
	{
		if (UGameplayActionInstance* Instance = GetActionInstance(Handle); Instance && Instance->State == EGameplayActionState::Paused)
		{
			Instance->State = EGameplayActionState::Running;
			QueueEvent(BuildEvent(*Instance, EGameplayActionEventType::Resumed));
			Instance->OnActionResumed();
		}
	}
	RecordSchedulerDecision(TEXT("Component resumed; queued actions are being evaluated."));
	EvaluateQueuedActions();
	RefreshComponentTickEnabled();
	FlushEventQueue();
	return EGameplayActionOperationResult::Succeeded;
}

bool UGameplayActionComponent::GetActionState(const FGameplayActionHandle Handle, EGameplayActionState& OutState) const
{
	if (const UGameplayActionInstance* Instance = GetActionInstance(Handle))
	{
		OutState = Instance->State;
		return true;
	}
	if (const EGameplayActionState* TerminalState = TerminalStates.Find(Handle))
	{
		OutState = *TerminalState;
		return true;
	}
	return false;
}

bool UGameplayActionComponent::GetActionResult(const FGameplayActionHandle Handle, FGameplayActionResult& OutResult) const
{
	if (const FGameplayActionResult* Result = TerminalResults.Find(Handle))
	{
		OutResult = *Result;
		return true;
	}
	return false;
}

UGameplayActionInstance* UGameplayActionComponent::GetActionInstance(const FGameplayActionHandle Handle) const
{
	const TObjectPtr<UGameplayActionInstance>* Found = ActionsByHandle.Find(Handle);
	return Found ? Found->Get() : nullptr;
}

bool UGameplayActionComponent::RegisterJournalSink(UObject* Sink)
{
	if (!IsInGameThread())
	{
		return false;
	}
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		GAMEPLAYACTIONS_LOG_WARNING(TEXT("%s rejected journal registration during Action Init, validation, or initial journal processing."),
			*GetNameSafe(this));
		return false;
	}
	if (!IsValid(Sink) || !Sink->GetClass()->ImplementsInterface(UGameplayActionJournalSink::StaticClass()))
	{
		return false;
	}
	if (JournalSink.GetObject() && JournalSink.GetObject() != Sink)
	{
		return false;
	}

	JournalSink.SetObject(Sink);
	JournalSink.SetInterface(Cast<IGameplayActionJournalSink>(Sink));
	return true;
}

void UGameplayActionComponent::UnregisterJournalSink(UObject* Sink)
{
	if (!IsInGameThread())
	{
		return;
	}
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		GAMEPLAYACTIONS_LOG_WARNING(TEXT("%s rejected journal unregistration during Action Init, validation, or initial journal processing."),
			*GetNameSafe(this));
		return;
	}
	if (!Sink || JournalSink.GetObject() == Sink)
	{
		JournalSink = nullptr;
	}
}

FGameplayActionDebugSnapshot UGameplayActionComponent::GetDebugSnapshot() const
{
	FGameplayActionDebugSnapshot Snapshot;
	Snapshot.Owner = GetOwner();
	Snapshot.bPaused = bActionsPaused;
	Snapshot.bAcceptingSubmissions = IsAcceptingSubmissions();
	Snapshot.bHasLastResult = bHasLastResult;
	Snapshot.LastResult = LastResult;
	Snapshot.LastSchedulerDecision = LastSchedulerDecision;

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	auto AddEntry = [this, Now](const FGameplayActionHandle Handle, TArray<FGameplayActionDebugEntry>& Destination)
	{
		if (const UGameplayActionInstance* Instance = GetActionInstance(Handle))
		{
			FGameplayActionDebugEntry& Entry = Destination.AddDefaulted_GetRef();
			Entry.Handle = Handle;
			Entry.ActionTag = Instance->ActionTag;
			Entry.State = Instance->State;
			Entry.Priority = Instance->Priority;
			Entry.ExecutionLocks = Instance->ExecutionLocks;
			Entry.SubmissionSequence = Instance->SubmissionSequence;
			Entry.ElapsedSeconds = FMath::Max(0.0, Now - Instance->AcceptedTimeSeconds);
			Entry.MaxQueueTimeSeconds = Instance->MaxQueueTimeSeconds;
			Entry.QueueElapsedSeconds = Instance->QueueElapsedSeconds;
			Entry.QueueRemainingSeconds = Instance->GetQueueRemainingSeconds();
			Entry.bHasQueueTimeout = Instance->HasQueueTimeout();
			Entry.bQueueTimeUnlimited = Instance->IsQueueTimeUnlimited();
		}
	};

	for (const FGameplayActionHandle Handle : ActiveHandles)
	{
		AddEntry(Handle, Snapshot.ActiveActions);
	}
	for (const FGameplayActionHandle Handle : QueuedHandles)
	{
		AddEntry(Handle, Snapshot.QueuedActions);
	}
	return Snapshot;
}

void UGameplayActionComponent::FinishActionFromInstance(
	UGameplayActionInstance* Instance,
	const EGameplayActionState TerminalState,
	const FGameplayTag ReasonTag,
	const FString& DiagnosticMessage)
{
	if (!IsInGameThread() || !Instance || Instance->OwningComponent != this)
	{
		return;
	}
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		GAMEPLAYACTIONS_LOG_WARNING(
			TEXT("%s rejected terminal completion for action %lld during Action Init, validation, or initial journal processing."),
			*GetNameSafe(this), Instance->Handle.GetValue());
		return;
	}
	FinishActionInternal(*Instance, TerminalState, ReasonTag, DiagnosticMessage, true);
	FlushEventQueue();
}

void UGameplayActionComponent::Activate(const bool bReset)
{
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		GAMEPLAYACTIONS_LOG_WARNING(TEXT("%s rejected reentrant activation during Action Init, validation, or initial journal processing."),
			*GetNameSafe(this));
		return;
	}
	Super::Activate(bReset);
	if (bReset)
	{
		ShutdownActions(GameplayActionTags::Result_Aborted_SystemReset);
	}
	bShuttingDown = false;
	bAcceptingSubmissions = true;
	RefreshComponentTickEnabled();
}

void UGameplayActionComponent::Deactivate()
{
	if (bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		GAMEPLAYACTIONS_LOG_WARNING(TEXT("%s rejected reentrant deactivation during Action Init, validation, or initial journal processing."),
			*GetNameSafe(this));
		return;
	}
	ShutdownActions(GameplayActionTags::Result_Aborted_ComponentDeactivated);
	Super::Deactivate();
}

void UGameplayActionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownActions(GameplayActionTags::Result_Aborted_OwnerEndPlay);
	Super::EndPlay(EndPlayReason);
}

void UGameplayActionComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	ShutdownActions(GameplayActionTags::Result_Aborted_SystemReset);
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UGameplayActionComponent::BeginDestroy()
{
	bAcceptingSubmissions = false;
	bShuttingDown = true;
	SetComponentTickEnabled(false);
	JournalSink = nullptr;
	PendingEvents.Reset();
	PendingEventSkipJournal.Reset();
	ActionsByHandle.Reset();
	ActiveHandles.Reset();
	QueuedHandles.Reset();
	Super::BeginDestroy();
}

bool UGameplayActionComponent::ValidateRequest(
	const FGameplayActionRequest& Request,
	FValidatedRequest& OutValidated,
	FString& OutDiagnostic) const
{
	if (!Request.bInitialized || !IsValid(Request.Definition))
	{
		OutDiagnostic = TEXT("The request was not created by the authoritative request factory.");
		return false;
	}
	if (!Request.Definition->InstanceClass || Request.Definition->InstanceClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutDiagnostic = TEXT("The Definition does not specify a concrete action instance class.");
		return false;
	}
	if (!Request.Definition->ActionTag.IsValid())
	{
		OutDiagnostic = TEXT("The Definition is missing its required Action Tag.");
		return false;
	}
	if (!Request.Parameters.IsValid() || !Request.Definition->DefaultParameters.IsValid()
		|| !Request.Parameters.HasSameLayout(Request.Definition->DefaultParameters))
	{
		OutDiagnostic = TEXT("The request Property Bag schema no longer matches the Definition schema.");
		return false;
	}
	if (Request.Definition->OptionalTimeout.bEnabled
		&& (!FMath::IsFinite(Request.Definition->OptionalTimeout.DurationSeconds)
			|| Request.Definition->OptionalTimeout.DurationSeconds < 0.0))
	{
		OutDiagnostic = TEXT("The Definition Optional Timeout duration must be finite and cannot be negative.");
		return false;
	}
	if (!FMath::IsFinite(Request.Definition->MaxQueueTimeSeconds) || Request.Definition->MaxQueueTimeSeconds < 0.0)
	{
		OutDiagnostic = TEXT("The Definition Max Queue Time must be finite and cannot be negative.");
		return false;
	}
	for (const FGameplayTag Lock : Request.Definition->ExecutionLocks)
	{
		if (!Lock.MatchesTag(GameplayActionTags::Lock_Root))
		{
			OutDiagnostic = FString::Printf(TEXT("Execution Lock '%s' is outside GameplayAction.Lock."), *Lock.ToString());
			return false;
		}
	}

	OutValidated.Definition = Request.Definition;
	OutValidated.Priority = Request.bOverridePriority ? Request.PriorityOverride : Request.Definition->DefaultPriority;
	OutValidated.BlockedPolicy = Request.bOverrideBlockedPolicy ? Request.BlockedPolicyOverride : Request.Definition->BlockedPolicy;
	return true;
}

bool UGameplayActionComponent::ValidateInstanceCanStart(
	UGameplayActionInstance& Instance,
	FGameplayTag& OutReason,
	FString& OutDiagnostic)
{
	TGuardValue<bool> ValidationGuard(bInValidationCallback, true);
	return Instance.CanStartAction(OutReason, OutDiagnostic);
}

FGameplayActionSubmissionResult UGameplayActionComponent::EvaluateSchedule(
	const UGameplayActionInstance& Incoming,
	TArray<FGameplayActionHandle>& OutConflicts) const
{
	FGameplayActionSubmissionResult Result;
	if (bActionsPaused)
	{
		Result.Status = EGameplayActionSubmissionStatus::AcceptedQueued;
		Result.DiagnosticMessage = TEXT("The component is paused; the action is queued without treating pause as a lock conflict.");
		return Result;
	}

	OutConflicts = FindConflicts(Incoming);
	if (OutConflicts.IsEmpty() || CanPreemptAll(Incoming, OutConflicts))
	{
		Result.Status = EGameplayActionSubmissionStatus::AcceptedStarted;
		return Result;
	}
	if (Incoming.BlockedPolicy == EGameplayActionBlockedPolicy::Queue)
	{
		Result.Status = EGameplayActionSubmissionStatus::AcceptedQueued;
		Result.DiagnosticMessage = TEXT("The action is queued until it can acquire its entire lock set atomically.");
		return Result;
	}

	return MakeRejectedResult(EGameplayActionSubmissionStatus::RejectedBlocked,
		GameplayActionTags::Result_Failure_CannotStart, TEXT("Execution locks are held by actions that cannot be preempted."));
}

TArray<FGameplayActionHandle> UGameplayActionComponent::FindConflicts(const UGameplayActionInstance& Incoming) const
{
	TArray<FGameplayActionHandle> Conflicts;
	for (const FGameplayActionHandle ActiveHandle : ActiveHandles)
	{
		const UGameplayActionInstance* Active = GetActionInstance(ActiveHandle);
		if (Active && Active != &Incoming && Active->ExecutionLocks.HasAnyExact(Incoming.ExecutionLocks))
		{
			Conflicts.Add(ActiveHandle);
		}
	}
	return Conflicts;
}

bool UGameplayActionComponent::CanPreemptAll(
	const UGameplayActionInstance& Incoming,
	const TArray<FGameplayActionHandle>& Conflicts) const
{
	if (Conflicts.IsEmpty())
	{
		return false;
	}
	for (const FGameplayActionHandle ConflictHandle : Conflicts)
	{
		const UGameplayActionInstance* Conflict = GetActionInstance(ConflictHandle);
		if (!Conflict
			|| (Conflict->State != EGameplayActionState::Running && Conflict->State != EGameplayActionState::Paused)
			|| !Conflict->bInterruptible
			|| Conflict->Priority >= Incoming.Priority)
		{
			return false;
		}
	}
	return true;
}

void UGameplayActionComponent::SortHandlesBySchedulerOrder(TArray<FGameplayActionHandle>& Handles) const
{
	Handles.Sort([this](const FGameplayActionHandle Left, const FGameplayActionHandle Right)
	{
		const UGameplayActionInstance* LeftInstance = GetActionInstance(Left);
		const UGameplayActionInstance* RightInstance = GetActionInstance(Right);
		if (!LeftInstance || !RightInstance)
		{
			return Left.GetValue() < Right.GetValue();
		}
		if (LeftInstance->Priority != RightInstance->Priority)
		{
			return LeftInstance->Priority > RightInstance->Priority;
		}
		return LeftInstance->SubmissionSequence < RightInstance->SubmissionSequence;
	});
}

void UGameplayActionComponent::InitializeAcceptedAction(UGameplayActionInstance& Instance)
{
	if (Instance.bHasInitialized || Instance.State == EGameplayActionState::Created
		|| Instance.State == EGameplayActionState::Ending || !IsRuntimeState(Instance.State))
	{
		return;
	}

	// Init runs only after the accepted instance is retained in ActionsByHandle. The guard deliberately
	// keeps the hook useful for local setup while preventing it from recursively mutating scheduler state.
	Instance.bHasInitialized = true;
	TGuardValue<bool> InitGuard(bInInitCallback, true);
	Instance.OnActionInit();
	RefreshComponentTickEnabled();
}

void UGameplayActionComponent::StartAction(UGameplayActionInstance& Instance)
{
	if (bActionsPaused || bShuttingDown || Instance.bHasStarted || !Instance.bHasInitialized
		|| Instance.State == EGameplayActionState::Ending || Instance.State == EGameplayActionState::Running)
	{
		return;
	}

	// Queued actions own no locks. Moving through Starting makes the all-or-none acquisition boundary
	// explicit before ActiveHandles becomes the authoritative lock-owner set.
	Instance.State = EGameplayActionState::Starting;
	QueuedHandles.RemoveSingle(Instance.Handle);
	ActiveHandles.AddUnique(Instance.Handle);
	Instance.State = EGameplayActionState::Running;
	Instance.bHasStarted = true;
	QueueEvent(BuildEvent(Instance, EGameplayActionEventType::Started));
	Instance.OnActionStarted();
	RefreshComponentTickEnabled();
}

bool UGameplayActionComponent::FinishActionInternal(
	UGameplayActionInstance& Instance,
	const EGameplayActionState TerminalState,
	const FGameplayTag ReasonTag,
	const FString& DiagnosticMessage,
	const bool bEvaluateQueueAfterRelease)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameplayActions_Finish);

	if (!TerminalStates.Contains(Instance.Handle)
		&& IsRuntimeState(Instance.State)
		&& Instance.State != EGameplayActionState::Ending
		&& (TerminalState == EGameplayActionState::Succeeded
			|| TerminalState == EGameplayActionState::Failed
			|| TerminalState == EGameplayActionState::Cancelled
			|| TerminalState == EGameplayActionState::Interrupted
			|| TerminalState == EGameplayActionState::Aborted))
	{
		Instance.State = EGameplayActionState::Ending;
		switch (TerminalState)
		{
		case EGameplayActionState::Cancelled:
			Instance.OnActionCancelled(ReasonTag);
			break;
		case EGameplayActionState::Interrupted:
			Instance.OnActionInterrupted(ReasonTag);
			break;
		case EGameplayActionState::Aborted:
			Instance.OnActionAborted(ReasonTag);
			break;
		default:
			break;
		}
		Instance.OnActionCleanup();

		FGameplayActionResult Result;
		Result.TerminalState = TerminalState;
		Result.ReasonTag = ReasonTag;
		Result.DiagnosticMessage = DiagnosticMessage;
		Instance.State = TerminalState;
		ActiveHandles.RemoveSingle(Instance.Handle);
		QueuedHandles.RemoveSingle(Instance.Handle);
		TerminalStates.Add(Instance.Handle, TerminalState);
		TerminalResults.Add(Instance.Handle, Result);
		LastResult = Result;
		bHasLastResult = true;

		FGameplayActionEvent EndedEvent = BuildEvent(Instance, EGameplayActionEventType::Ended);
		EndedEvent.bHasResult = true;
		EndedEvent.Result = Result;
		QueueEvent(MoveTemp(EndedEvent));

		if (bEvaluateQueueAfterRelease && !bShuttingDown)
		{
			EvaluateQueuedActions();
		}
		RefreshComponentTickEnabled();
		return true;
	}
	return false;
}

void UGameplayActionComponent::UpdateQueuedTimeouts(const float DeltaTime)
{
	if (DeltaTime <= 0.0f || bActionsPaused || bShuttingDown)
	{
		return;
	}

	TArray<FGameplayActionHandle> Candidates = QueuedHandles;
	SortHandlesBySchedulerOrder(Candidates);
	for (const FGameplayActionHandle Handle : Candidates)
	{
		UGameplayActionInstance* Instance = GetActionInstance(Handle);
		if (!Instance || Instance->State != EGameplayActionState::Queued || !Instance->HasQueueTimeout())
		{
			continue;
		}

		Instance->QueueElapsedSeconds += static_cast<double>(DeltaTime);
		if (Instance->QueueElapsedSeconds < Instance->MaxQueueTimeSeconds)
		{
			continue;
		}

		const FString Diagnostic = FString::Printf(
			TEXT("Queued action %lld exceeded its %.3f second queue limit after %.3f seconds."),
			Handle.GetValue(), Instance->MaxQueueTimeSeconds, Instance->QueueElapsedSeconds);
		FinishActionInternal(*Instance, EGameplayActionState::Failed,
			GameplayActionTags::Result_Failure_QueueTimeout, Diagnostic, false);
		RecordSchedulerDecision(Diagnostic);
	}
}

void UGameplayActionComponent::EvaluateQueuedActions()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameplayActions_QueueEvaluation);
	if (bEvaluatingQueue || bActionsPaused || bShuttingDown)
	{
		return;
	}
	TGuardValue<bool> QueueGuard(bEvaluatingQueue, true);

	bool bMadeProgress = true;
	while (bMadeProgress && !bActionsPaused && !bShuttingDown)
	{
		bMadeProgress = false;
		FString FirstBlockedDecision;
		QueuedHandles.RemoveAll([this](const FGameplayActionHandle Handle)
		{
			const UGameplayActionInstance* Instance = GetActionInstance(Handle);
			return !Instance || Instance->State != EGameplayActionState::Queued;
		});

		TArray<FGameplayActionHandle> Candidates = QueuedHandles;
		SortHandlesBySchedulerOrder(Candidates);
		for (const FGameplayActionHandle CandidateHandle : Candidates)
		{
			UGameplayActionInstance* Candidate = GetActionInstance(CandidateHandle);
			if (!Candidate || Candidate->State != EGameplayActionState::Queued)
			{
				continue;
			}

			TArray<FGameplayActionHandle> Conflicts = FindConflicts(*Candidate);
			if (!Conflicts.IsEmpty() && !CanPreemptAll(*Candidate, Conflicts))
			{
				if (FirstBlockedDecision.IsEmpty())
				{
					SortHandlesBySchedulerOrder(Conflicts);
					TArray<FString> ConflictValues;
					ConflictValues.Reserve(Conflicts.Num());
					for (const FGameplayActionHandle ConflictHandle : Conflicts)
					{
						ConflictValues.Add(FString::Printf(TEXT("%lld"), ConflictHandle.GetValue()));
					}
					FirstBlockedDecision = FString::Printf(
						TEXT("Queued action %lld remains blocked by active handles [%s]."),
						CandidateHandle.GetValue(), *FString::Join(ConflictValues, TEXT(", ")));
				}
				continue;
			}

			SortHandlesBySchedulerOrder(Conflicts);
			for (const FGameplayActionHandle ConflictHandle : Conflicts)
			{
				if (UGameplayActionInstance* Conflict = GetActionInstance(ConflictHandle))
				{
					FinishActionInternal(*Conflict, EGameplayActionState::Interrupted,
						GameplayActionTags::Result_Interrupted_HigherPriority,
						FString::Printf(TEXT("Preempted by queued action %lld."), CandidateHandle.GetValue()), false);
				}
			}
			StartAction(*Candidate);
			RecordSchedulerDecision(FString::Printf(TEXT("Started queued action %lld."), CandidateHandle.GetValue()));
			bMadeProgress = true;
			break;
		}
		if (!bMadeProgress && !FirstBlockedDecision.IsEmpty())
		{
			RecordSchedulerDecision(FirstBlockedDecision);
		}
	}
}

void UGameplayActionComponent::RefreshComponentTickEnabled()
{
	bool bShouldTick = false;
	if (!bActionsPaused && !bShuttingDown)
	{
		for (const FGameplayActionHandle Handle : ActiveHandles)
		{
			const UGameplayActionInstance* Instance = GetActionInstance(Handle);
			if (Instance && Instance->State == EGameplayActionState::Running && Instance->bActionTickEnabled)
			{
				bShouldTick = true;
				break;
			}
		}
		if (!bShouldTick)
		{
			for (const FGameplayActionHandle Handle : QueuedHandles)
			{
				const UGameplayActionInstance* Instance = GetActionInstance(Handle);
				if (Instance && Instance->State == EGameplayActionState::Queued && Instance->HasQueueTimeout())
				{
					bShouldTick = true;
					break;
				}
			}
		}
	}
	SetComponentTickEnabled(bShouldTick);
}

void UGameplayActionComponent::NotifyActionTickStateChanged(UGameplayActionInstance* Instance)
{
	if (Instance && Instance->OwningComponent == this)
	{
		RefreshComponentTickEnabled();
	}
}

FGameplayActionEvent UGameplayActionComponent::BuildEvent(
	const UGameplayActionInstance& Instance,
	const EGameplayActionEventType EventType) const
{
	FGameplayActionEvent Event;
	Event.EventType = EventType;
	Event.Handle = Instance.Handle;
	Event.DefinitionId = Instance.Definition ? Instance.Definition->GetPrimaryAssetId() : FPrimaryAssetId();
	Event.Definition = Instance.Definition;
	Event.ActionTag = Instance.ActionTag;
	Event.Parameters = Instance.Parameters;
	Event.Priority = Instance.Priority;
	Event.BlockedPolicy = Instance.BlockedPolicy;
	Event.ExecutionLocks = Instance.ExecutionLocks;
	Event.bInterruptible = Instance.bInterruptible;
	Event.OptionalTimeout = Instance.OptionalTimeout;
	Event.MaxQueueTimeSeconds = Instance.MaxQueueTimeSeconds;
	Event.QueueElapsedSeconds = Instance.QueueElapsedSeconds;
	Event.JournalRequirement = Instance.JournalRequirement;
	Event.OriginTag = Instance.OriginTag;
	Event.Correlation = Instance.Correlation;
	Event.Requester = Instance.Requester;
	Event.Owner = GetOwner();
	Event.SubmissionSequence = Instance.SubmissionSequence;
	return Event;
}

FGameplayActionEvent UGameplayActionComponent::BuildRequestEvent(
	const FGameplayActionRequest& Request,
	const FValidatedRequest* Validated,
	const EGameplayActionEventType EventType,
	const FGameplayActionSubmissionResult& SubmissionResult) const
{
	FGameplayActionEvent Event;
	Event.EventType = EventType;
	Event.Definition = Request.Definition;
	Event.DefinitionId = Request.Definition ? Request.Definition->GetPrimaryAssetId() : FPrimaryAssetId();
	Event.ActionTag = Request.Definition ? Request.Definition->ActionTag : FGameplayTag();
	Event.Parameters = Request.Parameters;
	Event.Priority = Validated ? Validated->Priority : 0;
	Event.BlockedPolicy = Validated ? Validated->BlockedPolicy : EGameplayActionBlockedPolicy::Queue;
	if (Request.Definition)
	{
		Event.ExecutionLocks = Request.Definition->ExecutionLocks;
		Event.bInterruptible = Request.Definition->bInterruptible;
		Event.OptionalTimeout = Request.Definition->OptionalTimeout;
		Event.MaxQueueTimeSeconds = Request.Definition->MaxQueueTimeSeconds;
		Event.JournalRequirement = Request.Definition->JournalRequirement;
	}
	Event.OriginTag = Request.OriginTag;
	Event.Correlation = Request.Correlation;
	Event.Requester = Request.Requester;
	Event.Owner = GetOwner();
	Event.bHasSubmissionResult = true;
	Event.SubmissionResult = SubmissionResult;
	return Event;
}

void UGameplayActionComponent::QueueEvent(FGameplayActionEvent&& Event, const bool bSkipJournal)
{
	PendingEvents.Add(MoveTemp(Event));
	PendingEventSkipJournal.Add(bSkipJournal ? 1 : 0);
}

void UGameplayActionComponent::FlushEventQueue()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GameplayActions_EventDispatch);
	if (bDispatchingEvents || bInInitialJournalTransaction || bInValidationCallback || bInInitCallback)
	{
		return;
	}
	TGuardValue<bool> DispatchGuard(bDispatchingEvents, true);

	while (!PendingEvents.IsEmpty())
	{
		FGameplayActionEvent Event = MoveTemp(PendingEvents[0]);
		const bool bSkipJournal = PendingEventSkipJournal.IsValidIndex(0) && PendingEventSkipJournal[0] != 0;
		PendingEvents.RemoveAt(0, 1, EAllowShrinking::No);
		PendingEventSkipJournal.RemoveAt(0, 1, EAllowShrinking::No);

		if (!bSkipJournal && Event.JournalRequirement != EGameplayActionJournalRequirement::Disabled && JournalSink.GetObject())
		{
			const FGameplayActionJournalResult JournalResult = WriteJournalEvent(Event);
			if (!JournalResult.IsAccepted())
			{
				GAMEPLAYACTIONS_LOG_WARNING(TEXT("Journal rejected %s event for action %lld: %s"),
					*UEnum::GetValueAsString(Event.EventType), Event.Handle.GetValue(), *JournalResult.DiagnosticMessage);
			}
		}
		BroadcastEvent(Event);

		if (Event.EventType == EGameplayActionEventType::Ended)
		{
			ReleaseInstanceAfterEndedEvent(Event.Handle);
		}
	}
}

FGameplayActionJournalResult UGameplayActionComponent::WriteJournalEvent(const FGameplayActionEvent& Event)
{
	if (!JournalSink.GetObject())
	{
		FGameplayActionJournalResult Result;
		Result.Status = EGameplayActionJournalWriteStatus::Rejected;
		Result.DiagnosticMessage = TEXT("No journal sink is registered.");
		return Result;
	}
	return IGameplayActionJournalSink::Execute_WriteGameplayActionEvent(JournalSink.GetObject(), Event);
}

void UGameplayActionComponent::BroadcastEvent(const FGameplayActionEvent& Event)
{
	OnActionEvent.Broadcast(Event);
	switch (Event.EventType)
	{
	case EGameplayActionEventType::Accepted:
		OnActionAccepted.Broadcast(Event);
		break;
	case EGameplayActionEventType::Rejected:
		OnActionRejected.Broadcast(Event);
		break;
	case EGameplayActionEventType::Started:
		OnActionStarted.Broadcast(Event);
		break;
	case EGameplayActionEventType::Paused:
		OnActionPaused.Broadcast(Event);
		break;
	case EGameplayActionEventType::Resumed:
		OnActionResumed.Broadcast(Event);
		break;
	case EGameplayActionEventType::Ended:
		OnActionEnded.Broadcast(Event);
		// Native bridge integrations need stable delegate handles and must still be able to inspect
		// the accepted instance. ReleaseInstanceAfterEndedEvent runs only after this call returns.
		NativeActionEnded.Broadcast(Event);
		break;
	default:
		break;
	}
}

FGameplayActionSubmissionResult UGameplayActionComponent::MakeRejectedResult(
	const EGameplayActionSubmissionStatus Status,
	const FGameplayTag ReasonTag,
	FString DiagnosticMessage) const
{
	FGameplayActionSubmissionResult Result;
	Result.Status = Status;
	Result.ReasonTag = ReasonTag;
	Result.DiagnosticMessage = MoveTemp(DiagnosticMessage);
	return Result;
}

void UGameplayActionComponent::RecordSchedulerDecision(const FString& Decision)
{
	LastSchedulerDecision = Decision;
	if (IsDetailedDebugEnabled())
	{
		GAMEPLAYACTIONS_LOG_INFO(TEXT("%s: %s"), *GetNameSafe(this), *Decision);
	}
}

bool UGameplayActionComponent::IsDetailedDebugEnabled() const
{
	return bEnableDebug && IsGameplayActionsDebugEnabled();
}

void UGameplayActionComponent::ShutdownActions(const FGameplayTag ReasonTag)
{
	if (bShuttingDown)
	{
		return;
	}
	bAcceptingSubmissions = false;
	bShuttingDown = true;
	bActionsPaused = false;
	SetComponentTickEnabled(false);

	TArray<FGameplayActionHandle> Handles;
	ActionsByHandle.GetKeys(Handles);
	SortHandlesBySchedulerOrder(Handles);
	for (const FGameplayActionHandle Handle : Handles)
	{
		if (UGameplayActionInstance* Instance = GetActionInstance(Handle))
		{
			FinishActionInternal(*Instance, EGameplayActionState::Aborted, ReasonTag,
				TEXT("Action aborted during component shutdown."), false);
		}
	}
	FlushEventQueue();
	JournalSink = nullptr;
	PendingEvents.Reset();
	PendingEventSkipJournal.Reset();
	ActionsByHandle.Reset();
	ActiveHandles.Reset();
	QueuedHandles.Reset();
}

void UGameplayActionComponent::ReleaseInstanceAfterEndedEvent(const FGameplayActionHandle Handle)
{
	ActionsByHandle.Remove(Handle);
}
