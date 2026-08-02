#pragma once

#include "Engine/DataAsset.h"
#include "Types/FootstepTypes.h"
#include "FootstepProfile.generated.h"

class FDataValidationContext;

/** Data-driven mapping from Unreal Physical Surfaces to neutral cosmetic responses. */
UCLASS(BlueprintType, meta = (DisplayName = "Footstep Profile"))
class FOOTSTEPSYSTEM_API UFootstepProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Surface-specific responses. SurfaceType_Default is a valid explicit key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Responses")
	TMap<TEnumAsByte<EPhysicalSurface>, FFootstepSurfaceResponse> SurfaceResponses;

	/** Enables the catch-all response used only when SurfaceResponses has no matching key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Fallback")
	bool bUseDefaultResponse = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Fallback", meta = (EditCondition = "bUseDefaultResponse"))
	FFootstepSurfaceResponse DefaultResponse;

	/**
	 * Copies the response selected for a surface.
	 *
	 * @param SurfaceType Surface observed by the floor query.
	 * @param OutResponse Selected surface or fallback response.
	 * @param bOutUsedFallback True only when DefaultResponse was selected.
	 * @return True when either a surface response or enabled fallback exists.
	 */
	UFUNCTION(BlueprintPure, Category = "Footstep System|Profile")
	bool ResolveResponse(
		TEnumAsByte<EPhysicalSurface> SurfaceType,
		FFootstepSurfaceResponse& OutResponse,
		bool& bOutUsedFallback) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
