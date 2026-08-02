#include "Components/IntentReplayComponent.h"

#include "Actions/GameplayActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameplayActionTags.h"
#include "IntentReplayModule.h"
#include "IntentReplayTags.h"
#include "Journal/IntentExecutionJournal.h"
#include "Playback/IntentReplayExecutionStrategy.h"
#include "Playback/IntentReplayPlaybackSession.h"
#include "Policies/IntentRecordabilityPolicy.h"
#include "Policies/IntentReplayTimeSource.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Recording/IntentRecordingSession.h"
#include "Recording/IntentReplayTrack.h"
#include "StructUtils/PropertyBag.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
	bool IsRecordingTerminal(const EIntentRecordingState State)
	{
		return State == EIntentRecordingState::Finalized
			|| State == EIntentRecordingState::Failed
			|| State == EIntentRecordingState::Cancelled;
	}

	bool IsPlaybackTerminal(const EIntentReplayPlaybackState State)
	{
		return State == EIntentReplayPlaybackState::Completed
			|| State == EIntentReplayPlaybackState::Failed
			|| State == EIntentReplayPlaybackState::Cancelled;
	}

	/** Accept the hidden pre-rename value so already serialized Blueprint nodes keep their behavior. */
	bool IsAsyncRecordingStop(const EIntentRecordingFinalizeMode FinalizeMode)
	{
		return FinalizeMode == EIntentRecordingFinalizeMode::AsyncStop
			|| FinalizeMode == EIntentRecordingFinalizeMode::DrainTrackedActions;
	}

	bool IsSuccessfulTerminalAction(const FGameplayActionResult& Result)
	{
		return Result.TerminalState == EGameplayActionState::Succeeded;
	}

	bool PropertyDescTypeEquals(const FPropertyBagPropertyDesc& Left, const FPropertyBagPropertyDesc& Right)
	{
		return Left.ValueType == Right.ValueType
			&& Left.ContainerTypes == Right.ContainerTypes
			&& Left.ValueTypeObject == Right.ValueTypeObject
			&& Left.KeyType == Right.KeyType
			&& Left.KeyTypeObject == Right.KeyTypeObject;
	}

	bool CopyRecordedBagValues(
		const FInstancedPropertyBag& Source,
		FGameplayActionRequest& Target,
		const EIntentReplayCompatibilityPolicy Policy,
		FIntentReplayCompatibilityReport& OutReport,
		FString& OutDiagnostic)
	{
		const UPropertyBag* SourceStruct = Source.GetPropertyBagStruct();
		const FInstancedPropertyBag& TargetBag = Target.GetParameters();
		const UPropertyBag* TargetStruct = TargetBag.GetPropertyBagStruct();
		if (!SourceStruct || !TargetStruct || !Source.GetValue().IsValid())
		{
			OutDiagnostic = TEXT("Recorded or current Definition Property Bag is invalid.");
			return false;
		}

		const TConstArrayView<FPropertyBagPropertyDesc> SourceDescs = SourceStruct->GetPropertyDescs();
		const TConstArrayView<FPropertyBagPropertyDesc> TargetDescs = TargetStruct->GetPropertyDescs();
		if (Policy == EIntentReplayCompatibilityPolicy::StrictRecordedSchema && SourceDescs.Num() != TargetDescs.Num())
		{
			OutDiagnostic = TEXT("Strict replay compatibility requires identical Property Bag field counts.");
			OutReport.bCompatible = false;
			return false;
		}

		for (const FPropertyBagPropertyDesc& TargetDesc : TargetDescs)
		{
			if (!SourceStruct->FindPropertyDescByName(TargetDesc.Name))
			{
				OutReport.AddedCurrentParameters.Add(TargetDesc.Name.ToString());
			}
		}

		const void* SourceMemory = Source.GetValue().GetMemory();
		for (const FPropertyBagPropertyDesc& SourceDesc : SourceDescs)
		{
			const FPropertyBagPropertyDesc* TargetDesc = TargetStruct->FindPropertyDescByName(SourceDesc.Name);
			if (!TargetDesc)
			{
				OutReport.RemovedRecordedParameters.Add(SourceDesc.Name.ToString());
				if (Policy == EIntentReplayCompatibilityPolicy::StrictRecordedSchema)
				{
					OutReport.bCompatible = false;
					OutDiagnostic = FString::Printf(
						TEXT("Strict replay compatibility could not find recorded parameter '%s'."),
						*SourceDesc.Name.ToString());
					return false;
				}
				continue;
			}
			if (!PropertyDescTypeEquals(SourceDesc, *TargetDesc)
				|| !SourceDesc.CachedProperty
				|| !TargetDesc->CachedProperty
				|| !TargetDesc->CachedProperty->SameType(SourceDesc.CachedProperty))
			{
				OutReport.TypeChangedParameters.Add(SourceDesc.Name.ToString());
				if (Policy == EIntentReplayCompatibilityPolicy::StrictRecordedSchema)
				{
					OutReport.bCompatible = false;
					OutDiagnostic = FString::Printf(
						TEXT("Strict replay compatibility found a type mismatch for '%s'."),
						*SourceDesc.Name.ToString());
					return false;
				}
				continue;
			}

			const void* SourceValue = SourceDesc.CachedProperty->ContainerPtrToValuePtr<void>(SourceMemory);
			const EGameplayActionParameterAccessResult AccessResult =
				UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
					Target,
					SourceDesc.Name,
					SourceDesc.CachedProperty,
					SourceValue);
			if (AccessResult != EGameplayActionParameterAccessResult::Success)
			{
				OutDiagnostic = FString::Printf(
					TEXT("Failed to copy recorded parameter '%s' into the replay request."),
					*SourceDesc.Name.ToString());
				OutReport.bCompatible = false;
				return false;
			}
		}

		if (Policy == EIntentReplayCompatibilityPolicy::StrictRecordedSchema
			&& (!OutReport.AddedCurrentParameters.IsEmpty()
				|| !OutReport.RemovedRecordedParameters.IsEmpty()
				|| !OutReport.TypeChangedParameters.IsEmpty()))
		{
			OutReport.bCompatible = false;
			OutDiagnostic = TEXT("Strict replay compatibility detected a Property Bag schema change.");
			return false;
		}

		return true;
	}
}

UIntentReplayComponent::UIntentReplayComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	TimeSourceClass = UIntentReplayWorldTimeSource::StaticClass();
	RecordabilityPolicyClass = UIntentRecordabilityPolicy::StaticClass();
	ExecutionStrategyClass = UIntentReplayDirectExecutionStrategy::StaticClass();
	ReplayOriginTag = IntentReplayTags::Origin_Replay;

	FGameplayTagContainer ExcludedOrigins;
	ExcludedOrigins.AddTag(IntentReplayTags::Origin_Replay);
	TrackEligibilityQuery = FGameplayTagQuery::MakeQuery_MatchNoTags(ExcludedOrigins);
}

FIntentReplayOperationResult UIntentReplayComponent::InitializeIntentReplay()
{
	if (bInitialized)
	{
		FIntentReplayOperationResult Result;
		Result.Status = EIntentReplayOperationStatus::Succeeded;
		return Result;
	}
	if (bShuttingDown)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("Intent Replay is shutting down."));
	}

	UGameplayActionComponent* Resolved = ActionComponentOverride;
	if (!Resolved && GetOwner())
	{
		Resolved = GetOwner()->FindComponentByClass<UGameplayActionComponent>();
	}
	if (!Resolved)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::MissingActionComponent,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("No Gameplay Action Component is assigned or available on the owning actor."));
	}

	TimeSource = NewObject<UIntentReplayTimeSource>(
		this,
		TimeSourceClass ? TimeSourceClass.Get() : UIntentReplayWorldTimeSource::StaticClass(),
		NAME_None,
		RF_Transient);
	RecordabilityPolicy = NewObject<UIntentRecordabilityPolicy>(
		this,
		RecordabilityPolicyClass ? RecordabilityPolicyClass.Get() : UIntentRecordabilityPolicy::StaticClass(),
		NAME_None,
		RF_Transient);
	ExecutionStrategy = NewObject<UIntentReplayExecutionStrategy>(
		this,
		ExecutionStrategyClass ? ExecutionStrategyClass.Get() : UIntentReplayDirectExecutionStrategy::StaticClass(),
		NAME_None,
		RF_Transient);
	if (!TimeSource || !RecordabilityPolicy || !ExecutionStrategy)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InternalFailure,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("Failed to create one or more Intent Replay policy objects."));
	}

	AmbientExecutionJournal = NewObject<UIntentExecutionJournal>(this, NAME_None, RF_Transient);
	FIntentExecutionJournalOptions AmbientOptions;
	AmbientExecutionJournal->Initialize(AmbientOptions, GetCurrentTimeSeconds());

	FIntentReplayOperationResult BindResult = BindResolvedActionComponent(Resolved);
	if (!BindResult.Succeeded())
	{
		return BindResult;
	}

	bInitialized = true;
	RecordDiagnostic(FString::Printf(
		TEXT("Initialized and registered on %s."),
		*GetNameSafe(BoundActionComponent)));
	INTENTREPLAY_LOG_INFO(
		TEXT("%s initialized for owner %s with action component %s."),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(BoundActionComponent));

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::SetActionComponent(
	UGameplayActionComponent* InActionComponent)
{
	if (bProcessingJournalEvent)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::RejectedReentrant,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("Action Component binding cannot change from inside journal processing."));
	}
	if (HasNonTerminalRecording() || HasNonTerminalPlayback())
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("Action Component binding cannot change while recording or playback is active."));
	}
	if (!IsValid(InActionComponent))
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidArgument,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("The requested Gameplay Action Component is invalid."));
	}

	ActionComponentOverride = InActionComponent;
	if (!bInitialized)
	{
		return InitializeIntentReplay();
	}

	UnbindActionComponent();
	return BindResolvedActionComponent(InActionComponent);
}

