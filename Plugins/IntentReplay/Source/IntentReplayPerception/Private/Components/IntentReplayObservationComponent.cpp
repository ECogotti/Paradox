#include "Components/IntentReplayObservationComponent.h"

#include "Actions/GameplayActionInstance.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Data/IntentReplayObservationSessions.h"
#include "Data/IntentReplayObservationTrack.h"
#include "Data/IntentReplayTimelineBundle.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "IntentReplayTags.h"
#include "IntentReplayPerceptionModule.h"
#include "Journal/IntentReplayObservationJournal.h"
#include "PerceptionKnowledgeTags.h"
#include "Playback/IntentReplayPlaybackSession.h"
#include "Policies/IntentReplayObservationPolicies.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Recording/IntentReplayTrack.h"
#include "Settings/IntentReplayPerceptionDeveloperSettings.h"
#include "Subsystems/PerceptionKnowledgeWorldSubsystem.h"
#include "TimerManager.h"
#include "Types/IntentReplayPerceptionValueComparison.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	bool IsRecordingTerminal(const EIntentReplayObservationRecordingState State)
	{
		return State == EIntentReplayObservationRecordingState::Finalized
			|| State == EIntentReplayObservationRecordingState::Failed
			|| State == EIntentReplayObservationRecordingState::Cancelled;
	}

	bool IsComparisonTerminal(const EIntentReplayObservationComparisonState State)
	{
		return State == EIntentReplayObservationComparisonState::Completed
			|| State == EIntentReplayObservationComparisonState::Failed
			|| State == EIntentReplayObservationComparisonState::Cancelled;
	}

	bool IsUnexpected(const EIntentReplayObservationMatchResult Result)
	{
		return Result == EIntentReplayObservationMatchResult::UnexpectedObservation
			|| Result == EIntentReplayObservationMatchResult::UnexpectedStateValue
			|| Result == EIntentReplayObservationMatchResult::UnexpectedStateStatus;
	}

	FPerceptionKnowledgeEntityId GetRecordedEntity(
		const FIntentReplayRecordedObservation& Observation)
	{
		return Observation.Type == EIntentReplayRecordedObservationType::State
			? Observation.State.EntityId
			: Observation.Event.SourceEntityId;
	}

	FString MatchResultToString(const EIntentReplayObservationMatchResult Result)
	{
		const UEnum* Enum = StaticEnum<EIntentReplayObservationMatchResult>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Result)) : TEXT("Unknown");
	}

	FString MismatchReasonToString(const EIntentReplayObservationMismatchReason Reason)
	{
		const UEnum* Enum = StaticEnum<EIntentReplayObservationMismatchReason>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Reason)) : TEXT("Unknown");
	}

	bool IsFiniteNonNegative(const double Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0;
	}

	bool IsVerifiedReplayCorrelation(
		const FIntentReplayObservationCorrelation& Correlation)
	{
		return Correlation.CausalRecordedIntentId.IsValid()
			&& Correlation.Justification
				== EIntentReplayObservationJustification::CorrelatedReplayIntent
			&& Correlation.Reliability
				== EIntentReplayObservationCorrelationReliability::Verified;
	}

	bool IsSameVerifiedReplayOccurrence(
		const FIntentReplayObservationCorrelation& Current,
		const FIntentReplayObservationCorrelation& Expected)
	{
		return IsVerifiedReplayCorrelation(Current)
			&& IsVerifiedReplayCorrelation(Expected)
			&& Current.CausalRecordedIntentId
				== Expected.CausalRecordedIntentId;
	}

	bool ShouldDeferExpectedExpiration(
		const FIntentReplayRecordedObservation& Observation,
		const FIntentReplayObservationMatchOptions& Options)
	{
		if (Observation.Type == EIntentReplayRecordedObservationType::State)
		{
			return Options.bStrictPersistentIdentity
				&& Options
					.bTreatPersistentStateObservationsAsOrderedSnapshots;
		}
		return Options.bTreatVerifiedCausalEventsAsOccurrenceIdentity
			&& IsVerifiedReplayCorrelation(Observation.Event.Correlation);
	}

	bool AreMatchOptionsValid(const FIntentReplayObservationMatchOptions& Options)
	{
		return IsFiniteNonNegative(Options.StateEarlyTolerance)
			&& IsFiniteNonNegative(Options.StateLateTolerance)
			&& IsFiniteNonNegative(Options.EventEarlyTolerance)
			&& IsFiniteNonNegative(Options.EventLateTolerance)
			&& IsFiniteNonNegative(Options.HearingEarlyTolerance)
			&& IsFiniteNonNegative(Options.HearingLateTolerance)
			&& IsFiniteNonNegative(Options.HearingLocationTolerance)
			&& IsFiniteNonNegative(Options.EventLocationTolerance)
			&& IsFiniteNonNegative(Options.FloatTolerance)
			&& IsFiniteNonNegative(Options.VectorTolerance)
			&& IsFiniteNonNegative(Options.StrengthTolerance)
			&& IsFiniteNonNegative(Options.LoudnessTolerance)
			&& IsFiniteNonNegative(Options.StateLocationTolerance)
			&& IsFiniteNonNegative(Options.StateConfidenceTolerance)
			&& Options.JournalOptions.MaxEntries > 0;
	}

	template <typename TimeGetter>
	int32 FindFirstIndexAtOrAfter(
		const TArray<int32>& OrderedIndices,
		const double MinimumTime,
		TimeGetter&& GetTime)
	{
		int32 Low = 0;
		int32 High = OrderedIndices.Num();
		while (Low < High)
		{
			const int32 Middle = Low + (High - Low) / 2;
			if (GetTime(OrderedIndices[Middle]) < MinimumTime)
			{
				Low = Middle + 1;
			}
			else
			{
				High = Middle;
			}
		}
		return Low;
	}
}

UIntentReplayObservationComponent::UIntentReplayObservationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	RecordPolicyClass = UIntentReplayObservationRecordPolicy::StaticClass();
	MatchPolicyClass = UIntentReplayObservationMatchPolicy::StaticClass();
}

void UIntentReplayObservationComponent::InitializeComponent()
{
	Super::InitializeComponent();
	InitializeObservationReplay();
}

void UIntentReplayObservationComponent::UninitializeComponent()
{
	ShutdownObservationReplay();
	Super::UninitializeComponent();
}

void UIntentReplayObservationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownObservationReplay();
	Super::EndPlay(EndPlayReason);
}

void UIntentReplayObservationComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	ShutdownObservationReplay();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::InitializeObservationReplay()
{
	if (bShuttingDown)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::ShuttingDown,
			TEXT("Observation Replay is shutting down."));
	}
	if (bInitialized)
	{
		return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
	}
	if (!IsInGameThread())
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::WrongThread,
			TEXT("Observation Replay must initialize on the Game Thread."));
	}

	BoundIntentReplaySource = IntentReplaySourceOverride;
	BoundPerceptionListener = PerceptionListenerOverride;
	if (AActor* Owner = GetOwner())
	{
		if (!BoundIntentReplaySource)
		{
			BoundIntentReplaySource = Owner->FindComponentByClass<UIntentReplayComponent>();
		}
		if (!BoundPerceptionListener)
		{
			BoundPerceptionListener =
				Owner->FindComponentByClass<UPerceptionKnowledgeListenerComponent>();
		}
	}
	if (!IsValid(BoundIntentReplaySource))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::MissingIntentReplaySource,
			TEXT("No valid IntentReplay component is bound."));
	}
	if (!IsValid(BoundPerceptionListener))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::MissingPerceptionListener,
			TEXT("No valid PerceptionKnowledge Listener is bound."));
	}
	if (BoundIntentReplaySource->GetWorld() != GetWorld()
		|| BoundPerceptionListener->GetWorld() != GetWorld())
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidArgument,
			TEXT("IntentReplay, Listener, and adapter must belong to the same World."));
	}
	if (!RecordPolicyClass || !MatchPolicyClass)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidArgument,
			TEXT("RecordPolicyClass and MatchPolicyClass are required."));
	}

	RecordPolicy = NewObject<UIntentReplayObservationRecordPolicy>(
		this,
		RecordPolicyClass,
		NAME_None,
		RF_Transient);
	MatchPolicy = NewObject<UIntentReplayObservationMatchPolicy>(
		this,
		MatchPolicyClass,
		NAME_None,
		RF_Transient);
	if (!RecordPolicy || !MatchPolicy)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InternalFailure,
			TEXT("Failed to instantiate observation policies."));
	}

	ObserverId = FGuid::NewGuid();
	bInitialized = true;
	BindSources();
	UpdateDebugResources();

	if (bAutoStartObservationRecording)
	{
		const FIntentReplayTimelineClockSnapshot Clock =
			BoundIntentReplaySource->GetRecordingClockSnapshot();
		if (Clock.bValid
			&& Clock.RecordingState == EIntentRecordingState::Recording
			&& Clock.RelativeTimeSeconds <= KINDA_SMALL_NUMBER)
		{
			StartSynchronizedObservationRecording(DefaultRecordingOptions);
		}
	}

	INTENTREPLAYPERCEPTION_LOG_INFO(
		TEXT("%s initialized owner=%s replay=%s listener=%s."),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(BoundIntentReplaySource),
		*GetNameSafe(BoundPerceptionListener));
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::SetIntentReplaySource(
	UIntentReplayComponent* InIntentReplaySource)
{
	if (!IsValid(InIntentReplaySource))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidArgument,
			TEXT("The supplied IntentReplay source is invalid."));
	}
	if (HasActiveSessions())
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("IntentReplay binding cannot change while an observation session is active."));
	}
	UnbindSources();
	IntentReplaySourceOverride = InIntentReplaySource;
	BoundIntentReplaySource = InIntentReplaySource;
	if (bInitialized)
	{
		BindSources();
	}
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::SetPerceptionKnowledgeListener(
	UPerceptionKnowledgeListenerComponent* InListener)
{
	if (!IsValid(InListener))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidArgument,
			TEXT("The supplied PerceptionKnowledge Listener is invalid."));
	}
	if (HasActiveSessions())
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("Listener binding cannot change while an observation session is active."));
	}
	UnbindSources();
	PerceptionListenerOverride = InListener;
	BoundPerceptionListener = InListener;
	if (bInitialized)
	{
		BindSources();
	}
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::StartSynchronizedObservationRecording(
	const FIntentReplayObservationRecordOptions& Options)
{
	if (Options.MaxRecordedObservations < 0)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidArgument,
			TEXT("MaxRecordedObservations cannot be negative."));
	}
	if (!bInitialized)
	{
		const FIntentReplayObservationOperationResult Initialization =
			InitializeObservationReplay();
		if (!Initialization.Succeeded())
		{
			return Initialization;
		}
	}
	if (ActiveRecordingSession && !IsRecordingTerminal(ActiveRecordingSession->State))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("A non-terminal Observation Recording Session already exists."));
	}

	const FIntentReplayTimelineClockSnapshot Clock =
		BoundIntentReplaySource->GetRecordingClockSnapshot();
	if (!Clock.bValid
		|| Clock.RecordingState != EIntentRecordingState::Recording
		|| !Clock.bAcceptingTimelinePoints)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::NoSynchronizedRecording,
			TEXT("IntentReplay has no active recording timeline."));
	}
	if (Clock.RelativeTimeSeconds > KINDA_SMALL_NUMBER)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::LateJoinRejected,
			TEXT("Observation recording must be bound before the authoritative recording starts."));
	}

	UIntentReplayObservationRecordingSession* Session =
		NewObject<UIntentReplayObservationRecordingSession>(this, NAME_None, RF_Transient);
	if (!Session)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InternalFailure,
			TEXT("Failed to allocate Observation Recording Session."));
	}
	Session->SessionId = FIntentReplayObservationRecordingSessionId::NewId();
	Session->ObservationTrackId = FIntentReplayObservationTrackId::NewId();
	Session->SourceRecordingSessionId = Clock.RecordingSessionId;
	Session->SourceTrackId = Clock.TrackId;
	Session->Options = Options;
	Session->State = EIntentReplayObservationRecordingState::Recording;
	ActiveRecordingSession = Session;
	RecordedRuntimeEventIds.Reset();
	LastRecordedStates.Reset();

	INTENTREPLAYPERCEPTION_LOG_INFO(
		TEXT("%s started synchronized observation recording %s for action track %s."),
		*GetNameSafe(this),
		*Session->SessionId.ToString(),
		*Session->SourceTrackId.ToString());
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::StopObservationRecording()
{
	if (!ActiveRecordingSession
		|| ActiveRecordingSession->State != EIntentReplayObservationRecordingState::Recording)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("No Observation Recording Session is accepting a stop request."));
	}
	const FIntentReplayTimelineClockSnapshot Clock =
		BoundIntentReplaySource->GetRecordingClockSnapshot();
	FreezeObservationRecording(Clock.RelativeTimeSeconds);
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::CancelObservationRecording()
{
	if (!ActiveRecordingSession || IsRecordingTerminal(ActiveRecordingSession->State))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("No non-terminal Observation Recording Session exists."));
	}
	ActiveRecordingSession->State = EIntentReplayObservationRecordingState::Cancelled;
	ActiveRecordingSession = nullptr;
	RecordedRuntimeEventIds.Reset();
	LastRecordedStates.Reset();
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

