#pragma once

#include "Engine/DeveloperSettings.h"
#include "IntentReplayPerceptionDeveloperSettings.generated.h"

/** Project-wide debug presentation defaults for the optional perception timeline module. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Intent Replay Perception"))
class INTENTREPLAYPERCEPTION_API UIntentReplayPerceptionDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0.05", Units = "s"))
	float DebugDrawInterval = 0.1f;

	UPROPERTY(config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0.0", Units = "cm"))
	float ComparativeBoundsPadding = 8.0f;

	UPROPERTY(config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0.05", Units = "s"))
	float RecentUnexpectedLifetime = 2.0f;

	UPROPERTY(config, EditAnywhere, Category = "Debug")
	FColor MatchedColor = FColor::Green;

	UPROPERTY(config, EditAnywhere, Category = "Debug")
	FColor UnexpectedColor = FColor::Red;

	UPROPERTY(config, EditAnywhere, Category = "Debug")
	FColor AmbiguousColor = FColor(255, 128, 0);

	UPROPERTY(config, EditAnywhere, Category = "Debug")
	FColor JustifiedColor = FColor::Purple;

	UPROPERTY(config, EditAnywhere, Category = "Debug")
	FColor PendingColor = FColor(128, 192, 255);

	UPROPERTY(config, EditAnywhere, Category = "Debug")
	FColor ConsumedColor = FColor(0, 96, 0);

	UPROPERTY(config, EditAnywhere, Category = "Debug")
	FColor InactiveColor = FColor::Silver;
};