FIntentRecordingStartResult UIntentReplayComponent::StartRecording(
	const FIntentRecordingOptions& Options)
{
	FIntentRecordingStartResult Result;
	if (bProcessingJournalEvent)
	{
		Result.Status = EIntentReplayOperationStatus::RejectedReentrant;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_NotInitialized,
			TEXT("Recording cannot start from inside journal processing."));
		return Result;
	}
	if (!bInitialized)
	{
		const FIntentReplayOperationResult Initialization = InitializeIntentReplay();
		if (!Initialization.Succeeded())
		{
			Result.Status = Initialization.Status;
			Result.Failure = Initialization.Failure;
			return Result;
		}
	}
	if (HasNonTerminalRecording())
	{
		Result.Status = EIntentReplayOperationStatus::RecordingAlreadyActive;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_NoRecordingSession,
			TEXT("A non-terminal recording session is already active."));
		return Result;
	}

	UIntentRecordingSession* Session = NewObject<UIntentRecordingSession>(this, NAME_None, RF_Transient);
	Session->TrackId = FIntentReplayTrackId::NewId();
	Session->SessionId = FIntentRecordingSessionId::NewId();
	Session->Options = Options;
	Session->StartTimeSeconds = GetCurrentTimeSeconds();
	Session->ExecutionJournal = NewObject<UIntentExecutionJournal>(Session, NAME_None, RF_Transient);
	Session->ExecutionJournal->Initialize(Options.JournalOptions, Session->StartTimeSeconds);
	ActiveRecordingSession = Session;
	SetRecordingState(*Session, EIntentRecordingState::Recording);

	Result.Status = EIntentReplayOperationStatus::Succeeded;
	Result.TrackId = Session->TrackId;
	Result.SessionId = Session->SessionId;
	OnRecordingStarted.Broadcast(Session->TrackId);
	RecordDiagnostic(FString::Printf(TEXT("Started recording track %s."), *Session->TrackId.ToString()));
	INTENTREPLAY_LOG_INFO(
		TEXT("%s started recording track %s for owner %s."),
		*GetNameSafe(this),
		*Session->TrackId.ToString(),
		*GetNameSafe(GetOwner()));
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::RequestStopRecording(
	const EIntentRecordingFinalizeMode FinalizeMode)
{
	if (bProcessingJournalEvent)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::RejectedReentrant,
			IntentReplayTags::Failure_NoRecordingSession,
			TEXT("Recording cannot stop from inside journal processing."));
	}
	if (!ActiveRecordingSession
		|| (ActiveRecordingSession->State != EIntentRecordingState::Recording
			&& ActiveRecordingSession->State != EIntentRecordingState::Paused))
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_NoRecordingSession,
			TEXT("No recording session is accepting a stop request."));
	}
	if (FinalizeMode != EIntentRecordingFinalizeMode::Immediate && !IsAsyncRecordingStop(FinalizeMode))
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidArgument,
			IntentReplayTags::Failure_NoRecordingSession,
			TEXT("The requested recording finalization mode is invalid."));
	}

	UIntentRecordingSession& Session = *ActiveRecordingSession;
	// Closing a paused clock first freezes one authoritative duration for either stop path.
	if (Session.bClockPaused)
	{
		Session.AccumulatedPausedSeconds += FMath::Max(
			0.0,
			GetCurrentTimeSeconds() - Session.PauseStartTimeSeconds);
		Session.bClockPaused = false;
	}
	Session.FinalRecordedDurationSeconds = GetRecordingElapsedSeconds(Session);

	// Immediate is synchronous by contract. AsyncStop may still complete synchronously when no
	// tracked action is outstanding, so callers should always treat OnRecordingFinalized as valid.
	if (FinalizeMode == EIntentRecordingFinalizeMode::Immediate
		|| Session.PendingTrackedHandles.IsEmpty())
	{
		FinalizeRecordingSession(Session);
	}
	else
	{
		// Draining is event-driven: lifecycle callbacks remove handles and finalize the session.
		// No polling, Tick, or blocking wait is introduced on the Game Thread.
		SetRecordingState(Session, EIntentRecordingState::Draining);
		RecordDiagnostic(FString::Printf(
			TEXT("Recording track %s is draining %d actions."),
			*Session.TrackId.ToString(),
			Session.PendingTrackedHandles.Num()));
	}

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::CancelRecording()
{
	if (bProcessingJournalEvent)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::RejectedReentrant,
			IntentReplayTags::Failure_NoRecordingSession,
			TEXT("Recording cannot be cancelled from inside journal processing."));
	}
	if (!ActiveRecordingSession || IsRecordingTerminal(ActiveRecordingSession->State))
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_NoRecordingSession,
			TEXT("No non-terminal recording session is active."));
	}

	UIntentRecordingSession* Session = ActiveRecordingSession;
	Session->FinalRecordedDurationSeconds = GetRecordingElapsedSeconds(*Session);
	SetRecordingState(*Session, EIntentRecordingState::Cancelled);
	LastRecordingSession = Session;
	ActiveRecordingSession = nullptr;
	RecordDiagnostic(FString::Printf(TEXT("Cancelled recording track %s."), *Session->TrackId.ToString()));

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::PauseRecording()
{
	if (!ActiveRecordingSession || ActiveRecordingSession->State != EIntentRecordingState::Recording)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_NoRecordingSession,
			TEXT("Only an actively recording session can be paused."));
	}

	UIntentRecordingSession& Session = *ActiveRecordingSession;
	Session.PauseStartTimeSeconds = GetCurrentTimeSeconds();
	Session.bClockPaused = true;
	SetRecordingState(Session, EIntentRecordingState::Paused);

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::ResumeRecording()
{
	if (!ActiveRecordingSession || ActiveRecordingSession->State != EIntentRecordingState::Paused)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_NoRecordingSession,
			TEXT("Only a paused recording session can be resumed."));
	}

	UIntentRecordingSession& Session = *ActiveRecordingSession;
	Session.AccumulatedPausedSeconds += FMath::Max(
		0.0,
		GetCurrentTimeSeconds() - Session.PauseStartTimeSeconds);
	Session.bClockPaused = false;
	SetRecordingState(Session, EIntentRecordingState::Recording);

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

EIntentRecordingState UIntentReplayComponent::GetRecordingState() const
{
	if (ActiveRecordingSession)
	{
		return ActiveRecordingSession->State;
	}
	return LastRecordingSession ? LastRecordingSession->State : EIntentRecordingState::Created;
}

FIntentReplayPrepareResult UIntentReplayComponent::PrepareReplay(
	UIntentReplayTrack* Track,
	const FIntentReplayPlaybackOptions& Options)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplay_PreparePlayback);
	FIntentReplayPrepareResult Result;
	if (bProcessingJournalEvent)
	{
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("Replay cannot be prepared from inside journal processing."));
		return Result;
	}
	if (!bInitialized)
	{
		const FIntentReplayOperationResult Initialization = InitializeIntentReplay();
		if (!Initialization.Succeeded())
		{
			Result.Failure = Initialization.Failure;
			return Result;
		}
	}
	if (!IsValid(Track))
	{
		Result.Failure = MakeFailure(IntentReplayTags::Failure_InvalidTrack, TEXT("The source replay track is invalid."));
		return Result;
	}
	if (HasNonTerminalPlayback())
	{
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("A non-terminal playback session is already active."));
		return Result;
	}

	const FIntentReplayTrackValidationResult Validation = Track->ValidateTrack();
	if (!Validation.bValid)
	{
		Result.Failure = MakeFailure(IntentReplayTags::Failure_InvalidTrack, Validation.DiagnosticMessage);
		return Result;
	}

	UIntentReplayPlaybackSession* Session =
		NewObject<UIntentReplayPlaybackSession>(this, NAME_None, RF_Transient);
	Session->SessionId = FIntentReplayPlaybackSessionId::NewId();
	Session->SourceTrack = Track;
	Session->Options = Options;
	Session->ExecutionJournal = NewObject<UIntentExecutionJournal>(Session, NAME_None, RF_Transient);
	Session->ExecutionJournal->Initialize(Options.JournalOptions, GetCurrentTimeSeconds());
	ActivePlaybackSession = Session;
	SetPlaybackState(*Session, EIntentReplayPlaybackState::Preparing);
	Result.SessionId = Session->SessionId;

	TArray<FSoftObjectPath> PathsToLoad;
	for (const FRecordedIntent& Entry : Track->GetEntries())
	{
		if (ResolveDefinition(Entry))
		{
			continue;
		}

		FSoftObjectPath Path = Entry.Definition.ToSoftObjectPath();
		if (!Path.IsValid() && Entry.DefinitionId.IsValid() && UAssetManager::IsInitialized())
		{
			Path = UAssetManager::Get().GetPrimaryAssetPath(Entry.DefinitionId);
		}
		if (!Path.IsValid())
		{
			const FIntentReplayFailure Failure = MakeFailure(
				IntentReplayTags::Failure_DefinitionUnavailable,
				FString::Printf(
					TEXT("Definition for recorded intent %s has no resolvable asset path."),
					*Entry.RecordedIntentId.ToString()),
				Entry.RecordedIntentId);
			FailReplay(*Session, Failure);
			Result.Failure = Failure;
			return Result;
		}
		PathsToLoad.AddUnique(Path);
	}

	if (PathsToLoad.IsEmpty())
	{
		FIntentReplayFailure Failure;
		if (!BuildPreparedReplayRequests(*Session, Failure))
		{
			FailReplay(*Session, Failure);
			Result.Failure = Failure;
			return Result;
		}
		Result.Status = EIntentReplayPrepareStatus::Ready;
		return Result;
	}

	PendingDefinitionLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		PathsToLoad,
		FStreamableDelegate::CreateUObject(
			this,
			&UIntentReplayComponent::HandleReplayAssetsLoaded,
			Session->SessionId),
		FStreamableManager::AsyncLoadHighPriority,
		false,
		false,
		TEXT("IntentReplay_PrepareDefinitions"));
	if (!PendingDefinitionLoadHandle.IsValid())
	{
		const FIntentReplayFailure Failure = MakeFailure(
			IntentReplayTags::Failure_DefinitionUnavailable,
			TEXT("Failed to create the asynchronous Definition load request."));
		FailReplay(*Session, Failure);
		Result.Failure = Failure;
		return Result;
	}

	Result.Status = EIntentReplayPrepareStatus::Preparing;
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::StartReplay()
{
	if (!ActivePlaybackSession || ActivePlaybackSession->State != EIntentReplayPlaybackState::Ready)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("Replay can start only from the Ready state."));
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	Session.StartTimeSeconds = GetCurrentTimeSeconds();
	Session.AccumulatedPausedSeconds = 0.0;
	Session.FinalElapsedSeconds = 0.0;
	Session.NextTimelineSequence = 0;
	Session.bClockStarted = true;
	Session.NextEntryIndex = 0;
	Session.ProcessedEntryCount = 0;
	Session.bAllEntriesSubmittedBroadcast = false;
	SetPlaybackState(Session, EIntentReplayPlaybackState::Playing);
	OnReplayStarted.Broadcast(Session.SessionId);
	RecordDiagnostic(FString::Printf(
		TEXT("Started replay session %s for track %s."),
		*Session.SessionId.ToString(),
		*Session.SourceTrack->GetTrackId().ToString()));
	ScheduleNextReplayEntry();

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::PauseReplay()
{
	if (!ActivePlaybackSession || ActivePlaybackSession->State != EIntentReplayPlaybackState::Playing)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("Only a playing replay session can be paused."));
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	ClearReplayScheduling();
	Session.PauseStartTimeSeconds = GetCurrentTimeSeconds();
	Session.bClockPaused = true;
	if (Session.Options.bPauseBoundActions
		&& BoundActionComponent
		&& !BoundActionComponent->IsActionsPaused()
		&& BoundActionComponent->PauseActions() == EGameplayActionOperationResult::Succeeded)
	{
		Session.bPausedBoundActionsBySession = true;
	}
	SetPlaybackState(Session, EIntentReplayPlaybackState::Paused);

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::ResumeReplay()
{
	if (!ActivePlaybackSession || ActivePlaybackSession->State != EIntentReplayPlaybackState::Paused)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("Only a paused replay session can be resumed."));
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	if (!Session.PendingExternalRecoveryByIntent.IsEmpty())
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::PendingExternalRecovery,
			IntentReplayTags::Failure_PendingExternalRecovery,
			FString::Printf(
				TEXT("Replay cannot resume while %d externally interrupted intent(s) remain unreconciled."),
				Session.PendingExternalRecoveryByIntent.Num()));
	}
	Session.AccumulatedPausedSeconds += FMath::Max(
		0.0,
		GetCurrentTimeSeconds() - Session.PauseStartTimeSeconds);
	Session.bClockPaused = false;
	if (Session.bPausedBoundActionsBySession && BoundActionComponent)
	{
		BoundActionComponent->ResumeActions();
		Session.bPausedBoundActionsBySession = false;
	}
	SetPlaybackState(Session, EIntentReplayPlaybackState::Playing);
	ScheduleNextReplayEntry();

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

