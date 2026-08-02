#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/IntentReplayObservationComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "Data/IntentReplayObservationSessions.h"
#include "Data/IntentReplayObservationTrack.h"
#include "Data/IntentReplayTimelineBundle.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayActionTags.h"
#include "Investigation/ParadoxCloneInvestigationComponent.h"
#include "Journal/IntentReplayObservationJournal.h"
#include "Paradox.h"
#include "Perception/ParadoxObservationResponsePolicy.h"
#include "Playback/IntentReplayPlaybackSession.h"
#include "Playback/ParadoxReplayRecoveryPolicy.h"
#include "Recording/IntentReplayTrack.h"
#include "Subsystems/PerceptionKnowledgeWorldSubsystem.h"

namespace ParadoxCloneBlackboardKeys
{
	const FName BehaviorMode(TEXT("BehaviorMode"));
	const FName InvestigationLocation(TEXT("InvestigationLocation"));
	const FName InvestigationSourceActor(TEXT("InvestigationSourceActor"));
	const FName InvestigationSourceEntityId(TEXT("InvestigationSourceEntityId"));
	const FName InvestigationJournalEntryId(TEXT("InvestigationJournalEntryId"));
	const FName InvestigationObservationType(TEXT("InvestigationObservationType"));
	const FName InvestigationSemanticTag(TEXT("InvestigationSemanticTag"));
	const FName InvestigationSense(TEXT("InvestigationSense"));
	const FName LastModeTransitionReason(TEXT("LastModeTransitionReason"));
	const FName InvestigationResponseRuleId(TEXT("InvestigationResponseRuleId"));
	const FName InvestigationPriority(TEXT("InvestigationPriority"));
	const FName InvestigationRevision(TEXT("InvestigationRevision"));
	const FName HasValidInvestigation(TEXT("HasValidInvestigation"));
	const FName ReplayResumeAvailable(TEXT("ReplayResumeAvailable"));
	const FName InvestigationConfidence(TEXT("InvestigationConfidence"));
}

