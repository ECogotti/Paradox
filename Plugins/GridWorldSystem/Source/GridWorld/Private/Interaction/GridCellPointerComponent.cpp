// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/GridCellPointerComponent.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "Subsystems/GridWorldSubsystem.h"

UGridCellPointerComponent::UGridCellPointerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGridCellPointerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearHoveredCell();
	Super::EndPlay(EndPlayReason);
}

FGridCellPointerResult UGridCellPointerComponent::UpdateFromScreenPosition(
	APlayerController* PlayerController,
	const FVector2D& ScreenPosition)
{
	if (!IsValid(PlayerController)
		|| PlayerController->GetWorld() != GetWorld()
		|| ScreenPosition.ContainsNaN())
	{
		return FinishMiss(EGridCellPointerStatus::InvalidInput);
	}
	FVector WorldOrigin;
	FVector WorldDirection;
	if (!PlayerController->DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, WorldOrigin, WorldDirection))
	{
		return FinishMiss(EGridCellPointerStatus::InvalidInput);
	}
	return UpdateFromWorldRay(WorldOrigin, WorldDirection, TraceDistance);
}

FGridCellPointerResult UGridCellPointerComponent::UpdateFromWorldRay(
	const FVector& WorldOrigin,
	const FVector& WorldDirection,
	double Distance)
{
	UWorld* World = GetWorld();
	const double ResolvedDistance = Distance > 0.0 ? Distance : TraceDistance;
	if (World == nullptr
		|| WorldOrigin.ContainsNaN()
		|| WorldDirection.ContainsNaN()
		|| !FMath::IsFinite(ResolvedDistance)
		|| ResolvedDistance <= 0.0)
	{
		return FinishMiss(EGridCellPointerStatus::InvalidInput);
	}
	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return FinishMiss(EGridCellPointerStatus::InvalidInput);
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GridWorldCellPointer), bTraceComplex, GetOwner());
	QueryParams.bReturnPhysicalMaterial = false;
	FHitResult HitResult;
	if (!World->LineTraceSingleByChannel(
		HitResult,
		WorldOrigin,
		WorldOrigin + Direction * ResolvedDistance,
		TraceChannel,
		QueryParams))
	{
		return FinishMiss(EGridCellPointerStatus::NoWorldHit, &HitResult);
	}
	return UpdateFromHitResult(HitResult);
}

FGridCellPointerResult UGridCellPointerComponent::UpdateFromHitResult(const FHitResult& HitResult)
{
	if (!HitResult.bBlockingHit || HitResult.ImpactPoint.ContainsNaN())
	{
		return FinishMiss(EGridCellPointerStatus::NoWorldHit, &HitResult);
	}
	FGridCellPointerResult Result = ResolveHit(HitResult);
	if (Result.Status == EGridCellPointerStatus::Success)
	{
		ApplyTarget(Result);
	}
	else if (bClearHoverOnMiss)
	{
		ClearHoveredCell();
	}
	return Result;
}

void UGridCellPointerComponent::ClearHoveredCell()
{
	if (!HoveredCell.IsValid())
	{
		return;
	}
	const FGridCellId PreviousCell = HoveredCell;
	if (bHasAppliedVisualizationHover)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGridRuntimeVisualizationSubsystem* Visualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>())
			{
				Visualization->SetCellHovered(PreviousCell, false);
			}
		}
	}
	bHasAppliedVisualizationHover = false;
	HoveredCell = FGridCellId();
	OnTargetCellChanged.Broadcast(PreviousCell, HoveredCell);
}

FGridCellPointerResult UGridCellPointerComponent::ResolveHit(const FHitResult& HitResult) const
{
	if (ProjectionExtent.ContainsNaN())
	{
		FGridCellPointerResult Result;
		Result.Status = EGridCellPointerStatus::InvalidInput;
		Result.HitResult = HitResult;
		return Result;
	}
	if (ProjectionPolicy == EGridCellPointerPolicy::ExistingCells)
	{
		return ResolveExistingCell(HitResult);
	}

	FGridCellPointerResult Result;
	Result.HitResult = HitResult;
	const UWorld* World = GetWorld();
	const UGridWorldSubsystem* GridSubsystem = World != nullptr ? World->GetSubsystem<UGridWorldSubsystem>() : nullptr;
	if (GridSubsystem == nullptr || GridSubsystem->GetNavigationData() == nullptr)
	{
		Result.Status = EGridCellPointerStatus::InvalidGrid;
		return Result;
	}
	const FGridCellQueryResult CellResult = GridSubsystem->ProjectPoint(HitResult.ImpactPoint, ProjectionExtent.GetAbs());
	if (CellResult.Status != EGridQueryStatus::Success)
	{
		Result.Status = EGridCellPointerStatus::NoCell;
		return Result;
	}
	Result.Status = EGridCellPointerStatus::Success;
	Result.CellId = CellResult.CellId;
	Result.WorldCenter = CellResult.WorldCenter;
	Result.FloorNormal = CellResult.FloorNormal;
	Result.bWalkable = CellResult.bWalkable;
	return Result;
}

