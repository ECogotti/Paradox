#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Oxygen/ParadoxOxygenTypes.h"
#include "ParadoxWorldInitializer.generated.h"

/** Placeable, per-map configuration root for Paradox World-scoped systems. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxWorldInitializer : public AActor
{
	GENERATED_BODY()

public:
	AParadoxWorldInitializer();

	UFUNCTION(BlueprintPure, Category = "Paradox|World Initialization|Oxygen")
	const FParadoxOxygenWorldConfiguration& GetOxygenConfiguration() const
	{
		return OxygenConfiguration;
	}

	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category = "Paradox|World Initialization")
	FParadoxOxygenWorldConfiguration OxygenConfiguration;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
