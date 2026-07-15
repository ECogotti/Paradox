// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridAStar.h"

#include "Navigation/GridAStarMath.h"

FGridAStarQuery::FGridAStarQuery()
{
	for (int32 AreaIndex = 0; AreaIndex < AreaCosts.Num(); ++AreaIndex)
	{
		AreaCosts[AreaIndex] = 1000;
		AreaEnteringCosts[AreaIndex] = 0;
	}
}

FGridAStarResult FGridAStar::FindPath(const FGridWorldSnapshot& Snapshot, const FGridAStarQuery& Query)
{
	return Query.PathOptimizationMode == EGridPathOptimizationMode::ShortestPath
		? FindShortestPath(Snapshot, Query)
		: FindDirectionalPath(Snapshot, Query);
}

FGridAStarResult FGridAStar::FindShortestPath(const FGridWorldSnapshot& Snapshot, const FGridAStarQuery& Query)
{
	FGridAStarResult Result;
	if (!Snapshot.Cells.IsValidIndex(Query.StartCellIndex)
		|| !Snapshot.Cells.IsValidIndex(Query.GoalCellIndex)
		|| Query.MaxVisitedNodes <= 0)
	{
		return Result;
	}

	const FGridCellData& StartCell = Snapshot.Cells[Query.StartCellIndex];
	const FGridCellData& GoalCell = Snapshot.Cells[Query.GoalCellIndex];
	if (!StartCell.bWalkable)
	{
		Result.Status = EGridQueryStatus::StartNotNavigable;
		return Result;
	}
	if (!GoalCell.bWalkable)
	{
		Result.Status = EGridQueryStatus::GoalNotNavigable;
		return Result;
	}

	Nodes.SetNum(Snapshot.Cells.Num(), EAllowShrinking::No);
	for (FNodeState& Node : Nodes)
	{
		Node = FNodeState();
	}
	OpenHeap.Reset(FMath::Min(Snapshot.Cells.Num(), Query.MaxVisitedNodes));

	FNodeState& StartState = Nodes[Query.StartCellIndex];
	StartState.G = 0;
	const bool bUseZeroHeuristic = Query.bAllowLinks && !Snapshot.Links.IsEmpty();
	StartState.H = bUseZeroHeuristic ? 0 : CalculateHeuristic(StartCell.Id.Coord, GoalCell.Id.Coord, Query.MovementMode);
	StartState.State = 1;
	HeapPush(FOpenEntry{Query.StartCellIndex, 0, StartState.H}, Snapshot);

	int32 BestPartialIndex = Query.StartCellIndex;
	while (!OpenHeap.IsEmpty() && Result.VisitedNodes < Query.MaxVisitedNodes)
	{
		const FOpenEntry CurrentEntry = HeapPop(Snapshot);
		FNodeState& CurrentState = Nodes[CurrentEntry.CellIndex];
		if (CurrentState.State == 2 || CurrentEntry.G != CurrentState.G)
		{
			continue;
		}

		CurrentState.State = 2;
		++Result.VisitedNodes;
		if (CurrentEntry.CellIndex == Query.GoalCellIndex)
		{
			Result.Status = EGridQueryStatus::Success;
			Result.TotalCost = CurrentState.G;
			BuildPath(Nodes, CurrentEntry.CellIndex, Result.CellIndices);
			Result.TurnCount = CountPathTurns(Snapshot, Result.CellIndices);
			return Result;
		}

		const FNodeState& BestState = Nodes[BestPartialIndex];
		if (CurrentState.H < BestState.H
			|| (CurrentState.H == BestState.H && CurrentState.G < BestState.G)
			|| (CurrentState.H == BestState.H && CurrentState.G == BestState.G
				&& Snapshot.Cells[CurrentEntry.CellIndex].Id.Coord < Snapshot.Cells[BestPartialIndex].Id.Coord))
		{
			BestPartialIndex = CurrentEntry.CellIndex;
		}

		const FGridCellData& CurrentCell = Snapshot.Cells[CurrentEntry.CellIndex];
		for (const int32 NeighborIndex : CurrentCell.Neighbors)
		{
			if (!CanTraverse(Snapshot, CurrentEntry.CellIndex, NeighborIndex, Query))
			{
				continue;
			}

			FNodeState& NeighborState = Nodes[NeighborIndex];
			if (NeighborState.State == 2)
			{
				continue;
			}

			const int64 TentativeG = CurrentState.G + CalculateMoveCost(CurrentCell, Snapshot.Cells[NeighborIndex], Query);
			if (TentativeG >= NeighborState.G)
			{
				continue;
			}

			NeighborState.G = TentativeG;
			NeighborState.H = bUseZeroHeuristic ? 0 : CalculateHeuristic(Snapshot.Cells[NeighborIndex].Id.Coord, GoalCell.Id.Coord, Query.MovementMode);
			NeighborState.Parent = CurrentEntry.CellIndex;
			NeighborState.State = 1;
			HeapPush(FOpenEntry{NeighborIndex, NeighborState.G, NeighborState.H}, Snapshot);
		}

		if (Query.bAllowLinks)
		{
			for (const FGridLinkData& Link : Snapshot.Links)
			{
				int32 NeighborIndex = INDEX_NONE;
				const uint16 QueryChannelMask = static_cast<uint16>(1u << FMath::Min<uint8>(Query.TraversalChannel, 15));
				if (Link.bEnabled && (Link.TraversalChannels & QueryChannelMask) != 0 && Link.FromCellIndex == CurrentEntry.CellIndex)
				{
					NeighborIndex = Link.ToCellIndex;
				}
				else if (Link.bEnabled && (Link.TraversalChannels & QueryChannelMask) != 0 && Link.bBidirectional && Link.ToCellIndex == CurrentEntry.CellIndex)
				{
					NeighborIndex = Link.FromCellIndex;
				}
				if (!Snapshot.Cells.IsValidIndex(NeighborIndex) || !CanTraverse(Snapshot, CurrentEntry.CellIndex, NeighborIndex, Query, true))
				{
					continue;
				}
				FNodeState& NeighborState = Nodes[NeighborIndex];
				if (NeighborState.State == 2)
				{
					continue;
				}
				const int64 TentativeG = CurrentState.G + CalculateMoveCost(CurrentCell, Snapshot.Cells[NeighborIndex], Query, Link.TraversalCost);
				if (TentativeG < NeighborState.G)
				{
					NeighborState.G = TentativeG;
					NeighborState.H = bUseZeroHeuristic ? 0 : CalculateHeuristic(Snapshot.Cells[NeighborIndex].Id.Coord, GoalCell.Id.Coord, Query.MovementMode);
					NeighborState.Parent = CurrentEntry.CellIndex;
					NeighborState.State = 1;
					HeapPush(FOpenEntry{NeighborIndex, NeighborState.G, NeighborState.H}, Snapshot);
				}
			}
		}
	}

	Result.bReachedSearchLimit = !OpenHeap.IsEmpty() && Result.VisitedNodes >= Query.MaxVisitedNodes;
	if (Query.bAllowPartialPath && BestPartialIndex != Query.StartCellIndex)
	{
		Result.Status = EGridQueryStatus::Partial;
		Result.TotalCost = Nodes[BestPartialIndex].G;
		BuildPath(Nodes, BestPartialIndex, Result.CellIndices);
		Result.TurnCount = CountPathTurns(Snapshot, Result.CellIndices);
	}
	else
	{
		Result.Status = EGridQueryStatus::Unreachable;
	}
	return Result;
}

