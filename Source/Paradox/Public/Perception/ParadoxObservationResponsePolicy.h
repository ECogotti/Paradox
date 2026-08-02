#pragma once

#include "Behavior/ParadoxCloneBehaviorTypes.h"
#include "Engine/DataAsset.h"
#include "ParadoxObservationResponsePolicy.generated.h"

/** One project-owned, class-agnostic rule mapping a comparison to an investigation priority. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxObservationResponseRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	FName RuleId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	bool bAnyObservationType = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response", meta = (EditCondition = "!bAnyObservationType"))
	EPerceptionKnowledgeObservationType ObservationType =
		EPerceptionKnowledgeObservationType::State;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	TArray<EIntentReplayObservationMatchResult> MatchResults;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	TArray<EIntentReplayObservationMismatchReason> MismatchReasons;

	/** Empty means any sense; parent tags match child tags. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	FGameplayTag SenseTag;

	/** Empty means any state/event semantic tag; parent tags match child tags. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	FGameplayTag SemanticTag;

	/** Optional semantic source categories resolved through GameplayTagAssetInterface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	FGameplayTagContainer RequiredSourceCategories;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumConfidence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	TArray<EParadoxCloneBehaviorMode> AllowedModes = {
		EParadoxCloneBehaviorMode::Replay,
		EParadoxCloneBehaviorMode::Investigating
	};

	/** Higher values replace lower-priority investigations; values <= 0 ignore the candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	int32 InvestigationPriority = 0;
};

UENUM(BlueprintType)
enum class EParadoxObservationResponseDecision : uint8
{
	Ignored,
	Investigate
};

USTRUCT(BlueprintType)
struct PARADOX_API FParadoxObservationResponseResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Observation Response")
	EParadoxObservationResponseDecision Decision =
		EParadoxObservationResponseDecision::Ignored;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Observation Response")
	int32 InvestigationPriority = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Observation Response")
	FName RuleId;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Observation Response")
	FString DiagnosticMessage;

	bool ShouldInvestigate() const
	{
		return Decision == EParadoxObservationResponseDecision::Investigate
			&& InvestigationPriority > 0;
	}
};

/**
 * Project response policy. Generic perception plugins remain neutral and never know these priorities.
 */
UCLASS(BlueprintType)
class PARADOX_API UParadoxObservationResponsePolicy : public UDataAsset
{
	GENERATED_BODY()

public:
	UParadoxObservationResponsePolicy();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	bool bIgnoreVerifiedSelfCausedObservations = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Observation Response")
	TArray<FParadoxObservationResponseRule> Rules;

	UFUNCTION(BlueprintPure, Category = "Paradox|Observation Response")
	FParadoxObservationResponseResult Evaluate(
		const FParadoxInvestigationContext& Candidate,
		EParadoxCloneBehaviorMode CurrentMode) const;
};

