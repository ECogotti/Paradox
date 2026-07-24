#pragma once

#include "CoreMinimal.h"
#include "Perception/ParadoxTemporalVisionTypes.h"
#include "Types/EntityRelationTypes.h"
#include "Types/IntentReplayTypes.h"
#include "ParadoxTimeLoopTypes.generated.h"

class AParadoxCharacter;
class AParadoxChronoSpawn;
class UIntentReplayTrack;

/** Authoritative phase of the Paradox time-loop coordinator. */
UENUM(BlueprintType)
enum class EParadoxTimeLoopPhase : uint8
{
	Disabled,
	LevelPreparation,
	ChronoSpawnSelection,
	RunPreparation,
	ActiveRun,
	RewindPreparation,
	WorldReset,
	TimelineReconstruction,
	AwaitingSynchronizedStart,
	ParadoxFailure,
	GameOver,
	LevelComplete,
	Error
};

/** Runtime presentation and availability state of one Chrono Spawn. */
UENUM(BlueprintType)
enum class EParadoxChronoSpawnState : uint8
{
	Available,
	Hovered,
	Selected,
	Occupied,
	Disabled
};

/** Temporal role assigned by the loop authority to one Paradox avatar. */
UENUM(BlueprintType)
enum class EParadoxTemporalEntityRole : uint8
{
	Unassigned,
	Player,
	Clone
};

/** Project-level runtime state kept separate from the immutable replay track. */
UENUM(BlueprintType)
enum class EParadoxClonePlaybackState : uint8
{
	Unprepared,
	Preparing,
	Ready,
	Playing,
	Completed,
	Failed,
	Stopped
};

/** Machine-readable outcome of one time-loop operation. */
UENUM(BlueprintType)
enum class EParadoxTimeLoopOperationStatus : uint8
{
	Succeeded,
	RejectedDisabled,
	RejectedInvalidPhase,
	InvalidConfiguration,
	InvalidChronoSpawn,
	MissingPlayer,
	MissingComponent,
	RecordingFailed,
	NoFutureSpawn,
	CameraConfigurationFailed,
	SynchronizedStartFailed,
	WorldStateFailed,
	CloneSpawnFailed,
	PlaybackFailed,
	TemporalDetectionFailed,
	ParadoxAccepted,
	ParadoxRecoveryFailed,
	GameOverReached,
	LevelCompleteReached,
	RestartRequested,
	InternalFailure
};

/** Why a physical temporal-vision overlap did not become an accepted paradox. */
UENUM(BlueprintType)
enum class EParadoxTemporalCandidateDisposition : uint8
{
	Unevaluated,
	IgnoredInactivePhase,
	IgnoredStaleSession,
	IgnoredSelf,
	IgnoredNonTemporalActor,
	IgnoredInvalidTemporalIndex,
	RelationQueryFailed,
	SafeTemporalOrder,
	ParadoxAccepted
};

