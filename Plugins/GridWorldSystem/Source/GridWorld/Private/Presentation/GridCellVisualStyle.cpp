// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridCellVisualStyle.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

UGridCellVisualStyle::UGridCellVisualStyle()
	: UnselectedColor(0.08f, 0.75f, 0.18f, 0.20f)
	, HoveredColor(1.0f, 0.75f, 0.05f, 0.70f)
	, SelectedColor(0.05f, 0.75f, 1.0f, 0.85f)
	, BlockedColor(0.25f, 0.02f, 0.02f, 0.0f)
	, HighCostColor(1.0f, 0.25f, 0.02f, 0.35f)
	, OccupiedColor(1.0f, 0.8f, 0.05f, 0.45f)
	, ReservedColor(0.05f, 0.9f, 0.85f, 0.50f)
	, PreviewPathColor(0.25f, 0.55f, 1.0f, 0.55f)
	, ActivePathColor(0.05f, 0.35f, 1.0f, 0.70f)
	, TraversedPathColor(0.15f, 0.2f, 0.35f, 0.35f)
	, CurrentPathColor(1.0f, 1.0f, 1.0f, 0.85f)
	, DestinationColor(0.8f, 0.1f, 1.0f, 0.85f)
	, InvalidPathColor(1.0f, 0.02f, 0.02f, 0.75f)
{
}

FLinearColor UGridCellVisualStyle::ResolveColor(const FGridCellVisualState& State) const
{
	if (State.InteractionState == EGridCellInteractionVisualState::Selected)
	{
		return SelectedColor;
	}
	if (State.InteractionState == EGridCellInteractionVisualState::Hovered)
	{
		return HoveredColor;
	}

	switch (State.PathState)
	{
	case EGridCellPathVisualState::Preview:
		return PreviewPathColor;
	case EGridCellPathVisualState::ActiveRemaining:
		return ActivePathColor;
	case EGridCellPathVisualState::ActiveCurrent:
		return CurrentPathColor;
	case EGridCellPathVisualState::ActiveTraversed:
		return TraversedPathColor;
	case EGridCellPathVisualState::Destination:
		return DestinationColor;
	case EGridCellPathVisualState::Invalid:
		return InvalidPathColor;
	default:
		break;
	}

	const EGridCellNavigationVisualFlags Flags = static_cast<EGridCellNavigationVisualFlags>(State.NavigationFlags);
	if (EnumHasAnyFlags(Flags, EGridCellNavigationVisualFlags::Blocked))
	{
		return BlockedColor;
	}
	if (EnumHasAnyFlags(Flags, EGridCellNavigationVisualFlags::Reserved))
	{
		return ReservedColor;
	}
	if (EnumHasAnyFlags(Flags, EGridCellNavigationVisualFlags::Occupied))
	{
		return OccupiedColor;
	}
	if (EnumHasAnyFlags(Flags, EGridCellNavigationVisualFlags::HighCost))
	{
		return HighCostColor;
	}
	return UnselectedColor;
}

bool UGridCellVisualStyle::Validate(FString& OutError) const
{
	if (CellMesh == nullptr)
	{
		OutError = TEXT("Cell Mesh is not assigned.");
		return false;
	}
	if (CellMaterial == nullptr)
	{
		OutError = TEXT("Cell Material is not assigned.");
		return false;
	}
	if (!CellMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
	{
		OutError = FString::Printf(
			TEXT("Cell Material '%s' is not compiled for Instanced Static Meshes. Enable Usage > Used with Instanced Static Meshes and save the material."),
			*CellMaterial->GetName());
		return false;
	}
	const FVector MeshSize = CellMesh->GetBounds().BoxExtent * 2.0;
	if (MeshSize.ContainsNaN() || MeshSize.X <= UE_SMALL_NUMBER || MeshSize.Y <= UE_SMALL_NUMBER)
	{
		OutError = TEXT("Cell Mesh must have finite, non-zero X/Y bounds.");
		return false;
	}
	const FLinearColor Colors[] = {
		UnselectedColor,
		HoveredColor,
		SelectedColor,
		BlockedColor,
		HighCostColor,
		OccupiedColor,
		ReservedColor,
		PreviewPathColor,
		ActivePathColor,
		TraversedPathColor,
		CurrentPathColor,
		DestinationColor,
		InvalidPathColor};
	for (const FLinearColor& Color : Colors)
	{
		if (!FMath::IsFinite(Color.R)
			|| !FMath::IsFinite(Color.G)
			|| !FMath::IsFinite(Color.B)
			|| !FMath::IsFinite(Color.A))
		{
			OutError = TEXT("Every style color channel must be finite.");
			return false;
		}
	}
	if (!FMath::IsFinite(CellInsetFraction)
		|| CellInsetFraction < 0.0f
		|| CellInsetFraction >= 0.5f
		|| !FMath::IsFinite(SurfaceOffset)
		|| SurfaceOffset < 0.0f
		|| StartCullDistance < 0
		|| EndCullDistance < 0
		|| (EndCullDistance > 0 && StartCullDistance > EndCullDistance))
	{
		OutError = TEXT("Cell inset, surface offset, or culling distances are invalid.");
		return false;
	}
	OutError.Reset();
	return true;
}