UParadoxCloneBehaviorCoordinatorComponent::
	UParadoxCloneBehaviorCoordinatorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ObservationMatchOptions
		.bTreatPersistentStateObservationsAsOrderedSnapshots = true;
	ObservationMatchOptions
		.bTreatVerifiedCausalEventsAsOccurrenceIdentity = true;
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::InitializeForRun(
	UIntentReplayTimelineBundle* TimelineBundle)
{
	if (bGoapHandoffTerminal)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::TerminalGoapHandoff,
			TEXT("This clone already entered terminal GOAP handoff."));
	}

	AParadoxCloneCharacter* Clone =
		Cast<AParadoxCloneCharacter>(GetOwner());
	ReplayComponent = Clone ? Clone->GetIntentReplayComponent() : nullptr;
	ObservationComponent =
		Clone ? Clone->GetObservationReplayComponent() : nullptr;
	InvestigationComponent =
		Clone ? Clone->GetInvestigationComponent() : nullptr;
	AParadoxCloneController* Controller = Clone
		? Cast<AParadoxCloneController>(Clone->GetController())
		: nullptr;
	PerceptionListener =
		Controller ? Controller->GetPerceptionKnowledgeListener() : nullptr;
	if (!Clone || !ReplayComponent || !InvestigationComponent)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ConfigurationError,
			TEXT("Clone behavior requires clone, IntentReplay, and Investigation components."));
	}
	if (!ReplayComponent->GetActivePlaybackSession())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("Clone behavior initialization requires a prepared playback session."));
	}

	UnbindRuntimeDelegates();
	const FParadoxCloneBehaviorOperationResult InvestigationInitialization =
		InvestigationComponent->InitializeInvestigation(
			Clone->GetGameplayActionComponent());
	if (!InvestigationInitialization.IsSuccess())
	{
		return InvestigationInitialization;
	}
	RecoveryMoveFinishedHandle =
		InvestigationComponent->OnRecoveryMoveFinishedNative().AddUObject(
			this,
			&UParadoxCloneBehaviorCoordinatorComponent::HandleRecoveryMoveFinished);

	ActiveTimelineBundle = TimelineBundle;
	ExpectedPlaybackSessionId =
		ReplayComponent->GetActivePlaybackSession()->GetSessionId();
	ExpectedObservationTrackId = FIntentReplayObservationTrackId();
	ExpectedObservationJournalId = FIntentReplayObservationJournalId();
	if (IsValid(TimelineBundle))
	{
		if (!ObservationComponent || !PerceptionListener)
		{
			return MakeResult(
				EParadoxCloneBehaviorOperationStatus::ConfigurationError,
				TEXT("A full Timeline Bundle requires Observation Replay and a controller-owned PerceptionKnowledge listener."));
		}
		ObservationComponent->SetIntentReplaySource(ReplayComponent);
		ObservationComponent->SetPerceptionKnowledgeListener(
			PerceptionListener);
		const FIntentReplayObservationOperationResult ObservationInitialization =
			ObservationComponent->InitializeObservationReplay();
		if (!ObservationInitialization.Succeeded())
		{
			return MakeResult(
				EParadoxCloneBehaviorOperationStatus::ConfigurationError,
				ObservationInitialization.DiagnosticMessage);
		}
		const FIntentReplayObservationOperationResult ComparisonStart =
			ObservationComponent->StartObservationComparison(
				TimelineBundle,
				ObservationMatchOptions);
		if (!ComparisonStart.Succeeded())
		{
			return MakeResult(
				EParadoxCloneBehaviorOperationStatus::ConfigurationError,
				ComparisonStart.DiagnosticMessage);
		}
		const UIntentReplayObservationComparisonSession* Comparison =
			ObservationComponent->GetActiveObservationComparisonSession();
		const UIntentReplayObservationJournal* Journal =
			ObservationComponent->GetActiveObservationJournal();
		if (!Comparison || !Journal
			|| !TimelineBundle->GetObservationTrack())
		{
			return MakeResult(
				EParadoxCloneBehaviorOperationStatus::ConfigurationError,
				TEXT("Observation comparison did not publish its authoritative session, track, or journal."));
		}
		ExpectedObservationTrackId =
			TimelineBundle->GetObservationTrack()->GetObservationTrackId();
		ExpectedObservationJournalId = Journal->GetJournalId();
		UnexpectedObservationHandle =
			ObservationComponent->OnObservationUnexpectedNative().AddUObject(
				this,
				&UParadoxCloneBehaviorCoordinatorComponent::HandleUnexpectedObservation);
	}
	else
	{
		PARADOX_LOG_WARNING(
			TEXT("Clone '%s' is using a legacy action-only timeline; perceptual comparison and investigation triggers are unavailable."),
			*GetNameSafe(Clone));
	}

	ReplayResumeContext.Reset();
	CurrentInvestigation = FParadoxInvestigationContext();
	PendingRecoveryIntentId = FRecordedIntentId();
	bWaitingForRecoveryMove = false;
	bRecoveryBlocked = false;
	bReplayResumeAvailable = false;
	bReplayStartAuthorized = false;
	bInitializedForRun = true;
	CurrentMode = EParadoxCloneBehaviorMode::Replay;
	++ModeRevision;
	LastModeTransitionReason = TEXT("RunInitialized");
	UpdateBlackboardMirror();
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("Clone behavior run initialized."));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::AuthorizeReplayStart()
{
	if (!bInitializedForRun || bGoapHandoffTerminal)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::NotInitialized,
			TEXT("Replay cannot be authorized before run initialization."));
	}
	if (CurrentMode != EParadoxCloneBehaviorMode::Replay)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("Replay start authorization is legal only in Replay mode."));
	}
	bReplayStartAuthorized = true;
	ReplayAuthorizedNative.Broadcast();
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("The synchronized barrier authorized the Behavior Tree Replay task."));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::
	StartAuthorizedReplayFromBehaviorTree()
{
	if (!bInitializedForRun || !ReplayComponent)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::NotInitialized,
			TEXT("Replay task has no initialized coordinator run."));
	}
	if (!bReplayStartAuthorized)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::NotAuthorized,
			TEXT("Replay task is waiting for synchronized-start authorization."));
	}
	if (CurrentMode != EParadoxCloneBehaviorMode::Replay
		|| bGoapHandoffTerminal)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("Replay task does not own the current behavior mode."));
	}

	FIntentReplayOperationResult ReplayResult;
	switch (ReplayComponent->GetPlaybackState())
	{
	case EIntentReplayPlaybackState::Ready:
		ReplayResult = ReplayComponent->StartReplay();
		break;
	case EIntentReplayPlaybackState::Paused:
		ReplayResult = ReplayComponent->ResumeReplay();
		break;
	case EIntentReplayPlaybackState::Playing:
	case EIntentReplayPlaybackState::Completed:
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::AlreadyInState,
			TEXT("Replay is already playing or completed."));
	default:
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("IntentReplay is not Ready, Paused, Playing, or Completed."));
	}
	if (!ReplayResult.Succeeded())
	{
		return MakeResult(
			ReplayResult.Status == EIntentReplayOperationStatus::PendingExternalRecovery
				? EParadoxCloneBehaviorOperationStatus::RecoveryPending
				: EParadoxCloneBehaviorOperationStatus::ActionRejected,
			ReplayResult.Failure.DiagnosticMessage);
	}
	bReplayResumeAvailable = false;
	UpdateBlackboardMirror();
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("Behavior Tree started or resumed IntentReplay."));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::CompleteInvestigation(
	const FParadoxInvestigationContext& CompletedContext,
	const FGameplayActionResult& Result)
{
	if (CurrentMode != EParadoxCloneBehaviorMode::Investigating
		|| !CurrentInvestigation.IsValid()
		|| CompletedContext.InvestigationRevision
			!= CurrentInvestigation.InvestigationRevision)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::StaleContext,
			TEXT("A stale investigation completion was ignored."));
	}
	if (Result.TerminalState != EGameplayActionState::Succeeded)
	{
		bRecoveryBlocked = true;
		BroadcastContinuityFailure(
			FString::Printf(
				TEXT("Investigation revision %d ended with %s: %s"),
				CompletedContext.InvestigationRevision,
				*UEnum::GetValueAsString(Result.TerminalState),
				*Result.DiagnosticMessage));
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ContinuityCannotBeRestored,
			Result.DiagnosticMessage);
	}
	return ContinueReplayRecovery();
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::RetryReplayContinuity()
{
	if (CurrentMode != EParadoxCloneBehaviorMode::Investigating
		|| !bRecoveryBlocked)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("Replay continuity is not currently blocked."));
	}
	bRecoveryBlocked = false;
	if (!InvestigationComponent->IsInvestigationActive()
		&& ReplayResumeContext.NextRecoveryIndex == 0)
	{
		return InvestigationComponent->StartInvestigation(CurrentInvestigation);
	}
	return ContinueReplayRecovery();
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::RequestEnterGoapMode()
{
	if (bGoapHandoffTerminal)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::AlreadyInState,
			TEXT("GOAP handoff is already terminal for this run."));
	}
	bGoapHandoffTerminal = true;
	bReplayStartAuthorized = false;
	if (InvestigationComponent)
	{
		InvestigationComponent->CancelInvestigation(
			GameplayActionTags::Result_Cancelled_ByRequester);
	}
	if (ReplayComponent)
	{
		const EIntentReplayPlaybackState State =
			ReplayComponent->GetPlaybackState();
		if (State == EIntentReplayPlaybackState::Preparing
			|| State == EIntentReplayPlaybackState::Ready
			|| State == EIntentReplayPlaybackState::Playing
			|| State == EIntentReplayPlaybackState::Paused)
		{
			ReplayComponent->StopReplay();
		}
	}
	if (ObservationComponent
		&& ObservationComponent->GetActiveObservationComparisonSession())
	{
		ObservationComponent->StopObservationComparison();
	}
	SetMode(EParadoxCloneBehaviorMode::Goap, TEXT("ExternalGoapHandoff"));
	if (BehaviorTreeComponent)
	{
		BehaviorTreeComponent->StopTree(EBTStopMode::Safe);
	}
	OnGoapHandoffRequested.Broadcast();
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("Behavior Tree stopped safely; external GOAP handoff requested."));
}

