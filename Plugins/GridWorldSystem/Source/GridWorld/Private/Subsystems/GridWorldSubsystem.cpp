// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/GridWorldSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridNavigationQueryFilter.h"

AGridNavigationData* UGridWorldSubsystem::GetNavigationData() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<AGridNavigationData> It(World); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed())
		{
			return *It;
		}
	}
	return nullptr;
}

FGridCellQueryResult UGridWorldSubsystem::ProjectPoint(const FVector& WorldLocation, const FVector& Extent) const
{
	FGridCellQueryResult Result;
	AGridNavigationData* NavData = GetNavigationData();
	if (NavData == nullptr || WorldLocation.ContainsNaN() || Extent.ContainsNaN())
	{
		Result.Status = NavData == nullptr ? EGridQueryStatus::InvalidGrid : EGridQueryStatus::InvalidInput;
		return Result;
	}
	FNavLocation Projected;
	if (!NavData->ProjectPoint(WorldLocation, Projected, Extent.GetAbs()))
	{
		Result.Status = EGridQueryStatus::StartNotNavigable;
		return Result;
	}
	const FGridWorldSnapshotPtr Snapshot = NavData->GetSnapshot();
	const int32 CellIndex = Snapshot.IsValid() ? Snapshot->ResolveNodeRef(Projected.NodeRef) : INDEX_NONE;
	if (!Snapshot.IsValid() || !Snapshot->Cells.IsValidIndex(CellIndex))
	{
		Result.Status = EGridQueryStatus::Stale;
		return Result;
	}
	const FGridCellData& Cell = Snapshot->Cells[CellIndex];
	Result.Status = EGridQueryStatus::Success;
	Result.CellId = Cell.Id;
	Result.Handle.GridId = Cell.Id.GridId;
	Result.Handle.DenseIndex = CellIndex;
	Result.Handle.TopologyGeneration = Snapshot->Revisions.Topology;
	Result.WorldCenter = Cell.WorldCenter;
	Result.FloorNormal = FVector(Cell.FloorNormal);
	Result.bWalkable = Cell.bWalkable;
	return Result;
}

FGridCellQueryResult UGridWorldSubsystem::GetCell(const FGridCellId& CellId) const
{
	FGridCellQueryResult Result;
	AGridNavigationData* NavData = GetNavigationData();
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid() || !CellId.IsValid())
	{
		Result.Status = NavData == nullptr ? EGridQueryStatus::InvalidGrid : EGridQueryStatus::InvalidInput;
		return Result;
	}
	const int32 CellIndex = Snapshot->FindCellIndex(CellId.GridId, CellId.Coord);
	if (!Snapshot->Cells.IsValidIndex(CellIndex))
	{
		Result.Status = EGridQueryStatus::InvalidInput;
		return Result;
	}
	const FGridCellData& Cell = Snapshot->Cells[CellIndex];
	Result.Status = EGridQueryStatus::Success;
	Result.CellId = Cell.Id;
	Result.Handle.GridId = Cell.Id.GridId;
	Result.Handle.DenseIndex = CellIndex;
	Result.Handle.TopologyGeneration = Snapshot->Revisions.Topology;
	Result.WorldCenter = Cell.WorldCenter;
	Result.FloorNormal = FVector(Cell.FloorNormal);
	Result.bWalkable = Cell.bWalkable;
	return Result;
}

FGridPathQueryResult UGridWorldSubsystem::FindPath(const FVector& Start, const FVector& Goal, TSubclassOf<UNavigationQueryFilter> FilterClass, bool bAllowPartialPath) const
{
	FGridPathQueryResult Output;
	AGridNavigationData* NavData = GetNavigationData();
	if (NavData == nullptr || Start.ContainsNaN() || Goal.ContainsNaN())
	{
		Output.Status = NavData == nullptr ? EGridQueryStatus::InvalidGrid : EGridQueryStatus::InvalidInput;
		return Output;
	}
	const FSharedConstNavQueryFilter Filter = FilterClass != nullptr
		? UNavigationQueryFilter::GetQueryFilter(*NavData, FilterClass)
		: NavData->GetDefaultQueryFilter();
	FPathFindingQuery Query(this, *NavData, Start, Goal, Filter);
	Query.SetAllowPartialPaths(bAllowPartialPath);
	const FPathFindingResult Result = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	const FGridNavigationPath* GridPath = Result.Path.IsValid() ? Result.Path->CastPath<FGridNavigationPath>() : nullptr;
	if (!Result.IsSuccessful() || GridPath == nullptr)
	{
		Output.Status = EGridQueryStatus::Unreachable;
		return Output;
	}
	Output.Status = GridPath->IsPartial() ? EGridQueryStatus::Partial : EGridQueryStatus::Success;
	Output.Cells = GridPath->CellPath;
	for (const FNavPathPoint& PathPoint : GridPath->GetPathPoints())
	{
		Output.WorldPoints.Add(PathPoint.Location);
	}
	Output.Length = GridPath->GetLength();
	Output.Cost = GridPath->GetCost();
	Output.OptimizationMode = GridPath->OptimizationMode;
	Output.TurnCount = GridPath->TurnCount;
	Output.VisitedNodes = GridPath->VisitedNodes;
	Output.Revisions = GridPath->Revisions;
	return Output;
}

