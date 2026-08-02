#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/PropertyBag.h"
#include "Types/GameplayActionTypes.h"
#include "IntentReplayTypes.generated.h"

class UGameplayActionDefinition;
class UIntentReplayTrack;

/** Stable identity of one finalized replay track. A new recording always receives a new ID. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayTrackId
{
	GENERATED_BODY()

	FIntentReplayTrackId() = default;
	explicit FIntentReplayTrackId(const FGuid& InValue) : Value(InValue) {}

	static FIntentReplayTrackId NewId() { return FIntentReplayTrackId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }

	friend bool operator==(const FIntentReplayTrackId& Left, const FIntentReplayTrackId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FIntentReplayTrackId& Left, const FIntentReplayTrackId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FIntentReplayTrackId& Id) { return GetTypeHash(Id.Value); }

private:
	/** Wrapped to prevent Blueprint callers from replacing an ID after it has been issued. */
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

/** Stable identity of one mutable recording attempt, distinct from the track it may publish. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentRecordingSessionId
{
	GENERATED_BODY()

	FIntentRecordingSessionId() = default;
	explicit FIntentRecordingSessionId(const FGuid& InValue) : Value(InValue) {}

	static FIntentRecordingSessionId NewId() { return FIntentRecordingSessionId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }

	friend bool operator==(const FIntentRecordingSessionId& Left, const FIntentRecordingSessionId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FIntentRecordingSessionId& Left, const FIntentRecordingSessionId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FIntentRecordingSessionId& Id) { return GetTypeHash(Id.Value); }

private:
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

/** Stable identity of one accepted action snapshot inside a track. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FRecordedIntentId
{
	GENERATED_BODY()

	FRecordedIntentId() = default;
	explicit FRecordedIntentId(const FGuid& InValue) : Value(InValue) {}

	static FRecordedIntentId NewId() { return FRecordedIntentId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }

	friend bool operator==(const FRecordedIntentId& Left, const FRecordedIntentId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FRecordedIntentId& Left, const FRecordedIntentId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FRecordedIntentId& Id) { return GetTypeHash(Id.Value); }

private:
	/** Wrapped to keep correlation identity read-only outside IntentReplay. */
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

/** Identity of one recipient-local playback session. Sessions never share runtime state. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayPlaybackSessionId
{
	GENERATED_BODY()

	FIntentReplayPlaybackSessionId() = default;
	explicit FIntentReplayPlaybackSessionId(const FGuid& InValue) : Value(InValue) {}

	static FIntentReplayPlaybackSessionId NewId() { return FIntentReplayPlaybackSessionId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }

	friend bool operator==(const FIntentReplayPlaybackSessionId& Left, const FIntentReplayPlaybackSessionId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FIntentReplayPlaybackSessionId& Left, const FIntentReplayPlaybackSessionId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FIntentReplayPlaybackSessionId& Id) { return GetTypeHash(Id.Value); }

private:
	/** Wrapped to prevent callers from confusing two independent playback runs. */
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

/** Lifecycle of the mutable recording session that produces one immutable track. */
UENUM(BlueprintType)
enum class EIntentRecordingState : uint8
{
	Created UMETA(ToolTip = "The session object exists but has not started accepting action snapshots."),
	Recording UMETA(ToolTip = "Accepted and eligible GameplayActions are appended to the current track."),
	Paused UMETA(ToolTip = "No new track entries are accepted and recording time is suspended."),
	Draining UMETA(ToolTip = "The asynchronous stop has closed the track to new entries and is waiting for already tracked actions to end."),
	Finalized UMETA(ToolTip = "The immutable track has been created and On Recording Finalized has fired."),
	Failed UMETA(ToolTip = "Recording could not preserve its invariants; inspect On Recording Failed and the structured failure."),
	Cancelled UMETA(ToolTip = "Recording was discarded intentionally and no finalized track was produced.")
};