UIntentReplayTimelineBundle* UIntentReplayObservationComponent::CreateTimelineBundle(
	UIntentReplayTrack* ActionTrack,
	UIntentReplayObservationTrack* ObservationTrack,
	FIntentReplayObservationOperationResult& OutResult)
{
	if (!IsValid(ActionTrack) || !IsValid(ObservationTrack))
	{
		OutResult = MakeResult(
			EIntentReplayObservationOperationStatus::InvalidArgument,
			TEXT("Both finalized tracks are required."));
		return nullptr;
	}
	UIntentReplayTimelineBundle* Bundle = NewObject<UIntentReplayTimelineBundle>(
		GetTransientPackage(),
		NAME_None,
		RF_Transient);
	if (!Bundle)
	{
		OutResult = MakeResult(
			EIntentReplayObservationOperationStatus::InternalFailure,
			TEXT("Failed to allocate Timeline Bundle."));
		return nullptr;
	}
	Bundle->InitializeFinalized(ActionTrack, ObservationTrack);
	const FIntentReplayTimelineBundleValidationResult Validation =
		Bundle->ValidateBundle();
	if (!Validation.bValid)
	{
		OutResult = MakeResult(
			EIntentReplayObservationOperationStatus::BundleInvalid,
			Validation.DiagnosticMessage);
		return nullptr;
	}
	OutResult = MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
	return Bundle;
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::StartObservationComparison(
	UIntentReplayTimelineBundle* TimelineBundle,
	const FIntentReplayObservationMatchOptions& Options)
{
	if (!AreMatchOptionsValid(Options))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidArgument,
			TEXT("Comparison tolerances must be finite/non-negative and journal capacity positive."));
	}
	if (!bInitialized)
	{
		const FIntentReplayObservationOperationResult Initialization =
			InitializeObservationReplay();
		if (!Initialization.Succeeded())
		{
			return Initialization;
		}
	}
	if (ActiveComparisonSession && !IsComparisonTerminal(ActiveComparisonSession->State))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("A non-terminal Observation Comparison Session already exists."));
	}
	if (!IsValid(TimelineBundle))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidArgument,
			TEXT("A valid Timeline Bundle is required."));
	}
	const FIntentReplayTimelineBundleValidationResult BundleValidation =
		TimelineBundle->ValidateBundle();
	if (!BundleValidation.bValid)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::BundleInvalid,
			BundleValidation.DiagnosticMessage);
	}

	UIntentReplayPlaybackSession* Playback =
		BoundIntentReplaySource->GetActivePlaybackSession();
	const FIntentReplayTimelineClockSnapshot Clock =
		BoundIntentReplaySource->GetPlaybackClockSnapshot();
	if (!Playback || !Clock.bValid || !Playback->GetSourceTrack())
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::ComparisonUnavailable,
			TEXT("IntentReplay has no prepared playback session."));
	}
	if (Playback->GetSourceTrack()->GetTrackId()
		!= TimelineBundle->GetActionTrack()->GetTrackId())
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::BundleInvalid,
			TEXT("The active playback uses a different Action Replay Track."));
	}
	if (Clock.PlaybackState != EIntentReplayPlaybackState::Ready
		&& Clock.PlaybackState != EIntentReplayPlaybackState::Playing
		&& Clock.PlaybackState != EIntentReplayPlaybackState::Paused)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::ComparisonUnavailable,
			TEXT("Comparison can be armed only for Ready, Playing, or Paused playback."));
	}
	if (Clock.PlaybackState == EIntentReplayPlaybackState::Playing
		&& Clock.RelativeTimeSeconds > KINDA_SMALL_NUMBER
		&& !Options.bAllowLateJoin)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::LateJoinRejected,
			TEXT("Comparison must be armed before StartReplay unless late join is enabled."));
	}

	UIntentReplayObservationComparisonSession* Session =
		NewObject<UIntentReplayObservationComparisonSession>(this, NAME_None, RF_Transient);
	Session->SessionId = FIntentReplayObservationComparisonSessionId::NewId();
	Session->TimelineBundle = TimelineBundle;
	Session->Options = Options;
	Session->Journal = NewObject<UIntentReplayObservationJournal>(
		Session,
		NAME_None,
		RF_Transient);
	Session->Journal->Initialize(
		Clock.PlaybackSessionId,
		TimelineBundle->GetActionTrack()->GetTrackId(),
		TimelineBundle->GetObservationTrack()->GetObservationTrackId(),
		ObserverId,
		MatchPolicy->GetClass()->GetFName(),
		Clock.RelativeTimeSeconds,
		Options.JournalOptions);
	Session->State = Clock.PlaybackState == EIntentReplayPlaybackState::Playing
		? EIntentReplayObservationComparisonState::Comparing
		: (Clock.PlaybackState == EIntentReplayPlaybackState::Paused
			? EIntentReplayObservationComparisonState::Paused
			: EIntentReplayObservationComparisonState::Created);
	ActiveComparisonSession = Session;
	LastComparedStates.Reset();
	BuildComparisonIndexes(*Session);
	Session->Journal->SetPendingExpected(
		TimelineBundle->GetObservationTrack()->GetEntryCount());
	ScheduleExpectedExpiration();
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::PauseObservationComparison()
{
	if (!ActiveComparisonSession || IsComparisonTerminal(ActiveComparisonSession->State))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("No comparison session can be paused."));
	}
	ActiveComparisonSession->bLocallyPaused = true;
	SetComparisonState(EIntentReplayObservationComparisonState::Paused);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpectedExpirationTimerHandle);
	}
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::ResumeObservationComparison()
{
	if (!ActiveComparisonSession
		|| IsComparisonTerminal(ActiveComparisonSession->State)
		|| !ActiveComparisonSession->bLocallyPaused)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("No locally paused comparison session can resume."));
	}
	ActiveComparisonSession->bLocallyPaused = false;
	const FIntentReplayTimelineClockSnapshot Clock =
		BoundIntentReplaySource->GetPlaybackClockSnapshot();
	SetComparisonState(Clock.PlaybackState == EIntentReplayPlaybackState::Playing
		? EIntentReplayObservationComparisonState::Comparing
		: EIntentReplayObservationComparisonState::Paused);
	ScheduleExpectedExpiration();
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::SetObservationComparisonEnabled(
	const bool bEnabled)
{
	if (!ActiveComparisonSession || IsComparisonTerminal(ActiveComparisonSession->State))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("No comparison session can change enabled state."));
	}
	ActiveComparisonSession->bComparisonEnabled = bEnabled;
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::StopObservationComparison()
{
	if (!ActiveComparisonSession || IsComparisonTerminal(ActiveComparisonSession->State))
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::InvalidState,
			TEXT("No non-terminal comparison session exists."));
	}
	CompleteComparison(EIntentReplayObservationComparisonState::Cancelled);
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

UIntentReplayObservationJournal*
UIntentReplayObservationComponent::GetActiveObservationJournal() const
{
	return ActiveComparisonSession ? ActiveComparisonSession->Journal.Get() : nullptr;
}

FIntentReplayObservationComparisonSummary
UIntentReplayObservationComponent::GetComparisonSummary() const
{
	return ActiveComparisonSession && ActiveComparisonSession->Journal
		? ActiveComparisonSession->Journal->GetSummary()
		: FIntentReplayObservationComparisonSummary();
}

TArray<FIntentReplayRecordedObservation>
UIntentReplayObservationComponent::GetPendingExpectedObservations() const
{
	TArray<FIntentReplayRecordedObservation> Result;
	if (!ActiveComparisonSession || !ActiveComparisonSession->TimelineBundle)
	{
		return Result;
	}
	for (const FIntentReplayRecordedObservation& Entry :
		ActiveComparisonSession->TimelineBundle->GetObservationTrack()->GetEntries())
	{
		const FRecordedObservationId Id = Entry.GetRecordedObservationId();
		if (!ActiveComparisonSession->ConsumedObservationIds.Contains(Id)
			&& !ActiveComparisonSession->ExpiredObservationIds.Contains(Id))
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

void UIntentReplayObservationComponent::SetDebugEnabled(const bool bEnabled)
{
	bEnableDebug = bEnabled;
	UpdateDebugResources();
}

FIntentReplayObservationDebugFrame
UIntentReplayObservationComponent::BuildDebugFrame() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplayPerception_BuildDebugSnapshot);
	const double StartSeconds = FPlatformTime::Seconds();
	FIntentReplayObservationDebugFrame Frame;
	if (!bEnableDebug || !IsIntentReplayPerceptionDebugEnabled())
	{
		return Frame;
	}
	Frame.bShouldDraw = true;
	Frame.Clock = BoundIntentReplaySource
		? BoundIntentReplaySource->GetPlaybackClockSnapshot()
		: FIntentReplayTimelineClockSnapshot();
	Frame.Summary = GetComparisonSummary();
	Frame.ComparisonState = ActiveComparisonSession
		? ActiveComparisonSession->State
		: EIntentReplayObservationComparisonState::Created;
	Frame.ActionTrackId = Frame.Clock.TrackId;
	if (BoundPerceptionListener)
	{
		Frame.bHasViewpoint = BoundPerceptionListener->GetListenerViewpoint(
			Frame.ViewLocation,
			Frame.ViewDirection);
	}
	if (!ActiveComparisonSession || !ActiveComparisonSession->Journal)
	{
		return Frame;
	}
	Frame.ObservationTrackId =
		ActiveComparisonSession->TimelineBundle->GetObservationTrack()
			->GetObservationTrackId();
	Frame.JournalId = ActiveComparisonSession->Journal->GetJournalId();
	Frame.HearingLocationTolerance =
		ActiveComparisonSession->Options.HearingLocationTolerance;
	Frame.bExpensiveDataBuilt = true;

	TMap<FPerceptionKnowledgeEntityId, FIntentReplayObservationJournalEntry> LatestByEntity;
	TMap<FPerceptionKnowledgeEntityId, EIntentReplayObservationDebugStatus> ForcedStatuses;
	const float RecentLifetime =
		GetDefault<UIntentReplayPerceptionDeveloperSettings>()
			->RecentUnexpectedLifetime;
	auto IsResultVisible = [&](const EIntentReplayObservationMatchResult Result)
	{
		if (Result == EIntentReplayObservationMatchResult::Matched)
		{
			return DebugFilter.bDrawMatched;
		}
		if (Result == EIntentReplayObservationMatchResult::Ambiguous)
		{
			return DebugFilter.bDrawAmbiguous;
		}
		if (IsUnexpected(Result))
		{
			return DebugFilter.bDrawUnexpected;
		}
		if (Result == EIntentReplayObservationMatchResult::ExpectedRecordPending)
		{
			return DebugFilter.bDrawPending;
		}
		return true;
	};
	for (const FIntentReplayObservationJournalEntry& Entry :
		ActiveComparisonSession->Journal->GetEntries())
	{
		const bool bHasCurrentState =
			Entry.CurrentObservation.Type
				== EPerceptionKnowledgeObservationType::State
			&& Entry.CurrentObservation.State.Key.StateTag.IsValid();
		const bool bHasCurrentEvent =
			Entry.CurrentObservation.Type
				== EPerceptionKnowledgeObservationType::Event
			&& Entry.CurrentObservation.Event.EventTag.IsValid();
		const bool bRepresentsState = bHasCurrentState
			|| (!bHasCurrentEvent
				&& Entry.bHasExpectedObservation
				&& Entry.ExpectedObservation.Type
					== EIntentReplayRecordedObservationType::State);
		if ((bRepresentsState && !DebugFilter.bDrawStates)
			|| (!bRepresentsState && !DebugFilter.bDrawEvents)
			|| !IsResultVisible(Entry.Result))
		{
			continue;
		}
		const FPerceptionKnowledgeEntityId EntityId =
			bHasCurrentState
				? Entry.CurrentObservation.State.Key.EntityId
				: (bHasCurrentEvent
					&& Entry.CurrentObservation.Event.SourceEntityId.IsValid()
					? Entry.CurrentObservation.Event.SourceEntityId
					: (Entry.bHasExpectedObservation
						? GetRecordedEntity(Entry.ExpectedObservation)
						: FPerceptionKnowledgeEntityId()));
		if (EntityId.IsValid())
		{
			LatestByEntity.Add(EntityId, Entry);
		}
		const double EventAge =
			Frame.Clock.RelativeTimeSeconds - Entry.CurrentRelativeTime;
		if (bHasCurrentEvent
			&& (IsUnexpected(Entry.Result)
				|| Entry.Result == EIntentReplayObservationMatchResult::Ambiguous)
			&& EventAge >= 0.0
			&& EventAge <= RecentLifetime
			&& Frame.RecentEventEntries.Num() < 32)
		{
			Frame.RecentEventEntries.Add(Entry);
		}
	}

	for (const FIntentReplayRecordedObservation& Expected :
		ActiveComparisonSession->TimelineBundle->GetObservationTrack()->GetEntries())
	{
		const FRecordedObservationId Id = Expected.GetRecordedObservationId();
		const bool bConsumed =
			ActiveComparisonSession->ConsumedObservationIds.Contains(Id);
		const bool bPending = !bConsumed
			&& !ActiveComparisonSession->ExpiredObservationIds.Contains(Id);
		if ((!bPending && !bConsumed)
			|| (bPending && !DebugFilter.bDrawPending)
			|| (bConsumed && !DebugFilter.bDrawConsumed)
			|| (Expected.Type == EIntentReplayRecordedObservationType::State
				&& !DebugFilter.bDrawStates)
			|| (Expected.Type == EIntentReplayRecordedObservationType::Event
				&& !DebugFilter.bDrawEvents))
		{
			continue;
		}
		const FPerceptionKnowledgeEntityId EntityId = GetRecordedEntity(Expected);
		if (!EntityId.IsValid()
			|| LatestByEntity.Contains(EntityId)
			|| (DebugFilter.SelectedEntityId.IsValid()
				&& EntityId != DebugFilter.SelectedEntityId))
		{
			continue;
		}
		FIntentReplayObservationJournalEntry Synthetic;
		Synthetic.bHasExpectedObservation = true;
		Synthetic.ExpectedObservation = Expected;
		Synthetic.Result = bPending
			? EIntentReplayObservationMatchResult::ExpectedRecordPending
			: EIntentReplayObservationMatchResult::Matched;
		LatestByEntity.Add(EntityId, MoveTemp(Synthetic));
		ForcedStatuses.Add(
			EntityId,
			bPending
				? EIntentReplayObservationDebugStatus::Pending
				: EIntentReplayObservationDebugStatus::Consumed);
	}

	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	for (const TPair<FPerceptionKnowledgeEntityId, FIntentReplayObservationJournalEntry>& Pair :
		LatestByEntity)
	{
		if (DebugFilter.SelectedEntityId.IsValid()
			&& Pair.Key != DebugFilter.SelectedEntityId)
		{
			continue;
		}
		FIntentReplayObservationDebugEntityFrame EntityFrame;
		EntityFrame.EntityId = Pair.Key;
		const FIntentReplayObservationJournalEntry& Entry = Pair.Value;
		if (const EIntentReplayObservationDebugStatus* Forced =
			ForcedStatuses.Find(Pair.Key))
		{
			EntityFrame.Status = *Forced;
		}
		else if (Entry.CurrentCorrelation.Reliability
				== EIntentReplayObservationCorrelationReliability::Verified
			&& Entry.CurrentCorrelation.Justification
				!= EIntentReplayObservationJustification::None
			&& IsUnexpected(Entry.Result))
		{
			EntityFrame.Status = EIntentReplayObservationDebugStatus::Justified;
		}
		else if (Entry.Result == EIntentReplayObservationMatchResult::Matched)
		{
			EntityFrame.Status = EIntentReplayObservationDebugStatus::Matched;
		}
		else if (Entry.Result == EIntentReplayObservationMatchResult::Ambiguous)
		{
			EntityFrame.Status = EIntentReplayObservationDebugStatus::Ambiguous;
		}
		else if (IsUnexpected(Entry.Result))
		{
			EntityFrame.Status = EIntentReplayObservationDebugStatus::Unexpected;
		}
		else
		{
			EntityFrame.Status = EIntentReplayObservationDebugStatus::Inactive;
		}
		EntityFrame.Color = GetDebugColor(EntityFrame.Status);
		EntityFrame.Label = DebugFilter.TextDetailLevel > 0
			? FString::Printf(
				TEXT("%s\n%s / %s"),
				*Pair.Key.ToShortString(),
				*MatchResultToString(Entry.Result),
				*MismatchReasonToString(Entry.Reason))
			: Pair.Key.ToShortString();
		if (Subsystem)
		{
			if (UPerceptionKnowledgeSourceComponent* Source = Subsystem->FindSource(Pair.Key))
			{
				if (AActor* Actor = Source->GetOwner())
				{
					EntityFrame.ActorName = Actor->GetName();
					Actor->GetActorBounds(
						false,
						EntityFrame.BoundsOrigin,
						EntityFrame.BoundsExtent);
				}
			}
		}
		if (Frame.bHasViewpoint
			&& !EntityFrame.BoundsExtent.IsNearlyZero()
			&& FVector::DistSquared(Frame.ViewLocation, EntityFrame.BoundsOrigin)
				> FMath::Square(DebugFilter.MaxDrawDistance))
		{
			continue;
		}
		Frame.Entities.Add(MoveTemp(EntityFrame));
	}

	UIntentReplayObservationComponent* MutableThis =
		const_cast<UIntentReplayObservationComponent*>(this);
	++MutableThis->RuntimeStats.DebugFramesBuilt;
	MutableThis->RuntimeStats.LastDebugBuildMilliseconds =
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	return Frame;
}

