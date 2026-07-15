// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"

/** Stable chunk coordinate used to localize generation and dirty-area rebuilds. */
struct GRIDWORLD_API FGridChunkCoord
{
	/** Region containing this chunk. */
	FGuid GridId;
	/** Chunk column in local-grid space. */
	int32 X = 0;
	/** Chunk row in local-grid space. */
	int32 Y = 0;
	/** Quantized vertical chunk layer. */
	int32 Layer = 0;

	bool operator==(const FGridChunkCoord& Other) const
	{
		return GridId == Other.GridId && X == Other.X && Y == Other.Y && Layer == Other.Layer;
	}

	bool operator<(const FGridChunkCoord& Other) const
	{
		if (GridId != Other.GridId)
		{
			return GridId < Other.GridId;
		}
		return Layer != Other.Layer ? Layer < Other.Layer : (Y != Other.Y ? Y < Other.Y : X < Other.X);
	}
};

FORCEINLINE uint32 GetTypeHash(const FGridChunkCoord& Coord)
{
	return HashCombineFast(GetTypeHash(Coord.GridId), HashCombineFast(HashCombineFast(GetTypeHash(Coord.X), GetTypeHash(Coord.Y)), GetTypeHash(Coord.Layer)));
}

/** Immutable generated settings copied from one bounds volume into a snapshot. */
struct GRIDWORLD_API FGridRegionData
{
	/** Persistent bounds identity. */
	FGuid GridId;
	/** Logical coordinate frame and fixed cell dimensions. */
	FGridTransform GridTransform;
	/** World-space AABB used for projection and query pruning. */
	FBox WorldBounds = FBox(ForceInit);
	/** Maximum ordinary directions generated for the region. */
	EGridMovementMode MovementMode = EGridMovementMode::FourDirections;
	/** Region-side diagonal corner-cutting permission. */
	bool bAllowCornerCutting = false;
	/** Physical waypoint policy copied to generated paths. */
	EGridPathFollowingStyle PathFollowingStyle = EGridPathFollowingStyle::Standard;
	/** Precise movement drive policy. */
	EGridPathDriveMode PathDriveMode = EGridPathDriveMode::DirectVelocity;
	/** Applies accelerated braking to the final Direct Velocity segment. */
	bool bUseAcceleratedFinalApproach = false;
	/** Horizontal center-gate tolerance in centimetres. */
	float CellCenterTolerance = 2.0f;
	/** Required final horizontal speed in centimetres per second. */
	float StopSpeedTolerance = 5.0f;
	/** Maximum residual upward discontinuity after removing the natural slope. */
	double MaxStepHeight = 45.0;
	/** Maximum residual downward discontinuity after removing the natural slope. */
	double MaxDropHeight = 45.0;
};

/** Immutable topology plus composed runtime overlay for one dense cell. */
struct GRIDWORLD_API FGridCellData
{
	/** Persistent cell identity. */
	FGridCellId Id;
	/** Generated floor-center position. */
	FVector WorldCenter = FVector::ZeroVector;
	/** Compact immutable world-space floor normal captured during topology generation. */
	FVector3f FloorNormal = FVector3f(0.0f, 0.0f, 1.0f);
	/** Fixed-point base traversal cost. */
	int32 TraversalCost = 1000;
	/** Unreal navigation area ID used by query filter cost overrides. */
	uint8 AreaId = 0;
	/** Include/exclude flags evaluated by the query filter. */
	uint16 TraversalFlags = MAX_uint16;
	/** Link/cell traversal-channel mask. */
	uint16 TraversalChannels = MAX_uint16;
	/** Final walkable state after topology and traversal overlays. */
	bool bWalkable = true;
	/** True when generation/authored topology blocked the cell. */
	bool bAuthoredBlocked = false;
	/** Allows an explicit modifier to remove the authored block. */
	bool bAuthoredBlockCanBeRemoved = false;
	/** True when any runtime occupancy overlaps this cell. */
	bool bOccupied = false;
	/** True when occupancy should block under Occupancy Policy = Block. */
	bool bOccupancyBlocks = false;
	/** Fixed-point occupancy cost used by Occupancy Policy = Add Cost. */
	int32 OccupancyCost = 0;
	/** Runtime-only owners affecting this cell. Used to distinguish an agent from other occupants. */
	TArray<FGuid, TInlineAllocator<2>> OccupancyOwners;
	/** Runtime-only reservation identities affecting this cell. */
	TArray<FGuid, TInlineAllocator<2>> ReservationOwners;
	/** Distinguishes authored manual snapshots from generated floor centers. */
	bool bHasAuthoredWorldCenter = false;
	/** Dense ordinary-neighbour indices; explicit links are stored separately. */
	TArray<int32, TInlineAllocator<8>> Neighbors;
};