/** Lifecycle of the recipient-local session that prepares and replays a finalized track. */
UENUM(BlueprintType)
enum class EIntentReplayPlaybackState : uint8
{
	Created UMETA(ToolTip = "The session exists but its replay requests have not been prepared."),
	Preparing UMETA(ToolTip = "Definition assets are loading asynchronously; wait for On Replay Prepared."),
	Ready UMETA(ToolTip = "All requests passed validation and Start Replay may be called."),
	Playing UMETA(ToolTip = "Entries are being submitted according to their recorded absolute timestamps."),
	Paused UMETA(ToolTip = "Timeline scheduling is suspended; session-owned actions are paused only when the playback option requests it."),
	Stopping UMETA(ToolTip = "Scheduling has stopped and only actions owned by this playback session are being cancelled."),
	Completed UMETA(ToolTip = "Every entry was processed and every action owned by this session reached a terminal state."),
	Failed UMETA(ToolTip = "Playback terminated because its configured failure policy required a stop."),
	Cancelled UMETA(ToolTip = "Playback was stopped intentionally before normal completion.")
};

/** Selects the authoritative IntentReplay timeline represented by a generic snapshot. */
UENUM(BlueprintType)
enum class EIntentReplayTimelineDomain : uint8
{
	Recording,
	Playback
};

/** Result of atomically sampling a synchronized timeline and allocating its next sequence. */
UENUM(BlueprintType)
enum class EIntentReplayTimelineCaptureStatus : uint8
{
	Succeeded,
	NoActiveSession,
	SessionMismatch,
	NotAccepting,
	Paused,
	WrongThread,
	ShuttingDown
};

/** Behavior applied to Accepted journal events while no recording session is active. */
UENUM(BlueprintType)
enum class EIntentNoRecordingSessionPolicy : uint8
{
	JournalOnly UMETA(ToolTip = "Accept the GameplayAction and retain its lifecycle only in the ambient Execution Journal."),
	RejectAcceptedActions UMETA(ToolTip = "Reject Accepted events through the GameplayActions journal transaction when no recorder can store them.")
};

/**
 * Determines when RequestStopRecording publishes its immutable track.
 *
 * Immediate is the safe default for synchronous Blueprint flows. AsyncStop is opt-in when the
 * caller needs original terminal results and can continue from OnRecordingFinalized.
 */
UENUM(BlueprintType)
enum class EIntentRecordingFinalizeMode : uint8
{
	Immediate = 1 UMETA(
		DisplayName = "Immediate",
		ToolTip = "Finalize synchronously before Request Stop Recording returns. Accepted entries are preserved, but actions still running have no Original Result."),

	AsyncStop = 0 UMETA(
		DisplayName = "Async Stop (Wait for Tracked Actions)",
		ToolTip = "Stop accepting new entries immediately, wait without blocking the Game Thread for tracked actions to end, then fire On Recording Finalized. Do not read Last Finalized Track until that event."),

	DrainTrackedActions = 2 UMETA(
		Hidden,
		Deprecated,
		DisplayName = "Drain Tracked Actions (Deprecated)",
		ToolTip = "Deprecated alias retained for existing Blueprint assets. Use Async Stop instead.")
};

/** Schema migration policy used when the current Definition differs from the recorded Definition. */
UENUM(BlueprintType)
enum class EIntentReplayCompatibilityPolicy : uint8
{
	StrictRecordedSchema UMETA(ToolTip = "Reject replay unless recorded schema and critical execution configuration exactly match the current Definition."),
	CopyCompatibleValuesUseCurrentDefaults UMETA(ToolTip = "Copy only fields with exactly matching names and types; fields added by the current Definition retain current defaults.")
};

/** Policy for a replay request rejected before a runtime action handle is created. */
UENUM(BlueprintType)
enum class EIntentReplaySubmissionFailurePolicy : uint8
{
	StopPlayback UMETA(ToolTip = "Fail the playback session immediately and cancel only its owned actions."),
	SkipFailedEntry UMETA(ToolTip = "Record the rejected submission in the journal and continue scheduling later entries.")
};

/** Policy for an action that was submitted successfully but later ends without success. */
UENUM(BlueprintType)
enum class EIntentReplayTerminalFailurePolicy : uint8
{
	ContinueTimeline UMETA(ToolTip = "Record the divergence and continue the recorded timeline."),
	StopPlayback UMETA(ToolTip = "Fail playback when any action owned by the session ends without success.")
};

