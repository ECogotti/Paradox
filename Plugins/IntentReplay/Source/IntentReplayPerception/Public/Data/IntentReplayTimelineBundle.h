#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayPerceptionTypes.h"
#include "IntentReplayTimelineBundle.generated.h"

class UIntentReplayObservationTrack;
class UIntentReplayTrack;

/** Immutable association preventing Action and Observation Tracks from different runs being paired. */
UCLASS(BlueprintType, Transient)
class INTENTREPLAYPERCEPTION_API UIntentReplayTimelineBundle : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Bundle")
	UIntentReplayTrack* GetActionTrack() const { return ActionTrack; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Bundle")
	UIntentReplayObservationTrack* GetObservationTrack() const { return ObservationTrack; }

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Bundle")
	FIntentReplayTimelineBundleValidationResult ValidateBundle() const;

	UFUNCTION(BlueprintPure, Category = "Intent Replay Perception|Bundle")
	bool IsFinalized() const { return bFinalized; }

private:
	void InitializeFinalized(
		UIntentReplayTrack* InActionTrack,
		UIntentReplayObservationTrack* InObservationTrack);

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayTrack> ActionTrack;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayObservationTrack> ObservationTrack;

	UPROPERTY()
	int32 FormatVersion = 1;

	UPROPERTY()
	bool bFinalized = false;

	friend class UIntentReplayObservationComponent;
	friend struct FIntentReplayPerceptionTestAccessor;
};
