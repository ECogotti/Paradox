// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/GridGoalContention.h"

void UE::GridWorld::Private::GatherGridGoalCandidates(
	const FGridWorldSnapshot& Snapshot,
	int32 DesiredCellIndex,
	bool bIncludeDesiredCell,
	int32 MaxSearchRadius,
	const TSet<FGridCellId>& RejectedCells,
	TArray<FGridGoalCandidate>& OutCandidates)
{
	OutCandidates.Reset();
	if (!Snapshot.Cells.IsValidIndex(DesiredCellIndex))
	{
		return;
	}

	const FGridCellId& DesiredCellId = Snapshot.Cells[DesiredCellIndex].Id;
	TArray<int32> Frontier;
	TArray<int32> NextFrontier;
	TSet<int32> Visited;
	Frontier.Add(DesiredCellIndex);
	Visited.Add(DesiredCellIndex);
	for (int32 SearchDistance = 0; SearchDistance <= FMath::Max(0, MaxSearchRadius) && !Frontier.IsEmpty(); ++SearchDistance)
	{
		for (const int32 CellIndex : Frontier)
		{
			const FGridCellData& Cell = Snapshot.Cells[CellIndex];
			if (Cell.bWalkable
				&& (bIncludeDesiredCell || Cell.Id != DesiredCellId)
				&& !RejectedCells.Contains(Cell.Id))
			{
				OutCandidates.Add({CellIndex, SearchDistance});
			}
			for (const int32 NeighborIndex : Cell.Neighbors)
			{
				if (!Snapshot.Cells.IsValidIndex(NeighborIndex)
					|| Visited.Contains(NeighborIndex)
					|| Snapshot.Cells[NeighborIndex].Id.GridId != DesiredCellId.GridId
					|| !Snapshot.Cells[NeighborIndex].bWalkable)
				{
					continue;
				}
				Visited.Add(NeighborIndex);
				NextFrontier.Add(NeighborIndex);
			}
		}
		Frontier = MoveTemp(NextFrontier);
		NextFrontier.Reset();
	}
	OutCandidates.Sort([&Snapshot](const FGridGoalCandidate& Left, const FGridGoalCandidate& Right)
	{
		if (Left.SearchDistance != Right.SearchDistance)
		{
			return Left.SearchDistance < Right.SearchDistance;
		}
		return Snapshot.Cells[Left.CellIndex].Id.Coord < Snapshot.Cells[Right.CellIndex].Id.Coord;
	});
}

bool UE::GridWorld::Private::HasGridGoalOccupancySeparation(
	const FGridWorldSnapshot& Snapshot,
	const FGridCellData& CandidateCell,
	const FGuid& OwnOccupantId,
	float AgentRadius,
	float AgentHeight,
	float AdditionalSeparation)
{
	const double RequiredDistance = FMath::Max(0.0f, AgentRadius * 2.0f + AdditionalSeparation);
	for (const FGridCellData& OccupiedCell : Snapshot.Cells)
	{
		const bool bOccupiedByOther = OccupiedCell.OccupancyOwners.ContainsByPredicate([OwnOccupantId](const FGuid& OwnerId)
		{
			return OwnerId.IsValid() && OwnerId != OwnOccupantId;
		});
		if (!bOccupiedByOther || FMath::Abs(OccupiedCell.WorldCenter.Z - CandidateCell.WorldCenter.Z) >= AgentHeight)
		{
			continue;
		}
		if (FVector::DistSquared2D(OccupiedCell.WorldCenter, CandidateCell.WorldCenter) < FMath::Square(RequiredDistance))
		{
			return false;
		}
	}
	return true;
}

bool UE::GridWorld::Private::BuildStopBeforeOccupiedCells(
	TConstArrayView<FGridCellId> FullPath,
	const FGridCellId& RequestedGoalCell,
	TArray<FGridCellId>& OutAdjustedPath,
	FGridCellId& OutEffectiveGoalCell)
{
	OutAdjustedPath.Reset();
	OutEffectiveGoalCell = FGridCellId();
	if (!RequestedGoalCell.IsValid()
		|| FullPath.Num() < 2
		|| FullPath.Last() != RequestedGoalCell)
	{
		return false;
	}

	OutAdjustedPath.Append(FullPath.GetData(), FullPath.Num() - 1);
	OutEffectiveGoalCell = OutAdjustedPath.Last();
	return OutEffectiveGoalCell.IsValid();
}