/** Storage policy for diagnostic lifecycle events. Journals never define replay track contents. */
UENUM(BlueprintType)
enum class EIntentExecutionJournalCapacityPolicy : uint8
{
	Disabled UMETA(ToolTip = "Do not retain Execution Journal events."),
	BoundedRingBuffer UMETA(ToolTip = "Retain only the newest Max Entries events, removing the oldest entries as capacity is reached."),
	UnboundedForCurrentSession UMETA(ToolTip = "Retain every event for this transient session; memory usage grows until the journal is cleared or collected.")
};

/** Structured reason returned when a Property Bag cannot be copied safely into an immutable track. */
UENUM(BlueprintType)
enum class EIntentRecordabilityStatus : uint8
{
	Recordable UMETA(ToolTip = "The full Property Bag can be deep-copied safely."),
	InvalidPropertyBag UMETA(ToolTip = "The bag, its generated struct, or its value memory is not initialized."),
	UnsupportedProperty UMETA(ToolTip = "A reflected property type is not handled by the active recordability policy."),
	UnsupportedContainer UMETA(ToolTip = "A container shape cannot be validated recursively by the active policy."),
	RuntimeObjectReference UMETA(ToolTip = "A parameter refers to an Actor or Actor Component whose world identity cannot survive reset."),
	TransientObjectReference UMETA(ToolTip = "A parameter refers to a transient UObject whose lifetime is not stable."),
	InvalidObjectReference UMETA(ToolTip = "A hard UObject reference does not identify a stable asset.")
};

/** Result category shared by synchronous component commands. */
UENUM(BlueprintType)
enum class EIntentReplayOperationStatus : uint8
{
	Succeeded UMETA(ToolTip = "The command completed or was accepted successfully."),
	NotInitialized UMETA(ToolTip = "IntentReplay has not completed initialization."),
	InvalidArgument UMETA(ToolTip = "A supplied object or value is invalid."),
	InvalidState UMETA(ToolTip = "The command is not legal in the component's current lifecycle state."),
	MissingActionComponent UMETA(ToolTip = "No valid GameplayActionComponent is bound on the same entity."),
	JournalRegistrationFailed UMETA(ToolTip = "GameplayActions rejected this component as its journal sink."),
	RecordingAlreadyActive UMETA(ToolTip = "A non-terminal recording session already exists."),
	PlaybackAlreadyActive UMETA(ToolTip = "A non-terminal playback session already exists."),
	TrackInvalid UMETA(ToolTip = "The supplied track failed immutable track validation."),
	DefinitionUnavailable UMETA(ToolTip = "A recorded GameplayAction Definition cannot be resolved or loaded."),
	CompatibilityFailure UMETA(ToolTip = "The recorded request is incompatible with its current Definition under the selected policy."),
	SubmissionFailure UMETA(ToolTip = "GameplayActions rejected a prepared replay request."),
	RejectedReentrant UMETA(ToolTip = "The command was called from inside the synchronous journal transaction and was rejected to preserve invariants."),
	PendingExternalRecovery UMETA(ToolTip = "Replay cannot resume until every externally interrupted intent is reissued or resolved as already satisfied."),
	RecordedIntentNotFound UMETA(ToolTip = "The requested recorded intent is not awaiting external recovery in this playback session."),
	InterruptionFailure UMETA(ToolTip = "GameplayActions could not interrupt one of the replay-owned actions selected for external recovery."),
	InternalFailure UMETA(ToolTip = "An unexpected internal invariant or allocation failed; inspect the structured diagnostic and LogIntentReplay.")
};

/** Immediate outcome of PrepareReplay; Preparing completes later through OnReplayPrepared. */
UENUM(BlueprintType)
enum class EIntentReplayPrepareStatus : uint8
{
	Ready UMETA(ToolTip = "Preparation finished synchronously and Start Replay may be called now."),
	Preparing UMETA(ToolTip = "Preparation was accepted and is loading assets asynchronously; wait for On Replay Prepared."),
	Rejected UMETA(ToolTip = "No usable playback session was prepared; inspect Failure.")
};

