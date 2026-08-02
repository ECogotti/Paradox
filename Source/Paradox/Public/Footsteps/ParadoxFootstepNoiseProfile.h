#pragma once

#include "Engine/DataAsset.h"
#include "Footsteps/ParadoxFootstepNoiseTypes.h"
#include "ParadoxFootstepNoiseProfile.generated.h"

class FDataValidationContext;

/** Project-specific mapping from physical surfaces to semantic footstep noise. */
UCLASS(BlueprintType, meta = (DisplayName = "Paradox Footstep Noise Profile"))
class PARADOX_API UParadoxFootstepNoiseProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UParadoxFootstepNoiseProfile();
	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception")
	TMap<TEnumAsByte<EPhysicalSurface>, FParadoxFootstepNoiseResponse> SurfaceResponses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception|Fallback")
	bool bUseDefaultResponse = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Footsteps|Perception|Fallback",
		meta = (EditCondition = "bUseDefaultResponse"))
	FParadoxFootstepNoiseResponse DefaultResponse;

	UFUNCTION(BlueprintPure, Category = "Paradox|Footsteps|Perception")
	bool ResolveResponse(
		TEnumAsByte<EPhysicalSurface> SurfaceType,
		FParadoxFootstepNoiseResponse& OutResponse,
		bool& bOutUsedFallback) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