/** Blueprint-safe evaluation copy for one actor-level physical mesh overlap. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxTemporalCandidateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	FParadoxTemporalOverlapSnapshot PhysicalOverlap;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	EParadoxTemporalCandidateDisposition Disposition =
		EParadoxTemporalCandidateDisposition::Unevaluated;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	EEntityRelationQueryStatus RelationStatus =
		EEntityRelationQueryStatus::EvaluationFailed;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	EEntityRelationDecision RelationDecision =
		EEntityRelationDecision::NoOpinion;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	FGameplayTagContainer OutcomeTags;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	FEntityRelationResult RelationResult;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	FString DiagnosticMessage;
};

/** Immutable value context supplied to presentation when one paradox is accepted. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	FGuid EventId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	TObjectPtr<AParadoxCharacter> Observer = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	TObjectPtr<UPrimitiveComponent> ObserverComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	TObjectPtr<AParadoxCharacter> Target = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	TObjectPtr<UPrimitiveComponent> TargetComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	int32 ObserverTemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	int32 TargetTemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	int32 CurrentGeneration = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	int32 DetectionSessionId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	FGameplayTag Cause;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	FEntityRelationResult RelationResult;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	FVector ObserverLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Failure")
	FString DiagnosticMessage;

	bool IsValid() const
	{
		return EventId.IsValid()
			&& Observer != nullptr
			&& ObserverComponent != nullptr
			&& Target != nullptr
			&& TargetComponent != nullptr
			&& ObserverTemporalIndex >= 0
			&& TargetTemporalIndex >= 0;
	}
};

/** Terminal context emitted after the final playable run is consolidated. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxGameOverContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Game Over")
	FGuid EventId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Game Over")
	int32 FinalTemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Game Over")
	int32 ConsolidatedTimelineCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Game Over")
	int32 MaximumTimelineCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Game Over")
	FString DiagnosticMessage;
};

/** Terminal context emitted when an external puzzle authority completes the level. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxLevelCompleteContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Level Complete")
	FGuid EventId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Level Complete")
	int32 CurrentTemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Level Complete")
	int32 ConsolidatedTimelineCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Level Complete")
	FString DiagnosticMessage;
};

/** Structured result returned by all public loop commands. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxTimeLoopOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop")
	EParadoxTimeLoopOperationStatus Status = EParadoxTimeLoopOperationStatus::InternalFailure;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop")
	EParadoxTimeLoopPhase Phase = EParadoxTimeLoopPhase::Disabled;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop")
	FString DiagnosticMessage;

	bool IsSuccess() const { return Status == EParadoxTimeLoopOperationStatus::Succeeded; }
};

/**
 * Immutable coordinator-owned record of one consolidated run.
 *
 * The reflected Track reference is required because Intent Replay tracks live in the transient
 * package and otherwise could be garbage-collected while the world is reset.
 */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxConsolidatedTimeline
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop")
	int32 TemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop")
	TObjectPtr<AParadoxChronoSpawn> ChronoSpawn = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop")
	TObjectPtr<UIntentReplayTrack> ReplayTrack = nullptr;

	bool IsValid() const;
};

/** Read-only value snapshot for one reconstructed clone's recipient-local replay session. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxClonePlaybackSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	TObjectPtr<AParadoxCharacter> Clone = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	int32 TemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	EParadoxClonePlaybackState State = EParadoxClonePlaybackState::Unprepared;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	FIntentReplayPlaybackSessionId SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	int32 ProcessedEntryCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	int32 TotalEntryCount = 0;
};

/** Structured diagnostic retained when one clone falls back to a stationary temporal entity. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxClonePlaybackFailure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	TObjectPtr<AParadoxCharacter> Clone = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	int32 TemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	FIntentReplayPlaybackSessionId SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	EIntentReplayPlaybackState ExecutorState = EIntentReplayPlaybackState::Created;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	FRecordedIntentId RecordedIntentId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	int32 TrackEntryIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	FGameplayTag ActionTag;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	FGameplayTag ReasonTag;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	FString DiagnosticMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	FVector CloneWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback")
	bool bHasIntendedDestination = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Time Loop|Playback", meta = (EditCondition = "bHasIntendedDestination"))
	FVector IntendedDestination = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxTimeLoopPhaseChangedEvent,
	EParadoxTimeLoopPhase, PreviousPhase,
	EParadoxTimeLoopPhase, NewPhase);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxTimeLoopOperationEvent,
	FParadoxTimeLoopOperationResult, Result);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxChronoSpawnEvent,
	AParadoxChronoSpawn*, ChronoSpawn);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxCloneReconstructedEvent,
	AParadoxCharacter*, Clone,
	int32, TemporalIndex);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxClonePlaybackEvent,
	const FParadoxClonePlaybackSnapshot&, Snapshot);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxClonePlaybackFailureEvent,
	const FParadoxClonePlaybackFailure&, Failure);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxTemporalCandidateEvent,
	const FParadoxTemporalCandidateSnapshot&, Snapshot);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxAcceptedEvent,
	const FParadoxContext&, Context);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxGameOverEvent,
	const FParadoxGameOverContext&, Context);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxLevelCompleteEvent,
	const FParadoxLevelCompleteContext&, Context);