/** Terminal outcome emitted by the playback lifecycle delegates. */
UENUM(BlueprintType)
enum class EIntentReplayTerminalStatus : uint8
{
	Completed UMETA(ToolTip = "All entries and session-owned actions completed normally."),
	Failed UMETA(ToolTip = "A configured failure policy terminated playback."),
	Cancelled UMETA(ToolTip = "Stop Replay or teardown cancelled the session intentionally.")
};

/** Result of recursively checking whether one Property Bag can be isolated from world state. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentRecordabilityResult
{
	GENERATED_BODY()

	/** High-level classification; Recordable is the only successful value. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	EIntentRecordabilityStatus Status = EIntentRecordabilityStatus::Recordable;

	/** Fully qualified field/container path at which validation stopped. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FString ParameterPath;

	/** Reflected FProperty class name of the rejected field. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FString PropertyType;

	/** Human-facing explanation for logs and tools; gameplay must branch on Status. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FString DiagnosticMessage;

	bool IsRecordable() const { return Status == EIntentRecordabilityStatus::Recordable; }
};

/** Structured failure shared by synchronous results and asynchronous lifecycle events. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayFailure
{
	GENERATED_BODY()

	/** Machine-readable failure category suitable for gameplay routing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FGameplayTag ReasonTag;

	/** Developer-facing context; not a stable gameplay identifier. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FString DiagnosticMessage;

	/** Optional entry identity when failure is attributable to one recorded request. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FRecordedIntentId RecordedIntentId;
};

/** Immediate result returned by ordinary recording/playback commands. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayOperationResult
{
	GENERATED_BODY()

	/** Succeeded when the command completed or its asynchronous work was accepted. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	EIntentReplayOperationStatus Status = EIntentReplayOperationStatus::InternalFailure;

	/** Populated when Status is not Succeeded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayFailure Failure;

	bool Succeeded() const { return Status == EIntentReplayOperationStatus::Succeeded; }
};

/** StartRecording result containing the identity reserved for the new track. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentRecordingStartResult
{
	GENERATED_BODY()

	/** Synchronous acceptance/failure category. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	EIntentReplayOperationStatus Status = EIntentReplayOperationStatus::InternalFailure;

	/** Valid only when recording started successfully. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayTrackId TrackId;

	/** Identity of the mutable recording attempt that owns the new track builder. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentRecordingSessionId SessionId;

	/** Populated when Status is not Succeeded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayFailure Failure;

	bool Succeeded() const { return Status == EIntentReplayOperationStatus::Succeeded; }
};

/**
 * Immutable view of one authoritative recording or playback clock.
 *
 * Irrelevant identity/state fields remain invalid/default according to Domain. The snapshot never
 * exposes mutable session objects.
 */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayTimelineClockSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	bool bValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentReplayTimelineDomain Domain = EIntentReplayTimelineDomain::Recording;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	FIntentRecordingSessionId RecordingSessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	FIntentReplayPlaybackSessionId PlaybackSessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	FIntentReplayTrackId TrackId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline", meta = (Units = "s"))
	double RelativeTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	int64 NextTimelineSequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	bool bClockStarted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	bool bPaused = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	bool bAcceptingTimelinePoints = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentRecordingState RecordingState = EIntentRecordingState::Created;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentReplayPlaybackState PlaybackState = EIntentReplayPlaybackState::Created;
};

