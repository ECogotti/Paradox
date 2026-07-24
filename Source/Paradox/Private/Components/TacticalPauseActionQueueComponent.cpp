#include "Components/TacticalPauseActionQueueComponent.h"

#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "GameplayActionTags.h"
#include "Paradox.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"

UTacticalPauseActionQueueComponent::UTacticalPauseActionQueueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTacticalPauseActionQueueComponent::BeginPlay()
{
	Super::BeginPlay();
	BindDependencies();

	if (TacticalPauseSubsystem && TacticalPauseSubsystem->IsPaused())
	{
		ApplyTacticalPauseState(true);
	}
}

void UTacticalPauseActionQueueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Do not leave work or a scheduler pause behind when an explicit override outlives this component.
	// Sibling components on the same Actor are also safe: they cannot tick between EndPlay callbacks.
	if (NextActionHandle.IsValid())
	{
		const EGameplayActionOperationResult ClearResult = ClearNextAction();
		if (ClearResult != EGameplayActionOperationResult::Succeeded
			&& ClearResult != EGameplayActionOperationResult::HandleNotFound)
		{
			PARADOX_LOG_WARNING(
				TEXT("Could not clear the planned next action during EndPlay for owner '%s' (result %d)."),
				*GetNameSafe(GetOwner()),
				static_cast<int32>(ClearResult));
		}
	}
	ApplyTacticalPauseState(false);
	UnbindDependencies();
	SetNextActionHandle(FGameplayActionHandle());
	bSchedulerPauseOwned = false;
	Super::EndPlay(EndPlayReason);
}

FGameplayActionSubmissionResult UTacticalPauseActionQueueComponent::SubmitOrReplaceNextAction(
	FGameplayActionRequest Request)
{
	if (!IsAcceptingTacticalPlanning())
	{
		return MakePlanningFailure(TEXT("Next actions can be planned only while Tactical Pause and the Gameplay Actions scheduler are paused."));
	}

	if (NextActionHandle.IsValid())
	{
		EGameplayActionState PreviousState = EGameplayActionState::Created;
		if (ResolvedActionComponent->GetActionState(NextActionHandle, PreviousState)
			&& PreviousState == EGameplayActionState::Queued)
		{
			const EGameplayActionOperationResult CancelResult = ResolvedActionComponent->CancelAction(
				NextActionHandle,
				GameplayActionTags::Result_Cancelled_ByRequester);
			if (CancelResult != EGameplayActionOperationResult::Succeeded)
			{
				return MakePlanningFailure(FString::Printf(
					TEXT("The previous next action %lld could not be replaced (operation result %d)."),
					NextActionHandle.GetValue(),
					static_cast<int32>(CancelResult)));
			}
		}
		SetNextActionHandle(FGameplayActionHandle());
	}

	// The scheduler already queues every submission while paused; the explicit override also makes
	// the request's planning intent unambiguous in snapshots, journaling, and future integrations.
	UGameplayActionBlueprintLibrary::SetRequestBlockedPolicy(Request, EGameplayActionBlockedPolicy::Queue);
	const FGameplayActionSubmissionResult Submission = ResolvedActionComponent->SubmitAction(Request);
	if (Submission.IsAccepted())
	{
		SetNextActionHandle(Submission.Handle);
		PARADOX_LOG_INFO(
			TEXT("Planned next action %lld for owner '%s'."),
			Submission.Handle.GetValue(),
			*GetNameSafe(GetOwner()));
	}
	return Submission;
}

EGameplayActionOperationResult UTacticalPauseActionQueueComponent::ClearNextAction()
{
	if (!ResolvedActionComponent)
	{
		return EGameplayActionOperationResult::InvalidState;
	}
	if (!NextActionHandle.IsValid())
	{
		return EGameplayActionOperationResult::HandleNotFound;
	}

	const FGameplayActionHandle HandleToCancel = NextActionHandle;
	const EGameplayActionOperationResult Result = ResolvedActionComponent->CancelAction(
		HandleToCancel,
		GameplayActionTags::Result_Cancelled_ByRequester);
	if (Result == EGameplayActionOperationResult::Succeeded
		|| Result == EGameplayActionOperationResult::HandleNotFound)
	{
		SetNextActionHandle(FGameplayActionHandle());
	}
	return Result;
}

bool UTacticalPauseActionQueueComponent::IsAcceptingTacticalPlanning() const
{
	return TacticalPauseSubsystem
		&& TacticalPauseSubsystem->IsPaused()
		&& ResolvedActionComponent
		&& ResolvedActionComponent->IsActionsPaused()
		&& ResolvedActionComponent->IsAcceptingSubmissions();
}

