#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/IntentReplayTypes.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "IntentReplayPerceptionTypes.generated.h"

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationTrackId
{
	GENERATED_BODY()
	FIntentReplayObservationTrackId() = default;
	explicit FIntentReplayObservationTrackId(const FGuid& InValue) : Value(InValue) {}
	static FIntentReplayObservationTrackId NewId() { return FIntentReplayObservationTrackId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	friend bool operator==(const FIntentReplayObservationTrackId& Left, const FIntentReplayObservationTrackId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FIntentReplayObservationTrackId& Left, const FIntentReplayObservationTrackId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FIntentReplayObservationTrackId& Id) { return GetTypeHash(Id.Value); }
private:
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay Perception", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FRecordedObservationId
{
	GENERATED_BODY()
	FRecordedObservationId() = default;
	explicit FRecordedObservationId(const FGuid& InValue) : Value(InValue) {}
	static FRecordedObservationId NewId() { return FRecordedObservationId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	friend bool operator==(const FRecordedObservationId& Left, const FRecordedObservationId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FRecordedObservationId& Left, const FRecordedObservationId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FRecordedObservationId& Id) { return GetTypeHash(Id.Value); }
private:
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay Perception", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationRecordingSessionId
{
	GENERATED_BODY()
	FIntentReplayObservationRecordingSessionId() = default;
	explicit FIntentReplayObservationRecordingSessionId(const FGuid& InValue) : Value(InValue) {}
	static FIntentReplayObservationRecordingSessionId NewId() { return FIntentReplayObservationRecordingSessionId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	friend bool operator==(const FIntentReplayObservationRecordingSessionId& Left, const FIntentReplayObservationRecordingSessionId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FIntentReplayObservationRecordingSessionId& Left, const FIntentReplayObservationRecordingSessionId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FIntentReplayObservationRecordingSessionId& Id) { return GetTypeHash(Id.Value); }
private:
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay Perception", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationComparisonSessionId
{
	GENERATED_BODY()
	FIntentReplayObservationComparisonSessionId() = default;
	explicit FIntentReplayObservationComparisonSessionId(const FGuid& InValue) : Value(InValue) {}
	static FIntentReplayObservationComparisonSessionId NewId() { return FIntentReplayObservationComparisonSessionId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	friend bool operator==(const FIntentReplayObservationComparisonSessionId& Left, const FIntentReplayObservationComparisonSessionId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FIntentReplayObservationComparisonSessionId& Left, const FIntentReplayObservationComparisonSessionId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FIntentReplayObservationComparisonSessionId& Id) { return GetTypeHash(Id.Value); }
private:
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay Perception", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationJournalId
{
	GENERATED_BODY()
	FIntentReplayObservationJournalId() = default;
	explicit FIntentReplayObservationJournalId(const FGuid& InValue) : Value(InValue) {}
	static FIntentReplayObservationJournalId NewId() { return FIntentReplayObservationJournalId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	friend bool operator==(const FIntentReplayObservationJournalId& Left, const FIntentReplayObservationJournalId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FIntentReplayObservationJournalId& Left, const FIntentReplayObservationJournalId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FIntentReplayObservationJournalId& Id) { return GetTypeHash(Id.Value); }
private:
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay Perception", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationJournalEntryId
{
	GENERATED_BODY()
	FIntentReplayObservationJournalEntryId() = default;
	explicit FIntentReplayObservationJournalEntryId(const FGuid& InValue) : Value(InValue) {}
	static FIntentReplayObservationJournalEntryId NewId() { return FIntentReplayObservationJournalEntryId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	friend bool operator==(const FIntentReplayObservationJournalEntryId& Left, const FIntentReplayObservationJournalEntryId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FIntentReplayObservationJournalEntryId& Left, const FIntentReplayObservationJournalEntryId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FIntentReplayObservationJournalEntryId& Id) { return GetTypeHash(Id.Value); }
private:
	UPROPERTY(VisibleAnywhere, Category = "Intent Replay Perception", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

UENUM(BlueprintType)
enum class EIntentReplayRecordedObservationType : uint8
{
	State,
	Event
};

UENUM(BlueprintType)
enum class EIntentReplayObservationRecordingState : uint8
{
	Created,
	Recording,
	Draining,
	Finalized,
	Failed,
	Cancelled
};

UENUM(BlueprintType)
enum class EIntentReplayObservationComparisonState : uint8
{
	Created,
	Comparing,
	Paused,
	Completed,
	Failed,
	Cancelled
};

UENUM(BlueprintType)
enum class EIntentReplayObservationOperationStatus : uint8
{
	Succeeded,
	NotInitialized,
	MissingIntentReplaySource,
	MissingPerceptionListener,
	InvalidArgument,
	InvalidState,
	NoSynchronizedRecording,
	MissingAuthoritativeClock,
	LateJoinRejected,
	PolicyFiltered,
	DuplicateRuntimeCallback,
	InvalidObservation,
	CapacityExceeded,
	TrackInvalid,
	BundleInvalid,
	ComparisonUnavailable,
	WrongThread,
	ShuttingDown,
	InternalFailure
};

UENUM(BlueprintType)
enum class EIntentReplayObservationMatchResult : uint8
{
	Matched,
	UnexpectedObservation,
	UnexpectedStateValue,
	UnexpectedStateStatus,
	Duplicate,
	Ambiguous,
	IgnoredByPolicy,
	IgnoredWhilePaused,
	ComparisonUnavailable,
	ExpectedRecordPending,
	ExpectedRecordExpiredUnobserved
};

UENUM(BlueprintType)
enum class EIntentReplayObservationMismatchReason : uint8
{
	None,
	NoCandidateInTimeWindow,
	EntityMismatch,
	StateTagMismatch,
	StateValueMismatch,
	StateStatusMismatch,
	EventTagMismatch,
	SenseMismatch,
	SourceMismatch,
	InstigatorMismatch,
	LocationOutsideTolerance,
	StatePositionOutsideTolerance,
	ConfidenceOutsideTolerance,
	StrengthOutsideTolerance,
	LoudnessOutsideTolerance,
	CauseTagMismatch,
	CausalCorrelationMismatch,
	AllCandidatesAlreadyConsumed,
	AmbiguousBestCandidate,
	UnsupportedComparison,
	InvalidCurrentObservation,
	InvalidRecordedObservation,
	PolicyFiltered,
	Paused
};

UENUM(BlueprintType)
enum class EIntentReplayObservationJustification : uint8
{
	None,
	ObserverCaused,
	CorrelatedReplayIntent
};

UENUM(BlueprintType)
enum class EIntentReplayObservationCorrelationReliability : uint8
{
	None,
	Explicit,
	Verified
};

/** Optional causal metadata layered around a PerceptionKnowledge observation. */
USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationCorrelation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Correlation")
	FRecordedIntentId CausalRecordedIntentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Correlation")
	FGuid ExternalCorrelationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Correlation")
	FGameplayTag OriginTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Correlation")
	EIntentReplayObservationJustification Justification =
		EIntentReplayObservationJustification::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Correlation")
	EIntentReplayObservationCorrelationReliability Reliability =
		EIntentReplayObservationCorrelationReliability::None;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayRecordedStateObservation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FRecordedObservationId RecordedObservationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FPerceptionKnowledgeEntityId EntityId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FGameplayTag StateTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FPerceptionKnowledgeValue Value;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	EPerceptionKnowledgeFactStatus Status = EPerceptionKnowledgeFactStatus::Known;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FGameplayTag SenseTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	float Confidence = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FVector ObservationLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	double SourceWorldTimestamp = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track", meta = (Units = "s"))
	double RelativeTimestamp = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	int64 TimelineSequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FIntentReplayObservationCorrelation Correlation;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayRecordedEventObservation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FRecordedObservationId RecordedObservationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FGuid SourceObservationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FGameplayTag EventTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FGameplayTag SenseTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FPerceptionKnowledgeEntityId SourceEntityId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FPerceptionKnowledgeEntityId InstigatorEntityId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	float Loudness = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	float Strength = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	float Confidence = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	double SourceWorldTimestamp = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FGameplayTag CauseTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track", meta = (Units = "s"))
	double RelativeTimestamp = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	int64 TimelineSequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FIntentReplayObservationCorrelation Correlation;
};

/** Explicit discriminated immutable recorded payload. */
USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayRecordedObservation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	EIntentReplayRecordedObservationType Type = EIntentReplayRecordedObservationType::State;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FIntentReplayRecordedStateObservation State;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Track")
	FIntentReplayRecordedEventObservation Event;

	FRecordedObservationId GetRecordedObservationId() const
	{
		return Type == EIntentReplayRecordedObservationType::State
			? State.RecordedObservationId
			: Event.RecordedObservationId;
	}

	double GetRelativeTimestamp() const
	{
		return Type == EIntentReplayRecordedObservationType::State
			? State.RelativeTimestamp
			: Event.RelativeTimestamp;
	}

	int64 GetTimelineSequence() const
	{
		return Type == EIntentReplayRecordedObservationType::State
			? State.TimelineSequence
			: Event.TimelineSequence;
	}
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationRecordOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	FGameplayTagQuery StateTagQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	FGameplayTagQuery EventTagQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	FGameplayTagQuery SenseTagQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	FGameplayTagQuery CauseTagQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	bool bRecordUnknown = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	bool bRecordInvalidated = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	bool bRecordReacquisition = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording")
	bool bRequireEventSourceIdentity = true;

	/** Zero is unlimited; capacity failure never silently truncates an authoritative track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Recording", meta = (ClampMin = "0"))
	int32 MaxRecordedObservations = 0;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationJournalOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Journal", meta = (ClampMin = "1"))
	int32 MaxEntries = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Journal")
	bool bBounded = true;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationMatchOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "s"))
	double StateEarlyTolerance = 0.25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "s"))
	double StateLateTolerance = 0.25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "s"))
	double EventEarlyTolerance = 0.25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "s"))
	double EventLateTolerance = 0.25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "s"))
	double HearingEarlyTolerance = 0.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "s"))
	double HearingLateTolerance = 0.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "cm"))
	double HearingLocationTolerance = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "cm"))
	double EventLocationTolerance = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0"))
	double FloatTolerance = 0.0001;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "cm"))
	double VectorTolerance = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0"))
	double StrengthTolerance = 0.1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0"))
	double LoudnessTolerance = 0.1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bCompareNonHearingEventLocation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bCompareStatePosition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0", Units = "cm"))
	double StateLocationTolerance = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bCompareStateConfidence = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (ClampMin = "0.0"))
	double StateConfidenceTolerance = 0.01;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bStrictPersistentIdentity = true;

	/**
	 * Matches State observations with the same persistent Entity ID, State Tag, and Sense as an
	 * ordered snapshot stream. Expected snapshots remain pending until consumed or comparison
	 * completion, so a late Sight reacquisition still compares status and value instead of
	 * becoming a generic no-candidate observation. Requires strict persistent identity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching", meta = (EditCondition = "bStrictPersistentIdentity"))
	bool bTreatPersistentStateObservationsAsOrderedSnapshots = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bCompareEventInstigatorWhenBothValid = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bReportEquivalentBestCandidatesAsAmbiguous = true;

	/**
	 * Treats an exact, verified Recorded Intent correlation as the identity of one event
	 * occurrence. Matching remains strict on source Entity ID, Event Tag, Sense, cause,
	 * instigator, and one-record/one-observation consumption, but tolerates replay-induced
	 * time, location, strength, and loudness drift. Correlated expected events remain pending
	 * until consumed or comparison completion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bTreatVerifiedCausalEventsAsOccurrenceIdentity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bAllowLateJoin = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	bool bJournalIgnoredWhilePaused = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Matching")
	FIntentReplayObservationJournalOptions JournalOptions;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationOperationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Result")
	EIntentReplayObservationOperationStatus Status =
		EIntentReplayObservationOperationStatus::InternalFailure;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Result")
	FString DiagnosticMessage;

	bool Succeeded() const
	{
		return Status == EIntentReplayObservationOperationStatus::Succeeded
			|| Status == EIntentReplayObservationOperationStatus::PolicyFiltered
			|| Status == EIntentReplayObservationOperationStatus::DuplicateRuntimeCallback;
	}
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationTrackValidationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Validation")
	bool bValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Validation")
	int32 InvalidEntryIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Validation")
	FString DiagnosticMessage;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayTimelineBundleValidationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Validation")
	bool bValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Validation")
	FString DiagnosticMessage;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationJournalEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	FIntentReplayObservationJournalEntryId JournalEntryId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	FPerceptionKnowledgeObservation CurrentObservation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	FIntentReplayObservationCorrelation CurrentCorrelation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	bool bHasExpectedObservation = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	FIntentReplayRecordedObservation ExpectedObservation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	EIntentReplayObservationMatchResult Result =
		EIntentReplayObservationMatchResult::ComparisonUnavailable;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	EIntentReplayObservationMismatchReason Reason =
		EIntentReplayObservationMismatchReason::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal", meta = (Units = "s"))
	double CurrentRelativeTime = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal", meta = (Units = "s"))
	double TimeDelta = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	bool bConsumedExpectedRecord = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int64 JournalSequence = 0;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationComparisonSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int32 Compared = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int32 Matched = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int32 Unexpected = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int32 Ambiguous = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int32 Duplicate = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int32 Ignored = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int32 ExpiredUnobserved = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Journal")
	int32 PendingExpected = 0;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationComparisonEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationComparisonSessionId ComparisonSessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Events")
	FIntentReplayPlaybackSessionId PlaybackSessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationTrackId ObservationTrackId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationJournalId JournalId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Events")
	FIntentReplayObservationJournalEntry Entry;
};

UENUM(BlueprintType)
enum class EIntentReplayObservationDebugStatus : uint8
{
	Inactive,
	Pending,
	Consumed,
	Matched,
	Unexpected,
	Ambiguous,
	Justified
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationDebugFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	bool bDrawStates = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	bool bDrawEvents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	bool bDrawMatched = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	bool bDrawUnexpected = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	bool bDrawAmbiguous = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	bool bDrawPending = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	bool bDrawConsumed = true;

	/** Invalid means all entities owned by this observer/session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug")
	FPerceptionKnowledgeEntityId SelectedEntityId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDrawDistance = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intent Replay Perception|Debug", meta = (ClampMin = "0", ClampMax = "2"))
	int32 TextDetailLevel = 1;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationDebugEntityFrame
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FPerceptionKnowledgeEntityId EntityId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FString ActorName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FVector BoundsOrigin = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FVector BoundsExtent = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	EIntentReplayObservationDebugStatus Status =
		EIntentReplayObservationDebugStatus::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FColor Color = FColor::Silver;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FString Label;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayObservationDebugFrame
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	bool bShouldDraw = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	bool bExpensiveDataBuilt = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	bool bHasViewpoint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FVector ViewLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FVector ViewDirection = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FIntentReplayTimelineClockSnapshot Clock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FIntentReplayTrackId ActionTrackId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FIntentReplayObservationTrackId ObservationTrackId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FIntentReplayObservationJournalId JournalId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	double HearingLocationTolerance = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	EIntentReplayObservationComparisonState ComparisonState =
		EIntentReplayObservationComparisonState::Created;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	FIntentReplayObservationComparisonSummary Summary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	TArray<FIntentReplayObservationDebugEntityFrame> Entities;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	TArray<FIntentReplayObservationJournalEntry> RecentEventEntries;
};

USTRUCT(BlueprintType)
struct INTENTREPLAYPERCEPTION_API FIntentReplayPerceptionRuntimeStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	int64 RecordedObservations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	int64 ComparedObservations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	int64 DuplicateObservations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	int64 MatchedObservations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	int64 UnexpectedObservations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	int64 AmbiguousObservations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	int64 ExpiredExpectedObservations = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	int64 DebugFramesBuilt = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intent Replay Perception|Debug")
	double LastDebugBuildMilliseconds = 0.0;
};