FIntentReplayExternalInterruptionResult
UIntentReplayComponent::BeginExternalReplayInterruption(const FGameplayTag InterruptionReason)
{
	FIntentReplayExternalInterruptionResult Result;
	if (!ActivePlaybackSession || ActivePlaybackSession->State != EIntentReplayPlaybackState::Playing)
	{
		Result.Status = EIntentReplayOperationStatus::InvalidState;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("External replay interruption can begin only from the Playing state."));
		return Result;
	}
	if (!BoundActionComponent)
	{
		Result.Status = EIntentReplayOperationStatus::MissingActionComponent;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_NotInitialized,
			TEXT("External replay interruption requires a bound GameplayActionComponent."));
		return Result;
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	Result.SessionId = Session.SessionId;
	if (!Session.PendingExternalRecoveryByIntent.IsEmpty())
	{
		Result.Status = EIntentReplayOperationStatus::PendingExternalRecovery;
		Result.SuspendedIntents = Session.GetPendingExternalRecoveryIntents();
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_PendingExternalRecovery,
			TEXT("This playback session already has externally interrupted intents awaiting recovery."));
		return Result;
	}

	struct FPendingInterruption
	{
		FGameplayActionHandle Handle;
		FRecordedIntent RecordedIntent;
		EGameplayActionState RuntimeState = EGameplayActionState::Created;
	};
	TArray<FPendingInterruption> PendingInterruptions;
	PendingInterruptions.Reserve(Session.ActiveReplayHandles.Num());
	for (const FGameplayActionHandle Handle : Session.ActiveReplayHandles)
	{
		const FRecordedIntentId* RecordedIntentId = Session.RecordByRuntimeHandle.Find(Handle);
		const FRecordedIntent* RecordedIntent = RecordedIntentId && Session.SourceTrack
			? Session.SourceTrack->GetEntries().FindByPredicate(
				[RecordedIntentId](const FRecordedIntent& Entry)
				{
					return Entry.RecordedIntentId == *RecordedIntentId;
				})
			: nullptr;
		EGameplayActionState RuntimeState = EGameplayActionState::Created;
		if (!RecordedIntent || !BoundActionComponent->GetActionState(Handle, RuntimeState))
		{
			Result.Status = EIntentReplayOperationStatus::InterruptionFailure;
			Result.Failure = MakeFailure(
				IntentReplayTags::Failure_ExternalInterruption,
				FString::Printf(
					TEXT("Replay-owned action %lld could not be resolved before external interruption."),
					Handle.GetValue()),
				RecordedIntentId ? *RecordedIntentId : FRecordedIntentId());
			return Result;
		}
		FPendingInterruption& Pending = PendingInterruptions.AddDefaulted_GetRef();
		Pending.Handle = Handle;
		Pending.RecordedIntent = *RecordedIntent;
		Pending.RuntimeState = RuntimeState;
	}
	PendingInterruptions.Sort(
		[](const FPendingInterruption& Left, const FPendingInterruption& Right)
		{
			return Left.RecordedIntent.TrackSequence < Right.RecordedIntent.TrackSequence;
		});

	const FIntentReplayOperationResult PauseResult = PauseReplay();
	if (!PauseResult.Succeeded())
	{
		Result.Status = PauseResult.Status;
		Result.Failure = PauseResult.Failure;
		return Result;
	}

	const FGameplayTag EffectiveReason = InterruptionReason.IsValid()
		? InterruptionReason
		: GameplayActionTags::Result_Interrupted_External;
	for (const FPendingInterruption& Pending : PendingInterruptions)
	{
		FIntentReplaySuspendedIntent Snapshot;
		Snapshot.RecordedIntent = Pending.RecordedIntent;
		Snapshot.InterruptedRuntimeHandle = Pending.Handle;
		Snapshot.InterruptedRuntimeState = Pending.RuntimeState;
		Session.PendingExternalRecoveryByIntent.Add(
			Pending.RecordedIntent.RecordedIntentId,
			Snapshot);
		Session.ExpectedExternalInterruptionReasons.Add(Pending.Handle, EffectiveReason);

		const EGameplayActionOperationResult InterruptionResult =
			BoundActionComponent->InterruptAction(Pending.Handle, InterruptionReason);
		if (InterruptionResult != EGameplayActionOperationResult::Succeeded)
		{
			Session.ExpectedExternalInterruptionReasons.Remove(Pending.Handle);
			Session.PendingExternalRecoveryByIntent.Remove(Pending.RecordedIntent.RecordedIntentId);
			Result.Status = EIntentReplayOperationStatus::InterruptionFailure;
			Result.SuspendedIntents = Session.GetPendingExternalRecoveryIntents();
			Result.Failure = MakeFailure(
				IntentReplayTags::Failure_ExternalInterruption,
				FString::Printf(
					TEXT("GameplayActions rejected external interruption of replay-owned action %lld (%s)."),
					Pending.Handle.GetValue(),
					*UEnum::GetValueAsString(InterruptionResult)),
				Pending.RecordedIntent.RecordedIntentId);
			return Result;
		}
	}

	Result.Status = EIntentReplayOperationStatus::Succeeded;
	Result.SuspendedIntents = Session.GetPendingExternalRecoveryIntents();
	RecordDiagnostic(FString::Printf(
		TEXT("Paused replay session %s with %d externally interrupted intent(s)."),
		*Session.SessionId.ToString(),
		Result.SuspendedIntents.Num()));
	return Result;
}

FIntentReplayRecoveryResult UIntentReplayComponent::ReissueExternallyInterruptedIntent(
	const FRecordedIntentId RecordedIntentId)
{
	FIntentReplayRecoveryResult Result;
	Result.RecordedIntentId = RecordedIntentId;
	if (!ActivePlaybackSession || ActivePlaybackSession->State != EIntentReplayPlaybackState::Paused)
	{
		Result.Status = EIntentReplayOperationStatus::InvalidState;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("An externally interrupted intent can be reissued only while replay is Paused."),
			RecordedIntentId);
		return Result;
	}
	if (!BoundActionComponent || !ExecutionStrategy)
	{
		Result.Status = EIntentReplayOperationStatus::MissingActionComponent;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_NotInitialized,
			TEXT("Intent recovery requires initialized GameplayActions and execution strategy."),
			RecordedIntentId);
		return Result;
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	const FIntentReplaySuspendedIntent* Snapshot =
		Session.PendingExternalRecoveryByIntent.Find(RecordedIntentId);
	if (!Snapshot)
	{
		Result.Status = EIntentReplayOperationStatus::RecordedIntentNotFound;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_RecordedIntentNotFound,
			TEXT("The requested Recorded Intent is not awaiting external recovery."),
			RecordedIntentId);
		return Result;
	}
	const int32 EntryIndex = Snapshot->RecordedIntent.TrackSequence;
	if (!Session.PreparedRequests.IsValidIndex(EntryIndex))
	{
		Result.Status = EIntentReplayOperationStatus::InternalFailure;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_Compatibility,
			TEXT("The suspended intent no longer maps to a prepared replay request."),
			RecordedIntentId);
		return Result;
	}

	const FIntentReplaySuspendedIntent SnapshotCopy = *Snapshot;
	Session.PendingExternalRecoveryByIntent.Remove(RecordedIntentId);
	Result.SubmissionResult = ExecutionStrategy->SubmitPreparedRequest(
		BoundActionComponent,
		Session.PreparedRequests[EntryIndex]);
	if (!Result.SubmissionResult.IsAccepted())
	{
		Session.PendingExternalRecoveryByIntent.Add(RecordedIntentId, SnapshotCopy);
		Result.Status = EIntentReplayOperationStatus::SubmissionFailure;
		Result.Failure = MakeFailure(
			IntentReplayTags::Failure_SubmissionRejected,
			Result.SubmissionResult.DiagnosticMessage,
			RecordedIntentId);
		OnRecordedIntentSubmissionFailed.Broadcast(RecordedIntentId, Result.SubmissionResult);

		FIntentExecutionEvent FailureEvent;
		FailureEvent.ObservedRelativeTimeSeconds = FMath::Max(
			0.0,
			GetCurrentTimeSeconds() - Session.ExecutionJournal->GetStartTimeSeconds());
		FailureEvent.TrackId = Session.SourceTrack
			? Session.SourceTrack->GetTrackId()
			: FIntentReplayTrackId();
		FailureEvent.RecordedIntentId = RecordedIntentId;
		FailureEvent.PlaybackSessionId = Session.SessionId;
		FailureEvent.DiagnosticMessage = Result.SubmissionResult.DiagnosticMessage;
		Session.ExecutionJournal->Append(MoveTemp(FailureEvent));
		return Result;
	}

	Session.RecordByRuntimeHandle.Add(Result.SubmissionResult.Handle, RecordedIntentId);
	FGameplayActionResult ExistingResult;
	if (!BoundActionComponent->GetActionResult(Result.SubmissionResult.Handle, ExistingResult))
	{
		Session.ActiveReplayHandles.Add(Result.SubmissionResult.Handle);
	}
	OnRecordedIntentSubmitted.Broadcast(RecordedIntentId, Result.SubmissionResult);
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

FIntentReplayOperationResult UIntentReplayComponent::ResolveExternallyInterruptedIntentAsSatisfied(
	const FRecordedIntentId RecordedIntentId)
{
	if (!ActivePlaybackSession || ActivePlaybackSession->State != EIntentReplayPlaybackState::Paused)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("An externally interrupted intent can be resolved only while replay is Paused."),
			RecordedIntentId);
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	if (Session.PendingExternalRecoveryByIntent.Remove(RecordedIntentId) == 0)
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::RecordedIntentNotFound,
			IntentReplayTags::Failure_RecordedIntentNotFound,
			TEXT("The requested Recorded Intent is not awaiting external recovery."),
			RecordedIntentId);
	}

	FIntentExecutionEvent ResolutionEvent;
	ResolutionEvent.ObservedRelativeTimeSeconds = FMath::Max(
		0.0,
		GetCurrentTimeSeconds() - Session.ExecutionJournal->GetStartTimeSeconds());
	ResolutionEvent.TrackId = Session.SourceTrack
		? Session.SourceTrack->GetTrackId()
		: FIntentReplayTrackId();
	ResolutionEvent.RecordedIntentId = RecordedIntentId;
	ResolutionEvent.PlaybackSessionId = Session.SessionId;
	ResolutionEvent.DiagnosticMessage =
		TEXT("Externally interrupted intent resolved as already satisfied without reissue.");
	Session.ExecutionJournal->Append(MoveTemp(ResolutionEvent));

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

bool UIntentReplayComponent::HasPendingExternalReplayRecovery() const
{
	return ActivePlaybackSession
		&& !ActivePlaybackSession->PendingExternalRecoveryByIntent.IsEmpty();
}

FIntentReplayOperationResult UIntentReplayComponent::StopReplay()
{
	if (!ActivePlaybackSession || IsPlaybackTerminal(ActivePlaybackSession->State))
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidState,
			IntentReplayTags::Failure_InvalidTrack,
			TEXT("No non-terminal replay session is active."));
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	Session.FinalElapsedSeconds = GetPlaybackElapsedSeconds(Session);
	bStoppingPlayback = true;
	SetPlaybackState(Session, EIntentReplayPlaybackState::Stopping);
	ClearReplayScheduling();
	if (Session.bPausedBoundActionsBySession && BoundActionComponent)
	{
		BoundActionComponent->ResumeActions();
		Session.bPausedBoundActionsBySession = false;
	}
	CancelReplayOwnedActions(Session);
	Session.PendingExternalRecoveryByIntent.Reset();
	Session.ExpectedExternalInterruptionReasons.Reset();
	bStoppingPlayback = false;
	SetPlaybackState(Session, EIntentReplayPlaybackState::Cancelled);

	FIntentReplayResult ReplayResult;
	ReplayResult.Status = EIntentReplayTerminalStatus::Cancelled;
	ReplayResult.SessionId = Session.SessionId;
	ReplayResult.TrackId = Session.SourceTrack ? Session.SourceTrack->GetTrackId() : FIntentReplayTrackId();
	ReplayResult.ProcessedEntries = Session.ProcessedEntryCount;
	ReplayResult.TotalEntries = Session.SourceTrack ? Session.SourceTrack->GetEntryCount() : 0;
	ReplayResult.Failure = MakeFailure(
		IntentReplayTags::Cancelled_PlaybackStopped,
		TEXT("Replay was stopped by the requester."));
	OnReplayStopped.Broadcast(ReplayResult);
	RecordDiagnostic(FString::Printf(TEXT("Stopped replay session %s."), *Session.SessionId.ToString()));

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

