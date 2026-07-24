// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Presentation/GridPresentationTypes.h"
#include "GridCellVisualStyle.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** Designer-authored mesh, material, colors, placement, and culling for runtime grid cells. */
UCLASS(BlueprintType, meta = (DisplayName = "Grid Cell Visual Style"))
class GRIDWORLD_API UGridCellVisualStyle : public UDataAsset
{
	GENERATED_BODY()

public:
	UGridCellVisualStyle();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	TObjectPtr<UStaticMesh> CellMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation", meta = (ToolTip = "Material override applied to every cell HISM. This replaces the material stored in the Static Mesh slot and must enable Used with Instanced Static Meshes."))
	TObjectPtr<UMaterialInterface> CellMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Interaction")
	FLinearColor UnselectedColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Interaction")
	FLinearColor HoveredColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Interaction")
	FLinearColor SelectedColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Navigation")
	FLinearColor BlockedColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Navigation")
	FLinearColor HighCostColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Navigation")
	FLinearColor OccupiedColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Navigation")
	FLinearColor ReservedColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Path")
	FLinearColor PreviewPathColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Path")
	FLinearColor ActivePathColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Path")
	FLinearColor TraversedPathColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Path")
	FLinearColor CurrentPathColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Path")
	FLinearColor DestinationColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Path")
	FLinearColor InvalidPathColor;

	/** Fraction removed from each edge of the logical cell. 0.03 produces a 94% plane. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Geometry", meta = (ClampMin = "0.0", ClampMax = "0.49"))
	float CellInsetFraction = 0.03f;

	/** Offset along the sampled floor normal used to avoid z-fighting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Geometry", meta = (ClampMin = "0.0"))
	float SurfaceOffset = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Culling", meta = (ClampMin = "0"))
	int32 StartCullDistance = 5000;

	/** Zero disables distance culling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Culling", meta = (ClampMin = "0"))
	int32 EndCullDistance = 15000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Rendering")
	bool bCastShadow = false;

	/** Resolves layered semantic state to the color written into per-instance custom data. */
	FLinearColor ResolveColor(const FGridCellVisualState& State) const;
	/** Validates required assets, mesh bounds, and numeric configuration. */
	bool Validate(FString& OutError) const;
};