FGridAStarResult FGridAStar::FindDirectionalPath(const FGridWorldSnapshot& Snapshot, const FGridAStarQuery& Query)
{
	FGridAStarResult Result;
	if (!Snapshot.Cells.IsValidIndex(Query.StartCellIndex)
		|| !Snapshot.Cells.IsValidIndex(Query.GoalCellIndex)
		|| Query.MaxVisitedNodes <= 0)
	{
		return Result;
	}

	const FGridCellData& StartCell = Snapshot.Cells[Query.StartCellIndex];
	const FGridCellData& GoalCell = Snapshot.Cells[Query.GoalCellIndex];
	if (!StartCell.bWalkable)
	{
		Result.Status = EGridQueryStatus::StartNotNavigable;
		return Result;
	}
	if (!GoalCell.bWalkable)
	{
		Result.Status = EGridQueryStatus::GoalNotNavigable;
		return Result;
	}
	if (Snapshot.Cells.Num() > MAX_int32 / DirectionStateCount)
	{
		Result.Status = EGridQueryStatus::InternalError;
		return Result;
	}

	const int32 TotalStateCount = Snapshot.Cells.Num() * DirectionStateCount;
	DirectionalNodes.SetNum(TotalStateCount, EAllowShrinking::No);
	for (FDirectionalNodeState& Node : DirectionalNodes)
	{
		Node = FDirectionalNodeState();
	}
	DirectionalOpenHeap.Reset(FMath::Min(TotalStateCount, Query.MaxVisitedNodes));

	const int32 StartStateIndex = Query.StartCellIndex * DirectionStateCount + NoDirection;
	FDirectionalNodeState& StartState = DirectionalNodes[StartStateIndex];
	StartState.TraversalCost = 0;
	StartState.TurnCount = 0;
	StartState.State = 1;
	DirectionalHeapPush(FDirectionalOpenEntry{StartStateIndex, 0, 0}, Snapshot, Query);

	int32 BestPartialStateIndex = StartStateIndex;
	int64 BestRemainingDistance = CalculateHeuristic(StartCell.Id.Coord, GoalCell.Id.Coord, Query.MovementMode);
	while (!DirectionalOpenHeap.IsEmpty() && Result.VisitedNodes < Query.MaxVisitedNodes)
	{
		const FDirectionalOpenEntry CurrentEntry = DirectionalHeapPop(Snapshot, Query);
		FDirectionalNodeState& CurrentState = DirectionalNodes[CurrentEntry.StateIndex];
		if (CurrentState.State == 2
			|| CurrentEntry.TraversalCost != CurrentState.TraversalCost
			|| CurrentEntry.TurnCount != CurrentState.TurnCount)
		{
			continue;
		}

		CurrentState.State = 2;
		++Result.VisitedNodes;
		const int32 CurrentCellIndex = CurrentEntry.StateIndex / DirectionStateCount;
		const uint8 CurrentDirection = static_cast<uint8>(CurrentEntry.StateIndex % DirectionStateCount);
		if (CurrentCellIndex == Query.GoalCellIndex)
		{
			Result.Status = EGridQueryStatus::Success;
			Result.TotalCost = CurrentState.TraversalCost;
			Result.TurnCount = CurrentState.TurnCount;
			BuildDirectionalPath(DirectionalNodes, CurrentEntry.StateIndex, Result.CellIndices);
			return Result;
		}

		const FGridCellData& CurrentCell = Snapshot.Cells[CurrentCellIndex];
		const int64 RemainingDistance = CalculateHeuristic(CurrentCell.Id.Coord, GoalCell.Id.Coord, Query.MovementMode);
		const FDirectionalNodeState& BestPartialState = DirectionalNodes[BestPartialStateIndex];
		const FDirectionalOpenEntry CandidatePartialEntry{
			CurrentEntry.StateIndex,
			CurrentState.TraversalCost,
			CurrentState.TurnCount};
		const FDirectionalOpenEntry BestPartialEntry{
			BestPartialStateIndex,
			BestPartialState.TraversalCost,
			BestPartialState.TurnCount};
		if (CurrentCellIndex != Query.StartCellIndex
			&& (BestPartialStateIndex == StartStateIndex
				|| RemainingDistance < BestRemainingDistance
				|| (RemainingDistance == BestRemainingDistance
					&& IsDirectionalEntryPreferred(CandidatePartialEntry, BestPartialEntry, Snapshot, Query))))
		{
			BestPartialStateIndex = CurrentEntry.StateIndex;
			BestRemainingDistance = RemainingDistance;
		}

		auto RelaxEdge = [this, &Snapshot, &Query, &CurrentState, CurrentEntry, CurrentDirection](
			int32 NeighborCellIndex,
			uint8 NextDirection,
			int64 MoveCost)
		{
			if (!Snapshot.Cells.IsValidIndex(NeighborCellIndex) || NextDirection >= DirectionStateCount)
			{
				return;
			}
			const int32 NeighborStateIndex = NeighborCellIndex * DirectionStateCount + NextDirection;
			FDirectionalNodeState& NeighborState = DirectionalNodes[NeighborStateIndex];
			if (NeighborState.State == 2)
			{
				return;
			}
			const int32 TurnIncrement = CurrentDirection != NoDirection
				&& NextDirection != NoDirection
				&& CurrentDirection != NextDirection
				? 1
				: 0;
			const int32 CandidateTurnCount = CurrentState.TurnCount < MAX_int32 - TurnIncrement
				? CurrentState.TurnCount + TurnIncrement
				: MAX_int32;
			const int64 CandidateTraversalCost = CurrentState.TraversalCost <= MAX_int64 - MoveCost
				? CurrentState.TraversalCost + MoveCost
				: MAX_int64;
			if (!IsDirectionalScoreBetter(
				CandidateTurnCount,
				CandidateTraversalCost,
				NeighborState.TurnCount,
				NeighborState.TraversalCost,
				Query))
			{
				return;
			}

			NeighborState.TraversalCost = CandidateTraversalCost;
			NeighborState.TurnCount = CandidateTurnCount;
			NeighborState.ParentState = CurrentEntry.StateIndex;
			NeighborState.State = 1;
			DirectionalHeapPush(
				FDirectionalOpenEntry{NeighborStateIndex, CandidateTraversalCost, CandidateTurnCount},
				Snapshot,
				Query);
		};

		for (const int32 NeighborCellIndex : CurrentCell.Neighbors)
		{
			if (!CanTraverse(Snapshot, CurrentCellIndex, NeighborCellIndex, Query))
			{
				continue;
			}
			const uint8 NextDirection = CalculateDirection(CurrentCell.Id.Coord, Snapshot.Cells[NeighborCellIndex].Id.Coord);
			if (NextDirection == NoDirection)
			{
				continue;
			}
			RelaxEdge(
				NeighborCellIndex,
				NextDirection,
				CalculateMoveCost(CurrentCell, Snapshot.Cells[NeighborCellIndex], Query));
		}

		if (Query.bAllowLinks)
		{
			for (const FGridLinkData& Link : Snapshot.Links)
			{
				int32 NeighborCellIndex = INDEX_NONE;
				const uint16 QueryChannelMask = static_cast<uint16>(1u << FMath::Min<uint8>(Query.TraversalChannel, 15));
				if (Link.bEnabled && (Link.TraversalChannels & QueryChannelMask) != 0 && Link.FromCellIndex == CurrentCellIndex)
				{
					NeighborCellIndex = Link.ToCellIndex;
				}
				else if (Link.bEnabled
					&& (Link.TraversalChannels & QueryChannelMask) != 0
					&& Link.bBidirectional
					&& Link.ToCellIndex == CurrentCellIndex)
				{
					NeighborCellIndex = Link.FromCellIndex;
				}
				if (!Snapshot.Cells.IsValidIndex(NeighborCellIndex)
					|| !CanTraverse(Snapshot, CurrentCellIndex, NeighborCellIndex, Query, true))
				{
					continue;
				}
				RelaxEdge(
					NeighborCellIndex,
					NoDirection,
					CalculateMoveCost(CurrentCell, Snapshot.Cells[NeighborCellIndex], Query, Link.TraversalCost));
			}
		}
	}

	Result.bReachedSearchLimit = !DirectionalOpenHeap.IsEmpty() && Result.VisitedNodes >= Query.MaxVisitedNodes;
	if (Query.bAllowPartialPath && BestPartialStateIndex != StartStateIndex)
	{
		const FDirectionalNodeState& BestPartialState = DirectionalNodes[BestPartialStateIndex];
		Result.Status = EGridQueryStatus::Partial;
		Result.TotalCost = BestPartialState.TraversalCost;
		Result.TurnCount = BestPartialState.TurnCount;
		BuildDirectionalPath(DirectionalNodes, BestPartialStateIndex, Result.CellIndices);
	}
	else
	{
		Result.Status = EGridQueryStatus::Unreachable;
	}
	return Result;
}