EIntentReplayPlaybackState UIntentReplayComponent::GetPlaybackState() const
{
	return ActivePlaybackSession
		? ActivePlaybackSession->State
		: EIntentReplayPlaybackState::Created;
}

FIntentReplayTimelineClockSnapshot UIntentReplayComponent::GetRecordingClockSnapshot() const
{
	if (ActiveRecordingSession)
	{
		return BuildRecordingClockSnapshot(*ActiveRecordingSession);
	}
	return LastRecordingSession
		? BuildRecordingClockSnapshot(*LastRecordingSession)
		: FIntentReplayTimelineClockSnapshot();
}

FIntentReplayTimelineClockSnapshot UIntentReplayComponent::GetPlaybackClockSnapshot() const
{
	return ActivePlaybackSession
		? BuildPlaybackClockSnapshot(*ActivePlaybackSession)
		: FIntentReplayTimelineClockSnapshot();
}

FIntentReplayTimelinePointResult UIntentReplayComponent::CaptureRecordingTimelinePoint(
	const FIntentRecordingSessionId ExpectedSessionId)
{
	if (!IsInGameThread())
	{
		FIntentReplayTimelinePointResult Result;
		Result.Status = EIntentReplayTimelineCaptureStatus::WrongThread;
		return Result;
	}
	if (bShuttingDown)
	{
		FIntentReplayTimelinePointResult Result;
		Result.Status = EIntentReplayTimelineCaptureStatus::ShuttingDown;
		return Result;
	}
	if (!ActiveRecordingSession)
	{
		FIntentReplayTimelinePointResult Result;
		Result.Status = EIntentReplayTimelineCaptureStatus::NoActiveSession;
		return Result;
	}
	if (ActiveRecordingSession->SessionId != ExpectedSessionId)
	{
		FIntentReplayTimelinePointResult Result;
		Result.Status = EIntentReplayTimelineCaptureStatus::SessionMismatch;
		Result.Clock = BuildRecordingClockSnapshot(*ActiveRecordingSession);
		return Result;
	}
	return CaptureRecordingTimelinePointInternal(*ActiveRecordingSession);
}

FIntentReplayTimelinePointResult UIntentReplayComponent::CapturePlaybackTimelinePoint(
	const FIntentReplayPlaybackSessionId ExpectedSessionId)
{
	FIntentReplayTimelinePointResult Result;
	if (!IsInGameThread())
	{
		Result.Status = EIntentReplayTimelineCaptureStatus::WrongThread;
		return Result;
	}
	if (bShuttingDown)
	{
		Result.Status = EIntentReplayTimelineCaptureStatus::ShuttingDown;
		return Result;
	}
	if (!ActivePlaybackSession)
	{
		Result.Status = EIntentReplayTimelineCaptureStatus::NoActiveSession;
		return Result;
	}
	Result.Clock = BuildPlaybackClockSnapshot(*ActivePlaybackSession);
	if (ActivePlaybackSession->SessionId != ExpectedSessionId)
	{
		Result.Status = EIntentReplayTimelineCaptureStatus::SessionMismatch;
		return Result;
	}
	if (ActivePlaybackSession->State == EIntentReplayPlaybackState::Paused)
	{
		Result.Status = EIntentReplayTimelineCaptureStatus::Paused;
		return Result;
	}
	if (ActivePlaybackSession->State != EIntentReplayPlaybackState::Playing)
	{
		Result.Status = EIntentReplayTimelineCaptureStatus::NotAccepting;
		return Result;
	}
	Result.Status = EIntentReplayTimelineCaptureStatus::Succeeded;
	Result.TimelineSequence = ActivePlaybackSession->NextTimelineSequence++;
	Result.Clock = BuildPlaybackClockSnapshot(*ActivePlaybackSession);
	return Result;
}

FIntentReplayDebugSnapshot UIntentReplayComponent::GetDebugSnapshot() const
{
	FIntentReplayDebugSnapshot Snapshot;
	Snapshot.Owner = GetOwner();
	Snapshot.BoundActionComponent = BoundActionComponent;
	Snapshot.bInitialized = bInitialized;
	Snapshot.bJournalRegistered = bJournalRegistered;
	Snapshot.NoRecordingSessionPolicy = NoRecordingSessionPolicy;
	Snapshot.LastDiagnostic = LastDiagnostic;
	if (ActiveRecordingSession)
	{
		Snapshot.RecordingState = ActiveRecordingSession->State;
		Snapshot.RecordingTrackId = ActiveRecordingSession->TrackId;
		Snapshot.RecordedEntryCount = ActiveRecordingSession->MutableEntries.Num();
		Snapshot.PendingDrainCount = ActiveRecordingSession->PendingTrackedHandles.Num();
		Snapshot.RecordingElapsedSeconds = GetRecordingElapsedSeconds(*ActiveRecordingSession);
	}
	else if (LastRecordingSession)
	{
		Snapshot.RecordingState = LastRecordingSession->State;
		Snapshot.RecordingTrackId = LastRecordingSession->TrackId;
		Snapshot.RecordedEntryCount = LastRecordingSession->MutableEntries.Num();
	}
	if (ActivePlaybackSession)
	{
		Snapshot.PlaybackState = ActivePlaybackSession->State;
		Snapshot.PlaybackSessionId = ActivePlaybackSession->SessionId;
		Snapshot.SourceTrackId = ActivePlaybackSession->SourceTrack
			? ActivePlaybackSession->SourceTrack->GetTrackId()
			: FIntentReplayTrackId();
		Snapshot.NextEntryIndex = ActivePlaybackSession->NextEntryIndex;
		Snapshot.SubmittedEntryCount = ActivePlaybackSession->ProcessedEntryCount;
		Snapshot.TotalEntryCount = ActivePlaybackSession->SourceTrack
			? ActivePlaybackSession->SourceTrack->GetEntryCount()
			: 0;
		Snapshot.ReplayOwnedActionCount = ActivePlaybackSession->ActiveReplayHandles.Num();
	}
	return Snapshot;
}

FGameplayActionJournalResult UIntentReplayComponent::WriteGameplayActionEvent_Implementation(
	const FGameplayActionEvent& Event)
{
	// GameplayActions invokes this synchronously inside its Accepted transaction. Returning Rejected
	// rolls back the action submission, so this path must remain deterministic and non-reentrant.
	if (!IsInGameThread() || bShuttingDown || !bInitialized)
	{
		FGameplayActionJournalResult Result;
		Result.Status = EGameplayActionJournalWriteStatus::Rejected;
		Result.ReasonTag = IntentReplayTags::Failure_NotInitialized;
		Result.DiagnosticMessage = TEXT("Intent Replay is not initialized on the Game Thread.");
		return Result;
	}

	// Public mutating APIs consult this guard instead of allowing nested submit/cancel operations to
	// invalidate handles and maps while the journal transaction is still being evaluated.
	TGuardValue<bool> JournalGuard(bProcessingJournalEvent, true);
	if (Event.EventType == EGameplayActionEventType::Accepted)
	{
		FGameplayActionJournalResult Result = HandleAcceptedJournalEvent(Event);
		if (Result.IsAccepted() && Event.Handle.IsValid())
		{
			SinkEventsAwaitingObserver.Add(Event.Handle);
		}
		return Result;
	}

	ProcessLifecycleEvent(Event);
	if (Event.Handle.IsValid())
	{
		SinkEventsAwaitingObserver.Add(Event.Handle);
	}

	FGameplayActionJournalResult Result;
	Result.Status = EGameplayActionJournalWriteStatus::Accepted;
	return Result;
}

void UIntentReplayComponent::InitializeComponent()
{
	Super::InitializeComponent();
	InitializeIntentReplay();
}

void UIntentReplayComponent::UninitializeComponent()
{
	ShutdownIntentReplay();
	Super::UninitializeComponent();
}

void UIntentReplayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownIntentReplay();
	Super::EndPlay(EndPlayReason);
}

void UIntentReplayComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	ShutdownIntentReplay();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UIntentReplayComponent::BeginDestroy()
{
	ShutdownIntentReplay();
	Super::BeginDestroy();
}

void UIntentReplayComponent::HandleObservedActionEvent(const FGameplayActionEvent& Event)
{
	// GameplayActions also broadcasts accepted journal events to observers. The hand-off set avoids
	// writing the same lifecycle event twice while still accepting events not delivered to the sink.
	if (Event.Handle.IsValid() && SinkEventsAwaitingObserver.Remove(Event.Handle) > 0)
	{
		return;
	}
	ProcessLifecycleEvent(Event);
}

FIntentReplayOperationResult UIntentReplayComponent::BindResolvedActionComponent(
	UGameplayActionComponent* InActionComponent)
{
	if (!IsValid(InActionComponent))
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::MissingActionComponent,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("The resolved Gameplay Action Component is invalid."));
	}
	if (GetOwner() && InActionComponent->GetOwner() && InActionComponent->GetOwner() != GetOwner())
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::InvalidArgument,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("Intent Replay currently supports an Action Component on the same owning actor only."));
	}
	// GameplayActions intentionally permits one authoritative sink. Recording through the same-actor
	// component makes action lifetime, teardown, and replay-handle ownership unambiguous.
	if (!InActionComponent->RegisterJournalSink(this))
	{
		return MakeOperationFailure(
			EIntentReplayOperationStatus::JournalRegistrationFailed,
			IntentReplayTags::Failure_NotInitialized,
			TEXT("The Gameplay Action Component rejected journal-sink registration, usually because another sink owns it."));
	}

	BoundActionComponent = InActionComponent;
	bJournalRegistered = true;
	BoundActionComponent->OnActionEvent.AddDynamic(
		this,
		&UIntentReplayComponent::HandleObservedActionEvent);

	FIntentReplayOperationResult Result;
	Result.Status = EIntentReplayOperationStatus::Succeeded;
	return Result;
}

void UIntentReplayComponent::UnbindActionComponent()
{
	if (BoundActionComponent)
	{
		BoundActionComponent->OnActionEvent.RemoveDynamic(
			this,
			&UIntentReplayComponent::HandleObservedActionEvent);
		if (bJournalRegistered)
		{
			BoundActionComponent->UnregisterJournalSink(this);
		}
	}
	bJournalRegistered = false;
	BoundActionComponent = nullptr;
}

bool UIntentReplayComponent::HasNonTerminalRecording() const
{
	return ActiveRecordingSession && !IsRecordingTerminal(ActiveRecordingSession->State);
}

bool UIntentReplayComponent::HasNonTerminalPlayback() const
{
	return ActivePlaybackSession && !IsPlaybackTerminal(ActivePlaybackSession->State);
}