void UParadoxCloneBehaviorCoordinatorComponent::SetBehaviorTreeContext(
	UBehaviorTreeComponent* InBehaviorTree,
	UBlackboardComponent* InBlackboard)
{
	BehaviorTreeComponent = InBehaviorTree;
	BlackboardComponent = InBlackboard;
	UpdateBlackboardMirror();
}

FParadoxCloneBehaviorDebugSnapshot
UParadoxCloneBehaviorCoordinatorComponent::GetDebugSnapshot() const
{
	FParadoxCloneBehaviorDebugSnapshot Snapshot;
	Snapshot.Mode = CurrentMode;
	Snapshot.ModeRevision = ModeRevision;
	Snapshot.LastTransitionReason = LastModeTransitionReason;
	Snapshot.bReplayStartAuthorized = bReplayStartAuthorized;
	Snapshot.bGoapHandoffTerminal = bGoapHandoffTerminal;
	Snapshot.bHasInvestigation = CurrentInvestigation.IsValid();
	Snapshot.Investigation = CurrentInvestigation;
	Snapshot.PendingRecoveryIntentCount = ReplayComponent
		&& ReplayComponent->GetActivePlaybackSession()
		? ReplayComponent->GetActivePlaybackSession()
			->GetPendingExternalRecoveryCount()
		: 0;
	return Snapshot;
}

