#pragma once

#include "Engine/DataAsset.h"
#include "Types/IntentReplayTypes.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "ParadoxReplayRecoveryPolicy.generated.h"

class AActor;
class UPerceptionKnowledgeListenerComponent;

namespace ParadoxReplayRecoveryParameters
{
	/** Standard optional FParadoxReplayRecoveryMetadata Property Bag field. */
	PARADOX_API extern const FName Metadata;
}

/** Stable semantic recovery hints authored into a replayable GameplayAction request. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxReplayRecoveryMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery")
	FPerceptionKnowledgeEntityId TargetEntityId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery")
	bool bRequireTargetEntity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery")
	bool bRequiresExecutionLocation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery", meta = (EditCondition = "bRequiresExecutionLocation"))
	FVector RequiredExecutionLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery", meta = (EditCondition = "bRequiresExecutionLocation", ClampMin = "0.0", Units = "cm"))
	float ExecutionLocationTolerance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery")
	bool bRequiresFacing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery", meta = (EditCondition = "bRequiresFacing"))
	FRotator RequiredFacing = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery")
	bool bHasExpectedSatisfiedState = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery", meta = (EditCondition = "bHasExpectedSatisfiedState"))
	FGameplayTag ExpectedStateTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Replay Recovery", meta = (EditCondition = "bHasExpectedSatisfiedState"))
	FPerceptionKnowledgeValue ExpectedStateValue;
};

UENUM(BlueprintType)
enum class EParadoxReplayRecoveryOutcome : uint8
{
	ReissueNow,
	MoveThenReissue,
	AlreadySatisfied,
	CannotRestore
};

USTRUCT(BlueprintType)
struct PARADOX_API FParadoxReplayRecoveryDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Replay Recovery")
	EParadoxReplayRecoveryOutcome Outcome =
		EParadoxReplayRecoveryOutcome::CannotRestore;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Replay Recovery")
	bool bHasMetadata = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Replay Recovery")
	FVector RepositionLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Replay Recovery")
	FString DiagnosticMessage;
};

/** Project policy that reconciles semantic intent outcomes without mutating the Recorded Intent. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxReplayRecoveryPolicy : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Paradox|Replay Recovery")
	FParadoxReplayRecoveryDecision Evaluate(
		const FIntentReplaySuspendedIntent& SuspendedIntent,
		AActor* Clone,
		UPerceptionKnowledgeListenerComponent* Listener) const;

	static bool ReadMetadata(
		const FRecordedIntent& RecordedIntent,
		FParadoxReplayRecoveryMetadata& OutMetadata);
};

