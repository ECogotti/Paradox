#pragma once

#include "CoreMinimal.h"
#include "Policies/IntentReplayTimeSource.h"
#include "UObject/SoftObjectPath.h"
#include "IntentReplayTestTypes.generated.h"

class UGameplayActionDefinition;
class UIntentReplayTrack;

USTRUCT()
struct FIntentReplayTestNestedPayload
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UObject>> Objects;

	UPROPERTY()
	TMap<FName, TObjectPtr<UObject>> ObjectMap;

	/** Exercises UE 5.8's reflected UTF-8 subpath storage during recursive validation. */
	UPROPERTY()
	FSoftObjectPath StableAssetPath;
};

UCLASS()
class UIntentReplayTestTimeSource : public UIntentReplayTimeSource
{
	GENERATED_BODY()

public:
	static void ResetTime() { CurrentTimeSeconds = 0.0; }
	static void AdvanceTime(const double DeltaSeconds) { CurrentTimeSeconds += FMath::Max(0.0, DeltaSeconds); }

protected:
	virtual double GetTimeSeconds_Implementation(UObject* WorldContextObject) const override;

private:
	static double CurrentTimeSeconds;
};

UCLASS()
class UIntentReplayTestTrackHolder : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UIntentReplayTrack> RetainedTrack;

	UPROPERTY()
	TObjectPtr<UGameplayActionDefinition> RetainedDefinition;
};
