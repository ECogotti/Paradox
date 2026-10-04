#pragma once

#include "Engine/DataAsset.h"
#include "ParadoxBlackHoleMaterialVariants.generated.h"

class UMaterialInterface;

UENUM(BlueprintType)
enum class EParadoxBlackHoleFeature : uint8
{
	AccretionDisk,
	GravitationalLensing,
	Refraction,
	FineNoise,
	DiskStructures,
	VariableVolume,
	Doppler,
	Glow
};

USTRUCT(BlueprintType)
struct FParadoxBlackHoleMaterialVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Black Hole")
	uint8 FeatureMask = 255;

	UPROPERTY(EditAnywhere, Category = "Black Hole")
	TObjectPtr<UMaterialInterface> Material;
};

/** Cooked material references: runtime switches parents, never static parameters. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxBlackHoleMaterialVariants : public UDataAsset
{
	GENERATED_BODY()

public:
	/** One entry per normalized mask. Duplicate or missing entries fail lookup. */
	UPROPERTY(EditAnywhere, Category = "Black Hole")
	TArray<FParadoxBlackHoleMaterialVariant> Variants;

	/** Legacy FogSafe parent families and their original feature defaults. */
	UPROPERTY(EditAnywhere, Category = "Black Hole")
	TArray<FParadoxBlackHoleMaterialVariant> SourceProfiles;

	static uint8 NormalizeMask(uint8 InMask);
	UMaterialInterface* FindVariant(uint8 InMask) const;
	bool FindSourceDefaults(UMaterialInterface* Source, uint8& OutMask) const;
};
