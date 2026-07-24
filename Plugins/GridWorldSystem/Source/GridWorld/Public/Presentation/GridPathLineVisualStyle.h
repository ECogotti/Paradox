// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Presentation/GridPresentationTypes.h"
#include "GridPathLineVisualStyle.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** Stable Per Instance Custom Data contract used by the default path-line material. */
struct GRIDWORLD_API FGridPathLineMaterialDataLayout
{
	static constexpr int32 PathState = 0;
	static constexpr int32 PathProgress = 1;
	static constexpr int32 ResolvedRed = 2;
	static constexpr int32 ResolvedGreen = 3;
	static constexpr int32 ResolvedBlue = 4;
	static constexpr int32 ResolvedAlpha = 5;
	static constexpr int32 NumFloats = 6;

	static void Write(
		EGridCellPathVisualState State,
		float Progress,
		const FLinearColor& Color,
		TArrayView<float> OutData);
};

/** Designer-authored meshes, materials, colors, placement, and culling for strict path polylines. */
UCLASS(BlueprintType, meta = (DisplayName = "Grid Path Line Visual Style"))
class GRIDWORLD_API UGridPathLineVisualStyle : public UDataAsset
{
	GENERATED_BODY()

public:
	UGridPathLineVisualStyle();

	/** Mesh whose local X axis is stretched between consecutive path points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line", meta = (ToolTip = "Mesh stretched along local X to form every strict-polyline segment."))
	TObjectPtr<UStaticMesh> SegmentMesh;

	/** Optional mesh placed at displayed path points. Null disables point markers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line")
	TObjectPtr<UStaticMesh> MarkerMesh;

	/** Material override for Segment Mesh. Must support Instanced Static Meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line")
	TObjectPtr<UMaterialInterface> SegmentMaterial;

	/** Optional marker override. Null reuses Segment Material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line")
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Color")
	FLinearColor PreviewColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Color")
	FLinearColor ActiveColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Color")
	FLinearColor TraversedColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Color")
	FLinearColor CurrentColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Color")
	FLinearColor DestinationColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Color")
	FLinearColor InvalidColor;

	/** Segment width in world centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Geometry", meta = (ClampMin = "0.1"))
	float LineWidth = 8.0f;

	/** Segment thickness in world centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Geometry", meta = (ClampMin = "0.1"))
	float LineThickness = 3.0f;

	/** Uniform world-space diameter of point markers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Geometry", meta = (ClampMin = "0.1", EditCondition = "MarkerMesh != nullptr"))
	float MarkerSize = 14.0f;

	/** Offset along each sampled floor normal used to keep the line above cell/floor rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Geometry", meta = (ClampMin = "0.0"))
	float SurfaceOffset = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Culling", meta = (ClampMin = "0"))
	int32 StartCullDistance = 5000;

	/** Zero disables distance culling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Culling", meta = (ClampMin = "0"))
	int32 EndCullDistance = 15000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Line|Rendering")
	bool bCastShadow = false;

	FLinearColor ResolveColor(EGridCellPathVisualState State) const;
	bool Validate(FString& OutError) const;
};