void UIntentReplayObservationComponent::DumpObservationTimelineToLog() const
{
	const FIntentReplayObservationComparisonSummary Summary = GetComparisonSummary();
	INTENTREPLAYPERCEPTION_LOG_INFO(
		TEXT("%s owner=%s initialized=%d recording=%s comparison=%s recorded=%d "
			"matched=%d unexpected=%d ambiguous=%d pending=%d."),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		bInitialized ? 1 : 0,
		ActiveRecordingSession
			? *UEnum::GetValueAsString(ActiveRecordingSession->State)
			: TEXT("None"),
		ActiveComparisonSession
			? *UEnum::GetValueAsString(ActiveComparisonSession->State)
			: TEXT("None"),
		LastFinalizedObservationTrack
			? LastFinalizedObservationTrack->GetEntryCount()
			: (ActiveRecordingSession ? ActiveRecordingSession->GetEntryCount() : 0),
		Summary.Matched,
		Summary.Unexpected,
		Summary.Ambiguous,
		Summary.PendingExpected);
}

void UIntentReplayObservationComponent::ResolveObservationCorrelation_Implementation(
	const FPerceptionKnowledgeObservation& Observation,
	FIntentReplayObservationCorrelation& OutCorrelation) const
{
	OutCorrelation = FIntentReplayObservationCorrelation();
	if (Observation.Type != EPerceptionKnowledgeObservationType::Event)
	{
		return;
	}
	const FPerceptionKnowledgeEntityId ObserverEntityId = ResolveObserverEntityId();
	if (ObserverEntityId.IsValid()
		&& Observation.Event.InstigatorEntityId == ObserverEntityId)
	{
		OutCorrelation.Justification =
			EIntentReplayObservationJustification::ObserverCaused;
		OutCorrelation.Reliability =
			EIntentReplayObservationCorrelationReliability::Verified;
		return;
	}

	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		GetWorld()
			? GetWorld()->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()
			: nullptr;
	UPerceptionKnowledgeSourceComponent* Source =
		Subsystem
			? Subsystem->FindSource(Observation.Event.SourceEntityId)
			: nullptr;
	AActor* SourceActor = Source ? Source->GetOwner() : nullptr;
	UIntentReplayComponent* SourceReplay = SourceActor
		? SourceActor->FindComponentByClass<UIntentReplayComponent>()
		: nullptr;
	const UIntentReplayPlaybackSession* PlaybackSession =
		SourceReplay ? SourceReplay->GetActivePlaybackSession() : nullptr;
	UGameplayActionComponent* SourceActions =
		SourceReplay ? SourceReplay->GetBoundActionComponent() : nullptr;
	if (!PlaybackSession || !SourceActions)
	{
		return;
	}

	FRecordedIntentId ResolvedIntentId;
	FGameplayTag ResolvedOrigin;
	for (const FGameplayActionHandle Handle :
		PlaybackSession->GetReplayOwnedActionHandles())
	{
		const UGameplayActionInstance* Instance =
			SourceActions->GetActionInstance(Handle);
		if (!Instance || Instance->GetState() != EGameplayActionState::Running)
		{
			continue;
		}
		const FGameplayActionCorrelationData ActionCorrelation =
			Instance->GetCorrelation();
		if (ActionCorrelation.Type
				!= IntentReplayTags::Correlation_RecordedIntent
			|| !ActionCorrelation.Id.IsValid())
		{
			continue;
		}

		const FRecordedIntentId CandidateIntentId(ActionCorrelation.Id);
		if (ResolvedIntentId.IsValid()
			&& ResolvedIntentId != CandidateIntentId)
		{
			// Several replay intents could plausibly cause this event. Do not guess.
			return;
		}
		ResolvedIntentId = CandidateIntentId;
		ResolvedOrigin = Instance->GetOriginTag();
	}

	if (ResolvedIntentId.IsValid())
	{
		OutCorrelation.CausalRecordedIntentId = ResolvedIntentId;
		OutCorrelation.OriginTag = ResolvedOrigin;
		OutCorrelation.Justification =
			EIntentReplayObservationJustification::CorrelatedReplayIntent;
		OutCorrelation.Reliability =
			EIntentReplayObservationCorrelationReliability::Verified;
	}
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::MakeResult(
	const EIntentReplayObservationOperationStatus Status,
	FString Diagnostic) const
{
	FIntentReplayObservationOperationResult Result;
	Result.Status = Status;
	Result.DiagnosticMessage = MoveTemp(Diagnostic);
	return Result;
}

bool UIntentReplayObservationComponent::HasActiveSessions() const
{
	return (ActiveRecordingSession && !IsRecordingTerminal(ActiveRecordingSession->State))
		|| (ActiveComparisonSession && !IsComparisonTerminal(ActiveComparisonSession->State));
}

void UIntentReplayObservationComponent::BindSources()
{
	UnbindSources();
	if (BoundIntentReplaySource)
	{
		TimelineLifecycleHandle =
			BoundIntentReplaySource->OnTimelineLifecycleChangedNative().AddUObject(
				this,
				&UIntentReplayObservationComponent::HandleTimelineLifecycleChanged);
	}
	if (BoundPerceptionListener)
	{
		ObservationProducedHandle =
			BoundPerceptionListener->OnObservationProducedNative().AddUObject(
				this,
				&UIntentReplayObservationComponent::HandleObservationProduced);
		EntityPerceptionChangedHandle =
			BoundPerceptionListener->OnEntityPerceptionChangedNative().AddUObject(
				this,
				&UIntentReplayObservationComponent::HandleEntityPerceptionChanged);
	}
}

void UIntentReplayObservationComponent::UnbindSources()
{
	if (BoundIntentReplaySource && TimelineLifecycleHandle.IsValid())
	{
		BoundIntentReplaySource->OnTimelineLifecycleChangedNative().Remove(
			TimelineLifecycleHandle);
	}
	if (BoundPerceptionListener)
	{
		if (ObservationProducedHandle.IsValid())
		{
			BoundPerceptionListener->OnObservationProducedNative().Remove(
				ObservationProducedHandle);
		}
		if (EntityPerceptionChangedHandle.IsValid())
		{
			BoundPerceptionListener->OnEntityPerceptionChangedNative().Remove(
				EntityPerceptionChangedHandle);
		}
	}
	TimelineLifecycleHandle.Reset();
	ObservationProducedHandle.Reset();
	EntityPerceptionChangedHandle.Reset();
}

void UIntentReplayObservationComponent::ShutdownObservationReplay()
{
	if (bShuttingDown)
	{
		return;
	}
	bShuttingDown = true;
	if (ActiveRecordingSession && !IsRecordingTerminal(ActiveRecordingSession->State))
	{
		ActiveRecordingSession->State =
			EIntentReplayObservationRecordingState::Cancelled;
	}
	if (ActiveComparisonSession && !IsComparisonTerminal(ActiveComparisonSession->State))
	{
		CompleteComparison(EIntentReplayObservationComparisonState::Cancelled);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpectedExpirationTimerHandle);
		World->GetTimerManager().ClearTimer(DebugTimerHandle);
	}
	if (DebugCanvasHandle.IsValid())
	{
		UDebugDrawService::Unregister(DebugCanvasHandle);
		DebugCanvasHandle.Reset();
	}
	UnbindSources();
	ActiveRecordingSession = nullptr;
	bInitialized = false;
}