void UParadoxCloneBehaviorCoordinatorComponent::HandleUnexpectedObservation(
	const FIntentReplayObservationComparisonEvent& Event)
{
	if (!IsComparisonAuthoritative(Event))
	{
		if (IsDetailedDebugEnabled())
		{
			PARADOX_LOG_WARNING(
				TEXT("Clone '%s' ignored a foreign comparison session/track/journal callback."),
				*GetNameSafe(GetOwner()));
		}
		return;
	}

	FParadoxInvestigationContext Candidate =
		BuildInvestigationCandidate(Event);
	const UParadoxObservationResponsePolicy* Policy =
		ObservationResponsePolicy
			? ObservationResponsePolicy
			: GetDefault<UParadoxObservationResponsePolicy>();
	const FParadoxObservationResponseResult Response =
		Policy->Evaluate(Candidate, CurrentMode);
	if (!Response.ShouldInvestigate())
	{
		if (IsDetailedDebugEnabled())
		{
			PARADOX_LOG_INFO(
				TEXT("Clone '%s' comparison entry=%s decision=Ignored: %s"),
				*GetNameSafe(GetOwner()),
				*Event.Entry.JournalEntryId.ToString(),
				*Response.DiagnosticMessage);
		}
		return;
	}
	Candidate.InvestigationPriority = Response.InvestigationPriority;
	Candidate.ResponseRuleId = Response.RuleId;
	Candidate.InvestigationRevision =
		FMath::Max(1, CurrentInvestigation.InvestigationRevision + 1);

	if (CurrentMode == EParadoxCloneBehaviorMode::Replay)
	{
		EnterInvestigation(MoveTemp(Candidate));
	}
	else if (CurrentMode == EParadoxCloneBehaviorMode::Investigating)
	{
		ConsiderInvestigationReplacement(MoveTemp(Candidate));
	}
}

