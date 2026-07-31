#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayPerceptionTypes.h"
#include "IntentReplayObservationPolicies.generated.h"

/** Replaceable semantic filter; duplicate and lifecycle ownership remain in native component code. */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class INTENTREPLAYPERCEPTION_API UIntentReplayObservationRecordPolicy : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Intent Replay Perception|Policy")
	bool IsObservationAllowed(
		const FPerceptionKnowledgeObservation& Observation,
		const FIntentReplayObservationRecordOptions& Options) const;
};

/**
 * Replaceable value-compatibility hook. Candidate indexing, consumption and deterministic
 * tie-breaking always remain under native ownership.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class INTENTREPLAYPERCEPTION_API UIntentReplayObservationMatchPolicy : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Intent Replay Perception|Policy")
	bool AreStateValuesEquivalent(
		const FPerceptionKnowledgeValue& Expected,
		const FPerceptionKnowledgeValue& Current,
		const FIntentReplayObservationMatchOptions& Options) const;

	void GetTimeWindow(
		EIntentReplayRecordedObservationType Type,
		FGameplayTag SenseTag,
		const FIntentReplayObservationMatchOptions& Options,
		double& OutEarlyTolerance,
		double& OutLateTolerance) const;
};
