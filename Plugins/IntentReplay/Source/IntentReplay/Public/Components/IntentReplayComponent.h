#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/GameplayActionJournalSink.h"
#include "Types/IntentReplayTypes.h"
#include "IntentReplayComponent.generated.h"

struct FStreamableHandle;
class UGameplayActionComponent;
class UGameplayActionDefinition;
class UIntentExecutionJournal;
class UIntentRecordabilityPolicy;
class UIntentRecordingSession;
class UIntentReplayExecutionStrategy;
class UIntentReplayPlaybackSession;
class UIntentReplayTimeSource;
class UIntentReplayTrack;

/** Fired after a fresh recording session has entered Recording. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIntentRecordingStartedDelegate, FIntentReplayTrackId, TrackId);
/** Fired for every recording state transition, including terminal transitions. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIntentRecordingStateChangedDelegate,
	EIntentRecordingState,
	PreviousState,
	EIntentRecordingState,
	NewState);
/**
 * Fired when a stop request has produced an immutable track.
 * Immediate stop fires this before RequestStopRecording returns; AsyncStop fires it later.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIntentRecordingFinalizedDelegate, UIntentReplayTrack*, Track);
/** Fired when recording cannot preserve a replay-safe snapshot. No track is published. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIntentRecordingFailedDelegate, const FIntentReplayFailure&, Failure);
/** Fired when asynchronous Definition loading and request preparation have completed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIntentReplayPreparedDelegate,
	FIntentReplayPlaybackSessionId,
	SessionId,
	UIntentReplayTrack*,
	Track);
/** Fired when a prepared playback session starts advancing its timeline. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIntentReplayStartedDelegate, FIntentReplayPlaybackSessionId, SessionId);
/** Fired for each prepared request after GameplayActions returns its submission result. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIntentRecordedIntentSubmittedDelegate,
	FRecordedIntentId,
	RecordedIntentId,
	const FGameplayActionSubmissionResult&,
	SubmissionResult);
/** Fired once the timeline has processed every entry; owned actions may still be active. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FIntentReplayAllEntriesSubmittedDelegate,
	FIntentReplayPlaybackSessionId,
	SessionId);
/** Shared signature for completed, failed, and user-stopped playback notifications. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIntentReplayFinishedDelegate, const FIntentReplayResult&, Result);
/** Fired after every authoritative recording/playback transition with an immutable clock snapshot. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FIntentReplayTimelineLifecycleDelegate,
	const FIntentReplayTimelineLifecycleEvent&,
	Event);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FIntentReplayTimelineLifecycleNativeDelegate,
	const FIntentReplayTimelineLifecycleEvent&);

/**
 * Records immutable GameplayActions request snapshots and replays finalized tracks on this actor.
 *
 * The component owns only transient sessions. Finalized tracks are created under the transient
 * package so an external reset/iteration coordinator can retain them through a UPROPERTY and pass
 * them to a different actor after the source actor has been destroyed.
 */
UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class INTENTREPLAY_API UIntentReplayComponent
	: public UActorComponent
	, public IGameplayActionJournalSink
{
	GENERATED_BODY()

public:
	UIntentReplayComponent();

	/** Explicitly initializes binding, policies, the ambient journal, and journal-sink registration. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay")
	FIntentReplayOperationResult InitializeIntentReplay();

	/** Changes the authoritative action source when no recording or playback session is active. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay")
	FIntentReplayOperationResult SetActionComponent(UGameplayActionComponent* InActionComponent);

	/** True after policies exist and this component is registered as the bound action component's sink. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay")
	bool IsIntentReplayInitialized() const { return bInitialized; }

	/** Returns the same-entity GameplayActionComponent that owns all observed and replayed actions. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay")
	UGameplayActionComponent* GetBoundActionComponent() const { return BoundActionComponent; }

	/** Starts a fresh mutable session with a new Track ID, clock, journal, and entry storage. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Recording")
	FIntentRecordingStartResult StartRecording(const FIntentRecordingOptions& Options);

	/**
	 * Stops accepting new entries and requests track publication.
	 *
	 * Immediate is the default and publishes before this function returns. AsyncStop waits
	 * event-driven for already tracked actions to end; consume its track from OnRecordingFinalized.
	 * Both modes retain every Accepted entry already committed to the session.
	 */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Recording")
	FIntentReplayOperationResult RequestStopRecording(
		EIntentRecordingFinalizeMode FinalizeMode = EIntentRecordingFinalizeMode::Immediate);

	/** Discards the active mutable session without creating a track. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Recording")
	FIntentReplayOperationResult CancelRecording();

	/** Suspends entry acceptance and the recording clock without affecting tracked GameplayActions. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Recording")
	FIntentReplayOperationResult PauseRecording();

	/** Resumes entry acceptance with paused wall time excluded from future timestamps. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Recording")
	FIntentReplayOperationResult ResumeRecording();

	/** Returns the active state, or the most recent terminal state when no session is active. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	EIntentRecordingState GetRecordingState() const;

	/** Returns the mutable non-terminal session, or None after finalize/cancel/failure. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	UIntentRecordingSession* GetActiveRecordingSession() const { return ActiveRecordingSession; }

	/** Returns the last terminal recording session for diagnostics and journal inspection. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	UIntentRecordingSession* GetLastRecordingSession() const { return LastRecordingSession; }

	/**
	 * Returns the last immutable track published by this component.
	 * During AsyncStop it does not represent the stopping session until OnRecordingFinalized fires:
	 * it remains the previous finalized track, or None if this component has never finalized one.
	 */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Recording")
	UIntentReplayTrack* GetLastFinalizedTrack() const { return LastFinalizedTrack; }

	/** Read-only view of the active or most recent recording timeline. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Timeline")
	FIntentReplayTimelineClockSnapshot GetRecordingClockSnapshot() const;

	/** Atomically captures recording-relative time and allocates deterministic cross-channel order. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Timeline")
	FIntentReplayTimelinePointResult CaptureRecordingTimelinePoint(
		FIntentRecordingSessionId ExpectedSessionId);

	/**
	 * Validates a finalized track and creates recipient-local prepared requests.
	 * Definition loading may make this asynchronous; StartReplay is legal only in Ready.
	 */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Playback")
	FIntentReplayPrepareResult PrepareReplay(
		UIntentReplayTrack* Track,
		const FIntentReplayPlaybackOptions& Options);

	/** Starts one-shot scheduling for a playback session that is Ready. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Playback")
	FIntentReplayOperationResult StartReplay();

	/** Suspends the replay clock and pending timer without losing the next absolute timestamp. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Playback")
	FIntentReplayOperationResult PauseReplay();

	/** Restarts scheduling after compensating for time spent paused. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Playback")
	FIntentReplayOperationResult ResumeReplay();

	/**
	 * Pauses scheduling, snapshots every currently replay-owned intent, and interrupts those actions.
	 * The resulting snapshots must be reissued or resolved before ResumeReplay can succeed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Playback|Recovery")
	FIntentReplayExternalInterruptionResult BeginExternalReplayInterruption(
		FGameplayTag InterruptionReason);

	/** Rebuilds and resubmits one immutable intent snapshot using the prepared replay request. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Playback|Recovery")
	FIntentReplayRecoveryResult ReissueExternallyInterruptedIntent(
		FRecordedIntentId RecordedIntentId);

	/** Reconciles an interrupted intent whose semantic outcome is already satisfied in the world. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Playback|Recovery")
	FIntentReplayOperationResult ResolveExternallyInterruptedIntentAsSatisfied(
		FRecordedIntentId RecordedIntentId);

	/** True while ResumeReplay is intentionally blocked by unreconciled external interruptions. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback|Recovery")
	bool HasPendingExternalReplayRecovery() const;

	/** Stops scheduling and cancels only handles created by the current playback session. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Playback")
	FIntentReplayOperationResult StopReplay();

	/** Returns the current playback lifecycle state, including its last terminal state. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	EIntentReplayPlaybackState GetPlaybackState() const;

	/** Returns the most recently prepared playback session, including after it becomes terminal. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Playback")
	UIntentReplayPlaybackSession* GetActivePlaybackSession() const { return ActivePlaybackSession; }

	/** Read-only view of the most recently prepared playback timeline. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Timeline")
	FIntentReplayTimelineClockSnapshot GetPlaybackClockSnapshot() const;

	/** Atomically captures playback-relative time and allocates deterministic cross-channel order. */
	UFUNCTION(BlueprintCallable, Category = "Intent Replay|Timeline")
	FIntentReplayTimelinePointResult CapturePlaybackTimelinePoint(
		FIntentReplayPlaybackSessionId ExpectedSessionId);

	/** Journal for observed GameplayActions that are not assigned to a recording or playback session. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Journal")
	UIntentExecutionJournal* GetAmbientExecutionJournal() const { return AmbientExecutionJournal; }

	/** Captures read-only diagnostic state without exposing mutable session internals. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Debug")
	FIntentReplayDebugSnapshot GetDebugSnapshot() const;

	/** Native lifecycle channel intended for optional synchronized runtime modules. */
	FIntentReplayTimelineLifecycleNativeDelegate& OnTimelineLifecycleChangedNative()
	{
		return TimelineLifecycleChangedNative;
	}

	/** GameplayActions synchronous journal transaction entry point; not a general caller API. */
	virtual FGameplayActionJournalResult WriteGameplayActionEvent_Implementation(
		const FGameplayActionEvent& Event) override;

	/** See FIntentRecordingStartedDelegate. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentRecordingStartedDelegate OnRecordingStarted;

	/** See FIntentRecordingStateChangedDelegate. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentRecordingStateChangedDelegate OnRecordingStateChanged;

	/** The authoritative place to receive tracks from AsyncStop. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentRecordingFinalizedDelegate OnRecordingFinalized;

	/** Carries the structured reason when the recorder cannot publish a track. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentRecordingFailedDelegate OnRecordingFailed;

	/** Fires only for preparation that initially returned Preparing. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentReplayPreparedDelegate OnReplayPrepared;

	/** Fires after StartReplay successfully enters Playing. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentReplayStartedDelegate OnReplayStarted;

	/** Successful GameplayActions submission for one Recorded Intent ID. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentRecordedIntentSubmittedDelegate OnRecordedIntentSubmitted;

	/** Rejected GameplayActions submission; the selected policy decides whether playback stops. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentRecordedIntentSubmittedDelegate OnRecordedIntentSubmissionFailed;

	/** Timeline exhausted notification; completion waits for remaining owned actions. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentReplayAllEntriesSubmittedDelegate OnReplayAllEntriesSubmitted;

	/** Normal terminal notification after timeline and owned actions are both finished. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentReplayFinishedDelegate OnReplayCompleted;

	/** Failure-policy terminal notification with a structured failure. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentReplayFinishedDelegate OnReplayFailed;

	/** User-requested StopReplay terminal notification. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentReplayFinishedDelegate OnReplayStopped;

	/** Generic immutable lifecycle notification; existing specialized events retain their behavior. */
	UPROPERTY(BlueprintAssignable, Category = "Intent Replay|Events")
	FIntentReplayTimelineLifecycleDelegate OnTimelineLifecycleChanged;

	/** Optional explicit component on the same actor. When unset, the first matching component is cached. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Intent Replay|Binding")
	TObjectPtr<UGameplayActionComponent> ActionComponentOverride;

	/** Controls Accepted transaction behavior when no recording session can own a track entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay|Recording")
	EIntentNoRecordingSessionPolicy NoRecordingSessionPolicy = EIntentNoRecordingSessionPolicy::JournalOnly;

	/**
	 * Query evaluated against a container containing both ActionTag and OriginTag.
	 * The default excludes GameplayAction.Origin.Replay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay|Recording")
	FGameplayTagQuery TrackEligibilityQuery;

	/** Origin written to new replay requests; defaults to GameplayAction.Origin.Replay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay|Playback")
	FGameplayTag ReplayOriginTag;

	/** Instanced per component during initialization; replace to customize the recording/playback clock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Intent Replay|Policies")
	TSubclassOf<UIntentReplayTimeSource> TimeSourceClass;

	/** Instanced per component and used transactionally before an Accepted entry can enter a track. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Intent Replay|Policies")
	TSubclassOf<UIntentRecordabilityPolicy> RecordabilityPolicyClass;

	/** Instanced per component and responsible only for submitting already prepared replay requests. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Intent Replay|Policies")
	TSubclassOf<UIntentReplayExecutionStrategy> ExecutionStrategyClass;

	/** Detailed diagnostics require this flag and the global IntentReplay.Debug CVar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay|Debug")
	bool bEnableDebug = false;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void BeginDestroy() override;

private:
	/** Observer fallback for lifecycle events not already consumed by the synchronous sink. */
	UFUNCTION()
	void HandleObservedActionEvent(const FGameplayActionEvent& Event);

	// Binding and clock helpers keep public commands independent from Unreal lifecycle ordering.
	FIntentReplayOperationResult BindResolvedActionComponent(UGameplayActionComponent* InActionComponent);
	void UnbindActionComponent();
	bool HasNonTerminalRecording() const;
	bool HasNonTerminalPlayback() const;
	double GetCurrentTimeSeconds() const;
	double GetRecordingElapsedSeconds(const UIntentRecordingSession& Session) const;
	double GetPlaybackElapsedSeconds(const UIntentReplayPlaybackSession& Session) const;
	FIntentReplayTimelinePointResult CaptureRecordingTimelinePointInternal(
		UIntentRecordingSession& Session);
	FIntentReplayTimelineClockSnapshot BuildRecordingClockSnapshot(
		const UIntentRecordingSession& Session) const;
	FIntentReplayTimelineClockSnapshot BuildPlaybackClockSnapshot(
		const UIntentReplayPlaybackSession& Session) const;
	bool IsReplayEvent(const FGameplayActionEvent& Event, FRecordedIntentId& OutRecordedIntentId) const;
	bool IsTrackEligible(const FGameplayActionEvent& Event) const;

	// Journal transaction/lifecycle routing. Accepted events are validated before they are committed;
	// later events resolve their original session through handle maps even after an async stop.
	FGameplayActionJournalResult HandleAcceptedJournalEvent(const FGameplayActionEvent& Event);
	void ProcessLifecycleEvent(const FGameplayActionEvent& Event);
	void AppendExecutionEvent(
		UIntentExecutionJournal* Journal,
		const FGameplayActionEvent& Event,
		FIntentReplayTrackId TrackId,
		FRecordedIntentId RecordedIntentId,
		FIntentReplayPlaybackSessionId PlaybackSessionId,
		const FString& DiagnosticMessage = FString());
	UIntentExecutionJournal* ResolveJournalForAcceptedEvent(
		const FGameplayActionEvent& Event,
		const FRecordedIntentId& ReplayRecordedIntentId) const;
	UIntentExecutionJournal* ResolveJournalForExistingHandle(FGameplayActionHandle Handle) const;

	// Recording state transitions and one-way publication into a transient immutable track.
	void SetRecordingState(UIntentRecordingSession& Session, EIntentRecordingState NewState);
	void FinalizeRecordingSession(UIntentRecordingSession& Session);
	void FailRecordingSession(UIntentRecordingSession& Session, const FIntentReplayFailure& Failure);
	void TryFinalizeDrainingSession(UIntentRecordingSession& Session);

	// Playback preparation, compatibility rebuilding, absolute-time scheduling, and termination.
	void SetPlaybackState(UIntentReplayPlaybackSession& Session, EIntentReplayPlaybackState NewState);
	void HandleReplayAssetsLoaded(FIntentReplayPlaybackSessionId ExpectedSessionId);
	bool BuildPreparedReplayRequests(UIntentReplayPlaybackSession& Session, FIntentReplayFailure& OutFailure);
	bool BuildPreparedRequest(
		const FRecordedIntent& RecordedIntent,
		EIntentReplayCompatibilityPolicy CompatibilityPolicy,
		FGameplayActionRequest& OutRequest,
		FIntentReplayCompatibilityReport& OutReport,
		FIntentReplayFailure& OutFailure) const;
	UGameplayActionDefinition* ResolveDefinition(const FRecordedIntent& RecordedIntent) const;
	void ScheduleNextReplayEntry();
	void SubmitDueReplayEntries();
	void HandleReplayTimelineDurationElapsed();
	void MarkAllEntriesSubmitted(UIntentReplayPlaybackSession& Session);
	void TryCompleteReplay(UIntentReplayPlaybackSession& Session);
	void CompleteReplay(UIntentReplayPlaybackSession& Session);
	void FailReplay(UIntentReplayPlaybackSession& Session, const FIntentReplayFailure& Failure);
	void CancelReplayOwnedActions(UIntentReplayPlaybackSession& Session);
	void ClearReplayScheduling();

	// Structured failure/debug helpers centralize observability and keep strings non-authoritative.
	FIntentReplayOperationResult MakeOperationFailure(
		EIntentReplayOperationStatus Status,
		FGameplayTag ReasonTag,
		FString DiagnosticMessage,
		FRecordedIntentId RecordedIntentId = FRecordedIntentId()) const;
	FIntentReplayFailure MakeFailure(
		FGameplayTag ReasonTag,
		FString DiagnosticMessage,
		FRecordedIntentId RecordedIntentId = FRecordedIntentId()) const;
	void RecordDiagnostic(FString Diagnostic);
	bool IsDetailedDebugEnabled() const;
	void ShutdownIntentReplay();

	/** Authoritative same-Actor GameplayActions endpoint; transient and GC-tracked. */
	UPROPERTY(Transient)
	TObjectPtr<UGameplayActionComponent> BoundActionComponent;

	/** Per-component policy instances created during initialization. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayTimeSource> TimeSource;

	UPROPERTY(Transient)
	TObjectPtr<UIntentRecordabilityPolicy> RecordabilityPolicy;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayExecutionStrategy> ExecutionStrategy;

	/** Diagnostic destination when no recording/playback session owns an observed event. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentExecutionJournal> AmbientExecutionJournal;

	/** Mutable recorder while non-terminal; cleared at publication/cancel/failure. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentRecordingSession> ActiveRecordingSession;

	/** Terminal recorder retained for journal and state inspection. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentRecordingSession> LastRecordingSession;

	/** Convenience reference only; external reset coordinators must retain their own UPROPERTY. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayTrack> LastFinalizedTrack;

	/** Most recently prepared recipient-local playback, including its terminal state. */
	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayPlaybackSession> ActivePlaybackSession;

	/** Routes late lifecycle events to the journal selected when their handle was Accepted. */
	UPROPERTY(Transient)
	TMap<FGameplayActionHandle, TObjectPtr<UIntentExecutionJournal>> JournalByHandle;

	/** Routes late Ended events to the recording that owns their optional original result. */
	UPROPERTY(Transient)
	TMap<FGameplayActionHandle, TObjectPtr<UIntentRecordingSession>> RecordingSessionByHandle;

	// Non-UObject runtime primitives. Every asynchronous resource is cancelled by ShutdownIntentReplay.
	TSet<FGameplayActionHandle> SinkEventsAwaitingObserver;
	TSharedPtr<FStreamableHandle> PendingDefinitionLoadHandle;
	FTimerHandle PlaybackTimerHandle;
	bool bInitialized = false;
	bool bJournalRegistered = false;
	bool bProcessingJournalEvent = false;
	bool bShuttingDown = false;
	bool bStoppingPlayback = false;
	FString LastDiagnostic;
	FIntentReplayTimelineLifecycleNativeDelegate TimelineLifecycleChangedNative;
};