double UIntentReplayComponent::GetCurrentTimeSeconds() const
{
	return TimeSource
		? TimeSource->GetTimeSeconds(const_cast<UIntentReplayComponent*>(this))
		: (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

double UIntentReplayComponent::GetRecordingElapsedSeconds(
	const UIntentRecordingSession& Session) const
{
	if (Session.State == EIntentRecordingState::Finalized
		|| Session.State == EIntentRecordingState::Failed
		|| Session.State == EIntentRecordingState::Cancelled
		|| Session.State == EIntentRecordingState::Draining)
	{
		return FMath::Max(0.0, Session.FinalRecordedDurationSeconds);
	}
	const double EndTime = Session.bClockPaused
		? Session.PauseStartTimeSeconds
		: GetCurrentTimeSeconds();
	return FMath::Max(
		0.0,
		EndTime - Session.StartTimeSeconds - Session.AccumulatedPausedSeconds);
}

double UIntentReplayComponent::GetPlaybackElapsedSeconds(
	const UIntentReplayPlaybackSession& Session) const
{
	if (Session.State == EIntentReplayPlaybackState::Completed
		|| Session.State == EIntentReplayPlaybackState::Failed
		|| Session.State == EIntentReplayPlaybackState::Cancelled
		|| Session.State == EIntentReplayPlaybackState::Stopping)
	{
		return FMath::Max(0.0, Session.FinalElapsedSeconds);
	}
	if (!Session.bClockStarted)
	{
		return 0.0;
	}
	const double EndTime = Session.bClockPaused
		? Session.PauseStartTimeSeconds
		: GetCurrentTimeSeconds();
	return FMath::Max(
		0.0,
		EndTime - Session.StartTimeSeconds - Session.AccumulatedPausedSeconds);
}

FIntentReplayTimelineClockSnapshot UIntentReplayComponent::BuildRecordingClockSnapshot(
	const UIntentRecordingSession& Session) const
{
	FIntentReplayTimelineClockSnapshot Snapshot;
	Snapshot.bValid = Session.SessionId.IsValid() && Session.TrackId.IsValid();
	Snapshot.Domain = EIntentReplayTimelineDomain::Recording;
	Snapshot.RecordingSessionId = Session.SessionId;
	Snapshot.TrackId = Session.TrackId;
	Snapshot.RelativeTimeSeconds = GetRecordingElapsedSeconds(Session);
	Snapshot.NextTimelineSequence = Session.NextTimelineSequence;
	Snapshot.bClockStarted = true;
	Snapshot.bPaused = Session.State == EIntentRecordingState::Paused;
	Snapshot.bAcceptingTimelinePoints = Session.State == EIntentRecordingState::Recording;
	Snapshot.RecordingState = Session.State;
	return Snapshot;
}

FIntentReplayTimelineClockSnapshot UIntentReplayComponent::BuildPlaybackClockSnapshot(
	const UIntentReplayPlaybackSession& Session) const
{
	FIntentReplayTimelineClockSnapshot Snapshot;
	Snapshot.bValid = Session.SessionId.IsValid() && IsValid(Session.SourceTrack);
	Snapshot.Domain = EIntentReplayTimelineDomain::Playback;
	Snapshot.PlaybackSessionId = Session.SessionId;
	Snapshot.TrackId = Session.SourceTrack
		? Session.SourceTrack->GetTrackId()
		: FIntentReplayTrackId();
	Snapshot.RelativeTimeSeconds = GetPlaybackElapsedSeconds(Session);
	Snapshot.NextTimelineSequence = Session.NextTimelineSequence;
	Snapshot.bClockStarted = Session.bClockStarted;
	Snapshot.bPaused = Session.State == EIntentReplayPlaybackState::Paused;
	Snapshot.bAcceptingTimelinePoints = Session.State == EIntentReplayPlaybackState::Playing;
	Snapshot.PlaybackState = Session.State;
	return Snapshot;
}

FIntentReplayTimelinePointResult UIntentReplayComponent::CaptureRecordingTimelinePointInternal(
	UIntentRecordingSession& Session)
{
	FIntentReplayTimelinePointResult Result;
	Result.Clock = BuildRecordingClockSnapshot(Session);
	if (Session.State == EIntentRecordingState::Paused)
	{
		Result.Status = EIntentReplayTimelineCaptureStatus::Paused;
		return Result;
	}
	if (Session.State != EIntentRecordingState::Recording)
	{
		Result.Status = EIntentReplayTimelineCaptureStatus::NotAccepting;
		return Result;
	}
	Result.Status = EIntentReplayTimelineCaptureStatus::Succeeded;
	Result.TimelineSequence = Session.NextTimelineSequence++;
	Result.Clock = BuildRecordingClockSnapshot(Session);
	return Result;
}

bool UIntentReplayComponent::IsReplayEvent(
	const FGameplayActionEvent& Event,
	FRecordedIntentId& OutRecordedIntentId) const
{
	if (!ActivePlaybackSession
		|| !ActivePlaybackSession->SourceTrack
		|| Event.OriginTag != ReplayOriginTag
		|| Event.Correlation.Type != IntentReplayTags::Correlation_RecordedIntent
		|| !Event.Correlation.Id.IsValid())
	{
		return false;
	}

	const FRecordedIntentId Candidate(Event.Correlation.Id);
	const bool bFound = ActivePlaybackSession->SourceTrack->GetEntries().ContainsByPredicate(
		[Candidate](const FRecordedIntent& Entry)
		{
			return Entry.RecordedIntentId == Candidate;
		});
	if (bFound)
	{
		OutRecordedIntentId = Candidate;
	}
	return bFound;
}

bool UIntentReplayComponent::IsTrackEligible(const FGameplayActionEvent& Event) const
{
	FGameplayTagContainer Tags;
	if (Event.ActionTag.IsValid())
	{
		Tags.AddTag(Event.ActionTag);
	}
	if (Event.OriginTag.IsValid())
	{
		Tags.AddTag(Event.OriginTag);
	}
	return TrackEligibilityQuery.IsEmpty() || TrackEligibilityQuery.Matches(Tags);
}

FGameplayActionJournalResult UIntentReplayComponent::HandleAcceptedJournalEvent(
	const FGameplayActionEvent& Event)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplay_RecordAcceptedAction);
	// Replay-origin correlation is recognized before eligibility so replay requests can be routed to
	// their playback journal and excluded from recursive recording by the default query.
	FRecordedIntentId ReplayRecordedIntentId;
	const bool bReplayEvent = IsReplayEvent(Event, ReplayRecordedIntentId);
	UIntentExecutionJournal* Journal = ResolveJournalForAcceptedEvent(Event, ReplayRecordedIntentId);

	const bool bRecordingAvailable = ActiveRecordingSession
		&& (ActiveRecordingSession->State == EIntentRecordingState::Recording
			|| ActiveRecordingSession->State == EIntentRecordingState::Paused
			|| ActiveRecordingSession->State == EIntentRecordingState::Draining);
	if (!bReplayEvent
		&& !bRecordingAvailable
		&& NoRecordingSessionPolicy == EIntentNoRecordingSessionPolicy::RejectAcceptedActions)
	{
		FGameplayActionJournalResult Result;
		Result.Status = EGameplayActionJournalWriteStatus::Rejected;
		Result.ReasonTag = IntentReplayTags::Failure_NoRecordingSession;
		Result.DiagnosticMessage = TEXT("Intent Replay requires an active Recording Session.");
		return Result;
	}

	FRecordedIntent NewEntry;
	bool bAppendToTrack = false;
	if (!bReplayEvent
		&& ActiveRecordingSession
		&& ActiveRecordingSession->State == EIntentRecordingState::Recording
		&& IsTrackEligible(Event))
	{
		if (ActiveRecordingSession->Options.MaxTrackEntries > 0
			&& ActiveRecordingSession->MutableEntries.Num() >= ActiveRecordingSession->Options.MaxTrackEntries)
		{
			FGameplayActionJournalResult Result;
			Result.Status = EGameplayActionJournalWriteStatus::Rejected;
			Result.ReasonTag = IntentReplayTags::Failure_UnrecordableParameters;
			Result.DiagnosticMessage = TEXT("The Recording Session reached its configured track-entry capacity.");
			return Result;
		}

		const FIntentRecordabilityResult Recordability =
			RecordabilityPolicy->ValidatePropertyBag(Event.GetParameters());
		if (!Recordability.IsRecordable())
		{
			RecordDiagnostic(Recordability.DiagnosticMessage);
			FGameplayActionJournalResult Result;
			Result.Status = EGameplayActionJournalWriteStatus::Rejected;
			Result.ReasonTag = IntentReplayTags::Failure_UnrecordableParameters;
			Result.DiagnosticMessage = FString::Printf(
				TEXT("%s (%s at %s)"),
				*Recordability.DiagnosticMessage,
				*Recordability.PropertyType,
				*Recordability.ParameterPath);
			return Result;
		}
		if ((!Event.Definition.IsValid() && Event.Definition.IsNull() && !Event.DefinitionId.IsValid())
			|| !Event.ActionTag.IsValid()
			|| !Event.Handle.IsValid())
		{
			FGameplayActionJournalResult Result;
			Result.Status = EGameplayActionJournalWriteStatus::Rejected;
			Result.ReasonTag = IntentReplayTags::Failure_UnrecordableParameters;
			Result.DiagnosticMessage = TEXT("The Accepted snapshot lacks stable Definition, Action Tag, or handle data.");
			return Result;
		}

		// Build the complete snapshot off to the side. Nothing enters mutable track storage until all
		// validation has succeeded, preserving the all-or-nothing journal transaction.
		NewEntry.RecordedIntentId = FRecordedIntentId::NewId();
		NewEntry.DefinitionId = Event.DefinitionId;
		NewEntry.Definition = Event.Definition;
		NewEntry.ActionTag = Event.ActionTag;
		NewEntry.Parameters = Event.GetParameters();
		NewEntry.EffectivePriority = Event.Priority;
		NewEntry.EffectiveBlockedPolicy = Event.BlockedPolicy;
		NewEntry.ExecutionLocks = Event.ExecutionLocks;
		NewEntry.bInterruptible = Event.bInterruptible;
		NewEntry.OptionalTimeout = Event.OptionalTimeout;
		NewEntry.MaxQueueTimeSeconds = Event.MaxQueueTimeSeconds;
		NewEntry.OriginalOriginTag = Event.OriginTag;
		NewEntry.OriginalCorrelation = Event.Correlation;
		NewEntry.TrackSequence = ActiveRecordingSession->MutableEntries.Num();
		NewEntry.OriginalSubmissionSequence = Event.SubmissionSequence;
		const FIntentReplayTimelinePointResult TimelinePoint =
			CaptureRecordingTimelinePointInternal(*ActiveRecordingSession);
		if (!TimelinePoint.Succeeded())
		{
			FGameplayActionJournalResult Result;
			Result.Status = EGameplayActionJournalWriteStatus::Rejected;
			Result.ReasonTag = IntentReplayTags::Failure_NoRecordingSession;
			Result.DiagnosticMessage = TEXT("The authoritative recording timeline stopped accepting points.");
			return Result;
		}
		NewEntry.RelativeAcceptedTimeSeconds = TimelinePoint.Clock.RelativeTimeSeconds;
		NewEntry.TimelineSequence = TimelinePoint.TimelineSequence;
		bAppendToTrack = true;
	}

	if (bAppendToTrack)
	{
		// The handle maps are runtime routing indexes only. They are never moved into the finalized
		// track, so destroying the source actor cannot leave a source-action reference in replay data.
		const int32 EntryIndex = ActiveRecordingSession->MutableEntries.Add(MoveTemp(NewEntry));
		ActiveRecordingSession->EntryIndexByHandle.Add(Event.Handle, EntryIndex);
		ActiveRecordingSession->PendingTrackedHandles.Add(Event.Handle);
		RecordingSessionByHandle.Add(Event.Handle, ActiveRecordingSession);
		ReplayRecordedIntentId = ActiveRecordingSession->MutableEntries[EntryIndex].RecordedIntentId;
	}

	if (Event.Handle.IsValid() && Journal)
	{
		JournalByHandle.Add(Event.Handle, Journal);
	}
	if (bReplayEvent && Event.Handle.IsValid() && ActivePlaybackSession)
	{
		// GameplayActions delivers the transactional Accepted snapshot before it preempts conflicts.
		// Correlate the new handle now so a synchronous Ended event can identify an intentional
		// same-session replacement without parsing its diagnostic string.
		ActivePlaybackSession->RecordByRuntimeHandle.Add(
			Event.Handle,
			ReplayRecordedIntentId);
	}
	AppendExecutionEvent(
		Journal,
		Event,
		bReplayEvent && ActivePlaybackSession && ActivePlaybackSession->SourceTrack
			? ActivePlaybackSession->SourceTrack->GetTrackId()
			: (ActiveRecordingSession ? ActiveRecordingSession->TrackId : FIntentReplayTrackId()),
		ReplayRecordedIntentId,
		bReplayEvent && ActivePlaybackSession
			? ActivePlaybackSession->SessionId
			: FIntentReplayPlaybackSessionId());

	FGameplayActionJournalResult Result;
	Result.Status = EGameplayActionJournalWriteStatus::Accepted;
	return Result;
}