int64 FGridAStar::CalculateHeuristic(const FGridCellCoord& From, const FGridCellCoord& To, EGridMovementMode MovementMode)
{
	return UE::GridWorld::AStarMath::CalculateHeuristic(
		From,
		To,
		MovementMode,
		OrthogonalCost,
		DiagonalCost);
}

bool FGridAStar::CanTraverse(const FGridWorldSnapshot& Snapshot, int32 FromIndex, int32 ToIndex, const FGridAStarQuery& Query, bool bExplicitLink)
{
	if (!Snapshot.Cells.IsValidIndex(FromIndex) || !Snapshot.Cells.IsValidIndex(ToIndex))
	{
		return false;
	}
	const FGridCellData& To = Snapshot.Cells[ToIndex];
	if (!To.bWalkable || (To.TraversalFlags & Query.ExcludeFlags) != 0 || (To.TraversalFlags & Query.IncludeFlags) == 0)
	{
		return false;
	}
	const uint16 QueryChannelMask = static_cast<uint16>(1u << FMath::Min<uint8>(Query.TraversalChannel, 15));
	if ((To.TraversalChannels & QueryChannelMask) == 0)
	{
		return false;
	}
	const bool bReservedByOther = !To.ReservationOwners.IsEmpty()
		&& (!Query.ReservationId.IsValid() || !To.ReservationOwners.Contains(Query.ReservationId));
	if (Query.OccupancyPolicy == EGridOccupancyPolicy::Block && (To.bOccupancyBlocks || bReservedByOther))
	{
		return false;
	}
	if (Query.DynamicAgentPolicy == EGridDynamicAgentPolicy::YieldThenRepath
		&& ToIndex != Query.GoalCellIndex
		&& IsOccupiedByOtherAgent(To, Query.IgnoredOccupancyOwnerId))
	{
		return false;
	}
	if (Query.DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor
		&& Query.TrafficReservations.IsValid())
	{
		const FGridCellData& From = Snapshot.Cells[FromIndex];
		if (Query.TrafficReservations->ConflictsWithCell(
			To.WorldCenter,
			Query.TrafficAgentRadius,
			Query.TrafficAgentHeight,
			Query.TrafficAdditionalSeparation,
			Query.IgnoredOccupancyOwnerId)
			|| Query.TrafficReservations->ConflictsWithSegment(
				From.WorldCenter,
				To.WorldCenter,
				Query.TrafficAgentRadius,
				Query.TrafficAgentHeight,
				Query.TrafficAdditionalSeparation,
				Query.IgnoredOccupancyOwnerId))
		{
			return false;
		}
	}

	if (To.AreaId >= Query.AreaCosts.Num() || Query.AreaCosts[To.AreaId] >= MAX_int32 / 4)
	{
		return false;
	}
	const FGridCellCoord& FromCoord = Snapshot.Cells[FromIndex].Id.Coord;
	const FGridCellCoord& ToCoord = To.Id.Coord;
	const int32 DeltaX = FMath::Abs(ToCoord.X - FromCoord.X);
	const int32 DeltaY = FMath::Abs(ToCoord.Y - FromCoord.Y);
	const bool bDiagonal = DeltaX == 1 && DeltaY == 1;
	if (bExplicitLink || !bDiagonal)
	{
		return true;
	}
	const FGridRegionData* Region = Snapshot.FindRegion(Snapshot.Cells[FromIndex].Id.GridId);
	if (Region == nullptr
		|| Region->MovementMode != EGridMovementMode::EightDirections
		|| Query.MovementMode != EGridMovementMode::EightDirections)
	{
		return false;
	}
	if (Region->bAllowCornerCutting && Query.bAllowCornerCutting)
	{
		return true;
	}

	const FGridCellData& From = Snapshot.Cells[FromIndex];
	auto HasPublishedWalkableSide = [&Snapshot, &From](int32 SideX, int32 SideY)
	{
		for (const int32 NeighborIndex : From.Neighbors)
		{
			if (!Snapshot.Cells.IsValidIndex(NeighborIndex))
			{
				continue;
			}
			const FGridCellData& Side = Snapshot.Cells[NeighborIndex];
			if (Side.Id.GridId == From.Id.GridId
				&& Side.Id.Coord.X == SideX
				&& Side.Id.Coord.Y == SideY
				&& Side.bWalkable)
			{
				return true;
			}
		}
		return false;
	};
	return HasPublishedWalkableSide(ToCoord.X, FromCoord.Y)
		&& HasPublishedWalkableSide(FromCoord.X, ToCoord.Y);
}