void UIntentReplayObservationComponent::HandleTimelineLifecycleChanged(
	const FIntentReplayTimelineLifecycleEvent& Event)
{
	if (bShuttingDown)
	{
		return;
	}
	if (Event.Domain == EIntentReplayTimelineDomain::Recording)
	{
		if (Event.NewRecordingState == EIntentRecordingState::Recording
			&& Event.PreviousRecordingState == EIntentRecordingState::Created
			&& bAutoStartObservationRecording
			&& !ActiveRecordingSession)
		{
			StartSynchronizedObservationRecording(DefaultRecordingOptions);
		}
		else if ((Event.NewRecordingState == EIntentRecordingState::Draining
				|| Event.NewRecordingState == EIntentRecordingState::Finalized)
			&& ActiveRecordingSession
			&& ActiveRecordingSession->State
				== EIntentReplayObservationRecordingState::Recording)
		{
			FreezeObservationRecording(Event.Clock.RelativeTimeSeconds);
		}

		if (Event.NewRecordingState == EIntentRecordingState::Finalized
			&& ActiveRecordingSession
			&& BoundIntentReplaySource
			&& BoundIntentReplaySource->GetLastFinalizedTrack())
		{
			FinalizeObservationRecording(
				*BoundIntentReplaySource->GetLastFinalizedTrack());
		}
		else if ((Event.NewRecordingState == EIntentRecordingState::Failed
				|| Event.NewRecordingState == EIntentRecordingState::Cancelled)
			&& ActiveRecordingSession)
		{
			ActiveRecordingSession->State =
				Event.NewRecordingState == EIntentRecordingState::Failed
					? EIntentReplayObservationRecordingState::Failed
					: EIntentReplayObservationRecordingState::Cancelled;
			ActiveRecordingSession = nullptr;
		}
		return;
	}

	if (!ActiveComparisonSession
		|| IsComparisonTerminal(ActiveComparisonSession->State))
	{
		return;
	}
	switch (Event.NewPlaybackState)
	{
	case EIntentReplayPlaybackState::Playing:
		if (!ActiveComparisonSession->bLocallyPaused)
		{
			SetComparisonState(EIntentReplayObservationComparisonState::Comparing);
			ProcessExpectedExpirations();
			ScheduleExpectedExpiration();
		}
		break;
	case EIntentReplayPlaybackState::Paused:
		SetComparisonState(EIntentReplayObservationComparisonState::Paused);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ExpectedExpirationTimerHandle);
		}
		break;
	case EIntentReplayPlaybackState::Stopping:
		SetComparisonState(EIntentReplayObservationComparisonState::Paused);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ExpectedExpirationTimerHandle);
		}
		break;
	case EIntentReplayPlaybackState::Completed:
		CompleteComparison(EIntentReplayObservationComparisonState::Completed);
		break;
	case EIntentReplayPlaybackState::Failed:
		CompleteComparison(EIntentReplayObservationComparisonState::Failed);
		break;
	case EIntentReplayPlaybackState::Cancelled:
		CompleteComparison(EIntentReplayObservationComparisonState::Cancelled);
		break;
	default:
		break;
	}
}

void UIntentReplayObservationComponent::HandleObservationProduced(
	const FPerceptionKnowledgeObservation& Observation)
{
	if (bShuttingDown)
	{
		return;
	}
	if (ActiveRecordingSession
		&& ActiveRecordingSession->State
			== EIntentReplayObservationRecordingState::Recording)
	{
		RecordObservation(Observation);
	}
	if (ActiveComparisonSession && !IsComparisonTerminal(ActiveComparisonSession->State))
	{
		CompareObservation(Observation);
	}
}

void UIntentReplayObservationComponent::HandleEntityPerceptionChanged(
	const FPerceptionKnowledgeEntityId EntityId,
	const FGameplayTag SenseTag,
	const bool bCurrentlyPerceived)
{
	if (!bCurrentlyPerceived)
	{
		return;
	}
	FObservedStateKey EpochKey;
	EpochKey.EntityId = EntityId;
	EpochKey.SenseTag = SenseTag;
	int64& Epoch = PerceptionEpochs.FindOrAdd(EpochKey);
	++Epoch;
}

void UIntentReplayObservationComponent::FreezeObservationRecording(
	const double FinalDuration)
{
	if (!ActiveRecordingSession
		|| ActiveRecordingSession->State
			!= EIntentReplayObservationRecordingState::Recording)
	{
		return;
	}
	ActiveRecordingSession->FinalRecordedDurationSeconds =
		FMath::Max(0.0, FinalDuration);
	ActiveRecordingSession->State =
		EIntentReplayObservationRecordingState::Draining;
}

void UIntentReplayObservationComponent::FinalizeObservationRecording(
	UIntentReplayTrack& ActionTrack)
{
	if (!ActiveRecordingSession
		|| ActiveRecordingSession->State
			!= EIntentReplayObservationRecordingState::Draining)
	{
		return;
	}
	UIntentReplayObservationRecordingSession& Session = *ActiveRecordingSession;
	if (ActionTrack.GetTrackId() != Session.SourceTrackId
		|| ActionTrack.GetSourceRecordingSessionId()
			!= Session.SourceRecordingSessionId
		|| ActionTrack.GetFormatVersion() < 2)
	{
		Session.State = EIntentReplayObservationRecordingState::Failed;
		ActiveRecordingSession = nullptr;
		INTENTREPLAYPERCEPTION_LOG_ERROR(
			TEXT("%s refused to pair observation recording with an unrelated Action Track."),
			*GetNameSafe(this));
		return;
	}

	Session.MutableEntries.Sort(
		[](const FIntentReplayRecordedObservation& Left,
			const FIntentReplayRecordedObservation& Right)
		{
			if (!FMath::IsNearlyEqual(
				Left.GetRelativeTimestamp(),
				Right.GetRelativeTimestamp()))
			{
				return Left.GetRelativeTimestamp() < Right.GetRelativeTimestamp();
			}
			return Left.GetTimelineSequence() < Right.GetTimelineSequence();
		});

	UIntentReplayObservationTrack* Track =
		NewObject<UIntentReplayObservationTrack>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient);
	if (!Track)
	{
		Session.State = EIntentReplayObservationRecordingState::Failed;
		ActiveRecordingSession = nullptr;
		return;
	}
	Track->InitializeFinalized(
		Session.ObservationTrackId,
		Session.SourceTrackId,
		Session.SourceRecordingSessionId,
		MoveTemp(Session.MutableEntries),
		ActionTrack.GetRecordedDurationSeconds());
	const FIntentReplayObservationTrackValidationResult Validation =
		Track->ValidateTrack();
	if (!Validation.bValid)
	{
		Session.State = EIntentReplayObservationRecordingState::Failed;
		ActiveRecordingSession = nullptr;
		INTENTREPLAYPERCEPTION_LOG_ERROR(
			TEXT("%s failed Observation Track validation: %s"),
			*GetNameSafe(this),
			*Validation.DiagnosticMessage);
		return;
	}

	FIntentReplayObservationOperationResult BundleResult;
	UIntentReplayTimelineBundle* Bundle =
		CreateTimelineBundle(&ActionTrack, Track, BundleResult);
	if (!BundleResult.Succeeded() || !Bundle)
	{
		Session.State = EIntentReplayObservationRecordingState::Failed;
		ActiveRecordingSession = nullptr;
		INTENTREPLAYPERCEPTION_LOG_ERROR(
			TEXT("%s failed Timeline Bundle validation: %s"),
			*GetNameSafe(this),
			*BundleResult.DiagnosticMessage);
		return;
	}

	Session.State = EIntentReplayObservationRecordingState::Finalized;
	LastFinalizedObservationTrack = Track;
	LastTimelineBundle = Bundle;
	ActiveRecordingSession = nullptr;
	ObservationTrackFinalizedNative.Broadcast(Track, Bundle);
	OnObservationTrackFinalized.Broadcast(Track, Bundle);
}

FIntentReplayObservationOperationResult
UIntentReplayObservationComponent::RecordObservation(
	const FPerceptionKnowledgeObservation& Observation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplayPerception_RecordObservation);
	if (!ActiveRecordingSession || !BoundIntentReplaySource || !RecordPolicy)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::NoSynchronizedRecording,
			TEXT("No synchronized Observation Recording Session exists."));
	}
	if (!RecordPolicy->IsObservationAllowed(
		Observation,
		ActiveRecordingSession->Options))
	{
		return MakeResult(EIntentReplayObservationOperationStatus::PolicyFiltered);
	}

	if (Observation.Type == EPerceptionKnowledgeObservationType::Event)
	{
		if (RecordedRuntimeEventIds.Contains(Observation.Event.ObservationId))
		{
			++RuntimeStats.DuplicateObservations;
			return MakeResult(
				EIntentReplayObservationOperationStatus::DuplicateRuntimeCallback);
		}
	}
	else
	{
		const FObservedStateKey Key{
			Observation.State.Key.EntityId,
			Observation.State.Key.StateTag,
			Observation.State.SenseTag };
		const FStateSignature* Previous = LastRecordedStates.Find(Key);
		FObservedStateKey EpochKey{ Key.EntityId, FGameplayTag(), Key.SenseTag };
		const int64 Epoch = PerceptionEpochs.FindRef(EpochKey);
		if (Previous
			&& Previous->Status == Observation.State.Status
			&& IntentReplayPerception::AreValuesExactlyEqual(
				Previous->Value,
				Observation.State.Value)
			&& (!ActiveRecordingSession->Options.bRecordReacquisition
				|| Previous->PerceptionEpoch == Epoch))
		{
			return MakeResult(EIntentReplayObservationOperationStatus::PolicyFiltered);
		}
	}

	if (ActiveRecordingSession->Options.MaxRecordedObservations > 0
		&& ActiveRecordingSession->MutableEntries.Num()
			>= ActiveRecordingSession->Options.MaxRecordedObservations)
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::CapacityExceeded,
			TEXT("Observation Track capacity was reached."));
	}
	const FIntentReplayTimelinePointResult TimelinePoint =
		BoundIntentReplaySource->CaptureRecordingTimelinePoint(
			ActiveRecordingSession->SourceRecordingSessionId);
	if (!TimelinePoint.Succeeded())
	{
		return MakeResult(
			EIntentReplayObservationOperationStatus::MissingAuthoritativeClock,
			TEXT("IntentReplay rejected the synchronized recording timeline point."));
	}

	FIntentReplayObservationCorrelation Correlation;
	ResolveObservationCorrelation(Observation, Correlation);
	FIntentReplayRecordedObservation Recorded = MakeRecordedObservation(
		Observation,
		TimelinePoint,
		Correlation);
	ActiveRecordingSession->MutableEntries.Add(Recorded);
	if (Observation.Type == EPerceptionKnowledgeObservationType::Event)
	{
		RecordedRuntimeEventIds.Add(Observation.Event.ObservationId);
	}
	else
	{
		FObservedStateKey Key{
			Observation.State.Key.EntityId,
			Observation.State.Key.StateTag,
			Observation.State.SenseTag };
		FObservedStateKey EpochKey{ Key.EntityId, FGameplayTag(), Key.SenseTag };
		FStateSignature Signature;
		Signature.Value = Observation.State.Value;
		Signature.Status = Observation.State.Status;
		Signature.PerceptionEpoch = PerceptionEpochs.FindRef(EpochKey);
		Signature.WorldTimestamp = Observation.State.WorldTimestamp;
		LastRecordedStates.Add(Key, MoveTemp(Signature));
	}
	++RuntimeStats.RecordedObservations;
	ObservationRecordedNative.Broadcast(Recorded);
	OnObservationRecorded.Broadcast(Recorded);
	return MakeResult(EIntentReplayObservationOperationStatus::Succeeded);
}

FIntentReplayRecordedObservation
UIntentReplayObservationComponent::MakeRecordedObservation(
	const FPerceptionKnowledgeObservation& Observation,
	const FIntentReplayTimelinePointResult& TimelinePoint,
	const FIntentReplayObservationCorrelation& Correlation) const
{
	FIntentReplayRecordedObservation Result;
	if (Observation.Type == EPerceptionKnowledgeObservationType::State)
	{
		Result.Type = EIntentReplayRecordedObservationType::State;
		Result.State.RecordedObservationId = FRecordedObservationId::NewId();
		Result.State.EntityId = Observation.State.Key.EntityId;
		Result.State.StateTag = Observation.State.Key.StateTag;
		Result.State.Value = Observation.State.Value;
		Result.State.Status = Observation.State.Status;
		Result.State.SenseTag = Observation.State.SenseTag;
		Result.State.Confidence = Observation.State.Confidence;
		Result.State.ObservationLocation = Observation.State.ObservationLocation;
		Result.State.SourceWorldTimestamp = Observation.State.WorldTimestamp;
		Result.State.RelativeTimestamp = TimelinePoint.Clock.RelativeTimeSeconds;
		Result.State.TimelineSequence = TimelinePoint.TimelineSequence;
		Result.State.Correlation = Correlation;
	}
	else
	{
		Result.Type = EIntentReplayRecordedObservationType::Event;
		Result.Event.RecordedObservationId = FRecordedObservationId::NewId();
		Result.Event.SourceObservationId = Observation.Event.ObservationId;
		Result.Event.EventTag = Observation.Event.EventTag;
		Result.Event.SenseTag = Observation.Event.SenseTag;
		Result.Event.SourceEntityId = Observation.Event.SourceEntityId;
		Result.Event.InstigatorEntityId = Observation.Event.InstigatorEntityId;
		Result.Event.WorldLocation = Observation.Event.WorldLocation;
		Result.Event.Loudness = Observation.Event.Loudness;
		Result.Event.Strength = Observation.Event.Strength;
		Result.Event.Confidence = Observation.Event.Confidence;
		Result.Event.SourceWorldTimestamp = Observation.Event.WorldTimestamp;
		Result.Event.CauseTag = Observation.Event.CauseTag;
		Result.Event.RelativeTimestamp = TimelinePoint.Clock.RelativeTimeSeconds;
		Result.Event.TimelineSequence = TimelinePoint.TimelineSequence;
		Result.Event.Correlation = Correlation;
	}
	return Result;
}

