// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridPathLineVisualStyle.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

void FGridPathLineMaterialDataLayout::Write(
	EGridCellPathVisualState State,
	float Progress,
	const FLinearColor& Color,
	TArrayView<float> OutData)
{
	if (OutData.Num() < NumFloats)
	{
		return;
	}
	OutData[PathState] = static_cast<float>(State);
	OutData[PathProgress] = FMath::Clamp(Progress, 0.0f, 1.0f);
	OutData[ResolvedRed] = Color.R;
	OutData[ResolvedGreen] = Color.G;
	OutData[ResolvedBlue] = Color.B;
	OutData[ResolvedAlpha] = Color.A;
}

UGridPathLineVisualStyle::UGridPathLineVisualStyle()
	: PreviewColor(0.25f, 0.55f, 1.0f, 0.85f)
	, ActiveColor(0.05f, 0.35f, 1.0f, 0.95f)
	, TraversedColor(0.12f, 0.18f, 0.32f, 0.65f)
	, CurrentColor(1.0f, 1.0f, 1.0f, 1.0f)
	, DestinationColor(0.8f, 0.1f, 1.0f, 1.0f)
	, InvalidColor(1.0f, 0.02f, 0.02f, 0.95f)
{
}

FLinearColor UGridPathLineVisualStyle::ResolveColor(EGridCellPathVisualState State) const
{
	switch (State)
	{
	case EGridCellPathVisualState::Preview:
		return PreviewColor;
	case EGridCellPathVisualState::ActiveTraversed:
		return TraversedColor;
	case EGridCellPathVisualState::ActiveCurrent:
		return CurrentColor;
	case EGridCellPathVisualState::Destination:
		return DestinationColor;
	case EGridCellPathVisualState::Invalid:
		return InvalidColor;
	case EGridCellPathVisualState::ActiveRemaining:
	default:
		return ActiveColor;
	}
}

bool UGridPathLineVisualStyle::Validate(FString& OutError) const
{
	if (SegmentMesh == nullptr)
	{
		OutError = TEXT("Segment Mesh is not assigned.");
		return false;
	}
	if (SegmentMaterial == nullptr)
	{
		OutError = TEXT("Segment Material is not assigned.");
		return false;
	}
	const UMaterialInterface* ResolvedMarkerMaterial = MarkerMaterial != nullptr ? MarkerMaterial.Get() : SegmentMaterial.Get();
	if (!SegmentMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes)
		|| (MarkerMesh != nullptr && !ResolvedMarkerMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes)))
	{
		OutError = TEXT("Every assigned path-line material must enable Used with Instanced Static Meshes.");
		return false;
	}
	const FVector SegmentSize = SegmentMesh->GetBounds().BoxExtent * 2.0;
	if (SegmentSize.ContainsNaN()
		|| SegmentSize.X <= UE_SMALL_NUMBER
		|| SegmentSize.Y <= UE_SMALL_NUMBER
		|| SegmentSize.Z <= UE_SMALL_NUMBER)
	{
		OutError = TEXT("Segment Mesh must have finite, non-zero X/Y/Z bounds.");
		return false;
	}
	if (MarkerMesh != nullptr)
	{
		const FVector MarkerBoundsSize = MarkerMesh->GetBounds().BoxExtent * 2.0;
		if (MarkerBoundsSize.ContainsNaN()
			|| MarkerBoundsSize.X <= UE_SMALL_NUMBER
			|| MarkerBoundsSize.Y <= UE_SMALL_NUMBER
			|| MarkerBoundsSize.Z <= UE_SMALL_NUMBER)
		{
			OutError = TEXT("Marker Mesh must have finite, non-zero X/Y/Z bounds.");
			return false;
		}
	}
	const FLinearColor Colors[] = {
		PreviewColor,
		ActiveColor,
		TraversedColor,
		CurrentColor,
		DestinationColor,
		InvalidColor};
	for (const FLinearColor& Color : Colors)
	{
		if (!FMath::IsFinite(Color.R)
			|| !FMath::IsFinite(Color.G)
			|| !FMath::IsFinite(Color.B)
			|| !FMath::IsFinite(Color.A))
		{
			OutError = TEXT("Every path-line color channel must be finite.");
			return false;
		}
	}
	if (!FMath::IsFinite(LineWidth)
		|| LineWidth <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(LineThickness)
		|| LineThickness <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(MarkerSize)
		|| MarkerSize <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(SurfaceOffset)
		|| SurfaceOffset < 0.0f
		|| StartCullDistance < 0
		|| EndCullDistance < 0
		|| (EndCullDistance > 0 && StartCullDistance > EndCullDistance))
	{
		OutError = TEXT("Path-line dimensions, surface offset, or culling distances are invalid.");
		return false;
	}
	OutError.Reset();
	return true;
}