bool FGridAStar::IsOccupiedByOtherAgent(const FGridCellData& Cell, const FGuid& IgnoredOwnerId)
{
	return Cell.OccupancyOwners.ContainsByPredicate([&Cell, &IgnoredOwnerId](const FGuid& OwnerId)
	{
		return OwnerId != IgnoredOwnerId && !Cell.ReservationOwners.Contains(OwnerId);
	});
}

int64 FGridAStar::CalculateMoveCost(const FGridCellData& From, const FGridCellData& To, const FGridAStarQuery& Query, int32 ExplicitLinkCost)
{
	const int32 DeltaX = FMath::Abs(To.Id.Coord.X - From.Id.Coord.X);
	const int32 DeltaY = FMath::Abs(To.Id.Coord.Y - From.Id.Coord.Y);
	const int64 DistanceCost = ExplicitLinkCost > 0 ? ExplicitLinkCost : (DeltaX == 1 && DeltaY == 1 ? DiagonalCost : OrthogonalCost);
	const int64 CellCost = FMath::Max<int64>(1, (DistanceCost * static_cast<int64>(To.TraversalCost) + 500) / 1000);
	const int64 AreaCost = To.AreaId < Query.AreaCosts.Num() ? Query.AreaCosts[To.AreaId] : 1000;
	const int64 EnteringCost = To.AreaId < Query.AreaEnteringCosts.Num() ? Query.AreaEnteringCosts[To.AreaId] : 0;
	const int64 DynamicOccupancyCost = Query.OccupancyPolicy == EGridOccupancyPolicy::AddCost ? To.OccupancyCost : 0;
	return FMath::Max<int64>(1, (CellCost * AreaCost + 500) / 1000 + EnteringCost + DynamicOccupancyCost);
}