FGridReachabilityResult UGridWorldSubsystem::FindReachableCells(const FVector& Origin, double MaxWorldDistance, int32 MaxCells, TSubclassOf<UNavigationQueryFilter> FilterClass) const
{
	FGridReachabilityResult Output;
	AGridNavigationData* NavData = GetNavigationData();
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid() || Origin.ContainsNaN() || MaxWorldDistance < 0.0 || MaxCells <= 0)
	{
		Output.Status = NavData == nullptr ? EGridQueryStatus::InvalidGrid : EGridQueryStatus::InvalidInput;
		return Output;
	}
	FNavLocation StartLocation;
	if (!NavData->ProjectPoint(Origin, StartLocation, FVector(MaxWorldDistance, MaxWorldDistance, MaxWorldDistance)))
	{
		Output.Status = EGridQueryStatus::StartNotNavigable;
		return Output;
	}
	const int32 StartIndex = Snapshot->ResolveNodeRef(StartLocation.NodeRef);
	if (!Snapshot->Cells.IsValidIndex(StartIndex))
	{
		Output.Status = EGridQueryStatus::Stale;
		return Output;
	}
	const FSharedConstNavQueryFilter Filter = FilterClass != nullptr
		? UNavigationQueryFilter::GetQueryFilter(*NavData, FilterClass)
		: NavData->GetDefaultQueryFilter();
	const FGridNavigationQueryFilterImpl* GridFilter = Filter.IsValid()
		? static_cast<const FGridNavigationQueryFilterImpl*>(Filter->GetImplementation())
		: nullptr;
	auto IsTraversable = [&Filter, GridFilter](const FGridCellData& Cell)
	{
		if (!Cell.bWalkable)
		{
			return false;
		}
		if (Filter.IsValid() && ((Cell.TraversalFlags & Filter->GetExcludeFlags()) != 0 || (Cell.TraversalFlags & Filter->GetIncludeFlags()) == 0))
		{
			return false;
		}
		if (GridFilter != nullptr)
		{
			const uint16 ChannelMask = static_cast<uint16>(1u << FMath::Min<uint8>(GridFilter->GetTraversalChannel(), 15));
			if ((Cell.TraversalChannels & ChannelMask) == 0)
			{
				return false;
			}
			if (GridFilter->GetAreaCost(Cell.AreaId) >= BIG_NUMBER * 0.5f)
			{
				return false;
			}
			const bool bReservedByOther = !Cell.ReservationOwners.IsEmpty()
				&& (!GridFilter->GetReservationId().IsValid() || !Cell.ReservationOwners.Contains(GridFilter->GetReservationId()));
			if (GridFilter->GetOccupancyPolicy() == EGridOccupancyPolicy::Block && (Cell.bOccupancyBlocks || bReservedByOther))
			{
				return false;
			}
		}
		return true;
	};

	TArray<int32> Queue;
	TBitArray<> Visited(false, Snapshot->Cells.Num());
	Queue.Add(StartIndex);
	Visited[StartIndex] = true;
	for (int32 QueueIndex = 0; QueueIndex < Queue.Num() && Output.Cells.Num() < MaxCells; ++QueueIndex)
	{
		const int32 CellIndex = Queue[QueueIndex];
		const FGridCellData& Cell = Snapshot->Cells[CellIndex];
		++Output.VisitedNodes;
		if (FVector::DistSquared(Origin, Cell.WorldCenter) > FMath::Square(MaxWorldDistance))
		{
			continue;
		}
		Output.Cells.Add(Cell.Id);
		Output.WorldPoints.Add(Cell.WorldCenter);
		for (const int32 NeighborIndex : Cell.Neighbors)
		{
			if (Snapshot->Cells.IsValidIndex(NeighborIndex) && !Visited[NeighborIndex] && IsTraversable(Snapshot->Cells[NeighborIndex]))
			{
				Visited[NeighborIndex] = true;
				Queue.Add(NeighborIndex);
			}
		}
		if (GridFilter == nullptr || GridFilter->AllowsLinks())
		{
			for (const FGridLinkData& Link : Snapshot->Links)
			{
				int32 LinkedIndex = Link.FromCellIndex == CellIndex
					? Link.ToCellIndex
					: (Link.bBidirectional && Link.ToCellIndex == CellIndex ? Link.FromCellIndex : INDEX_NONE);
				const uint16 ChannelMask = GridFilter != nullptr
					? static_cast<uint16>(1u << FMath::Min<uint8>(GridFilter->GetTraversalChannel(), 15))
					: 1u;
				if (Link.bEnabled && (Link.TraversalChannels & ChannelMask) != 0 && Snapshot->Cells.IsValidIndex(LinkedIndex) && !Visited[LinkedIndex] && IsTraversable(Snapshot->Cells[LinkedIndex]))
				{
					Visited[LinkedIndex] = true;
					Queue.Add(LinkedIndex);
				}
			}
		}
	}
	Output.Status = Queue.Num() > Output.VisitedNodes && Output.Cells.Num() >= MaxCells ? EGridQueryStatus::Partial : EGridQueryStatus::Success;
	Output.Revisions = Snapshot->Revisions;
	NavData->SetDebugReachability(Output.WorldPoints);
	return Output;
}