/** Atomic clock sample plus deterministic sequence allocated from the selected session. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayTimelinePointResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentReplayTimelineCaptureStatus Status = EIntentReplayTimelineCaptureStatus::NoActiveSession;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	FIntentReplayTimelineClockSnapshot Clock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	int64 TimelineSequence = INDEX_NONE;

	bool Succeeded() const { return Status == EIntentReplayTimelineCaptureStatus::Succeeded; }
};

/** Immutable notification emitted after one authoritative session state transition. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayTimelineLifecycleEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentReplayTimelineDomain Domain = EIntentReplayTimelineDomain::Recording;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentRecordingState PreviousRecordingState = EIntentRecordingState::Created;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentRecordingState NewRecordingState = EIntentRecordingState::Created;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentReplayPlaybackState PreviousPlaybackState = EIntentReplayPlaybackState::Created;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	EIntentReplayPlaybackState NewPlaybackState = EIntentReplayPlaybackState::Created;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Timeline")
	FIntentReplayTimelineClockSnapshot Clock;
};

/** Immediate PrepareReplay result; asynchronous completion is delivered by OnReplayPrepared. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayPrepareResult
{
	GENERATED_BODY()

	/** Ready, Preparing, or Rejected. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	EIntentReplayPrepareStatus Status = EIntentReplayPrepareStatus::Rejected;

	/** Valid for both Ready and Preparing so callbacks can reject stale sessions. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayPlaybackSessionId SessionId;

	/** Populated only when preparation was rejected. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayFailure Failure;

	bool WasAccepted() const { return Status != EIntentReplayPrepareStatus::Rejected; }
};

/** Terminal playback summary broadcast on exactly one finished delegate. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayResult
{
	GENERATED_BODY()

	/** Whether the session completed, failed, or was cancelled. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	EIntentReplayTerminalStatus Status = EIntentReplayTerminalStatus::Failed;

	/** Playback identity local to the recipient component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayPlaybackSessionId SessionId;

	/** Immutable source track shared by this playback session. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayTrackId TrackId;

	/** Number of track entries whose submission was attempted. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	int32 ProcessedEntries = 0;

	/** Entry count captured from the source track at termination. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	int32 TotalEntries = 0;

	/** Populated for Failed, and with the cancellation reason for Cancelled. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayFailure Failure;
};

/** Controls how many diagnostic Execution Journal events are retained. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentExecutionJournalOptions
{
	GENERATED_BODY()

	/** Maximum events retained by BoundedRingBuffer; ignored by the other policies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay", meta = (ClampMin = "1", EditCondition = "CapacityPolicy == EIntentExecutionJournalCapacityPolicy::BoundedRingBuffer"))
	int32 MaxEntries = 1024;

	/** Disabled, bounded, or unbounded storage for this transient journal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	EIntentExecutionJournalCapacityPolicy CapacityPolicy = EIntentExecutionJournalCapacityPolicy::BoundedRingBuffer;
};

/** Configuration copied into a fresh recording session at StartRecording. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentRecordingOptions
{
	GENERATED_BODY()

	/** Optional diagnostic label identifying the source or time-loop iteration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	FString SourceLabel;

	/** User metadata copied into the immutable track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	FGameplayTagContainer MetadataTags;

	/** Zero means unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay", meta = (ClampMin = "0"))
	int32 MaxTrackEntries = 0;

	/** Capacity policy for this recording session's diagnostic journal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	FIntentExecutionJournalOptions JournalOptions;
};

/** Recipient-local behavior selected when preparing a replay. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayPlaybackOptions
{
	GENERATED_BODY()

	/** How recorded Property Bag schema/configuration is compared with current Definitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	EIntentReplayCompatibilityPolicy CompatibilityPolicy = EIntentReplayCompatibilityPolicy::StrictRecordedSchema;

	/** Whether one rejected SubmitAction fails playback or skips only that entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	EIntentReplaySubmissionFailurePolicy SubmissionFailurePolicy = EIntentReplaySubmissionFailurePolicy::StopPlayback;

	/** Whether a submitted action ending without success stops future timeline entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	EIntentReplayTerminalFailurePolicy TerminalFailurePolicy = EIntentReplayTerminalFailurePolicy::ContinueTimeline;

	/** If true, PauseReplay also asks GameplayActions to pause handles owned by this session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	bool bPauseBoundActions = false;

	/** Capacity policy for this playback session's diagnostic journal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay")
	FIntentExecutionJournalOptions JournalOptions;
};

/** Per-entry differences discovered while rebuilding a request from the current Definition. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayCompatibilityReport
{
	GENERATED_BODY()

	/** True when the chosen policy permits this entry to replay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	bool bCompatible = true;

	/** Fields present only in the current Definition; compatible mode keeps their defaults. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	TArray<FString> AddedCurrentParameters;

	/** Fields present only in the recorded snapshot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	TArray<FString> RemovedRecordedParameters;

	/** Same-named fields whose reflected types no longer match exactly. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	TArray<FString> TypeChangedParameters;

	/** Critical execution settings that differ from the recorded request. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	TArray<FString> ConfigurationChanges;
};

/** Result of validating format, identity, ordering, timestamps, and entry invariants. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayTrackValidationResult
{
	GENERATED_BODY()

	/** True only when the track is safe to prepare. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	bool bValid = false;

	/** First invalid entry, or INDEX_NONE for track-level failures and successful validation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	int32 InvalidEntryIndex = INDEX_NONE;

	/** Human-readable validation detail. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FString DiagnosticMessage;
};

/**
 * Immutable semantic snapshot of one Accepted GameplayAction request.
 *
 * It intentionally contains no source Actor, action instance, or runtime handle. Parameters are
 * private so Blueprint can inspect them only through the type-safe wildcard accessor.
 */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FRecordedIntent
{
	GENERATED_BODY()

public:
	/** Read-only C++ view of the deep-copied Property Bag. */
	const FInstancedPropertyBag& GetParameters() const { return Parameters; }

	/** Stable entry identity reused as replay correlation, never as a runtime action handle. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FRecordedIntentId RecordedIntentId;

	/** Primary Asset identity used as the first stable Definition resolution path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FPrimaryAssetId DefinitionId;

	/** Soft fallback identity; does not force the Definition to remain loaded with the source Actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	TSoftObjectPtr<UGameplayActionDefinition> Definition;

	/** Semantic action identity captured from the accepted request. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FGameplayTag ActionTag;

	/** Priority actually accepted by GameplayActions after Definition/request resolution. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	int32 EffectivePriority = 0;

	/** Queue/reject/cancel behavior actually accepted for the original request. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	EGameplayActionBlockedPolicy EffectiveBlockedPolicy = EGameplayActionBlockedPolicy::Queue;

	/** Execution locks required by the accepted request. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FGameplayTagContainer ExecutionLocks;

	/** Whether GameplayActions allowed the original action to be interrupted. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	bool bInterruptible = true;

	/** Optional action timeout preserved independently of the original action instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FGameplayActionTimeout OptionalTimeout;

	/** Maximum time the original request was allowed to remain queued. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	double MaxQueueTimeSeconds = 0.0;

	/** Origin that produced the recording; replay replaces it with ReplayOriginTag. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FGameplayTag OriginalOriginTag;

	/** Original caller correlation retained for diagnostics, not reused for the replay request. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FGameplayActionCorrelationData OriginalCorrelation;

	/** Contiguous immutable order within the finalized track. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	int32 TrackSequence = 0;

	/** Ordering shared with synchronized external channels at the same relative timestamp. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	int64 TimelineSequence = 0;

	/** Submission order assigned by the source GameplayActionComponent. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	int64 OriginalSubmissionSequence = 0;

	/** Accepted time relative to the recording clock, excluding paused duration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay", meta = (Units = "s"))
	double RelativeAcceptedTimeSeconds = 0.0;

	/** True only when the source action reached Ended before track finalization. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	bool bHasOriginalResult = false;

	/** Source action terminal result, absent after Immediate stop when that action was still active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay", meta = (EditCondition = "bHasOriginalResult"))
	FGameplayActionResult OriginalResult;

private:
	/** Deep-copied mutable storage made private before publication as an immutable track. */
	UPROPERTY()
	FInstancedPropertyBag Parameters;

	friend class UIntentReplayComponent;
	friend class UIntentRecordingSession;
};