void UIntentReplayComponent::ProcessLifecycleEvent(const FGameplayActionEvent& Event)
{
	// Resolve by handle before looking at current sessions: Ended may arrive after recording has
	// entered AsyncStop and stopped accepting any new actions.
	FRecordedIntentId ReplayRecordedIntentId;
	const bool bReplayEvent = IsReplayEvent(Event, ReplayRecordedIntentId);
	UIntentExecutionJournal* Journal = ResolveJournalForExistingHandle(Event.Handle);
	if (!Journal)
	{
		Journal = ResolveJournalForAcceptedEvent(Event, ReplayRecordedIntentId);
		if (Event.EventType == EGameplayActionEventType::Accepted && Event.Handle.IsValid())
		{
			JournalByHandle.Add(Event.Handle, Journal);
		}
	}

	FIntentReplayTrackId TrackId;
	FIntentReplayPlaybackSessionId PlaybackSessionId;
	if (bReplayEvent && ActivePlaybackSession)
	{
		TrackId = ActivePlaybackSession->SourceTrack
			? ActivePlaybackSession->SourceTrack->GetTrackId()
			: FIntentReplayTrackId();
		PlaybackSessionId = ActivePlaybackSession->SessionId;
	}
	else if (const TObjectPtr<UIntentRecordingSession>* FoundSession = RecordingSessionByHandle.Find(Event.Handle))
	{
		if (*FoundSession)
		{
			TrackId = (*FoundSession)->TrackId;
			if (const int32* EntryIndex = (*FoundSession)->EntryIndexByHandle.Find(Event.Handle);
				EntryIndex && (*FoundSession)->MutableEntries.IsValidIndex(*EntryIndex))
			{
				ReplayRecordedIntentId = (*FoundSession)->MutableEntries[*EntryIndex].RecordedIntentId;
			}
		}
	}
	else if (ActiveRecordingSession)
	{
		TrackId = ActiveRecordingSession->TrackId;
	}

	AppendExecutionEvent(
		Journal,
		Event,
		TrackId,
		ReplayRecordedIntentId,
		PlaybackSessionId);

	if (bReplayEvent
		&& ActivePlaybackSession
		&& Event.EventType == EGameplayActionEventType::Accepted
		&& Event.Handle.IsValid())
	{
		// Definitions with journaling Disabled reach IntentReplay only through the observer delegate.
		// Accepted is still dispatched before any preempted Ended event, so retain the same causal
		// ordering guarantee as the transactional journal path.
		ActivePlaybackSession->RecordByRuntimeHandle.Add(
			Event.Handle,
			ReplayRecordedIntentId);
	}

	if (Event.EventType != EGameplayActionEventType::Ended)
	{
		return;
	}

	if (const TObjectPtr<UIntentRecordingSession>* FoundSession = RecordingSessionByHandle.Find(Event.Handle))
	{
		UIntentRecordingSession* Session = *FoundSession;
		if (Session)
		{
			// Original results are optional enrichment. Immediate publication freezes the track before
			// late Ended callbacks, whereas AsyncStop keeps the mutable entry available until this point.
			if (const int32* EntryIndex = Session->EntryIndexByHandle.Find(Event.Handle);
				EntryIndex
				&& Session->MutableEntries.IsValidIndex(*EntryIndex)
				&& Session->State != EIntentRecordingState::Finalized)
			{
				FRecordedIntent& Entry = Session->MutableEntries[*EntryIndex];
				Entry.bHasOriginalResult = Event.bHasResult;
				if (Event.bHasResult)
				{
					Entry.OriginalResult = Event.Result;
				}
			}
			Session->PendingTrackedHandles.Remove(Event.Handle);
			TryFinalizeDrainingSession(*Session);
		}
		RecordingSessionByHandle.Remove(Event.Handle);
	}

	if (bReplayEvent && ActivePlaybackSession)
	{
		UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
		Session.RecordByRuntimeHandle.Add(Event.Handle, ReplayRecordedIntentId);
		Session.ActiveReplayHandles.Remove(Event.Handle);
		const FGameplayTag ExpectedExternalReason =
			Session.ExpectedExternalInterruptionReasons.FindRef(Event.Handle);
		const bool bExpectedExternalInterruption =
			ExpectedExternalReason.IsValid()
			&& Event.bHasResult
			&& Event.Result.TerminalState == EGameplayActionState::Interrupted
			&& Event.Result.ReasonTag.MatchesTagExact(ExpectedExternalReason);
		Session.ExpectedExternalInterruptionReasons.Remove(Event.Handle);
		if (bExpectedExternalInterruption)
		{
			if (FIntentReplaySuspendedIntent* SuspendedIntent =
				Session.PendingExternalRecoveryByIntent.Find(ReplayRecordedIntentId))
			{
				SuspendedIntent->bHasInterruptionResult = true;
				SuspendedIntent->InterruptionResult = Event.Result;
			}
		}
		const bool bExpectedSameSessionPreemption =
			Event.bHasResult
			&& Event.Result.TerminalState == EGameplayActionState::Interrupted
			&& Event.Result.ReasonTag
				.MatchesTagExact(GameplayActionTags::Result_Interrupted_HigherPriority)
			&& Event.Result.CausingActionHandle.IsValid()
			&& Session.RecordByRuntimeHandle.Contains(
				Event.Result.CausingActionHandle);
		if (!bStoppingPlayback
			&& Event.bHasResult
			&& !IsSuccessfulTerminalAction(Event.Result)
			&& !bExpectedSameSessionPreemption
			&& !bExpectedExternalInterruption
			&& Session.Options.TerminalFailurePolicy == EIntentReplayTerminalFailurePolicy::StopPlayback
			&& !IsPlaybackTerminal(Session.State))
		{
			FailReplay(
				Session,
				MakeFailure(
					IntentReplayTags::Failure_SubmissionRejected,
					FString::Printf(
						TEXT("Replayed action %lld ended with %s."),
						Event.Handle.GetValue(),
						*UEnum::GetValueAsString(Event.Result.TerminalState)),
					ReplayRecordedIntentId));
		}
		else
		{
			TryCompleteReplay(Session);
		}
	}

	JournalByHandle.Remove(Event.Handle);
}

void UIntentReplayComponent::AppendExecutionEvent(
	UIntentExecutionJournal* Journal,
	const FGameplayActionEvent& Event,
	const FIntentReplayTrackId TrackId,
	const FRecordedIntentId RecordedIntentId,
	const FIntentReplayPlaybackSessionId PlaybackSessionId,
	const FString& DiagnosticMessage)
{
	if (!Journal)
	{
		return;
	}
	FIntentExecutionEvent ExecutionEvent;
	ExecutionEvent.ObservedRelativeTimeSeconds = FMath::Max(
		0.0,
		GetCurrentTimeSeconds() - Journal->GetStartTimeSeconds());
	ExecutionEvent.bHasActionEvent = true;
	ExecutionEvent.ActionEvent = Event;
	ExecutionEvent.TrackId = TrackId;
	ExecutionEvent.RecordedIntentId = RecordedIntentId;
	ExecutionEvent.PlaybackSessionId = PlaybackSessionId;
	ExecutionEvent.DiagnosticMessage = DiagnosticMessage;
	Journal->Append(MoveTemp(ExecutionEvent));
}

UIntentExecutionJournal* UIntentReplayComponent::ResolveJournalForAcceptedEvent(
	const FGameplayActionEvent& Event,
	const FRecordedIntentId& ReplayRecordedIntentId) const
{
	if (ReplayRecordedIntentId.IsValid() && ActivePlaybackSession)
	{
		return ActivePlaybackSession->ExecutionJournal;
	}
	if (ActiveRecordingSession
		&& (ActiveRecordingSession->State == EIntentRecordingState::Recording
			|| ActiveRecordingSession->State == EIntentRecordingState::Paused
			|| ActiveRecordingSession->State == EIntentRecordingState::Draining))
	{
		return ActiveRecordingSession->ExecutionJournal;
	}
	return AmbientExecutionJournal;
}

UIntentExecutionJournal* UIntentReplayComponent::ResolveJournalForExistingHandle(
	const FGameplayActionHandle Handle) const
{
	const TObjectPtr<UIntentExecutionJournal>* Found = JournalByHandle.Find(Handle);
	return Found ? Found->Get() : nullptr;
}

void UIntentReplayComponent::SetRecordingState(
	UIntentRecordingSession& Session,
	const EIntentRecordingState NewState)
{
	if (Session.State == NewState)
	{
		return;
	}
	const EIntentRecordingState PreviousState = Session.State;
	Session.State = NewState;
	OnRecordingStateChanged.Broadcast(PreviousState, NewState);
	FIntentReplayTimelineLifecycleEvent Event;
	Event.Domain = EIntentReplayTimelineDomain::Recording;
	Event.PreviousRecordingState = PreviousState;
	Event.NewRecordingState = NewState;
	Event.Clock = BuildRecordingClockSnapshot(Session);
	TimelineLifecycleChangedNative.Broadcast(Event);
	OnTimelineLifecycleChanged.Broadcast(Event);
}

