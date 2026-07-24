#pragma once

#include "Camera/ParadoxCameraTypes.h"
#include "Engine/DeveloperSettings.h"
#include "ParadoxCameraSettings.generated.h"

/** Project-wide fallback values overridden optionally by each camera bounds volume. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Paradox Camera"))
class PARADOX_API UParadoxCameraSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(Config, EditAnywhere, Category = "Defaults")
	FParadoxCameraConfiguration DefaultConfiguration;
};