/**
 * Immutable recovery snapshot captured before a replay-owned action is externally interrupted.
 *
 * The snapshot contains the finalized Recorded Intent rather than a mutable runtime request or
 * action instance. Runtime state/result fields describe only the interrupted execution.
 */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplaySuspendedIntent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	FRecordedIntent RecordedIntent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	FGameplayActionHandle InterruptedRuntimeHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	EGameplayActionState InterruptedRuntimeState = EGameplayActionState::Created;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	bool bHasInterruptionResult = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery", meta = (EditCondition = "bHasInterruptionResult"))
	FGameplayActionResult InterruptionResult;
};

/** Atomic result of pausing replay and interrupting its currently owned actions for recovery. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayExternalInterruptionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	EIntentReplayOperationStatus Status = EIntentReplayOperationStatus::InternalFailure;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	FIntentReplayPlaybackSessionId SessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	TArray<FIntentReplaySuspendedIntent> SuspendedIntents;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	FIntentReplayFailure Failure;

	bool Succeeded() const { return Status == EIntentReplayOperationStatus::Succeeded; }
};

/** Result of reconciling one externally interrupted intent by submitting it again. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayRecoveryResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	EIntentReplayOperationStatus Status = EIntentReplayOperationStatus::InternalFailure;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	FRecordedIntentId RecordedIntentId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	FGameplayActionSubmissionResult SubmissionResult;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Recovery")
	FIntentReplayFailure Failure;

	bool Succeeded() const { return Status == EIntentReplayOperationStatus::Succeeded; }
};

/** One diagnostic lifecycle observation stored in an Execution Journal. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentExecutionEvent
{
	GENERATED_BODY()

	/** Observation time relative to the owning journal's clock origin. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	double ObservedRelativeTimeSeconds = 0.0;

	/** False for synthetic diagnostics such as a rejected replay submission. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	bool bHasActionEvent = false;

	/** Deep snapshot supplied by GameplayActions when bHasActionEvent is true. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FGameplayActionEvent ActionEvent;

	/** Recording or source track associated with the observation, when known. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayTrackId TrackId;

	/** Recorded entry associated with the observation, when known. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FRecordedIntentId RecordedIntentId;

	/** Recipient-local playback associated with the observation, when known. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FIntentReplayPlaybackSessionId PlaybackSessionId;

	/** Supplemental developer-facing context, especially for synthetic events. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay")
	FString DiagnosticMessage;
};

/** Point-in-time, read-only view of component/session state for Blueprint debugging tools. */
USTRUCT(BlueprintType)
struct INTENTREPLAY_API FIntentReplayDebugSnapshot
{
	GENERATED_BODY()