uint8 FGridAStar::CalculateDirection(const FGridCellCoord& From, const FGridCellCoord& To)
{
	const int32 DeltaX = FMath::Clamp(To.X - From.X, -1, 1);
	const int32 DeltaY = FMath::Clamp(To.Y - From.Y, -1, 1);
	if (DeltaX == 1)
	{
		return DeltaY > 0 ? 2 : (DeltaY < 0 ? 8 : 1);
	}
	if (DeltaX == -1)
	{
		return DeltaY > 0 ? 4 : (DeltaY < 0 ? 6 : 5);
	}
	return DeltaY > 0 ? 3 : (DeltaY < 0 ? 7 : NoDirection);
}

bool FGridAStar::IsExplicitLinkTransition(const FGridWorldSnapshot& Snapshot, int32 FromIndex, int32 ToIndex)
{
	return Snapshot.Links.ContainsByPredicate([FromIndex, ToIndex](const FGridLinkData& Link)
	{
		return Link.bEnabled
			&& ((Link.FromCellIndex == FromIndex && Link.ToCellIndex == ToIndex)
				|| (Link.bBidirectional && Link.ToCellIndex == FromIndex && Link.FromCellIndex == ToIndex));
	});
}

int32 FGridAStar::CountPathTurns(const FGridWorldSnapshot& Snapshot, TConstArrayView<int32> CellIndices)
{
	int32 TurnCount = 0;
	uint8 PreviousDirection = NoDirection;
	for (int32 PathIndex = 1; PathIndex < CellIndices.Num(); ++PathIndex)
	{
		const int32 FromIndex = CellIndices[PathIndex - 1];
		const int32 ToIndex = CellIndices[PathIndex];
		if (!Snapshot.Cells.IsValidIndex(FromIndex) || !Snapshot.Cells.IsValidIndex(ToIndex))
		{
			continue;
		}
		if (IsExplicitLinkTransition(Snapshot, FromIndex, ToIndex))
		{
			PreviousDirection = NoDirection;
			continue;
		}
		const uint8 Direction = CalculateDirection(Snapshot.Cells[FromIndex].Id.Coord, Snapshot.Cells[ToIndex].Id.Coord);
		if (Direction == NoDirection)
		{
			continue;
		}
		if (PreviousDirection != NoDirection && PreviousDirection != Direction)
		{
			++TurnCount;
		}
		PreviousDirection = Direction;
	}
	return TurnCount;
}