bool UIntentReplayObservationComponent::IsRedundantStateObservation(
	const FPerceptionKnowledgeStateObservation& State,
	const bool bForRecording)
{
	FObservedStateKey Key{ State.Key.EntityId, State.Key.StateTag, State.SenseTag };
	FObservedStateKey EpochKey{ Key.EntityId, FGameplayTag(), Key.SenseTag };
	const int64 Epoch = PerceptionEpochs.FindRef(EpochKey);
	TMap<FObservedStateKey, FStateSignature>& Signatures =
		bForRecording ? LastRecordedStates : LastComparedStates;
	const FStateSignature* Previous = Signatures.Find(Key);
	const bool bRedundant = Previous
		&& Previous->Status == State.Status
		&& IntentReplayPerception::AreValuesExactlyEqual(
			Previous->Value,
			State.Value)
		&& Previous->PerceptionEpoch == Epoch;
	FStateSignature Current;
	Current.Value = State.Value;
	Current.Status = State.Status;
	Current.PerceptionEpoch = Epoch;
	Current.WorldTimestamp = State.WorldTimestamp;
	Signatures.Add(Key, MoveTemp(Current));
	return bRedundant;
}

void UIntentReplayObservationComponent::BuildComparisonIndexes(
	UIntentReplayObservationComparisonSession& Session)
{
	if (!Session.TimelineBundle || !Session.TimelineBundle->GetObservationTrack())
	{
		return;
	}
	const TArray<FIntentReplayRecordedObservation>& Entries =
		Session.TimelineBundle->GetObservationTrack()->GetEntries();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FIntentReplayRecordedObservation& Entry = Entries[Index];
		if (Entry.Type == EIntentReplayRecordedObservationType::State)
		{
			UIntentReplayObservationComparisonSession::FStateIndexKey Key{
				Entry.State.EntityId,
				Entry.State.StateTag,
				Entry.State.SenseTag };
			Session.StateIndex.FindOrAdd(Key).Add(Index);
			Session.StateTagIndex.Add(Entry.State.StateTag, Index);
		}
		else
		{
			UIntentReplayObservationComparisonSession::FEventIndexKey Key{
				Entry.Event.EventTag,
				Entry.Event.SourceEntityId,
				Entry.Event.SenseTag };
			Session.EventIndex.FindOrAdd(Key).Add(Index);
			Session.EventTagIndex.Add(Entry.Event.EventTag, Index);
		}
	}
}

void UIntentReplayObservationComponent::CompareObservation(
	const FPerceptionKnowledgeObservation& Observation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplayPerception_CompareObservation);
	if (!ActiveComparisonSession || !ActiveComparisonSession->Journal)
	{
		return;
	}

	FIntentReplayObservationCorrelation Correlation;
	ResolveObservationCorrelation(Observation, Correlation);
	if (!ActiveComparisonSession->bComparisonEnabled)
	{
		FIntentReplayObservationJournalEntry Entry;
		Entry.CurrentObservation = Observation;
		Entry.CurrentCorrelation = Correlation;
		Entry.Result = EIntentReplayObservationMatchResult::IgnoredByPolicy;
		Entry.Reason = EIntentReplayObservationMismatchReason::PolicyFiltered;
		Entry.CurrentRelativeTime =
			BoundIntentReplaySource->GetPlaybackClockSnapshot().RelativeTimeSeconds;
		AppendComparisonEntry(MoveTemp(Entry));
		return;
	}

	const FIntentReplayTimelineClockSnapshot Clock =
		BoundIntentReplaySource->GetPlaybackClockSnapshot();
	if (ActiveComparisonSession->State == EIntentReplayObservationComparisonState::Paused
		|| Clock.bPaused)
	{
		if (ActiveComparisonSession->Options.bJournalIgnoredWhilePaused)
		{
			FIntentReplayObservationJournalEntry Entry;
			Entry.CurrentObservation = Observation;
			Entry.CurrentCorrelation = Correlation;
			Entry.Result = EIntentReplayObservationMatchResult::IgnoredWhilePaused;
			Entry.Reason = EIntentReplayObservationMismatchReason::Paused;
			Entry.CurrentRelativeTime = Clock.RelativeTimeSeconds;
			AppendComparisonEntry(MoveTemp(Entry));
		}
		return;
	}

	const FIntentReplayTimelinePointResult TimelinePoint =
		BoundIntentReplaySource->CapturePlaybackTimelinePoint(
			Clock.PlaybackSessionId);
	if (!TimelinePoint.Succeeded())
	{
		FIntentReplayObservationJournalEntry Entry;
		Entry.CurrentObservation = Observation;
		Entry.CurrentCorrelation = Correlation;
		Entry.Result = EIntentReplayObservationMatchResult::ComparisonUnavailable;
		Entry.Reason = EIntentReplayObservationMismatchReason::UnsupportedComparison;
		Entry.CurrentRelativeTime = Clock.RelativeTimeSeconds;
		AppendComparisonEntry(MoveTemp(Entry));
		return;
	}

	if (Observation.Type == EPerceptionKnowledgeObservationType::Event)
	{
		if (ActiveComparisonSession->SeenCurrentEventIds.Contains(
			Observation.Event.ObservationId))
		{
			FIntentReplayObservationJournalEntry Entry;
			Entry.CurrentObservation = Observation;
			Entry.CurrentCorrelation = Correlation;
			Entry.Result = EIntentReplayObservationMatchResult::Duplicate;
			Entry.CurrentRelativeTime = TimelinePoint.Clock.RelativeTimeSeconds;
			AppendComparisonEntry(MoveTemp(Entry));
			++RuntimeStats.DuplicateObservations;
			return;
		}
		ActiveComparisonSession->SeenCurrentEventIds.Add(
			Observation.Event.ObservationId);
		AppendComparisonEntry(
			MatchEventObservation(Observation.Event, Correlation, TimelinePoint));
	}
	else
	{
		if (IsRedundantStateObservation(Observation.State, false))
		{
			FIntentReplayObservationJournalEntry Entry;
			Entry.CurrentObservation = Observation;
			Entry.CurrentCorrelation = Correlation;
			Entry.Result = EIntentReplayObservationMatchResult::IgnoredByPolicy;
			Entry.Reason = EIntentReplayObservationMismatchReason::PolicyFiltered;
			Entry.CurrentRelativeTime = TimelinePoint.Clock.RelativeTimeSeconds;
			AppendComparisonEntry(MoveTemp(Entry));
			return;
		}
		AppendComparisonEntry(
			MatchStateObservation(Observation.State, Correlation, TimelinePoint));
	}
}

