#pragma once

#include "CoreMinimal.h"
#include "Policies/IntentReplayTimeSource.h"
#include "IntentReplayPerceptionTestTypes.generated.h"

class UGameplayActionDefinition;
class UIntentReplayTimelineBundle;

UCLASS()
class UIntentReplayPerceptionTestTimeSource : public UIntentReplayTimeSource
{
	GENERATED_BODY()

public:
	static void ResetTime() { CurrentTimeSeconds = 0.0; }
	static void AdvanceTime(const double DeltaSeconds)
	{
		CurrentTimeSeconds += FMath::Max(0.0, DeltaSeconds);
	}

protected:
	virtual double GetTimeSeconds_Implementation(UObject* WorldContextObject) const override;

private:
	static double CurrentTimeSeconds;
};

UCLASS()
class UIntentReplayPerceptionTestBundleHolder : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UIntentReplayTimelineBundle> Bundle;

	UPROPERTY()
	TObjectPtr<UGameplayActionDefinition> Definition;
};