int64 FGridAStar::CalculateBalancedScore(int64 TraversalCost, int32 TurnCount, int64 TurnPenaltyCost)
{
	if (TraversalCost == MAX_int64)
	{
		return MAX_int64;
	}
	const int64 SafeTurnPenalty = FMath::Max<int64>(0, TurnPenaltyCost);
	if (TurnCount <= 0 || SafeTurnPenalty == 0)
	{
		return TraversalCost;
	}
	if (SafeTurnPenalty > (MAX_int64 - TraversalCost) / TurnCount)
	{
		return MAX_int64;
	}
	return TraversalCost + SafeTurnPenalty * TurnCount;
}

bool FGridAStar::IsDirectionalScoreBetter(
	int32 LeftTurnCount,
	int64 LeftTraversalCost,
	int32 RightTurnCount,
	int64 RightTraversalCost,
	const FGridAStarQuery& Query)
{
	if (Query.PathOptimizationMode == EGridPathOptimizationMode::FewestTurns)
	{
		return LeftTurnCount != RightTurnCount
			? LeftTurnCount < RightTurnCount
			: LeftTraversalCost < RightTraversalCost;
	}

	const int64 LeftScore = CalculateBalancedScore(LeftTraversalCost, LeftTurnCount, Query.BalancedTurnPenaltyCost);
	const int64 RightScore = CalculateBalancedScore(RightTraversalCost, RightTurnCount, Query.BalancedTurnPenaltyCost);
	if (LeftScore != RightScore)
	{
		return LeftScore < RightScore;
	}
	if (LeftTurnCount != RightTurnCount)
	{
		return LeftTurnCount < RightTurnCount;
	}
	return LeftTraversalCost < RightTraversalCost;
}