FParadoxInvestigationContext
UParadoxCloneBehaviorCoordinatorComponent::BuildInvestigationCandidate(
	const FIntentReplayObservationComparisonEvent& Event) const
{
	FParadoxInvestigationContext Candidate;
	Candidate.Comparison = Event;
	Candidate.PlaybackSessionId = Event.PlaybackSessionId;
	Candidate.ObservationTrackId = Event.ObservationTrackId;
	Candidate.JournalId = Event.JournalId;
	Candidate.JournalEntryId = Event.Entry.JournalEntryId;
	Candidate.ObservationType = Event.Entry.CurrentObservation.Type;
	Candidate.Correlation = Event.Entry.CurrentCorrelation;
	if (Candidate.ObservationType == EPerceptionKnowledgeObservationType::State)
	{
		const FPerceptionKnowledgeStateObservation& State =
			Event.Entry.CurrentObservation.State;
		Candidate.SenseTag = State.SenseTag;
		Candidate.SemanticTag = State.Key.StateTag;
		Candidate.SourceEntityId = State.Key.EntityId;
		Candidate.InvestigationLocation = State.ObservationLocation;
		Candidate.Confidence = State.Confidence;
	}
	else
	{
		const FPerceptionKnowledgeEventObservation& ObservationEvent =
			Event.Entry.CurrentObservation.Event;
		Candidate.SenseTag = ObservationEvent.SenseTag;
		Candidate.SemanticTag = ObservationEvent.EventTag;
		Candidate.SourceEntityId = ObservationEvent.SourceEntityId;
		Candidate.InvestigationLocation = ObservationEvent.WorldLocation;
		Candidate.Confidence = ObservationEvent.Confidence;
	}

	if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
		GetWorld()
			? GetWorld()->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()
			: nullptr)
	{
		if (UPerceptionKnowledgeSourceComponent* Source =
			Subsystem->FindSource(Candidate.SourceEntityId))
		{
			Candidate.SourceActor = Source->GetOwner();
			if (Candidate.ObservationType
					== EPerceptionKnowledgeObservationType::State
				&& Candidate.SourceActor.IsValid())
			{
				Candidate.InvestigationLocation =
					Candidate.SourceActor->GetActorLocation();
			}
			if (const IGameplayTagAssetInterface* TagInterface =
				Cast<IGameplayTagAssetInterface>(Candidate.SourceActor.Get()))
			{
				TagInterface->GetOwnedGameplayTags(Candidate.SourceCategories);
			}
		}
	}
	return Candidate;
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::EnterInvestigation(
	FParadoxInvestigationContext Candidate)
{
	const FParadoxCloneBehaviorOperationResult Validation =
		InvestigationComponent->ValidateInvestigation(Candidate);
	if (!Validation.IsSuccess())
	{
		return Validation;
	}
	const FIntentReplayExternalInterruptionResult Interruption =
		ReplayComponent->BeginExternalReplayInterruption(
			ParadoxGameplayTags::Result_Interrupted_ByInvestigation);
	if (!Interruption.Succeeded())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			Interruption.Failure.DiagnosticMessage);
	}

	ReplayResumeContext.Reset();
	ReplayResumeContext.bCaptured = true;
	ReplayResumeContext.PlaybackSessionId = Interruption.SessionId;
	if (UIntentReplayPlaybackSession* Session =
		ReplayComponent->GetActivePlaybackSession();
		Session && Session->GetSourceTrack())
	{
		ReplayResumeContext.ReplayTrackId =
			Session->GetSourceTrack()->GetTrackId();
	}
	ReplayResumeContext.SuspendedIntents = Interruption.SuspendedIntents;
	CurrentInvestigation = MoveTemp(Candidate);
	bReplayResumeAvailable = false;
	bRecoveryBlocked = false;
	SetMode(EParadoxCloneBehaviorMode::Investigating, TEXT("UnexpectedObservation"));
	const FIntentReplayObservationJournalEntry& ComparisonEntry =
		CurrentInvestigation.Comparison.Entry;
	double ExpectedTime = -1.0;
	FVector ExpectedLocation = FVector::ZeroVector;
	if (ComparisonEntry.bHasExpectedObservation)
	{
		ExpectedTime =
			ComparisonEntry.ExpectedObservation.GetRelativeTimestamp();
		ExpectedLocation =
			ComparisonEntry.ExpectedObservation.Type
				== EIntentReplayRecordedObservationType::Event
			? ComparisonEntry.ExpectedObservation.Event.WorldLocation
			: ComparisonEntry.ExpectedObservation.State.ObservationLocation;
	}
	PARADOX_LOG_WARNING(
		TEXT("Clone '%s' entered Investigating: result=%s mismatch=%s source=%s semantic=%s current_time=%.3f expected_time=%.3f delta=%.3f current_location=%s expected_location=%s causal_intent=%s priority=%d rule=%s revision=%d."),
		*GetNameSafe(GetOwner()),
		*UEnum::GetValueAsString(ComparisonEntry.Result),
		*UEnum::GetValueAsString(ComparisonEntry.Reason),
		*CurrentInvestigation.SourceEntityId.ToString(),
		*CurrentInvestigation.SemanticTag.ToString(),
		ComparisonEntry.CurrentRelativeTime,
		ExpectedTime,
		ComparisonEntry.TimeDelta,
		*CurrentInvestigation.InvestigationLocation.ToCompactString(),
		*ExpectedLocation.ToCompactString(),
		*CurrentInvestigation.Correlation.CausalRecordedIntentId.ToString(),
		CurrentInvestigation.InvestigationPriority,
		*CurrentInvestigation.ResponseRuleId.ToString(),
		CurrentInvestigation.InvestigationRevision);
	if (IsDetailedDebugEnabled())
	{
		PARADOX_LOG_INFO(
			TEXT("Clone '%s' decision=Investigating priority=%d rule=%s revision=%d."),
			*GetNameSafe(GetOwner()),
			CurrentInvestigation.InvestigationPriority,
			*CurrentInvestigation.ResponseRuleId.ToString(),
			CurrentInvestigation.InvestigationRevision);
	}
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("Replay suspended and authoritative investigation context committed."));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::ConsiderInvestigationReplacement(
	FParadoxInvestigationContext Candidate)
{
	const int32 PreviousPriority =
		CurrentInvestigation.InvestigationPriority;
	if (Candidate.InvestigationPriority
		<= PreviousPriority)
	{
		if (IsDetailedDebugEnabled())
		{
			PARADOX_LOG_INFO(
				TEXT("Clone '%s' investigation decision=Ignored current=%d candidate=%d rule=%s revision=%d."),
				*GetNameSafe(GetOwner()),
				CurrentInvestigation.InvestigationPriority,
				Candidate.InvestigationPriority,
				*Candidate.ResponseRuleId.ToString(),
				CurrentInvestigation.InvestigationRevision);
		}
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::Ignored,
			TEXT("Lower or equal priority cannot replace the current investigation."),
			TEXT("Ignored"));
	}

	FParadoxCloneBehaviorOperationResult Replacement;
	if (InvestigationComponent->IsInvestigationActive())
	{
		Replacement =
			InvestigationComponent->RetargetInvestigation(Candidate);
	}
	else if (CurrentInvestigation.InvestigationRevision > 0
		&& InvestigationComponent->GetActiveRevision()
			== CurrentInvestigation.InvestigationRevision)
	{
		// The BT task is still latent while continuity is being reconciled. Restart its single
		// executor immediately; StartInvestigation broadcasts the new exact revision.
		Replacement = InvestigationComponent->StartInvestigation(Candidate);
		if (Replacement.IsSuccess())
		{
			Replacement.Status =
				EParadoxCloneBehaviorOperationStatus::Replaced;
		}
	}
	else
	{
		// The Investigating branch has not entered its native task yet. Validate only and let that
		// task start the latest committed context once, avoiding a duplicate executor.
		Replacement = InvestigationComponent->ValidateInvestigation(Candidate);
		if (Replacement.IsSuccess())
		{
			Replacement.Status =
				EParadoxCloneBehaviorOperationStatus::Replaced;
		}
	}
	if (!Replacement.IsSuccess())
	{
		return Replacement;
	}

	CurrentInvestigation = MoveTemp(Candidate);
	bWaitingForRecoveryMove = false;
	PendingRecoveryIntentId = FRecordedIntentId();
	bRecoveryBlocked = false;
	UpdateBlackboardMirror();
	if (IsDetailedDebugEnabled())
	{
		PARADOX_LOG_WARNING(
			TEXT("Clone '%s' investigation decision=Replaced current=%d candidate=%d rule=%s revision=%d."),
			*GetNameSafe(GetOwner()),
			PreviousPriority,
			CurrentInvestigation.InvestigationPriority,
			*CurrentInvestigation.ResponseRuleId.ToString(),
			CurrentInvestigation.InvestigationRevision);
	}
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Replaced,
		TEXT("Higher-priority investigation replaced the previous target."),
		TEXT("Replaced"));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::ContinueReplayRecovery()
{
	if (!ReplayResumeContext.bCaptured
		|| ReplayResumeContext.PlaybackSessionId
			!= ExpectedPlaybackSessionId)
	{
		BroadcastContinuityFailure(
			TEXT("Replay resume context is missing or belongs to another playback session."));
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ContinuityCannotBeRestored,
			TEXT("Replay resume context is invalid."));
	}
	const UParadoxReplayRecoveryPolicy* Policy = ReplayRecoveryPolicy
		? ReplayRecoveryPolicy
		: GetDefault<UParadoxReplayRecoveryPolicy>();
	while (ReplayResumeContext.SuspendedIntents.IsValidIndex(
		ReplayResumeContext.NextRecoveryIndex))
	{
		const FIntentReplaySuspendedIntent& Suspended =
			ReplayResumeContext.SuspendedIntents[
				ReplayResumeContext.NextRecoveryIndex];
		const FParadoxReplayRecoveryDecision Decision = Policy->Evaluate(
			Suspended,
			GetOwner(),
			PerceptionListener);
		if (Decision.Outcome == EParadoxReplayRecoveryOutcome::AlreadySatisfied)
		{
			const FIntentReplayOperationResult Resolution =
				ReplayComponent->ResolveExternallyInterruptedIntentAsSatisfied(
					Suspended.RecordedIntent.RecordedIntentId);
			if (!Resolution.Succeeded())
			{
				BroadcastContinuityFailure(
					Resolution.Failure.DiagnosticMessage);
				return MakeResult(
					EParadoxCloneBehaviorOperationStatus::ContinuityCannotBeRestored,
					Resolution.Failure.DiagnosticMessage);
			}
			++ReplayResumeContext.NextRecoveryIndex;
			continue;
		}
		if (Decision.Outcome == EParadoxReplayRecoveryOutcome::MoveThenReissue)
		{
			PendingRecoveryIntentId =
				Suspended.RecordedIntent.RecordedIntentId;
			bWaitingForRecoveryMove = true;
			const FParadoxCloneBehaviorOperationResult Move =
				InvestigationComponent->StartReplayRecoveryMove(
					Decision.RepositionLocation,
					CurrentInvestigation.InvestigationRevision,
					CurrentInvestigation.InvestigationPriority);
			if (!Move.IsSuccess())
			{
				bWaitingForRecoveryMove = false;
				PendingRecoveryIntentId = FRecordedIntentId();
				BroadcastContinuityFailure(Move.DiagnosticMessage);
				return MakeResult(
					EParadoxCloneBehaviorOperationStatus::ContinuityCannotBeRestored,
					Move.DiagnosticMessage);
			}
			UpdateBlackboardMirror();
			return MakeResult(
				EParadoxCloneBehaviorOperationStatus::RecoveryPending,
				Decision.DiagnosticMessage);
		}
		if (Decision.Outcome == EParadoxReplayRecoveryOutcome::CannotRestore)
		{
			BroadcastContinuityFailure(Decision.DiagnosticMessage);
			return MakeResult(
				EParadoxCloneBehaviorOperationStatus::ContinuityCannotBeRestored,
				Decision.DiagnosticMessage);
		}

		const FIntentReplayRecoveryResult Reissue =
			ReplayComponent->ReissueExternallyInterruptedIntent(
				Suspended.RecordedIntent.RecordedIntentId);
		if (!Reissue.Succeeded())
		{
			BroadcastContinuityFailure(
				Reissue.Failure.DiagnosticMessage);
			return MakeResult(
				EParadoxCloneBehaviorOperationStatus::ContinuityCannotBeRestored,
				Reissue.Failure.DiagnosticMessage);
		}
		++ReplayResumeContext.NextRecoveryIndex;
	}

	if (ReplayComponent->HasPendingExternalReplayRecovery())
	{
		BroadcastContinuityFailure(
			TEXT("IntentReplay still owns recovery entries after project reconciliation."));
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ContinuityCannotBeRestored,
			TEXT("IntentReplay recovery set is inconsistent."));
	}

	bReplayResumeAvailable = true;
	bRecoveryBlocked = false;
	CurrentInvestigation = FParadoxInvestigationContext();
	SetMode(EParadoxCloneBehaviorMode::Replay, TEXT("InvestigationCompleted"));
	ReplayContinuityNative.Broadcast(true);
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("All interrupted intents reconciled; the Replay Behavior Tree branch may resume."));
}