FGridCellPointerResult UGridCellPointerComponent::ResolveExistingCell(const FHitResult& HitResult) const
{
	FGridCellPointerResult Result;
	Result.HitResult = HitResult;
	const UWorld* World = GetWorld();
	const UGridWorldSubsystem* GridSubsystem = World != nullptr ? World->GetSubsystem<UGridWorldSubsystem>() : nullptr;
	const AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid())
	{
		Result.Status = EGridCellPointerStatus::InvalidGrid;
		return Result;
	}

	const FVector Extent = ProjectionExtent.GetAbs();
	const FVector Point = HitResult.ImpactPoint;
	const FGridCellData* BestCell = nullptr;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (const FGridCellData& Cell : Snapshot->Cells)
	{
		const FVector Delta = Cell.WorldCenter - Point;
		if (FMath::Abs(Delta.X) > Extent.X || FMath::Abs(Delta.Y) > Extent.Y || FMath::Abs(Delta.Z) > Extent.Z)
		{
			continue;
		}
		const double DistanceSquared = Delta.SizeSquared();
		if (DistanceSquared < BestDistanceSquared
			|| (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared)
				&& BestCell != nullptr
				&& (Cell.Id.GridId < BestCell->Id.GridId
					|| (Cell.Id.GridId == BestCell->Id.GridId && Cell.Id.Coord < BestCell->Id.Coord))))
		{
			BestCell = &Cell;
			BestDistanceSquared = DistanceSquared;
		}
	}
	if (BestCell == nullptr)
	{
		Result.Status = EGridCellPointerStatus::NoCell;
		return Result;
	}
	Result.Status = EGridCellPointerStatus::Success;
	Result.CellId = BestCell->Id;
	Result.WorldCenter = BestCell->WorldCenter;
	Result.FloorNormal = FVector(BestCell->FloorNormal);
	Result.bWalkable = BestCell->bWalkable;
	return Result;
}

FGridCellPointerResult UGridCellPointerComponent::FinishMiss(
	EGridCellPointerStatus Status,
	const FHitResult* HitResult)
{
	FGridCellPointerResult Result;
	Result.Status = Status;
	if (HitResult != nullptr)
	{
		Result.HitResult = *HitResult;
	}
	if (bClearHoverOnMiss)
	{
		ClearHoveredCell();
	}
	return Result;
}

void UGridCellPointerComponent::ApplyTarget(const FGridCellPointerResult& Result)
{
	if (Result.Status != EGridCellPointerStatus::Success)
	{
		return;
	}
	if (Result.CellId == HoveredCell)
	{
		if (bApplyHoverToVisualization != bHasAppliedVisualizationHover)
		{
			if (UWorld* World = GetWorld())
			{
				if (UGridRuntimeVisualizationSubsystem* Visualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>())
				{
					if (bApplyHoverToVisualization)
					{
						bHasAppliedVisualizationHover = Visualization->SetCellHovered(HoveredCell, true);
					}
					else
					{
						Visualization->SetCellHovered(HoveredCell, false);
						bHasAppliedVisualizationHover = false;
					}
				}
			}
		}
		return;
	}
	const FGridCellId PreviousCell = HoveredCell;
	if (bHasAppliedVisualizationHover)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGridRuntimeVisualizationSubsystem* Visualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>())
			{
				Visualization->SetCellHovered(PreviousCell, false);
			}
		}
	}
	bHasAppliedVisualizationHover = false;
	if (bApplyHoverToVisualization)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGridRuntimeVisualizationSubsystem* Visualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>())
			{
				bHasAppliedVisualizationHover = Visualization->SetCellHovered(Result.CellId, true);
			}
		}
	}
	HoveredCell = Result.CellId;
	OnTargetCellChanged.Broadcast(PreviousCell, HoveredCell);
}
