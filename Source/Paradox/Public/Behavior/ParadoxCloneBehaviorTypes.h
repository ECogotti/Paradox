#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/IntentReplayPerceptionTypes.h"
#include "Types/IntentReplayTypes.h"
#include "ParadoxCloneBehaviorTypes.generated.h"

class AActor;

/** Authoritative high-level behavior owned exclusively by the clone coordinator. */
UENUM(BlueprintType)
enum class EParadoxCloneBehaviorMode : uint8
{
	Replay,
	Investigating,
	Goap UMETA(DisplayName = "GOAP")
};

/** Stable operational outcomes used by coordinator, policy, and investigation commands. */
UENUM(BlueprintType)
enum class EParadoxCloneBehaviorOperationStatus : uint8
{
	Succeeded,
	Ignored,
	Replaced,
	AlreadyInState,
	NotInitialized,
	NotAuthorized,
	InvalidArgument,
	InvalidState,
	StaleContext,
	ForeignComparison,
	PolicyRejected,
	ActionRejected,
	RecoveryPending,
	ContinuityCannotBeRestored,
	TerminalGoapHandoff,
	ConfigurationError
};

/** Structured command result; diagnostics are developer-facing, never gameplay identifiers. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxCloneBehaviorOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior")
	EParadoxCloneBehaviorOperationStatus Status =
		EParadoxCloneBehaviorOperationStatus::ConfigurationError;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior")
	FName Reason;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior")
	FString DiagnosticMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior")
	int32 InvestigationRevision = 0;

	bool IsSuccess() const
	{
		return Status == EParadoxCloneBehaviorOperationStatus::Succeeded
			|| Status == EParadoxCloneBehaviorOperationStatus::Ignored
			|| Status == EParadoxCloneBehaviorOperationStatus::Replaced
			|| Status == EParadoxCloneBehaviorOperationStatus::AlreadyInState;
	}
};

/**
 * One authoritative investigation objective derived from an immutable comparison event.
 *
 * The Source Actor is deliberately weak; Entity ID and world location remain usable when the
 * observed actor disappears. Comparison and source tracks remain immutable.
 */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInvestigationContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FIntentReplayObservationComparisonEvent Comparison;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	EPerceptionKnowledgeObservationType ObservationType =
		EPerceptionKnowledgeObservationType::State;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FGameplayTag SenseTag;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FGameplayTag SemanticTag;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FGameplayTagContainer SourceCategories;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FPerceptionKnowledgeEntityId SourceEntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	TWeakObjectPtr<AActor> SourceActor;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FVector InvestigationLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	float Confidence = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FIntentReplayObservationCorrelation Correlation;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FIntentReplayPlaybackSessionId PlaybackSessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FIntentReplayObservationTrackId ObservationTrackId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FIntentReplayObservationJournalId JournalId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FIntentReplayObservationJournalEntryId JournalEntryId;

	/** Higher values replace lower-priority objectives. Equal values never oscillate. */
	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	int32 InvestigationPriority = 0;

	/** Data-driven policy rule that assigned InvestigationPriority. */
	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	FName ResponseRuleId;

	/** Monotonic authority guard; callbacks from older revisions are ignored. */
	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Investigation")
	int32 InvestigationRevision = 0;

	bool IsValid() const
	{
		return PlaybackSessionId.IsValid()
			&& ObservationTrackId.IsValid()
			&& JournalId.IsValid()
			&& JournalEntryId.IsValid()
			&& SenseTag.IsValid()
			&& SemanticTag.IsValid()
			&& InvestigationPriority > 0
			&& InvestigationRevision > 0
			&& !InvestigationLocation.ContainsNaN();
	}
};

/** Captured exactly once when Replay first yields to an investigation chain. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxReplayResumeContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Recovery")
	bool bCaptured = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Recovery")
	FIntentReplayPlaybackSessionId PlaybackSessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Recovery")
	FIntentReplayTrackId ReplayTrackId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Recovery")
	TArray<FIntentReplaySuspendedIntent> SuspendedIntents;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Recovery")
	int32 NextRecoveryIndex = 0;

	void Reset()
	{
		*this = FParadoxReplayResumeContext();
	}
};

/** Read-only diagnostic copy of the coordinator's authoritative state. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxCloneBehaviorDebugSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Debug")
	EParadoxCloneBehaviorMode Mode = EParadoxCloneBehaviorMode::Replay;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Debug")
	int32 ModeRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Debug")
	FName LastTransitionReason;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Debug")
	bool bReplayStartAuthorized = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Debug")
	bool bGoapHandoffTerminal = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Debug")
	bool bHasInvestigation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Debug")
	FParadoxInvestigationContext Investigation;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Clone Behavior|Debug")
	int32 PendingRecoveryIntentCount = 0;
};