void UIntentReplayComponent::FinalizeRecordingSession(UIntentRecordingSession& Session)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplay_FinalizeTrack);
	if (IsRecordingTerminal(Session.State))
	{
		return;
	}

	TArray<FRecordedIntent> FinalizedEntries = Session.MutableEntries;
	FinalizedEntries.Sort(
		[](const FRecordedIntent& Left, const FRecordedIntent& Right)
		{
			if (!FMath::IsNearlyEqual(Left.RelativeAcceptedTimeSeconds, Right.RelativeAcceptedTimeSeconds))
			{
				return Left.RelativeAcceptedTimeSeconds < Right.RelativeAcceptedTimeSeconds;
			}
			if (Left.TimelineSequence != Right.TimelineSequence)
			{
				return Left.TimelineSequence < Right.TimelineSequence;
			}
			return Left.TrackSequence < Right.TrackSequence;
		});
	for (int32 Index = 0; Index < FinalizedEntries.Num(); ++Index)
	{
		FinalizedEntries[Index].TrackSequence = Index;
	}

	const double Duration = FMath::Max(
		Session.FinalRecordedDurationSeconds,
		FinalizedEntries.IsEmpty() ? 0.0 : FinalizedEntries.Last().RelativeAcceptedTimeSeconds);
	// The transient package deliberately decouples the track lifetime from the source Actor. GC still
	// requires an external reflected reference when a coordinator carries it across world reset.
	UIntentReplayTrack* Track = NewObject<UIntentReplayTrack>(
		GetTransientPackage(),
		NAME_None,
		RF_Transient);
	if (!Track)
	{
		FailRecordingSession(
			Session,
			MakeFailure(
				IntentReplayTags::Failure_InvalidTrack,
				TEXT("Failed to allocate the finalized replay track.")));
		return;
	}
	// Publication is one-way: entries are moved out of the mutable session and become externally
	// readable only through const/copy accessors on the finalized track.
	Track->InitializeFinalized(
		Session.TrackId,
		Session.SessionId,
		MoveTemp(FinalizedEntries),
		Duration,
		Session.Options.SourceLabel,
		Session.Options.MetadataTags);
	const FIntentReplayTrackValidationResult Validation = Track->ValidateTrack();
	if (!Validation.bValid)
	{
		FailRecordingSession(
			Session,
			MakeFailure(IntentReplayTags::Failure_InvalidTrack, Validation.DiagnosticMessage));
		return;
	}

	LastFinalizedTrack = Track;
	LastRecordingSession = &Session;
	SetRecordingState(Session, EIntentRecordingState::Finalized);
	if (ActiveRecordingSession == &Session)
	{
		ActiveRecordingSession = nullptr;
	}
	OnRecordingFinalized.Broadcast(Track);
	RecordDiagnostic(FString::Printf(
		TEXT("Finalized track %s with %d entries."),
		*Track->GetTrackId().ToString(),
		Track->GetEntryCount()));
	INTENTREPLAY_LOG_INFO(
		TEXT("%s finalized track %s with %d entries."),
		*GetNameSafe(this),
		*Track->GetTrackId().ToString(),
		Track->GetEntryCount());
}

void UIntentReplayComponent::FailRecordingSession(
	UIntentRecordingSession& Session,
	const FIntentReplayFailure& Failure)
{
	Session.FinalRecordedDurationSeconds = GetRecordingElapsedSeconds(Session);
	SetRecordingState(Session, EIntentRecordingState::Failed);
	LastRecordingSession = &Session;
	if (ActiveRecordingSession == &Session)
	{
		ActiveRecordingSession = nullptr;
	}
	OnRecordingFailed.Broadcast(Failure);
	RecordDiagnostic(Failure.DiagnosticMessage);
	INTENTREPLAY_LOG_ERROR(
		TEXT("%s failed recording track %s: %s"),
		*GetNameSafe(this),
		*Session.TrackId.ToString(),
		*Failure.DiagnosticMessage);
}

void UIntentReplayComponent::TryFinalizeDrainingSession(UIntentRecordingSession& Session)
{
	if (Session.State == EIntentRecordingState::Draining
		&& Session.PendingTrackedHandles.IsEmpty())
	{
		FinalizeRecordingSession(Session);
	}
}

void UIntentReplayComponent::SetPlaybackState(
	UIntentReplayPlaybackSession& Session,
	const EIntentReplayPlaybackState NewState)
{
	if (Session.State == NewState)
	{
		return;
	}
	const EIntentReplayPlaybackState PreviousState = Session.State;
	Session.State = NewState;
	FIntentReplayTimelineLifecycleEvent Event;
	Event.Domain = EIntentReplayTimelineDomain::Playback;
	Event.PreviousPlaybackState = PreviousState;
	Event.NewPlaybackState = NewState;
	Event.Clock = BuildPlaybackClockSnapshot(Session);
	TimelineLifecycleChangedNative.Broadcast(Event);
	OnTimelineLifecycleChanged.Broadcast(Event);
}

void UIntentReplayComponent::HandleReplayAssetsLoaded(
	const FIntentReplayPlaybackSessionId ExpectedSessionId)
{
	PendingDefinitionLoadHandle.Reset();
	// Async callbacks capture the session ID, not an unchecked session pointer. A stop, failure, or
	// newer PrepareReplay therefore turns a late callback into a harmless no-op.
	if (!ActivePlaybackSession
		|| ActivePlaybackSession->SessionId != ExpectedSessionId
		|| ActivePlaybackSession->State != EIntentReplayPlaybackState::Preparing)
	{
		return;
	}

	FIntentReplayFailure Failure;
	if (!BuildPreparedReplayRequests(*ActivePlaybackSession, Failure))
	{
		FailReplay(*ActivePlaybackSession, Failure);
	}
}

bool UIntentReplayComponent::BuildPreparedReplayRequests(
	UIntentReplayPlaybackSession& Session,
	FIntentReplayFailure& OutFailure)
{
	Session.PreparedRequests.Reset();
	Session.CompatibilityReports.Reset();
	if (!Session.SourceTrack)
	{
		OutFailure = MakeFailure(IntentReplayTags::Failure_InvalidTrack, TEXT("Playback lost its source track."));
		return false;
	}

	// Prepared requests are recipient-local and rebuilt from current Definitions. The shared track is
	// never mutated, so parallel clones cannot overwrite one another's compatibility/default values.
	for (const FRecordedIntent& RecordedIntent : Session.SourceTrack->GetEntries())
	{
		FGameplayActionRequest Request;
		FIntentReplayCompatibilityReport Report;
		if (!BuildPreparedRequest(
			RecordedIntent,
			Session.Options.CompatibilityPolicy,
			Request,
			Report,
			OutFailure))
		{
			return false;
		}
		Session.PreparedRequests.Add(MoveTemp(Request));
		Session.CompatibilityReports.Add(MoveTemp(Report));
	}

	SetPlaybackState(Session, EIntentReplayPlaybackState::Ready);
	OnReplayPrepared.Broadcast(Session.SessionId, Session.SourceTrack);
	RecordDiagnostic(FString::Printf(
		TEXT("Prepared replay session %s with %d requests."),
		*Session.SessionId.ToString(),
		Session.PreparedRequests.Num()));
	return true;
}

bool UIntentReplayComponent::BuildPreparedRequest(
	const FRecordedIntent& RecordedIntent,
	const EIntentReplayCompatibilityPolicy CompatibilityPolicy,
	FGameplayActionRequest& OutRequest,
	FIntentReplayCompatibilityReport& OutReport,
	FIntentReplayFailure& OutFailure) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplay_BuildReplayRequest);
	UGameplayActionDefinition* Definition = ResolveDefinition(RecordedIntent);
	if (!Definition)
	{
		OutFailure = MakeFailure(
			IntentReplayTags::Failure_DefinitionUnavailable,
			FString::Printf(
				TEXT("Definition for recorded intent %s is unavailable."),
				*RecordedIntent.RecordedIntentId.ToString()),
			RecordedIntent.RecordedIntentId);
		return false;
	}

	const auto AddConfigChange = [&OutReport](const TCHAR* Change)
	{
		OutReport.ConfigurationChanges.Add(Change);
	};
	if (Definition->ActionTag != RecordedIntent.ActionTag)
	{
		AddConfigChange(TEXT("ActionTag changed"));
	}
	if (Definition->ExecutionLocks != RecordedIntent.ExecutionLocks)
	{
		AddConfigChange(TEXT("ExecutionLocks changed"));
	}
	if (Definition->bInterruptible != RecordedIntent.bInterruptible)
	{
		AddConfigChange(TEXT("Interruptibility changed"));
	}
	if (Definition->OptionalTimeout.bEnabled != RecordedIntent.OptionalTimeout.bEnabled
		|| !FMath::IsNearlyEqual(
			Definition->OptionalTimeout.DurationSeconds,
			RecordedIntent.OptionalTimeout.DurationSeconds))
	{
		AddConfigChange(TEXT("OptionalTimeout changed"));
	}
	if (!FMath::IsNearlyEqual(
		Definition->MaxQueueTimeSeconds,
		RecordedIntent.MaxQueueTimeSeconds))
	{
		AddConfigChange(TEXT("MaxQueueTimeSeconds changed"));
	}
	if (CompatibilityPolicy == EIntentReplayCompatibilityPolicy::StrictRecordedSchema
		&& !OutReport.ConfigurationChanges.IsEmpty())
	{
		OutReport.bCompatible = false;
		OutFailure = MakeFailure(
			IntentReplayTags::Failure_Compatibility,
			FString::Printf(
				TEXT("Strict replay compatibility rejected Definition changes for recorded intent %s: %s."),
				*RecordedIntent.RecordedIntentId.ToString(),
				*FString::Join(OutReport.ConfigurationChanges, TEXT(", "))),
			RecordedIntent.RecordedIntentId);
		return false;
	}

	// Start from current Definition defaults, then copy recorded values according to the requested
	// compatibility policy. Compatible mode can therefore retain newly introduced defaults safely.
	FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(Definition);
	if (!Creation.WasCreated())
	{
		OutFailure = MakeFailure(
			IntentReplayTags::Failure_Compatibility,
			Creation.DiagnosticMessage,
			RecordedIntent.RecordedIntentId);
		return false;
	}

	FString CopyDiagnostic;
	if (!CopyRecordedBagValues(
		RecordedIntent.GetParameters(),
		Creation.Request,
		CompatibilityPolicy,
		OutReport,
		CopyDiagnostic))
	{
		OutFailure = MakeFailure(
			IntentReplayTags::Failure_Compatibility,
			CopyDiagnostic,
			RecordedIntent.RecordedIntentId);
		return false;
	}

	UGameplayActionBlueprintLibrary::SetRequestPriority(
		Creation.Request,
		RecordedIntent.EffectivePriority);
	UGameplayActionBlueprintLibrary::SetRequestBlockedPolicy(
		Creation.Request,
		RecordedIntent.EffectiveBlockedPolicy);
	FGameplayActionCorrelationData Correlation;
	// Replay receives a fresh runtime request/handle. Only semantic correlation points back to the
	// immutable Recorded Intent ID; the original source handle is intentionally absent.
	Correlation.Type = IntentReplayTags::Correlation_RecordedIntent;
	Correlation.Id = RecordedIntent.RecordedIntentId.GetGuid();
	UGameplayActionBlueprintLibrary::SetRequestContext(
		Creation.Request,
		ReplayOriginTag,
		GetOwner(),
		Correlation);
	OutRequest = MoveTemp(Creation.Request);
	return true;
}

UGameplayActionDefinition* UIntentReplayComponent::ResolveDefinition(
	const FRecordedIntent& RecordedIntent) const
{
	if (UGameplayActionDefinition* Definition = RecordedIntent.Definition.Get())
	{
		return Definition;
	}
	if (RecordedIntent.DefinitionId.IsValid() && UAssetManager::IsInitialized())
	{
		if (UGameplayActionDefinition* Definition =
			UAssetManager::Get().GetPrimaryAssetObject<UGameplayActionDefinition>(
				RecordedIntent.DefinitionId))
		{
			return Definition;
		}
	}
	return Cast<UGameplayActionDefinition>(
		RecordedIntent.Definition.ToSoftObjectPath().ResolveObject());
}

void UIntentReplayComponent::ScheduleNextReplayEntry()
{
	if (!ActivePlaybackSession
		|| ActivePlaybackSession->State != EIntentReplayPlaybackState::Playing
		|| !ActivePlaybackSession->SourceTrack)
	{
		return;
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	if (Session.NextEntryIndex >= Session.SourceTrack->GetEntryCount())
	{
		MarkAllEntriesSubmitted(Session);
		TryCompleteReplay(Session);
		return;
	}

	const FRecordedIntent& NextEntry =
		Session.SourceTrack->GetEntries()[Session.NextEntryIndex];
	// Recompute delay from the absolute recorded timestamp every time. This avoids cumulative drift
	// and naturally submits multiple overdue entries together after a hitch.
	const double Delay = FMath::Max(
		0.0,
		NextEntry.RelativeAcceptedTimeSeconds - GetPlaybackElapsedSeconds(Session));
	if (Delay <= KINDA_SMALL_NUMBER)
	{
		SubmitDueReplayEntries();
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PlaybackTimerHandle,
			this,
			&UIntentReplayComponent::SubmitDueReplayEntries,
			static_cast<float>(Delay),
			false);
	}
	else
	{
		FailReplay(
			Session,
			MakeFailure(
				IntentReplayTags::Failure_InvalidTrack,
				TEXT("Replay scheduling requires a valid World.")));
	}
}