bool FGridAStar::IsEntryPreferred(const FOpenEntry& Left, const FOpenEntry& Right, const FGridWorldSnapshot& Snapshot)
{
	if (Left.F() != Right.F())
	{
		return Left.F() < Right.F();
	}
	if (Left.H != Right.H)
	{
		return Left.H < Right.H;
	}
	if (Left.G != Right.G)
	{
		return Left.G < Right.G;
	}
	return Snapshot.Cells[Left.CellIndex].Id.Coord < Snapshot.Cells[Right.CellIndex].Id.Coord;
}

bool FGridAStar::IsDirectionalEntryPreferred(
	const FDirectionalOpenEntry& Left,
	const FDirectionalOpenEntry& Right,
	const FGridWorldSnapshot& Snapshot,
	const FGridAStarQuery& Query)
{
	if (IsDirectionalScoreBetter(
		Left.TurnCount,
		Left.TraversalCost,
		Right.TurnCount,
		Right.TraversalCost,
		Query))
	{
		return true;
	}
	if (IsDirectionalScoreBetter(
		Right.TurnCount,
		Right.TraversalCost,
		Left.TurnCount,
		Left.TraversalCost,
		Query))
	{
		return false;
	}

	const int32 LeftCellIndex = Left.StateIndex / DirectionStateCount;
	const int32 RightCellIndex = Right.StateIndex / DirectionStateCount;
	const FGridCellId& LeftCellId = Snapshot.Cells[LeftCellIndex].Id;
	const FGridCellId& RightCellId = Snapshot.Cells[RightCellIndex].Id;
	if (LeftCellId.GridId != RightCellId.GridId)
	{
		return LeftCellId.GridId < RightCellId.GridId;
	}
	if (LeftCellId.Coord != RightCellId.Coord)
	{
		return LeftCellId.Coord < RightCellId.Coord;
	}
	return Left.StateIndex % DirectionStateCount < Right.StateIndex % DirectionStateCount;
}

void FGridAStar::HeapPush(const FOpenEntry& Entry, const FGridWorldSnapshot& Snapshot)
{
	int32 ChildIndex = OpenHeap.Add(Entry);
	while (ChildIndex > 0)
	{
		const int32 ParentIndex = (ChildIndex - 1) / 2;
		if (!IsEntryPreferred(OpenHeap[ChildIndex], OpenHeap[ParentIndex], Snapshot))
		{
			break;
		}
		OpenHeap.Swap(ChildIndex, ParentIndex);
		ChildIndex = ParentIndex;
	}
}