void UParadoxCloneBehaviorCoordinatorComponent::HandleRecoveryMoveFinished(
	const int32 InvestigationRevision,
	const bool bSucceeded)
{
	if (!bWaitingForRecoveryMove
		|| InvestigationRevision != CurrentInvestigation.InvestigationRevision
		|| !PendingRecoveryIntentId.IsValid())
	{
		return;
	}
	bWaitingForRecoveryMove = false;
	if (!bSucceeded)
	{
		PendingRecoveryIntentId = FRecordedIntentId();
		BroadcastContinuityFailure(
			TEXT("Replay recovery repositioning failed."));
		return;
	}
	const FIntentReplayRecoveryResult Reissue =
		ReplayComponent->ReissueExternallyInterruptedIntent(
			PendingRecoveryIntentId);
	PendingRecoveryIntentId = FRecordedIntentId();
	if (!Reissue.Succeeded())
	{
		BroadcastContinuityFailure(Reissue.Failure.DiagnosticMessage);
		return;
	}
	++ReplayResumeContext.NextRecoveryIndex;
	ContinueReplayRecovery();
}

void UParadoxCloneBehaviorCoordinatorComponent::SetMode(
	const EParadoxCloneBehaviorMode NewMode,
	const FName Reason)
{
	if (CurrentMode == NewMode)
	{
		UpdateBlackboardMirror();
		return;
	}
	const EParadoxCloneBehaviorMode Previous = CurrentMode;
	CurrentMode = NewMode;
	LastModeTransitionReason = Reason;
	++ModeRevision;
	UpdateBlackboardMirror();
	OnModeChanged.Broadcast(Previous, NewMode, ModeRevision);
	ModeChangedNative.Broadcast(Previous, NewMode, ModeRevision);
}