void UTacticalPauseActionQueueComponent::BindDependencies()
{
	UnbindDependencies();
	ResolvedActionComponent = ActionComponentOverride;
	if (!ResolvedActionComponent && GetOwner())
	{
		ResolvedActionComponent = GetOwner()->FindComponentByClass<UGameplayActionComponent>();
	}
	TacticalPauseSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UTacticalPauseWorldSubsystem>()
		: nullptr;

	if (!ResolvedActionComponent)
	{
		PARADOX_LOG_ERROR(
			TEXT("Tactical planning component '%s' has no GameplayActionComponent on owner '%s'."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
	}
	else
	{
		ResolvedActionComponent->OnActionStarted.AddDynamic(
			this,
			&UTacticalPauseActionQueueComponent::HandleActionStarted);
		ResolvedActionComponent->OnActionEnded.AddDynamic(
			this,
			&UTacticalPauseActionQueueComponent::HandleActionEnded);
	}

	if (!TacticalPauseSubsystem)
	{
		PARADOX_LOG_ERROR(
			TEXT("Tactical planning component '%s' could not resolve TacticalPause for World '%s'."),
			*GetNameSafe(this),
			*GetNameSafe(GetWorld()));
	}
	else
	{
		TacticalPauseSubsystem->OnPausedNative().AddUObject(
			this,
			&UTacticalPauseActionQueueComponent::HandleTacticalPaused);
		TacticalPauseSubsystem->OnResumedNative().AddUObject(
			this,
			&UTacticalPauseActionQueueComponent::HandleTacticalResumed);
	}
}

void UTacticalPauseActionQueueComponent::UnbindDependencies()
{
	if (TacticalPauseSubsystem)
	{
		TacticalPauseSubsystem->OnPausedNative().RemoveAll(this);
		TacticalPauseSubsystem->OnResumedNative().RemoveAll(this);
	}
	if (ResolvedActionComponent)
	{
		ResolvedActionComponent->OnActionStarted.RemoveDynamic(
			this,
			&UTacticalPauseActionQueueComponent::HandleActionStarted);
		ResolvedActionComponent->OnActionEnded.RemoveDynamic(
			this,
			&UTacticalPauseActionQueueComponent::HandleActionEnded);
	}
	TacticalPauseSubsystem = nullptr;
	ResolvedActionComponent = nullptr;
}

void UTacticalPauseActionQueueComponent::ApplyTacticalPauseState(bool bPaused)
{
	if (!ResolvedActionComponent)
	{
		return;
	}

	if (bPaused)
	{
		if (ResolvedActionComponent->IsActionsPaused())
		{
			if (bSchedulerPauseOwned)
			{
				// Repeated pause notifications are idempotent and must not discard ownership.
				return;
			}
			// Preserve a pause that another gameplay system already owns.
			bSchedulerPauseOwned = false;
			return;
		}
		bSchedulerPauseOwned = ResolvedActionComponent->PauseActions()
			== EGameplayActionOperationResult::Succeeded;
		if (!bSchedulerPauseOwned)
		{
			PARADOX_LOG_ERROR(
				TEXT("Failed to pause Gameplay Actions for tactical planning on owner '%s'."),
				*GetNameSafe(GetOwner()));
		}
		return;
	}

	if (bSchedulerPauseOwned)
	{
		if (!ResolvedActionComponent->IsActionsPaused())
		{
			// Another authority already resumed the scheduler, so no owned pause remains.
			bSchedulerPauseOwned = false;
			return;
		}
		const EGameplayActionOperationResult ResumeResult = ResolvedActionComponent->ResumeActions();
		if (ResumeResult != EGameplayActionOperationResult::Succeeded)
		{
			PARADOX_LOG_ERROR(
				TEXT("Failed to resume Gameplay Actions after Tactical Pause on owner '%s' (result %d)."),
				*GetNameSafe(GetOwner()),
				static_cast<int32>(ResumeResult));
			return;
		}
	}
	bSchedulerPauseOwned = false;
}

void UTacticalPauseActionQueueComponent::SetNextActionHandle(FGameplayActionHandle NewHandle)
{
	if (NextActionHandle == NewHandle)
	{
		return;
	}
	const FGameplayActionHandle PreviousHandle = NextActionHandle;
	NextActionHandle = NewHandle;
	OnNextActionChanged.Broadcast(PreviousHandle, NextActionHandle);
}

FGameplayActionSubmissionResult UTacticalPauseActionQueueComponent::MakePlanningFailure(
	const FString& DiagnosticMessage) const
{
	FGameplayActionSubmissionResult Failure;
	Failure.Status = EGameplayActionSubmissionStatus::RejectedBlocked;
	Failure.ReasonTag = GameplayActionTags::Result_Failure_CannotStart;
	Failure.DiagnosticMessage = DiagnosticMessage;
	PARADOX_LOG_WARNING(
		TEXT("Tactical planning request rejected for owner '%s': %s"),
		*GetNameSafe(GetOwner()),
		*DiagnosticMessage);
	return Failure;
}

void UTacticalPauseActionQueueComponent::HandleTacticalPaused(const FTacticalPauseStateChange& Change)
{
	ApplyTacticalPauseState(true);
}

void UTacticalPauseActionQueueComponent::HandleTacticalResumed(const FTacticalPauseStateChange& Change)
{
	ApplyTacticalPauseState(false);
}

void UTacticalPauseActionQueueComponent::HandleActionStarted(const FGameplayActionEvent& Event)
{
	if (Event.Handle == NextActionHandle)
	{
		SetNextActionHandle(FGameplayActionHandle());
	}
}

void UTacticalPauseActionQueueComponent::HandleActionEnded(const FGameplayActionEvent& Event)
{
	if (Event.Handle == NextActionHandle)
	{
		SetNextActionHandle(FGameplayActionHandle());
	}
}