void UIntentReplayComponent::SubmitDueReplayEntries()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IntentReplay_SubmitDueEntries);
	if (!ActivePlaybackSession
		|| ActivePlaybackSession->State != EIntentReplayPlaybackState::Playing
		|| !ActivePlaybackSession->SourceTrack)
	{
		return;
	}

	UIntentReplayPlaybackSession& Session = *ActivePlaybackSession;
	const TArray<FRecordedIntent>& Entries = Session.SourceTrack->GetEntries();
	const double Elapsed = GetPlaybackElapsedSeconds(Session);
	// One timer wake-up may cover several equal/overdue timestamps. Order remains the contiguous
	// TrackSequence order validated by UIntentReplayTrack.
	while (Session.NextEntryIndex < Entries.Num()
		&& Entries[Session.NextEntryIndex].RelativeAcceptedTimeSeconds <= Elapsed + KINDA_SMALL_NUMBER)
	{
		const int32 EntryIndex = Session.NextEntryIndex++;
		const FRecordedIntent& Entry = Entries[EntryIndex];
		if (!Session.PreparedRequests.IsValidIndex(EntryIndex))
		{
			FailReplay(
				Session,
				MakeFailure(
					IntentReplayTags::Failure_Compatibility,
					TEXT("Prepared request indexing became inconsistent."),
					Entry.RecordedIntentId));
			return;
		}

		const FGameplayActionSubmissionResult Submission =
			ExecutionStrategy->SubmitPreparedRequest(
				BoundActionComponent,
				Session.PreparedRequests[EntryIndex]);
		++Session.ProcessedEntryCount;
		if (Submission.IsAccepted())
		{
			Session.RecordByRuntimeHandle.Add(Submission.Handle, Entry.RecordedIntentId);
			FGameplayActionResult ExistingResult;
			// An action may complete synchronously inside SubmitAction. Do not retain a handle that is
			// already terminal or completion would wait forever for a callback that already happened.
			if (!BoundActionComponent->GetActionResult(Submission.Handle, ExistingResult))
			{
				Session.ActiveReplayHandles.Add(Submission.Handle);
			}
			OnRecordedIntentSubmitted.Broadcast(Entry.RecordedIntentId, Submission);
		}
		else
		{
			OnRecordedIntentSubmissionFailed.Broadcast(Entry.RecordedIntentId, Submission);
			FIntentExecutionEvent FailureEvent;
			FailureEvent.ObservedRelativeTimeSeconds = FMath::Max(
				0.0,
				GetCurrentTimeSeconds() - Session.ExecutionJournal->GetStartTimeSeconds());
			FailureEvent.TrackId = Session.SourceTrack->GetTrackId();
			FailureEvent.RecordedIntentId = Entry.RecordedIntentId;
			FailureEvent.PlaybackSessionId = Session.SessionId;
			FailureEvent.DiagnosticMessage = Submission.DiagnosticMessage;
			Session.ExecutionJournal->Append(MoveTemp(FailureEvent));
			if (Session.Options.SubmissionFailurePolicy
				== EIntentReplaySubmissionFailurePolicy::StopPlayback)
			{
				FailReplay(
					Session,
					MakeFailure(
						IntentReplayTags::Failure_SubmissionRejected,
						Submission.DiagnosticMessage,
						Entry.RecordedIntentId));
				return;
			}
		}
	}

	if (Session.NextEntryIndex >= Entries.Num())
	{
		MarkAllEntriesSubmitted(Session);
		TryCompleteReplay(Session);
	}
	else
	{
		ScheduleNextReplayEntry();
	}
}

void UIntentReplayComponent::MarkAllEntriesSubmitted(UIntentReplayPlaybackSession& Session)
{
	if (!Session.bAllEntriesSubmittedBroadcast)
	{
		Session.bAllEntriesSubmittedBroadcast = true;
		OnReplayAllEntriesSubmitted.Broadcast(Session.SessionId);
	}
}

void UIntentReplayComponent::TryCompleteReplay(UIntentReplayPlaybackSession& Session)
{
	// Exhausting the timeline is not enough: Completed is emitted only after every session-owned
	// action is terminal and every recoverable external interruption has been reconciled, preserving
	// accurate lifecycle semantics for Behavior Tree coordinators.
	if (Session.State == EIntentReplayPlaybackState::Playing
		&& Session.bAllEntriesSubmittedBroadcast
		&& Session.ActiveReplayHandles.IsEmpty()
		&& Session.PendingExternalRecoveryByIntent.IsEmpty())
	{
		CompleteReplay(Session);
	}
}

void UIntentReplayComponent::CompleteReplay(UIntentReplayPlaybackSession& Session)
{
	ClearReplayScheduling();
	Session.FinalElapsedSeconds = GetPlaybackElapsedSeconds(Session);
	SetPlaybackState(Session, EIntentReplayPlaybackState::Completed);
	FIntentReplayResult Result;
	Result.Status = EIntentReplayTerminalStatus::Completed;
	Result.SessionId = Session.SessionId;
	Result.TrackId = Session.SourceTrack ? Session.SourceTrack->GetTrackId() : FIntentReplayTrackId();
	Result.ProcessedEntries = Session.ProcessedEntryCount;
	Result.TotalEntries = Session.SourceTrack ? Session.SourceTrack->GetEntryCount() : 0;
	OnReplayCompleted.Broadcast(Result);
	RecordDiagnostic(FString::Printf(TEXT("Completed replay session %s."), *Session.SessionId.ToString()));
}

void UIntentReplayComponent::FailReplay(
	UIntentReplayPlaybackSession& Session,
	const FIntentReplayFailure& Failure)
{
	if (IsPlaybackTerminal(Session.State))
	{
		return;
	}
	Session.FinalElapsedSeconds = GetPlaybackElapsedSeconds(Session);
	bStoppingPlayback = true;
	SetPlaybackState(Session, EIntentReplayPlaybackState::Stopping);
	ClearReplayScheduling();
	CancelReplayOwnedActions(Session);
	Session.PendingExternalRecoveryByIntent.Reset();
	Session.ExpectedExternalInterruptionReasons.Reset();
	bStoppingPlayback = false;
	SetPlaybackState(Session, EIntentReplayPlaybackState::Failed);

	FIntentReplayResult Result;
	Result.Status = EIntentReplayTerminalStatus::Failed;
	Result.SessionId = Session.SessionId;
	Result.TrackId = Session.SourceTrack ? Session.SourceTrack->GetTrackId() : FIntentReplayTrackId();
	Result.ProcessedEntries = Session.ProcessedEntryCount;
	Result.TotalEntries = Session.SourceTrack ? Session.SourceTrack->GetEntryCount() : 0;
	Result.Failure = Failure;
	OnReplayFailed.Broadcast(Result);
	RecordDiagnostic(Failure.DiagnosticMessage);
	INTENTREPLAY_LOG_ERROR(
		TEXT("%s failed replay session %s: %s"),
		*GetNameSafe(this),
		*Session.SessionId.ToString(),
		*Failure.DiagnosticMessage);
}

void UIntentReplayComponent::CancelReplayOwnedActions(UIntentReplayPlaybackSession& Session)
{
	if (!BoundActionComponent)
	{
		Session.ActiveReplayHandles.Reset();
		return;
	}

	// Copy before cancellation because CancelAction can synchronously emit Ended and mutate the
	// session set through ProcessLifecycleEvent. Foreign handles never enter this array.
	TArray<FGameplayActionHandle> Handles = Session.ActiveReplayHandles.Array();
	Handles.Sort(
		[](const FGameplayActionHandle Left, const FGameplayActionHandle Right)
		{
			return Left.GetValue() < Right.GetValue();
		});
	for (const FGameplayActionHandle Handle : Handles)
	{
		BoundActionComponent->CancelAction(
			Handle,
			IntentReplayTags::Cancelled_PlaybackStopped);
	}
	Session.ActiveReplayHandles.Reset();
}

void UIntentReplayComponent::ClearReplayScheduling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlaybackTimerHandle);
	}
	if (PendingDefinitionLoadHandle.IsValid())
	{
		PendingDefinitionLoadHandle->CancelHandle();
		PendingDefinitionLoadHandle.Reset();
	}
}

FIntentReplayOperationResult UIntentReplayComponent::MakeOperationFailure(
	const EIntentReplayOperationStatus Status,
	const FGameplayTag ReasonTag,
	FString DiagnosticMessage,
	const FRecordedIntentId RecordedIntentId) const
{
	FIntentReplayOperationResult Result;
	Result.Status = Status;
	Result.Failure = MakeFailure(ReasonTag, MoveTemp(DiagnosticMessage), RecordedIntentId);
	return Result;
}

FIntentReplayFailure UIntentReplayComponent::MakeFailure(
	const FGameplayTag ReasonTag,
	FString DiagnosticMessage,
	const FRecordedIntentId RecordedIntentId) const
{
	FIntentReplayFailure Failure;
	Failure.ReasonTag = ReasonTag;
	Failure.DiagnosticMessage = MoveTemp(DiagnosticMessage);
	Failure.RecordedIntentId = RecordedIntentId;
	return Failure;
}

void UIntentReplayComponent::RecordDiagnostic(FString Diagnostic)
{
	LastDiagnostic = MoveTemp(Diagnostic);
	if (IsDetailedDebugEnabled())
	{
		INTENTREPLAY_LOG_INFO(
			TEXT("%s owner=%s: %s"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*LastDiagnostic);
	}
}

bool UIntentReplayComponent::IsDetailedDebugEnabled() const
{
	return bEnableDebug && IsIntentReplayDebugEnabled();
}

void UIntentReplayComponent::ShutdownIntentReplay()
{
	if (bShuttingDown)
	{
		return;
	}
	bShuttingDown = true;

	// All Unreal lifecycle exits converge here. The guard makes repeated callbacks idempotent while
	// timer/load cancellation and delegate unbinding remain symmetrical with initialization.
	ClearReplayScheduling();
	if (ActivePlaybackSession && !IsPlaybackTerminal(ActivePlaybackSession->State))
	{
		ActivePlaybackSession->FinalElapsedSeconds =
			GetPlaybackElapsedSeconds(*ActivePlaybackSession);
		bStoppingPlayback = true;
		CancelReplayOwnedActions(*ActivePlaybackSession);
		ActivePlaybackSession->PendingExternalRecoveryByIntent.Reset();
		ActivePlaybackSession->ExpectedExternalInterruptionReasons.Reset();
		bStoppingPlayback = false;
		SetPlaybackState(*ActivePlaybackSession, EIntentReplayPlaybackState::Cancelled);
	}
	if (ActiveRecordingSession && !IsRecordingTerminal(ActiveRecordingSession->State))
	{
		ActiveRecordingSession->FinalRecordedDurationSeconds =
			GetRecordingElapsedSeconds(*ActiveRecordingSession);
		SetRecordingState(*ActiveRecordingSession, EIntentRecordingState::Cancelled);
		LastRecordingSession = ActiveRecordingSession;
		ActiveRecordingSession = nullptr;
	}
	UnbindActionComponent();
	JournalByHandle.Reset();
	RecordingSessionByHandle.Reset();
	SinkEventsAwaitingObserver.Reset();
	bInitialized = false;
}