FGridAStar::FOpenEntry FGridAStar::HeapPop(const FGridWorldSnapshot& Snapshot)
{
	check(!OpenHeap.IsEmpty());
	const FOpenEntry Result = OpenHeap[0];
	if (OpenHeap.Num() == 1)
	{
		OpenHeap.Pop(EAllowShrinking::No);
		return Result;
	}

	OpenHeap[0] = OpenHeap.Pop(EAllowShrinking::No);
	int32 ParentIndex = 0;
	while (true)
	{
		const int32 LeftIndex = ParentIndex * 2 + 1;
		if (!OpenHeap.IsValidIndex(LeftIndex))
		{
			break;
		}
		const int32 RightIndex = LeftIndex + 1;
		int32 PreferredChild = LeftIndex;
		if (OpenHeap.IsValidIndex(RightIndex) && IsEntryPreferred(OpenHeap[RightIndex], OpenHeap[LeftIndex], Snapshot))
		{
			PreferredChild = RightIndex;
		}
		if (!IsEntryPreferred(OpenHeap[PreferredChild], OpenHeap[ParentIndex], Snapshot))
		{
			break;
		}
		OpenHeap.Swap(ParentIndex, PreferredChild);
		ParentIndex = PreferredChild;
	}
	return Result;
}

void FGridAStar::DirectionalHeapPush(
	const FDirectionalOpenEntry& Entry,
	const FGridWorldSnapshot& Snapshot,
	const FGridAStarQuery& Query)
{
	int32 ChildIndex = DirectionalOpenHeap.Add(Entry);
	while (ChildIndex > 0)
	{
		const int32 ParentIndex = (ChildIndex - 1) / 2;
		if (!IsDirectionalEntryPreferred(DirectionalOpenHeap[ChildIndex], DirectionalOpenHeap[ParentIndex], Snapshot, Query))
		{
			break;
		}
		DirectionalOpenHeap.Swap(ChildIndex, ParentIndex);
		ChildIndex = ParentIndex;
	}
}

FGridAStar::FDirectionalOpenEntry FGridAStar::DirectionalHeapPop(
	const FGridWorldSnapshot& Snapshot,
	const FGridAStarQuery& Query)
{
	check(!DirectionalOpenHeap.IsEmpty());
	const FDirectionalOpenEntry Result = DirectionalOpenHeap[0];
	if (DirectionalOpenHeap.Num() == 1)
	{
		DirectionalOpenHeap.Pop(EAllowShrinking::No);
		return Result;
	}

	DirectionalOpenHeap[0] = DirectionalOpenHeap.Pop(EAllowShrinking::No);
	int32 ParentIndex = 0;
	while (true)
	{
		const int32 LeftIndex = ParentIndex * 2 + 1;
		if (!DirectionalOpenHeap.IsValidIndex(LeftIndex))
		{
			break;
		}
		const int32 RightIndex = LeftIndex + 1;
		int32 PreferredChild = LeftIndex;
		if (DirectionalOpenHeap.IsValidIndex(RightIndex)
			&& IsDirectionalEntryPreferred(DirectionalOpenHeap[RightIndex], DirectionalOpenHeap[LeftIndex], Snapshot, Query))
		{
			PreferredChild = RightIndex;
		}
		if (!IsDirectionalEntryPreferred(DirectionalOpenHeap[PreferredChild], DirectionalOpenHeap[ParentIndex], Snapshot, Query))
		{
			break;
		}
		DirectionalOpenHeap.Swap(ParentIndex, PreferredChild);
		ParentIndex = PreferredChild;
	}
	return Result;
}

void FGridAStar::BuildPath(const TArray<FNodeState>& NodeStates, int32 EndIndex, TArray<int32>& OutPath)
{
	OutPath.Reset();
	for (int32 Index = EndIndex; Index != INDEX_NONE; Index = NodeStates[Index].Parent)
	{
		OutPath.Add(Index);
	}
	Algo::Reverse(OutPath);
}

void FGridAStar::BuildDirectionalPath(
	const TArray<FDirectionalNodeState>& NodeStates,
	int32 EndStateIndex,
	TArray<int32>& OutPath)
{
	OutPath.Reset();
	for (int32 StateIndex = EndStateIndex;
		StateIndex != INDEX_NONE;
		StateIndex = NodeStates[StateIndex].ParentState)
	{
		OutPath.Add(StateIndex / DirectionStateCount);
	}
	Algo::Reverse(OutPath);
}
