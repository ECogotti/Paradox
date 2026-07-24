// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "Navigation/GridTrafficReservation.h"
#include "Navigation/GridWorldSnapshot.h"

/** Plain-value query consumed by deterministic A* without retaining UObject state. */
struct GRIDWORLD_API FGridAStarQuery
{
	FGridAStarQuery();

	/** Dense start cell index. */
	int32 StartCellIndex = INDEX_NONE;
	/** Dense requested goal cell index. */
	int32 GoalCellIndex = INDEX_NONE;
	/** Query-side direction limit; regions may be stricter. */
	EGridMovementMode MovementMode = EGridMovementMode::EightDirections;
	/** Objective used for open-set ordering. */
	EGridPathOptimizationMode PathOptimizationMode = EGridPathOptimizationMode::ShortestPath;
	/** Fixed-point cost added per turn in Balanced mode. */
	int64 BalancedTurnPenaltyCost = 2000;
	/** Query-side corner-cutting permission. */
	bool bAllowCornerCutting = false;
	/** Allows returning the best geometrically reachable cell. */
	bool bAllowPartialPath = false;
	/** Hard bound on expanded cells or directional states. */
	int32 MaxVisitedNodes = 65536;
	/** Required traversal flags. */
	uint16 IncludeFlags = MAX_uint16;
	/** Rejected traversal flags. */
	uint16 ExcludeFlags = 0;
	/** Enables authored special transitions. */
	bool bAllowLinks = true;
	/** Ordinary occupancy composition policy. */
	EGridOccupancyPolicy OccupancyPolicy = EGridOccupancyPolicy::Ignore;
	/** Live-agent following/avoidance policy. */
	EGridDynamicAgentPolicy DynamicAgentPolicy = EGridDynamicAgentPolicy::Ignore;
	/** Occupancy owner ignored as the querying agent. */
	FGuid IgnoredOccupancyOwnerId;
	/** Immutable traffic view retained for async-safe conflict checks. */
	FGridTrafficReservationSnapshotPtr TrafficReservations;
	/** Querying agent horizontal capsule radius. */
	float TrafficAgentRadius = 42.0f;
	/** Querying agent full capsule height. */
	float TrafficAgentHeight = 192.0f;
	/** Extra requested traffic clearance. */
	float TrafficAdditionalSeparation = 5.0f;
	/** Authored reservation identity allowed to overlap itself. */
	FGuid ReservationId;
	/** Selected 0-15 traversal channel. */
	uint8 TraversalChannel = 0;
	/** Fixed-point multiplicative area costs. */
	TStaticArray<int32, 64> AreaCosts;
	/** Fixed-point entering costs. */
	TStaticArray<int32, 64> AreaEnteringCosts;
};

/** Ordered cell result plus observable search accounting. */
struct GRIDWORLD_API FGridAStarResult
{
	/** Complete, partial, or explicit failure status. */
	EGridQueryStatus Status = EGridQueryStatus::InvalidInput;
	/** Ordered dense cell indices. */
	TArray<int32> CellIndices;
	/** Real traversal cost excluding Balanced synthetic penalties. */
	int64 TotalCost = 0;
	/** Ordinary direction changes in CellIndices. */
	int32 TurnCount = 0;
	/** Expanded cells or directional states. */
	int32 VisitedNodes = 0;
	/** True when MaxVisitedNodes terminated the search. */
	bool bReachedSearchLimit = false;
	/** @return True for complete or allowed partial results. */
	bool IsSuccessful() const { return Status == EGridQueryStatus::Success || Status == EGridQueryStatus::Partial; }
};

/** Pure-data authoritative validation of one supplied ordered cell-index sequence. */
struct GRIDWORLD_API FGridAStarPathValidationResult
{
	FGridAStarResult PathResult;
	EGridInjectedPathFailureReason FailureReason = EGridInjectedPathFailureReason::InvalidPath;
	int32 InvalidCellIndex = INDEX_NONE;
	int32 InvalidSegmentIndex = INDEX_NONE;

	bool IsValid() const { return FailureReason == EGridInjectedPathFailureReason::None; }
};

/** Deterministic, allocation-bounded A* implementation with integer costs. */
class GRIDWORLD_API FGridAStar
{
public:
	static constexpr int32 OrthogonalCost = 1000;
	static constexpr int32 DiagonalCost = 1414;

	/** Executes the selected search strategy against immutable Snapshot. @return Owned path/result values. */
	FGridAStarResult FindPath(const FGridWorldSnapshot& Snapshot, const FGridAStarQuery& Query);
	/** Validates and costs an exact sequence through the same transition rules used by FindPath. */
	FGridAStarPathValidationResult ValidatePath(
		const FGridWorldSnapshot& Snapshot,
		const FGridAStarQuery& Query,
		TConstArrayView<int32> CellIndices,
		bool bIsPartial) const;

private:
	static constexpr int32 DirectionStateCount = 9;
	static constexpr uint8 NoDirection = 0;

	struct FNodeState
	{
		/** Cheapest known real cost from start. */
		int64 G = MAX_int64;
		/** Admissible geometric lower bound to goal. */
		int64 H = MAX_int64;
		/** Dense predecessor used for reconstruction. */
		int32 Parent = INDEX_NONE;
		/** 0 unseen, 1 open, 2 closed. */
		uint8 State = 0;
	};