	/** Weak owner identity; the snapshot never extends an Actor's lifetime. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	TWeakObjectPtr<UObject> Owner;

	/** Weak bound GameplayActionComponent identity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	TWeakObjectPtr<UObject> BoundActionComponent;

	/** Whether initialization and policy creation succeeded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	bool bInitialized = false;

	/** Whether GameplayActions currently recognizes this component as its synchronous sink. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	bool bJournalRegistered = false;

	/** Policy that currently applies when Accepted arrives outside a recording session. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	EIntentNoRecordingSessionPolicy NoRecordingSessionPolicy = EIntentNoRecordingSessionPolicy::JournalOnly;

	/** Active or last terminal recording lifecycle state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	EIntentRecordingState RecordingState = EIntentRecordingState::Created;

	/** Track identity reserved by the active/last recording session. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	FIntentReplayTrackId RecordingTrackId;

	/** Accepted entry snapshots currently visible on the active/last recording session. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	int32 RecordedEntryCount = 0;

	/** Actions whose terminal result is still required by AsyncStop. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	int32 PendingDrainCount = 0;

	/** Recording-clock elapsed seconds with paused duration removed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	double RecordingElapsedSeconds = 0.0;

	/** Current state of the most recently prepared playback session. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	EIntentReplayPlaybackState PlaybackState = EIntentReplayPlaybackState::Created;

	/** Recipient-local identity of the most recently prepared playback session. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	FIntentReplayPlaybackSessionId PlaybackSessionId;

	/** Immutable source Track ID used by the playback session. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	FIntentReplayTrackId SourceTrackId;

	/** First source entry not yet processed by the scheduler. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	int32 NextEntryIndex = 0;

	/** Entries whose submission has already been attempted, accepted or rejected. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	int32 SubmittedEntryCount = 0;

	/** Total entries in the immutable source track. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	int32 TotalEntryCount = 0;

	/** Currently active/queued handles created by this playback session only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	int32 ReplayOwnedActionCount = 0;

	/** Most recent component diagnostic; informational text is not a stable gameplay contract. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay|Debug")
	FString LastDiagnostic;
};