FIntentReplayObservationJournalEntry
UIntentReplayObservationComponent::MatchStateObservation(
	const FPerceptionKnowledgeStateObservation& State,
	const FIntentReplayObservationCorrelation& Correlation,
	const FIntentReplayTimelinePointResult& TimelinePoint)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplayPerception_FindCandidates);
	FIntentReplayObservationJournalEntry Result;
	Result.CurrentObservation = FPerceptionKnowledgeObservation::FromState(State);
	Result.CurrentCorrelation = Correlation;
	Result.CurrentRelativeTime = TimelinePoint.Clock.RelativeTimeSeconds;
	if (!State.Key.EntityId.IsValid()
		|| !State.Key.StateTag.IsValid()
		|| !State.SenseTag.IsValid()
		|| (State.Status == EPerceptionKnowledgeFactStatus::Known
			&& !State.Value.IsValid()))
	{
		Result.Result = EIntentReplayObservationMatchResult::ComparisonUnavailable;
		Result.Reason = EIntentReplayObservationMismatchReason::InvalidCurrentObservation;
		return Result;
	}

	UIntentReplayObservationComparisonSession& Session = *ActiveComparisonSession;
	const TArray<FIntentReplayRecordedObservation>& Entries =
		Session.TimelineBundle->GetObservationTrack()->GetEntries();
	UIntentReplayObservationComparisonSession::FStateIndexKey Key{
		State.Key.EntityId, State.Key.StateTag, State.SenseTag };
	double Early = 0.0;
	double Late = 0.0;
	MatchPolicy->GetTimeWindow(
		EIntentReplayRecordedObservationType::State,
		State.SenseTag,
		Session.Options,
		Early,
		Late);
	const double MinimumTime = Result.CurrentRelativeTime - Early;
	const double MaximumTime = Result.CurrentRelativeTime + Late;
	TArray<int32> Candidates;
	bool bHadConsumedCandidate = false;

	auto TryAddCandidate = [&](const int32 Index, const bool bRequireTimeWindow)
	{
		const FIntentReplayRecordedObservation& Candidate = Entries[Index];
		if (Candidate.State.SenseTag != State.SenseTag
			|| (bRequireTimeWindow
				&& (Candidate.State.RelativeTimestamp < MinimumTime
					|| Candidate.State.RelativeTimestamp > MaximumTime)))
		{
			return;
		}
		if (Session.ConsumedObservationIds.Contains(
			Candidate.State.RecordedObservationId))
		{
			bHadConsumedCandidate = true;
			return;
		}
		if (!Session.ExpiredObservationIds.Contains(
			Candidate.State.RecordedObservationId))
		{
			Candidates.AddUnique(Index);
		}
	};

	if (Session.Options.bStrictPersistentIdentity)
	{
		if (const TArray<int32>* Indexed = Session.StateIndex.Find(Key))
		{
			const int32 First = FindFirstIndexAtOrAfter(
				*Indexed,
				MinimumTime,
				[&Entries](const int32 Index)
				{
					return Entries[Index].State.RelativeTimestamp;
				});
			for (int32 Position = First; Position < Indexed->Num(); ++Position)
			{
				const int32 Index = (*Indexed)[Position];
				if (Entries[Index].State.RelativeTimestamp > MaximumTime)
				{
					break;
				}
				TryAddCandidate(Index, true);
			}
		}
	}
	else
	{
		TArray<int32> SameTag;
		Session.StateTagIndex.MultiFind(State.Key.StateTag, SameTag);
		for (const int32 Index : SameTag)
		{
			TryAddCandidate(Index, true);
		}
	}

	const bool bUseOrderedPersistentState =
		Session.Options.bStrictPersistentIdentity
		&& Session.Options
			.bTreatPersistentStateObservationsAsOrderedSnapshots;
	if (bUseOrderedPersistentState)
	{
		if (const TArray<int32>* Indexed = Session.StateIndex.Find(Key))
		{
			for (const int32 Index : *Indexed)
			{
				TryAddCandidate(Index, false);
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason = bHadConsumedCandidate
			? EIntentReplayObservationMismatchReason::AllCandidatesAlreadyConsumed
			: EIntentReplayObservationMismatchReason::NoCandidateInTimeWindow;
		TArray<int32> SameTag;
		Session.StateTagIndex.MultiFind(State.Key.StateTag, SameTag);
		for (const int32 Index : SameTag)
		{
			const FIntentReplayRecordedStateObservation& Candidate = Entries[Index].State;
			if (FMath::Abs(Candidate.RelativeTimestamp - Result.CurrentRelativeTime)
				> FMath::Max(Early, Late))
			{
				continue;
			}
			if (Candidate.SenseTag != State.SenseTag)
			{
				Result.Reason = EIntentReplayObservationMismatchReason::SenseMismatch;
				break;
			}
			if (Candidate.EntityId != State.Key.EntityId)
			{
				Result.Reason = EIntentReplayObservationMismatchReason::EntityMismatch;
				break;
			}
		}
		return Result;
	}

	struct FStateCandidateScore
	{
		bool bOrderedPersistentSnapshot = false;
		int32 StatusPenalty = 0;
		int32 ValuePenalty = 0;
		int32 PositionPenalty = 0;
		int32 ConfidencePenalty = 0;
		double TimeDelta = 0.0;
		double PositionDelta = 0.0;
		double ConfidenceDelta = 0.0;
		int64 TimelineSequence = 0;
	};
	auto ScoreCandidate = [&](const int32 Index)
	{
		const FIntentReplayRecordedStateObservation& Candidate =
			Entries[Index].State;
		FStateCandidateScore Score;
		Score.bOrderedPersistentSnapshot = bUseOrderedPersistentState;
		Score.StatusPenalty = Candidate.Status == State.Status ? 0 : 1;
		Score.ValuePenalty =
			Candidate.Status == EPerceptionKnowledgeFactStatus::Known
				&& State.Status == EPerceptionKnowledgeFactStatus::Known
				&& !MatchPolicy->AreStateValuesEquivalent(
					Candidate.Value,
					State.Value,
					Session.Options)
			? 1
			: 0;
		Score.PositionDelta = Session.Options.bCompareStatePosition
			? FVector::Dist(
				Candidate.ObservationLocation,
				State.ObservationLocation)
			: 0.0;
		Score.PositionPenalty = Session.Options.bCompareStatePosition
				&& Score.PositionDelta > Session.Options.StateLocationTolerance
			? 1
			: 0;
		Score.ConfidenceDelta = Session.Options.bCompareStateConfidence
			? FMath::Abs(Candidate.Confidence - State.Confidence)
			: 0.0;
		Score.ConfidencePenalty = Session.Options.bCompareStateConfidence
				&& Score.ConfidenceDelta > Session.Options.StateConfidenceTolerance
			? 1
			: 0;
		Score.TimeDelta = FMath::Abs(
			Candidate.RelativeTimestamp - Result.CurrentRelativeTime);
		Score.TimelineSequence = Candidate.TimelineSequence;
		return Score;
	};
	auto IsScoreLess = [](const FStateCandidateScore& Left,
		const FStateCandidateScore& Right)
	{
		if (Left.bOrderedPersistentSnapshot
			&& Right.bOrderedPersistentSnapshot
			&& Left.TimelineSequence != Right.TimelineSequence)
		{
			return Left.TimelineSequence < Right.TimelineSequence;
		}
		if (Left.StatusPenalty != Right.StatusPenalty)
		{
			return Left.StatusPenalty < Right.StatusPenalty;
		}
		if (Left.ValuePenalty != Right.ValuePenalty)
		{
			return Left.ValuePenalty < Right.ValuePenalty;
		}
		if (Left.PositionPenalty != Right.PositionPenalty)
		{
			return Left.PositionPenalty < Right.PositionPenalty;
		}
		if (Left.ConfidencePenalty != Right.ConfidencePenalty)
		{
			return Left.ConfidencePenalty < Right.ConfidencePenalty;
		}
		if (!FMath::IsNearlyEqual(Left.TimeDelta, Right.TimeDelta))
		{
			return Left.TimeDelta < Right.TimeDelta;
		}
		if (!FMath::IsNearlyEqual(Left.PositionDelta, Right.PositionDelta))
		{
			return Left.PositionDelta < Right.PositionDelta;
		}
		if (!FMath::IsNearlyEqual(Left.ConfidenceDelta, Right.ConfidenceDelta))
		{
			return Left.ConfidenceDelta < Right.ConfidenceDelta;
		}
		return Left.TimelineSequence < Right.TimelineSequence;
	};
	auto AreScoresEquivalent = [](const FStateCandidateScore& Left,
		const FStateCandidateScore& Right)
	{
		return Left.StatusPenalty == Right.StatusPenalty
			&& Left.ValuePenalty == Right.ValuePenalty
			&& Left.PositionPenalty == Right.PositionPenalty
			&& Left.ConfidencePenalty == Right.ConfidencePenalty
			&& (!Left.bOrderedPersistentSnapshot
				|| !Right.bOrderedPersistentSnapshot
				|| Left.TimelineSequence == Right.TimelineSequence)
			&& FMath::IsNearlyEqual(Left.TimeDelta, Right.TimeDelta)
			&& FMath::IsNearlyEqual(Left.PositionDelta, Right.PositionDelta)
			&& FMath::IsNearlyEqual(Left.ConfidenceDelta, Right.ConfidenceDelta);
	};
	TMap<int32, FStateCandidateScore> CandidateScores;
	for (const int32 Candidate : Candidates)
	{
		CandidateScores.Add(Candidate, ScoreCandidate(Candidate));
	}
	Candidates.Sort(
		[&](const int32 Left, const int32 Right)
		{
			return IsScoreLess(
				CandidateScores.FindChecked(Left),
				CandidateScores.FindChecked(Right));
		});
	if (Candidates.Num() > 1
		&& Session.Options.bReportEquivalentBestCandidatesAsAmbiguous)
	{
		if (AreScoresEquivalent(
			CandidateScores.FindChecked(Candidates[0]),
			CandidateScores.FindChecked(Candidates[1])))
		{
			Result.Result = EIntentReplayObservationMatchResult::Ambiguous;
			Result.Reason = EIntentReplayObservationMismatchReason::AmbiguousBestCandidate;
			Result.bHasExpectedObservation = true;
			Result.ExpectedObservation = Entries[Candidates[0]];
			return Result;
		}
	}

	const FIntentReplayRecordedObservation& Expected = Entries[Candidates[0]];
	Result.bHasExpectedObservation = true;
	Result.ExpectedObservation = Expected;
	Result.TimeDelta =
		Result.CurrentRelativeTime - Expected.State.RelativeTimestamp;
	if (Expected.State.Status != State.Status)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedStateStatus;
		Result.Reason = EIntentReplayObservationMismatchReason::StateStatusMismatch;
	}
	else if (State.Status == EPerceptionKnowledgeFactStatus::Known
		&& !MatchPolicy->AreStateValuesEquivalent(
			Expected.State.Value,
			State.Value,
			Session.Options))
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedStateValue;
		Result.Reason = EIntentReplayObservationMismatchReason::StateValueMismatch;
	}
	else if (Session.Options.bCompareStatePosition
		&& FVector::Dist(
			Expected.State.ObservationLocation,
			State.ObservationLocation)
			> Session.Options.StateLocationTolerance)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason =
			EIntentReplayObservationMismatchReason::StatePositionOutsideTolerance;
	}
	else if (Session.Options.bCompareStateConfidence
		&& FMath::Abs(Expected.State.Confidence - State.Confidence)
			> Session.Options.StateConfidenceTolerance)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason =
			EIntentReplayObservationMismatchReason::ConfidenceOutsideTolerance;
	}
	else
	{
		Result.Result = EIntentReplayObservationMatchResult::Matched;
		Result.Reason = EIntentReplayObservationMismatchReason::None;
	}
	Session.ConsumedObservationIds.Add(Expected.State.RecordedObservationId);
	Result.bConsumedExpectedRecord = true;
	return Result;
}