	struct FOpenEntry
	{
		/** Dense cell represented by the heap entry. */
		int32 CellIndex = INDEX_NONE;
		/** Real start cost captured when enqueued. */
		int64 G = 0;
		/** Heuristic captured when enqueued. */
		int64 H = 0;
		/** @return Open-set score. */
		int64 F() const { return G + H; }
	};

	struct FDirectionalNodeState
	{
		/** Cheapest real traversal cost for this cell plus incoming direction. */
		int64 TraversalCost = MAX_int64;
		/** Turns accumulated for this directional state. */
		int32 TurnCount = MAX_int32;
		/** Encoded predecessor directional state. */
		int32 ParentState = INDEX_NONE;
		/** 0 unseen, 1 open, 2 closed. */
		uint8 State = 0;
	};

	struct FDirectionalOpenEntry
	{
		/** Encoded cell plus incoming-direction state. */
		int32 StateIndex = INDEX_NONE;
		/** Real traversal cost captured when enqueued. */
		int64 TraversalCost = 0;
		/** Turns captured when enqueued. */
		int32 TurnCount = 0;
	};

	/** Reused cell-only states allocated only for Shortest Path. */
	TArray<FNodeState> Nodes;
	/** Binary heap for Shortest Path entries. */
	TArray<FOpenEntry> OpenHeap;
	/** Reused cell-plus-direction states allocated only for turn-aware modes. */
	TArray<FDirectionalNodeState> DirectionalNodes;
	/** Binary heap for turn-aware entries. */
	TArray<FDirectionalOpenEntry> DirectionalOpenHeap;

	/** Runs compatibility-preserving cell-only A*. */
	FGridAStarResult FindShortestPath(const FGridWorldSnapshot& Snapshot, const FGridAStarQuery& Query);
	/** Runs zero-heuristic cell-plus-incoming-direction search. */
	FGridAStarResult FindDirectionalPath(const FGridWorldSnapshot& Snapshot, const FGridAStarQuery& Query);
	/** Calculates horizontal Manhattan/octile cost; Layer is ignored for walking ramps. */
	static int64 CalculateHeuristic(const FGridCellCoord& From, const FGridCellCoord& To, EGridMovementMode MovementMode);
	/** Evaluates topology, region limits, filter flags, occupancy, and traffic reservations. */
	static bool CanTraverse(const FGridWorldSnapshot& Snapshot, int32 FromIndex, int32 ToIndex, const FGridAStarQuery& Query, bool bExplicitLink = false);
	/** @return True when Cell is occupied by an owner other than IgnoredOwnerId. */
	static bool IsOccupiedByOtherAgent(const FGridCellData& Cell, const FGuid& IgnoredOwnerId);
	/** Computes real fixed-point edge cost without synthetic turn penalties. */
	static int64 CalculateMoveCost(const FGridCellData& From, const FGridCellData& To, const FGridAStarQuery& Query, int32 ExplicitLinkCost = 0);
	/** Encodes the eight possible horizontal directions; Layer-only change does not affect direction. */
	static uint8 CalculateDirection(const FGridCellCoord& From, const FGridCellCoord& To);
	/** @return True when transition exists only through an authored link. */
	static bool IsExplicitLinkTransition(const FGridWorldSnapshot& Snapshot, int32 FromIndex, int32 ToIndex);
	/** Counts ordinary direction changes; authored links reset incoming direction. */
	static int32 CountPathTurns(const FGridWorldSnapshot& Snapshot, TConstArrayView<int32> CellIndices);
	/** Saturating Balanced objective calculation. */
	static int64 CalculateBalancedScore(int64 TraversalCost, int32 TurnCount, int64 TurnPenaltyCost);
	/** Compares Fewest Turns or Balanced objectives with deterministic secondary ordering. */
	static bool IsDirectionalScoreBetter(
		int32 LeftTurnCount,
		int64 LeftTraversalCost,
		int32 RightTurnCount,
		int64 RightTraversalCost,
		const FGridAStarQuery& Query);
	/** Deterministic Shortest Path heap comparator. */
	static bool IsEntryPreferred(const FOpenEntry& Left, const FOpenEntry& Right, const FGridWorldSnapshot& Snapshot);
	/** Deterministic turn-aware heap comparator. */
	static bool IsDirectionalEntryPreferred(
		const FDirectionalOpenEntry& Left,
		const FDirectionalOpenEntry& Right,
		const FGridWorldSnapshot& Snapshot,
		const FGridAStarQuery& Query);
	/** Pushes one Shortest Path entry while preserving binary-heap order. */
	void HeapPush(const FOpenEntry& Entry, const FGridWorldSnapshot& Snapshot);
	/** Pops the preferred Shortest Path entry. */
	FOpenEntry HeapPop(const FGridWorldSnapshot& Snapshot);
	/** Pushes one turn-aware entry while preserving policy ordering. */
	void DirectionalHeapPush(const FDirectionalOpenEntry& Entry, const FGridWorldSnapshot& Snapshot, const FGridAStarQuery& Query);
	/** Pops the preferred turn-aware entry. */
	FDirectionalOpenEntry DirectionalHeapPop(const FGridWorldSnapshot& Snapshot, const FGridAStarQuery& Query);
	/** Reconstructs a cell-only predecessor chain into start-to-end order. */
	static void BuildPath(const TArray<FNodeState>& NodeStates, int32 EndIndex, TArray<int32>& OutPath);
	/** Reconstructs and decodes a directional predecessor chain. */
	static void BuildDirectionalPath(const TArray<FDirectionalNodeState>& NodeStates, int32 EndStateIndex, TArray<int32>& OutPath);
};