void UParadoxCloneBehaviorCoordinatorComponent::UpdateBlackboardMirror()
{
	if (!BlackboardComponent)
	{
		return;
	}
	BlackboardComponent->SetValueAsEnum(
		ParadoxCloneBlackboardKeys::BehaviorMode,
		static_cast<uint8>(CurrentMode));
	BlackboardComponent->SetValueAsName(
		ParadoxCloneBlackboardKeys::LastModeTransitionReason,
		LastModeTransitionReason);
	const bool bHasInvestigation = CurrentInvestigation.IsValid();
	BlackboardComponent->SetValueAsBool(
		ParadoxCloneBlackboardKeys::HasValidInvestigation,
		bHasInvestigation);
	BlackboardComponent->SetValueAsBool(
		ParadoxCloneBlackboardKeys::ReplayResumeAvailable,
		bReplayResumeAvailable);
	BlackboardComponent->SetValueAsVector(
		ParadoxCloneBlackboardKeys::InvestigationLocation,
		CurrentInvestigation.InvestigationLocation);
	BlackboardComponent->SetValueAsObject(
		ParadoxCloneBlackboardKeys::InvestigationSourceActor,
		CurrentInvestigation.SourceActor.Get());
	BlackboardComponent->SetValueAsString(
		ParadoxCloneBlackboardKeys::InvestigationSourceEntityId,
		CurrentInvestigation.SourceEntityId.ToString());
	BlackboardComponent->SetValueAsString(
		ParadoxCloneBlackboardKeys::InvestigationJournalEntryId,
		CurrentInvestigation.JournalEntryId.ToString());
	BlackboardComponent->SetValueAsEnum(
		ParadoxCloneBlackboardKeys::InvestigationObservationType,
		static_cast<uint8>(CurrentInvestigation.ObservationType));
	BlackboardComponent->SetValueAsName(
		ParadoxCloneBlackboardKeys::InvestigationSemanticTag,
		CurrentInvestigation.SemanticTag.GetTagName());
	BlackboardComponent->SetValueAsName(
		ParadoxCloneBlackboardKeys::InvestigationSense,
		CurrentInvestigation.SenseTag.GetTagName());
	BlackboardComponent->SetValueAsName(
		ParadoxCloneBlackboardKeys::InvestigationResponseRuleId,
		CurrentInvestigation.ResponseRuleId);
	BlackboardComponent->SetValueAsInt(
		ParadoxCloneBlackboardKeys::InvestigationPriority,
		CurrentInvestigation.InvestigationPriority);
	BlackboardComponent->SetValueAsInt(
		ParadoxCloneBlackboardKeys::InvestigationRevision,
		CurrentInvestigation.InvestigationRevision);
	BlackboardComponent->SetValueAsFloat(
		ParadoxCloneBlackboardKeys::InvestigationConfidence,
		CurrentInvestigation.Confidence);
}