/** Immutable authored special transition between two dense cells. */
struct GRIDWORLD_API FGridLinkData
{
	/** Persistent authored link identity. */
	FGuid LinkId;
	/** Dense source cell index. */
	int32 FromCellIndex = INDEX_NONE;
	/** Dense destination cell index. */
	int32 ToCellIndex = INDEX_NONE;
	/** Fixed-point traversal cost. */
	int32 TraversalCost = 1000;
	/** Allowed traversal-channel mask. */
	uint16 TraversalChannels = MAX_uint16;
	/** Enables the reverse transition. */
	bool bBidirectional = true;
	/** Final overlay-enabled state. */
	bool bEnabled = true;
};

/** Dense cells and bounds belonging to one generation chunk. */
struct GRIDWORLD_API FGridChunkData
{
	/** Stable chunk coordinate. */
	FGridChunkCoord Coord;
	/** Dense snapshot cell indices contained by this chunk. */
	TArray<int32> CellIndices;
	/** World-space AABB covering generated cells. */
	FBox WorldBounds = FBox(ForceInit);
};

/** Immutable after Finalize. Query threads may retain this object without touching UObject state. */
struct GRIDWORLD_API FGridWorldSnapshot
{
	static constexpr int32 DefaultChunkSize = 16;

	/** Legacy/single-region identity retained for compatibility. */
	FGuid GridId;
	/** Legacy/single-region transform retained for compatibility. */
	FGridTransform GridTransform;
	/** Independent topology, traversal, and occupancy revisions. */
	FGridRevisionSet Revisions;
	/** AABB covering every published region. */
	FBox WorldBounds = FBox(ForceInit);
	/** Number of cells along each X/Y chunk edge. */
	int32 ChunkSize = DefaultChunkSize;
	/** Hash of supported-agent and generation settings used to reject incompatible data. */
	uint32 AgentSettingsHash = 0;
	/** Dense immutable cell array. */
	TArray<FGridCellData> Cells;
	/** Explicit authored transitions. */
	TArray<FGridLinkData> Links;
	/** Persistent region settings keyed by GridId. */
	TMap<FGuid, FGridRegionData> Regions;
	/** Persistent identity to dense-index lookup rebuilt by Finalize. */
	TMap<FGridCellId, int32> CellIndexById;
	/** Chunk table rebuilt by Finalize. */
	TMap<FGridChunkCoord, FGridChunkData> Chunks;

	/** Validates and indexes the snapshot before publication. @param OutError Optional failure reason. */
	bool Finalize(FString* OutError = nullptr);
	/** Finds Coord in the legacy/single GridId. @return Dense index or INDEX_NONE. */
	int32 FindCellIndex(const FGridCellCoord& Coord) const;
	/** Finds a persistent coordinate in InGridId. @return Dense index or INDEX_NONE. */
	int32 FindCellIndex(const FGuid& InGridId, const FGridCellCoord& Coord) const;
	/** @return Borrowed cell pointer in the legacy/single GridId, or nullptr. */
	const FGridCellData* FindCell(const FGridCellCoord& Coord) const;
	/** @return Borrowed cell pointer for CellId, or nullptr. */
	const FGridCellData* FindCell(const FGridCellId& CellId) const;
	/** @return Borrowed region data for InGridId, or nullptr. */
	const FGridRegionData* FindRegion(const FGuid& InGridId) const { return Regions.Find(InGridId); }
	/** @return True when CellIndex addresses the dense cell array. */
	bool IsCellIndexValid(int32 CellIndex) const { return Cells.IsValidIndex(CellIndex); }
	/** Encodes CellIndex with the current 32-bit topology generation. */
	NavNodeRef MakeNodeRef(int32 CellIndex) const;
	/** Validates and resolves NodeRef. @return Dense index or INDEX_NONE when stale/invalid. */
	int32 ResolveNodeRef(NavNodeRef NodeRef) const;

	/** Packs generation and one-based dense index into a 64-bit Unreal NavNodeRef. */
	static NavNodeRef EncodeNodeRef(uint32 TopologyGeneration, uint32 DenseIndex);
	/** Unpacks a nonzero node ref. @return False when the encoded value is invalid. */
	static bool DecodeNodeRef(NavNodeRef NodeRef, uint32& OutTopologyGeneration, uint32& OutDenseIndex);
	/** Maps a cell coordinate to a stable chunk using floor division for negative coordinates. */
	static FGridChunkCoord CellToChunk(const FGridCellCoord& Coord, int32 InChunkSize = DefaultChunkSize, const FGuid& InGridId = FGuid());
};

/** Thread-safe shared ownership of a published immutable GridWorld snapshot. */
using FGridWorldSnapshotPtr = TSharedPtr<const FGridWorldSnapshot, ESPMode::ThreadSafe>;