FIntentReplayObservationJournalEntry
UIntentReplayObservationComponent::MatchEventObservation(
	const FPerceptionKnowledgeEventObservation& Event,
	const FIntentReplayObservationCorrelation& Correlation,
	const FIntentReplayTimelinePointResult& TimelinePoint)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplayPerception_FindCandidates);
	FIntentReplayObservationJournalEntry Result;
	Result.CurrentObservation = FPerceptionKnowledgeObservation::FromEvent(Event);
	Result.CurrentCorrelation = Correlation;
	Result.CurrentRelativeTime = TimelinePoint.Clock.RelativeTimeSeconds;
	if (!Event.ObservationId.IsValid()
		|| !Event.EventTag.IsValid()
		|| !Event.SenseTag.IsValid())
	{
		Result.Result = EIntentReplayObservationMatchResult::ComparisonUnavailable;
		Result.Reason = EIntentReplayObservationMismatchReason::InvalidCurrentObservation;
		return Result;
	}

	UIntentReplayObservationComparisonSession& Session = *ActiveComparisonSession;
	const TArray<FIntentReplayRecordedObservation>& Entries =
		Session.TimelineBundle->GetObservationTrack()->GetEntries();
	UIntentReplayObservationComparisonSession::FEventIndexKey Key{
		Event.EventTag, Event.SourceEntityId, Event.SenseTag };
	double Early = 0.0;
	double Late = 0.0;
	MatchPolicy->GetTimeWindow(
		EIntentReplayRecordedObservationType::Event,
		Event.SenseTag,
		Session.Options,
		Early,
		Late);
	const double MinimumTime = Result.CurrentRelativeTime - Early;
	const double MaximumTime = Result.CurrentRelativeTime + Late;
	TArray<int32> Candidates;
	bool bHadConsumedCandidate = false;

	auto TryAddCandidate = [&](const int32 Index, const bool bRequireTimeWindow)
	{
		const FIntentReplayRecordedObservation& Candidate = Entries[Index];
		if (Candidate.Event.SenseTag != Event.SenseTag
			|| (bRequireTimeWindow
				&& (Candidate.Event.RelativeTimestamp < MinimumTime
					|| Candidate.Event.RelativeTimestamp > MaximumTime)))
		{
			return;
		}
		if (Session.ConsumedObservationIds.Contains(
			Candidate.Event.RecordedObservationId))
		{
			bHadConsumedCandidate = true;
			return;
		}
		if (!Session.ExpiredObservationIds.Contains(
			Candidate.Event.RecordedObservationId))
		{
			Candidates.AddUnique(Index);
		}
	};

	if (Session.Options.bStrictPersistentIdentity)
	{
		if (const TArray<int32>* Indexed = Session.EventIndex.Find(Key))
		{
			const int32 First = FindFirstIndexAtOrAfter(
				*Indexed,
				MinimumTime,
				[&Entries](const int32 Index)
				{
					return Entries[Index].Event.RelativeTimestamp;
				});
			for (int32 Position = First; Position < Indexed->Num(); ++Position)
			{
				const int32 Index = (*Indexed)[Position];
				if (Entries[Index].Event.RelativeTimestamp > MaximumTime)
				{
					break;
				}
				TryAddCandidate(Index, true);
			}
		}
	}
	else
	{
		TArray<int32> SameTag;
		Session.EventTagIndex.MultiFind(Event.EventTag, SameTag);
		for (const int32 Index : SameTag)
		{
			TryAddCandidate(Index, true);
		}
	}

	const bool bUseVerifiedCausalOccurrence =
		Session.Options.bTreatVerifiedCausalEventsAsOccurrenceIdentity
		&& IsVerifiedReplayCorrelation(Correlation);
	if (bUseVerifiedCausalOccurrence)
	{
		if (const TArray<int32>* Indexed = Session.EventIndex.Find(Key))
		{
			for (const int32 Index : *Indexed)
			{
				if (IsSameVerifiedReplayOccurrence(
					Correlation,
					Entries[Index].Event.Correlation))
				{
					TryAddCandidate(Index, false);
				}
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason = bHadConsumedCandidate
			? EIntentReplayObservationMismatchReason::AllCandidatesAlreadyConsumed
			: EIntentReplayObservationMismatchReason::NoCandidateInTimeWindow;
		TArray<int32> SameTag;
		Session.EventTagIndex.MultiFind(Event.EventTag, SameTag);
		for (const int32 Index : SameTag)
		{
			const FIntentReplayRecordedEventObservation& Candidate = Entries[Index].Event;
			if (FMath::Abs(Candidate.RelativeTimestamp - Result.CurrentRelativeTime)
				> FMath::Max(Early, Late))
			{
				continue;
			}
			if (Candidate.SenseTag != Event.SenseTag)
			{
				Result.Reason = EIntentReplayObservationMismatchReason::SenseMismatch;
				break;
			}
			if (Candidate.SourceEntityId != Event.SourceEntityId)
			{
				Result.Reason = EIntentReplayObservationMismatchReason::SourceMismatch;
				break;
			}
		}
		return Result;
	}

	struct FEventCandidateScore
	{
		bool bVerifiedCausalOccurrence = false;
		int32 CorrelationPenalty = 0;
		int32 InstigatorPenalty = 0;
		int32 CausePenalty = 0;
		int32 LocationPenalty = 0;
		int32 StrengthPenalty = 0;
		int32 LoudnessPenalty = 0;
		double TimeDelta = 0.0;
		double LocationDelta = 0.0;
		double StrengthDelta = 0.0;
		double LoudnessDelta = 0.0;
		int64 TimelineSequence = 0;
	};
	auto ScoreCandidate = [&](const int32 Index)
	{
		const FIntentReplayRecordedEventObservation& Candidate =
			Entries[Index].Event;
		FEventCandidateScore Score;
		Score.bVerifiedCausalOccurrence =
			bUseVerifiedCausalOccurrence
			&& IsSameVerifiedReplayOccurrence(
				Correlation,
				Candidate.Correlation);
		const bool bHasCausalCorrelation =
			Correlation.CausalRecordedIntentId.IsValid()
			|| Candidate.Correlation.CausalRecordedIntentId.IsValid();
		const bool bHasExternalCorrelation =
			Correlation.ExternalCorrelationId.IsValid()
			|| Candidate.Correlation.ExternalCorrelationId.IsValid();
		Score.CorrelationPenalty =
			(bHasCausalCorrelation
				&& Correlation.CausalRecordedIntentId
					!= Candidate.Correlation.CausalRecordedIntentId)
				|| (bHasExternalCorrelation
					&& Correlation.ExternalCorrelationId
						!= Candidate.Correlation.ExternalCorrelationId)
			? 1
			: 0;
		Score.InstigatorPenalty =
			Session.Options.bCompareEventInstigatorWhenBothValid
				&& Candidate.InstigatorEntityId.IsValid()
				&& Event.InstigatorEntityId.IsValid()
				&& Candidate.InstigatorEntityId != Event.InstigatorEntityId
			? 1
			: 0;
		Score.CausePenalty = Candidate.CauseTag == Event.CauseTag ? 0 : 1;
		const bool bCompareLocation =
			Event.SenseTag == PerceptionKnowledgeTags::Sense_Hearing
			|| Session.Options.bCompareNonHearingEventLocation;
		Score.LocationDelta = bCompareLocation
				&& !Score.bVerifiedCausalOccurrence
			? FVector::Dist(Candidate.WorldLocation, Event.WorldLocation)
			: 0.0;
		const double LocationTolerance =
			Event.SenseTag == PerceptionKnowledgeTags::Sense_Hearing
			? Session.Options.HearingLocationTolerance
			: Session.Options.EventLocationTolerance;
		Score.LocationPenalty =
			bCompareLocation
				&& !Score.bVerifiedCausalOccurrence
				&& Score.LocationDelta > LocationTolerance
			? 1
			: 0;
		Score.StrengthDelta = Score.bVerifiedCausalOccurrence
			? 0.0
			: FMath::Abs(Candidate.Strength - Event.Strength);
		Score.StrengthPenalty =
			Score.StrengthDelta > Session.Options.StrengthTolerance ? 1 : 0;
		Score.LoudnessDelta = Score.bVerifiedCausalOccurrence
			? 0.0
			: FMath::Abs(Candidate.Loudness - Event.Loudness);
		Score.LoudnessPenalty =
			Score.LoudnessDelta > Session.Options.LoudnessTolerance ? 1 : 0;
		Score.TimeDelta = Score.bVerifiedCausalOccurrence
			? 0.0
			: FMath::Abs(
				Candidate.RelativeTimestamp - Result.CurrentRelativeTime);
		Score.TimelineSequence = Candidate.TimelineSequence;
		return Score;
	};
	auto IsScoreLess = [](const FEventCandidateScore& Left,
		const FEventCandidateScore& Right)
	{
		if (Left.CorrelationPenalty != Right.CorrelationPenalty)
		{
			return Left.CorrelationPenalty < Right.CorrelationPenalty;
		}
		if (Left.InstigatorPenalty != Right.InstigatorPenalty)
		{
			return Left.InstigatorPenalty < Right.InstigatorPenalty;
		}
		if (Left.CausePenalty != Right.CausePenalty)
		{
			return Left.CausePenalty < Right.CausePenalty;
		}
		if (Left.bVerifiedCausalOccurrence
			&& Right.bVerifiedCausalOccurrence
			&& Left.TimelineSequence != Right.TimelineSequence)
		{
			return Left.TimelineSequence < Right.TimelineSequence;
		}
		if (Left.LocationPenalty != Right.LocationPenalty)
		{
			return Left.LocationPenalty < Right.LocationPenalty;
		}
		if (Left.StrengthPenalty != Right.StrengthPenalty)
		{
			return Left.StrengthPenalty < Right.StrengthPenalty;
		}
		if (Left.LoudnessPenalty != Right.LoudnessPenalty)
		{
			return Left.LoudnessPenalty < Right.LoudnessPenalty;
		}
		if (!FMath::IsNearlyEqual(Left.TimeDelta, Right.TimeDelta))
		{
			return Left.TimeDelta < Right.TimeDelta;
		}
		if (!FMath::IsNearlyEqual(Left.LocationDelta, Right.LocationDelta))
		{
			return Left.LocationDelta < Right.LocationDelta;
		}
		if (!FMath::IsNearlyEqual(Left.StrengthDelta, Right.StrengthDelta))
		{
			return Left.StrengthDelta < Right.StrengthDelta;
		}
		if (!FMath::IsNearlyEqual(Left.LoudnessDelta, Right.LoudnessDelta))
		{
			return Left.LoudnessDelta < Right.LoudnessDelta;
		}
		return Left.TimelineSequence < Right.TimelineSequence;
	};
	auto AreScoresEquivalent = [](const FEventCandidateScore& Left,
		const FEventCandidateScore& Right)
	{
		return Left.CorrelationPenalty == Right.CorrelationPenalty
			&& Left.InstigatorPenalty == Right.InstigatorPenalty
			&& Left.CausePenalty == Right.CausePenalty
			&& Left.LocationPenalty == Right.LocationPenalty
			&& Left.StrengthPenalty == Right.StrengthPenalty
			&& Left.LoudnessPenalty == Right.LoudnessPenalty
			&& (!Left.bVerifiedCausalOccurrence
				|| !Right.bVerifiedCausalOccurrence
				|| Left.TimelineSequence == Right.TimelineSequence)
			&& FMath::IsNearlyEqual(Left.TimeDelta, Right.TimeDelta)
			&& FMath::IsNearlyEqual(Left.LocationDelta, Right.LocationDelta)
			&& FMath::IsNearlyEqual(Left.StrengthDelta, Right.StrengthDelta)
			&& FMath::IsNearlyEqual(Left.LoudnessDelta, Right.LoudnessDelta);
	};
	TMap<int32, FEventCandidateScore> CandidateScores;
	for (const int32 Candidate : Candidates)
	{
		CandidateScores.Add(Candidate, ScoreCandidate(Candidate));
	}
	Candidates.Sort(
		[&](const int32 Left, const int32 Right)
		{
			return IsScoreLess(
				CandidateScores.FindChecked(Left),
				CandidateScores.FindChecked(Right));
		});
	if (Candidates.Num() > 1
		&& Session.Options.bReportEquivalentBestCandidatesAsAmbiguous)
	{
		if (AreScoresEquivalent(
			CandidateScores.FindChecked(Candidates[0]),
			CandidateScores.FindChecked(Candidates[1])))
		{
			Result.Result = EIntentReplayObservationMatchResult::Ambiguous;
			Result.Reason = EIntentReplayObservationMismatchReason::AmbiguousBestCandidate;
			Result.bHasExpectedObservation = true;
			Result.ExpectedObservation = Entries[Candidates[0]];
			return Result;
		}
	}

	const FIntentReplayRecordedObservation& Expected = Entries[Candidates[0]];
	Result.bHasExpectedObservation = true;
	Result.ExpectedObservation = Expected;
	Result.TimeDelta =
		Result.CurrentRelativeTime - Expected.Event.RelativeTimestamp;
	Result.Result = EIntentReplayObservationMatchResult::Matched;
	Result.Reason = EIntentReplayObservationMismatchReason::None;
	const FEventCandidateScore& SelectedScore =
		CandidateScores.FindChecked(Candidates[0]);
	if (SelectedScore.CorrelationPenalty != 0)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason =
			EIntentReplayObservationMismatchReason::CausalCorrelationMismatch;
	}
	else if (Session.Options.bCompareEventInstigatorWhenBothValid
		&& Expected.Event.InstigatorEntityId.IsValid()
		&& Event.InstigatorEntityId.IsValid()
		&& Expected.Event.InstigatorEntityId != Event.InstigatorEntityId)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason = EIntentReplayObservationMismatchReason::InstigatorMismatch;
	}
	else if (Expected.Event.CauseTag != Event.CauseTag)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason = EIntentReplayObservationMismatchReason::CauseTagMismatch;
	}
	else if (!SelectedScore.bVerifiedCausalOccurrence
		&& SelectedScore.LocationPenalty != 0)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason = EIntentReplayObservationMismatchReason::LocationOutsideTolerance;
	}
	else if (!SelectedScore.bVerifiedCausalOccurrence
		&& FMath::Abs(Expected.Event.Strength - Event.Strength)
		> Session.Options.StrengthTolerance)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason = EIntentReplayObservationMismatchReason::StrengthOutsideTolerance;
	}
	else if (!SelectedScore.bVerifiedCausalOccurrence
		&& FMath::Abs(Expected.Event.Loudness - Event.Loudness)
		> Session.Options.LoudnessTolerance)
	{
		Result.Result = EIntentReplayObservationMatchResult::UnexpectedObservation;
		Result.Reason = EIntentReplayObservationMismatchReason::LoudnessOutsideTolerance;
	}
	Session.ConsumedObservationIds.Add(Expected.Event.RecordedObservationId);
	Result.bConsumedExpectedRecord = true;
	return Result;
}

void UIntentReplayObservationComponent::AppendComparisonEntry(
	FIntentReplayObservationJournalEntry&& Entry)
{
	if (!ActiveComparisonSession || !ActiveComparisonSession->Journal)
	{
		return;
	}
	Entry.JournalEntryId = FIntentReplayObservationJournalEntryId::NewId();
	Entry.JournalSequence = ActiveComparisonSession->NextJournalSequence++;
	const EIntentReplayObservationMatchResult Result = Entry.Result;
	FIntentReplayObservationComparisonEvent Event;
	Event.ComparisonSessionId = ActiveComparisonSession->SessionId;
	Event.PlaybackSessionId =
		ActiveComparisonSession->Journal->GetPlaybackSessionId();
	Event.ObservationTrackId =
		ActiveComparisonSession->TimelineBundle->GetObservationTrack()
			->GetObservationTrackId();
	Event.JournalId = ActiveComparisonSession->Journal->GetJournalId();
	Event.Entry = Entry;
	ActiveComparisonSession->Journal->Append(MoveTemp(Entry));
	ActiveComparisonSession->Journal->SetPendingExpected(
		GetPendingExpectedObservations().Num());
	++RuntimeStats.ComparedObservations;

	ObservationComparedNative.Broadcast(Event);
	OnObservationCompared.Broadcast(Event);
	if (Result == EIntentReplayObservationMatchResult::Matched)
	{
		++RuntimeStats.MatchedObservations;
		ObservationMatchedNative.Broadcast(Event);
		OnObservationMatched.Broadcast(Event);
	}
	else if (IsUnexpected(Result))
	{
		++RuntimeStats.UnexpectedObservations;
		ObservationUnexpectedNative.Broadcast(Event);
		OnObservationUnexpected.Broadcast(Event);
	}
	else if (Result == EIntentReplayObservationMatchResult::Ambiguous)
	{
		++RuntimeStats.AmbiguousObservations;
		ObservationAmbiguousNative.Broadcast(Event);
		OnObservationAmbiguous.Broadcast(Event);
	}
	else if (Result
		== EIntentReplayObservationMatchResult::ExpectedRecordExpiredUnobserved)
	{
		++RuntimeStats.ExpiredExpectedObservations;
	}
}

void UIntentReplayObservationComponent::SetComparisonState(
	const EIntentReplayObservationComparisonState NewState)
{
	if (ActiveComparisonSession && ActiveComparisonSession->State != NewState)
	{
		ActiveComparisonSession->State = NewState;
	}
}

void UIntentReplayObservationComponent::CompleteComparison(
	const EIntentReplayObservationComparisonState TerminalState)
{
	if (!ActiveComparisonSession || IsComparisonTerminal(ActiveComparisonSession->State))
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpectedExpirationTimerHandle);
	}
	if (TerminalState == EIntentReplayObservationComparisonState::Completed)
	{
		ExpireAllPendingExpected();
	}
	else
	{
		ProcessExpectedExpirations();
	}
	ActiveComparisonSession->State = TerminalState;
	ActiveComparisonSession->Journal->SetPendingExpected(
		GetPendingExpectedObservations().Num());
	ActiveComparisonSession->Journal->MarkTerminal(
		TerminalState,
		BoundIntentReplaySource->GetPlaybackClockSnapshot().RelativeTimeSeconds);
	const FIntentReplayObservationComparisonSummary Summary =
		ActiveComparisonSession->Journal->GetSummary();
	ObservationJournalCompletedNative.Broadcast(
		ActiveComparisonSession->Journal,
		Summary);
	OnObservationJournalCompleted.Broadcast(
		ActiveComparisonSession->Journal,
		Summary);
}

void UIntentReplayObservationComponent::ExpireAllPendingExpected()
{
	if (!ActiveComparisonSession)
	{
		return;
	}
	const double CurrentTime =
		BoundIntentReplaySource->GetPlaybackClockSnapshot().RelativeTimeSeconds;
	TArray<FIntentReplayRecordedObservation> NewlyExpired;
	for (const FIntentReplayRecordedObservation& Entry :
		ActiveComparisonSession->TimelineBundle->GetObservationTrack()->GetEntries())
	{
		const FRecordedObservationId Id = Entry.GetRecordedObservationId();
		if (!ActiveComparisonSession->ConsumedObservationIds.Contains(Id)
			&& !ActiveComparisonSession->ExpiredObservationIds.Contains(Id))
		{
			ActiveComparisonSession->ExpiredObservationIds.Add(Id);
			NewlyExpired.Add(Entry);
		}
	}
	for (const FIntentReplayRecordedObservation& Entry : NewlyExpired)
	{
		FIntentReplayObservationJournalEntry JournalEntry;
		JournalEntry.bHasExpectedObservation = true;
		JournalEntry.ExpectedObservation = Entry;
		JournalEntry.Result =
			EIntentReplayObservationMatchResult::ExpectedRecordExpiredUnobserved;
		JournalEntry.Reason =
			EIntentReplayObservationMismatchReason::NoCandidateInTimeWindow;
		JournalEntry.CurrentRelativeTime = CurrentTime;
		JournalEntry.TimeDelta =
			CurrentTime - Entry.GetRelativeTimestamp();
		AppendComparisonEntry(MoveTemp(JournalEntry));
	}
}

