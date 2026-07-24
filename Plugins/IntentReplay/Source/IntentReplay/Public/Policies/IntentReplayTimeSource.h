#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IntentReplayTimeSource.generated.h"

/** Replaceable monotonic clock used for recording timestamps and playback scheduling. */
UCLASS(BlueprintType, Blueprintable, Abstract, EditInlineNew, DefaultToInstanced)
class INTENTREPLAY_API UIntentReplayTimeSource : public UObject
{
	GENERATED_BODY()

public:
	/** Returns seconds in a stable clock domain for the supplied world context. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Intent Replay|Time")
	double GetTimeSeconds(UObject* WorldContextObject) const;
};

/** Default clock backed by UWorld::GetTimeSeconds. */
UCLASS()
class INTENTREPLAY_API UIntentReplayWorldTimeSource : public UIntentReplayTimeSource
{
	GENERATED_BODY()

public:
	/** Delegates to the base world-time implementation. */
	virtual double GetTimeSeconds_Implementation(UObject* WorldContextObject) const override;
};