void UParadoxCloneBehaviorCoordinatorComponent::BroadcastContinuityFailure(
	const FString& Diagnostic)
{
	bRecoveryBlocked = true;
	bReplayResumeAvailable = false;
	UpdateBlackboardMirror();
	OnReplayContinuityCannotBeRestored.Broadcast(
		CurrentInvestigation,
		Diagnostic);
	ReplayContinuityNative.Broadcast(false);
	PARADOX_LOG_ERROR(
		TEXT("Clone '%s' cannot restore replay continuity at investigation revision %d: %s"),
		*GetNameSafe(GetOwner()),
		CurrentInvestigation.InvestigationRevision,
		*Diagnostic);
}

bool UParadoxCloneBehaviorCoordinatorComponent::IsComparisonAuthoritative(
	const FIntentReplayObservationComparisonEvent& Event) const
{
	return bInitializedForRun
		&& !bGoapHandoffTerminal
		&& ExpectedPlaybackSessionId.IsValid()
		&& Event.PlaybackSessionId == ExpectedPlaybackSessionId
		&& ExpectedObservationTrackId.IsValid()
		&& Event.ObservationTrackId == ExpectedObservationTrackId
		&& ExpectedObservationJournalId.IsValid()
		&& Event.JournalId == ExpectedObservationJournalId;
}

bool UParadoxCloneBehaviorCoordinatorComponent::IsDetailedDebugEnabled() const
{
	return bEnableDebug && IsParadoxCloneBehaviorDebugEnabled();
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneBehaviorCoordinatorComponent::MakeResult(
	const EParadoxCloneBehaviorOperationStatus Status,
	FString Diagnostic,
	const FName Reason) const
{
	FParadoxCloneBehaviorOperationResult Result;
	Result.Status = Status;
	Result.Reason = Reason;
	Result.DiagnosticMessage = MoveTemp(Diagnostic);
	Result.InvestigationRevision =
		CurrentInvestigation.InvestigationRevision;
	return Result;
}

void UParadoxCloneBehaviorCoordinatorComponent::UnbindRuntimeDelegates()
{
	if (ObservationComponent && UnexpectedObservationHandle.IsValid())
	{
		ObservationComponent->OnObservationUnexpectedNative().Remove(
			UnexpectedObservationHandle);
	}
	if (InvestigationComponent && RecoveryMoveFinishedHandle.IsValid())
	{
		InvestigationComponent->OnRecoveryMoveFinishedNative().Remove(
			RecoveryMoveFinishedHandle);
	}
	UnexpectedObservationHandle.Reset();
	RecoveryMoveFinishedHandle.Reset();
}

void UParadoxCloneBehaviorCoordinatorComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	UnbindRuntimeDelegates();
	bInitializedForRun = false;
	Super::EndPlay(EndPlayReason);
}