void UIntentReplayObservationComponent::ScheduleExpectedExpiration()
{
	if (!ActiveComparisonSession
		|| ActiveComparisonSession->State
			!= EIntentReplayObservationComparisonState::Comparing
		|| !GetWorld())
	{
		return;
	}
	const FIntentReplayTimelineClockSnapshot Clock =
		BoundIntentReplaySource->GetPlaybackClockSnapshot();
	double NextExpiration = TNumericLimits<double>::Max();
	for (const FIntentReplayRecordedObservation& Entry :
		ActiveComparisonSession->TimelineBundle->GetObservationTrack()->GetEntries())
	{
		const FRecordedObservationId Id = Entry.GetRecordedObservationId();
		if (ActiveComparisonSession->ConsumedObservationIds.Contains(Id)
			|| ActiveComparisonSession->ExpiredObservationIds.Contains(Id))
		{
			continue;
		}
		if (ShouldDeferExpectedExpiration(
			Entry,
			ActiveComparisonSession->Options))
		{
			continue;
		}
		double Early = 0.0;
		double Late = 0.0;
		const FGameplayTag Sense = Entry.Type == EIntentReplayRecordedObservationType::State
			? Entry.State.SenseTag
			: Entry.Event.SenseTag;
		MatchPolicy->GetTimeWindow(
			Entry.Type,
			Sense,
			ActiveComparisonSession->Options,
			Early,
			Late);
		NextExpiration = FMath::Min(
			NextExpiration,
			Entry.GetRelativeTimestamp() + Late);
	}
	if (NextExpiration == TNumericLimits<double>::Max())
	{
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(
		ExpectedExpirationTimerHandle,
		this,
		&UIntentReplayObservationComponent::ProcessExpectedExpirations,
		static_cast<float>(FMath::Max(0.01, NextExpiration - Clock.RelativeTimeSeconds)),
		false);
}

void UIntentReplayObservationComponent::ProcessExpectedExpirations()
{
	if (!ActiveComparisonSession
		|| ActiveComparisonSession->State
			!= EIntentReplayObservationComparisonState::Comparing)
	{
		return;
	}
	const double CurrentTime =
		BoundIntentReplaySource->GetPlaybackClockSnapshot().RelativeTimeSeconds;
	TArray<FIntentReplayRecordedObservation> NewlyExpired;
	for (const FIntentReplayRecordedObservation& Entry :
		ActiveComparisonSession->TimelineBundle->GetObservationTrack()->GetEntries())
	{
		const FRecordedObservationId Id = Entry.GetRecordedObservationId();
		if (ActiveComparisonSession->ConsumedObservationIds.Contains(Id)
			|| ActiveComparisonSession->ExpiredObservationIds.Contains(Id))
		{
			continue;
		}
		if (ShouldDeferExpectedExpiration(
			Entry,
			ActiveComparisonSession->Options))
		{
			continue;
		}
		double Early = 0.0;
		double Late = 0.0;
		MatchPolicy->GetTimeWindow(
			Entry.Type,
			Entry.Type == EIntentReplayRecordedObservationType::State
				? Entry.State.SenseTag
				: Entry.Event.SenseTag,
			ActiveComparisonSession->Options,
			Early,
			Late);
		if (CurrentTime > Entry.GetRelativeTimestamp() + Late)
		{
			ActiveComparisonSession->ExpiredObservationIds.Add(Id);
			NewlyExpired.Add(Entry);
		}
	}
	for (const FIntentReplayRecordedObservation& Entry : NewlyExpired)
	{
		FIntentReplayObservationJournalEntry JournalEntry;
		JournalEntry.bHasExpectedObservation = true;
		JournalEntry.ExpectedObservation = Entry;
		JournalEntry.Result =
			EIntentReplayObservationMatchResult::ExpectedRecordExpiredUnobserved;
		JournalEntry.Reason =
			EIntentReplayObservationMismatchReason::NoCandidateInTimeWindow;
		JournalEntry.CurrentRelativeTime = CurrentTime;
		JournalEntry.TimeDelta =
			CurrentTime - Entry.GetRelativeTimestamp();
		AppendComparisonEntry(MoveTemp(JournalEntry));
	}
	ScheduleExpectedExpiration();
}

FPerceptionKnowledgeEntityId
UIntentReplayObservationComponent::ResolveObserverEntityId() const
{
	if (!BoundPerceptionListener || !GetWorld())
	{
		return FPerceptionKnowledgeEntityId();
	}
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		GetWorld()->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>();
	if (!Subsystem)
	{
		return FPerceptionKnowledgeEntityId();
	}
	if (AActor* Body = BoundPerceptionListener->GetResolvedBodyActor())
	{
		const FPerceptionKnowledgeEntityId BodyId = Subsystem->ResolveEntityId(Body);
		if (BodyId.IsValid())
		{
			return BodyId;
		}
	}
	return Subsystem->ResolveEntityId(BoundPerceptionListener->GetOwner());
}

FColor UIntentReplayObservationComponent::GetDebugColor(
	const EIntentReplayObservationDebugStatus Status) const
{
	const UIntentReplayPerceptionDeveloperSettings* Settings =
		GetDefault<UIntentReplayPerceptionDeveloperSettings>();
	switch (Status)
	{
	case EIntentReplayObservationDebugStatus::Matched:
		return Settings->MatchedColor;
	case EIntentReplayObservationDebugStatus::Unexpected:
		return Settings->UnexpectedColor;
	case EIntentReplayObservationDebugStatus::Ambiguous:
		return Settings->AmbiguousColor;
	case EIntentReplayObservationDebugStatus::Justified:
		return Settings->JustifiedColor;
	case EIntentReplayObservationDebugStatus::Pending:
		return Settings->PendingColor;
	case EIntentReplayObservationDebugStatus::Consumed:
		return Settings->ConsumedColor;
	default:
		return Settings->InactiveColor;
	}
}

void UIntentReplayObservationComponent::UpdateDebugResources()
{
	if (!GetWorld())
	{
		return;
	}
	if (!bEnableDebug)
	{
		GetWorld()->GetTimerManager().ClearTimer(DebugTimerHandle);
		if (DebugCanvasHandle.IsValid())
		{
			UDebugDrawService::Unregister(DebugCanvasHandle);
			DebugCanvasHandle.Reset();
		}
		return;
	}
	const float Interval =
		GetDefault<UIntentReplayPerceptionDeveloperSettings>()->DebugDrawInterval;
	GetWorld()->GetTimerManager().SetTimer(
		DebugTimerHandle,
		this,
		&UIntentReplayObservationComponent::DrawDebugTimer,
		FMath::Max(0.05f, Interval),
		true);
	if (!DebugCanvasHandle.IsValid())
	{
		DebugCanvasHandle = UDebugDrawService::Register(
			TEXT("Game"),
			FDebugDrawDelegate::CreateUObject(
				this,
				&UIntentReplayObservationComponent::DrawTimelineHud));
	}
}

void UIntentReplayObservationComponent::DrawDebugTimer()
{
	if (!bEnableDebug || !IsIntentReplayPerceptionDebugEnabled() || !GetWorld())
	{
		return;
	}
	CachedDebugFrame = BuildDebugFrame();
	if (!CachedDebugFrame.bShouldDraw || !CachedDebugFrame.bExpensiveDataBuilt)
	{
		return;
	}
	const float Padding =
		GetDefault<UIntentReplayPerceptionDeveloperSettings>()
			->ComparativeBoundsPadding;
	for (const FIntentReplayObservationDebugEntityFrame& Entity :
		CachedDebugFrame.Entities)
	{
		if (!Entity.BoundsExtent.IsNearlyZero())
		{
			DrawDebugBox(
				GetWorld(),
				Entity.BoundsOrigin,
				Entity.BoundsExtent + FVector(Padding),
				Entity.Color,
				false,
				0.0f,
				0,
				2.0f);
		}
		if (CachedDebugFrame.bHasViewpoint)
		{
			DrawDebugLine(
				GetWorld(),
				CachedDebugFrame.ViewLocation,
				Entity.BoundsOrigin,
				Entity.Color,
				false,
				0.0f,
				0,
				1.5f);
		}
		if (DebugFilter.TextDetailLevel > 0)
		{
			DrawDebugString(
				GetWorld(),
				Entity.BoundsOrigin + FVector(0.0, 0.0, Entity.BoundsExtent.Z + 20.0),
				Entity.Label,
				nullptr,
				Entity.Color,
				0.0f,
				true);
		}
	}
	for (const FIntentReplayObservationJournalEntry& Entry :
		CachedDebugFrame.RecentEventEntries)
	{
		if (Entry.CurrentObservation.Type != EPerceptionKnowledgeObservationType::Event)
		{
			continue;
		}
		const FColor Color = IsUnexpected(Entry.Result)
			? GetDebugColor(EIntentReplayObservationDebugStatus::Unexpected)
			: GetDebugColor(Entry.Result == EIntentReplayObservationMatchResult::Ambiguous
				? EIntentReplayObservationDebugStatus::Ambiguous
				: EIntentReplayObservationDebugStatus::Matched);
		if (Entry.CurrentObservation.Event.SenseTag
			== PerceptionKnowledgeTags::Sense_Hearing)
		{
			DrawDebugSphere(
				GetWorld(),
				Entry.CurrentObservation.Event.WorldLocation,
				20.0f,
				12,
				Color,
				false,
				0.0f,
				0,
				1.5f);
			DrawDebugSphere(
				GetWorld(),
				Entry.CurrentObservation.Event.WorldLocation,
				static_cast<float>(CachedDebugFrame.HearingLocationTolerance),
				16,
				Color,
				false,
				0.0f,
				0,
				0.5f);
			if (Entry.bHasExpectedObservation
				&& Entry.ExpectedObservation.Type
					== EIntentReplayRecordedObservationType::Event
				&& Entry.ExpectedObservation.Event.SenseTag
					== PerceptionKnowledgeTags::Sense_Hearing)
			{
				DrawDebugSphere(
					GetWorld(),
					Entry.ExpectedObservation.Event.WorldLocation,
					static_cast<float>(CachedDebugFrame.HearingLocationTolerance),
					16,
					Color,
					false,
					0.0f,
					0,
					0.75f);
			}
		}
		else
		{
			DrawDebugPoint(
				GetWorld(),
				Entry.CurrentObservation.Event.WorldLocation,
				12.0f,
				Color,
				false,
				0.0f,
				0);
		}
		if (CachedDebugFrame.bHasViewpoint)
		{
			DrawDebugLine(
				GetWorld(),
				CachedDebugFrame.ViewLocation,
				Entry.CurrentObservation.Event.WorldLocation,
				Color,
				false,
				0.0f,
				0,
				1.0f);
		}
	}
}

void UIntentReplayObservationComponent::DrawTimelineHud(
	UCanvas* Canvas,
	APlayerController* PlayerController)
{
	if (!Canvas
		|| !bEnableDebug
		|| !IsIntentReplayPerceptionDebugEnabled()
		|| !CachedDebugFrame.bShouldDraw
		|| !GEngine)
	{
		return;
	}
	const FString Text = FString::Printf(
		TEXT("IntentReplayPerception\nAction %s  Observation %s\nPlayback %s  Journal %s\n"
			"Time %.3f  State %s\nMatched %d  Unexpected %d  Ambiguous %d  Pending %d"),
		*CachedDebugFrame.ActionTrackId.ToString().Left(8),
		*CachedDebugFrame.ObservationTrackId.ToString().Left(8),
		*CachedDebugFrame.Clock.PlaybackSessionId.ToString().Left(8),
		*CachedDebugFrame.JournalId.ToString().Left(8),
		CachedDebugFrame.Clock.RelativeTimeSeconds,
		*UEnum::GetValueAsString(CachedDebugFrame.ComparisonState),
		CachedDebugFrame.Summary.Matched,
		CachedDebugFrame.Summary.Unexpected,
		CachedDebugFrame.Summary.Ambiguous,
		CachedDebugFrame.Summary.PendingExpected);
	Canvas->SetDrawColor(FColor::White);
	Canvas->DrawText(GEngine->GetSmallFont(), Text, 32.0f, 96.0f);
}

#if WITH_EDITOR
EDataValidationResult UIntentReplayObservationComponent::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!RecordPolicyClass)
	{
		Context.AddError(FText::FromString(TEXT("Record Policy Class is required.")));
		Result = EDataValidationResult::Invalid;
	}
	if (!MatchPolicyClass)
	{
		Context.AddError(FText::FromString(TEXT("Match Policy Class is required.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
