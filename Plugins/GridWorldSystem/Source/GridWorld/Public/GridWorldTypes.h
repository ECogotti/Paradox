// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.generated.h"

/** Explicit outcome shared by synchronous and asynchronous GridWorld queries. */
UENUM(BlueprintType)
enum class EGridQueryStatus : uint8
{
	/** Complete valid result. */
	Success,
	/** Best reachable prefix returned because the goal or search budget prevented completion. */
	Partial,
	/** One or more caller-supplied values are invalid. */
	InvalidInput,
	/** No matching authoritative grid exists. */
	InvalidGrid,
	/** Start projection failed. */
	StartNotNavigable,
	/** Goal projection failed. */
	GoalNotNavigable,
	/** Start and goal are valid but disconnected under the selected filter. */
	Unreachable,
	/** Query was cancelled before publication. */
	Cancelled,
	/** A revisioned transient reference no longer matches current topology. */
	Stale,
	/** Requested operation is not implemented by GridWorld. */
	Unsupported,
	/** An invariant failed despite otherwise valid input. */
	InternalError
};

/** Maximum ordinary-neighbour directions allowed by a region or query. */
UENUM(BlueprintType)
enum class EGridMovementMode : uint8
{
	/** Orthogonal X/Y neighbours only. */
	FourDirections,
	/** Orthogonal and diagonal X/Y neighbours. */
	EightDirections
};

/** Selects the objective used to choose between valid GridWorld paths. */
UENUM(BlueprintType)
enum class EGridPathOptimizationMode : uint8
{
	/** Minimize real traversal cost using the optimized cell-only A*. */
	ShortestPath UMETA(DisplayName = "Shortest Path"),
	/** Lexicographically minimize turns, then real traversal cost. */
	FewestTurns UMETA(DisplayName = "Fewest Turns"),
	/** Minimize real cost plus the configured equivalent-cell turn penalty. */
	Balanced UMETA(DisplayName = "Balanced")
};

/** Physical path-following policy authored per GridWorld bounds region. */
UENUM(BlueprintType)
enum class EGridPathFollowingStyle : uint8
{
	/** Delegate every waypoint and completion decision to Unreal path following. */
	Standard UMETA(DisplayName = "Standard"),
	/** Enforce curve/final center gates while allowing continuous collinear movement. */
	CenterConstrained UMETA(DisplayName = "Center-Constrained"),
	/** Enforce every ordered cell-center gate. */
	CellByCell UMETA(DisplayName = "Cell-by-Cell")
};

/** Chooses how precise GridWorld path segments drive the navigation movement interface. */
UENUM(BlueprintType)
enum class EGridPathDriveMode : uint8
{
	/** Use acceleration, braking, and inertia from the movement component. */
	Accelerated UMETA(DisplayName = "Accelerated"),
	/** Request constant segment velocity and instant direction changes. */
	DirectVelocity UMETA(DisplayName = "Direct Velocity")
};

/** Determines how ordinary runtime occupancy affects one path query. */
UENUM(BlueprintType)
enum class EGridOccupancyPolicy : uint8
{
	/** Occupancy does not alter path selection. */
	Ignore,
	/** Occupancy contributes its configured traversal cost. */
	AddCost,
	/** Blocking occupancy makes affected cells unavailable. */
	Block
};

/** Optional runtime response to other GridWorld-controlled Pawns occupying the upcoming corridor. */
UENUM(BlueprintType)
enum class EGridDynamicAgentPolicy : uint8
{
	/** Do not inspect other moving agents. */
	Ignore UMETA(DisplayName = "Ignore"),
	/** Stop before a currently occupied look-ahead cell and keep the path. */
	Yield UMETA(DisplayName = "Yield"),
	/** Yield first, then try one localized repath after the configured delay. */
	YieldThenRepath UMETA(DisplayName = "Yield Then Repath"),
	/** Reserves a rolling, capsule-aware prefix of the active path before movement is allowed. */
	ReservedCorridor UMETA(DisplayName = "Reserved Corridor")
};

/** Optional policy used by Move To Grid Cell when multiple agents want the same destination. */
UENUM(BlueprintType)
enum class EGridGoalContentionPolicy : uint8
{
	/** Do not claim or redirect a contested destination. */
	Ignore UMETA(DisplayName = "Ignore"),
	/** Move first, then repeatedly redirect if the reached goal is occupied. */
	RedirectOnCompletion UMETA(DisplayName = "Redirect on Completion"),
	/** Atomically claim a separated goal before movement begins. */
	ReserveBeforeMove UMETA(DisplayName = "Reserve Before Move"),
	/** Atomically reject the request when its exact destination belongs to another agent. */
	RejectOccupied UMETA(DisplayName = "Reject Occupied"),
	/**
	 * Build the route to the requested destination, but when that destination is occupied,
	 * atomically stop at the immediately preceding cell in that same route.
	 */
	StopBeforeOccupied UMETA(DisplayName = "Stop Before Occupied")
};

/** Stable integer address of a cell inside one grid. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellCoord
{
	GENERATED_BODY()

	/** Horizontal local-grid column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	int32 X = 0;

	/** Horizontal local-grid row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	int32 Y = 0;

	/** Vertically quantized surface layer relative to the grid origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	int32 Layer = 0;

	FGridCellCoord() = default;
	FGridCellCoord(int32 InX, int32 InY, int32 InLayer) : X(InX), Y(InY), Layer(InLayer) {}

	bool operator==(const FGridCellCoord& Other) const
	{
		return X == Other.X && Y == Other.Y && Layer == Other.Layer;
	}

	bool operator!=(const FGridCellCoord& Other) const { return !(*this == Other); }

	bool operator<(const FGridCellCoord& Other) const
	{
		return Layer != Other.Layer ? Layer < Other.Layer : (Y != Other.Y ? Y < Other.Y : X < Other.X);
	}
};

FORCEINLINE uint32 GetTypeHash(const FGridCellCoord& Coord)
{
	return HashCombineFast(HashCombineFast(GetTypeHash(Coord.X), GetTypeHash(Coord.Y)), GetTypeHash(Coord.Layer));
}

/** Persistent cell identity. It remains stable when dense storage is rebuilt. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellId
{
	GENERATED_BODY()

	/** Persistent identity of the bounds region that owns the cell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGuid GridId;

	/** Stable integer coordinate within GridId. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGridCellCoord Coord;

	/** @return True when the cell belongs to a persistent grid identity. */
	bool IsValid() const { return GridId.IsValid(); }
	bool operator==(const FGridCellId& Other) const { return GridId == Other.GridId && Coord == Other.Coord; }
};

FORCEINLINE uint32 GetTypeHash(const FGridCellId& CellId)
{
	return HashCombineFast(GetTypeHash(CellId.GridId), GetTypeHash(CellId.Coord));
}

/** Fast transient cell reference. TopologyGeneration prevents use after a rebuild. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellHandle
{
	GENERATED_BODY()

	/** Persistent region identity copied from the referenced cell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGuid GridId;

	/** Zero-based index in the published immutable snapshot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	int32 DenseIndex = INDEX_NONE;

	/** Topology revision that must still match before DenseIndex is dereferenced. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	int64 TopologyGeneration = 0;

	/** @return True when every handle field is populated; the current snapshot must still validate it. */
	bool IsSet() const { return GridId.IsValid() && DenseIndex != INDEX_NONE && TopologyGeneration > 0; }
};

/** Independent monotonic revisions used for selective invalidation and repathing. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridRevisionSet
{
	GENERATED_BODY()

	/** Generated cells, neighbours, regions, chunks, or links changed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	int64 Topology = 0;

	/** Runtime blockers, costs, or link enabled states changed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	int64 Traversal = 0;

	/** Runtime occupancy or reservations changed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	int64 Occupancy = 0;
};

/** Rigid coordinate frame used by a grid. Actor scale changes bounds extents, never logical cell size. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridTransform
{
	GENERATED_BODY()

	/** World-space origin of coordinate (0,0,0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	FVector Origin = FVector::ZeroVector;

	/** Grid yaw frame; pitch and roll are rejected by bounds validation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	FRotator Rotation = FRotator::ZeroRotator;

	/** X/Y are horizontal cell dimensions; Z is vertical layer spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World", meta = (ClampMin = "1.0"))
	FVector CellSize = FVector(100.0, 100.0, 50.0);

	/** @return True when cell dimensions are finite/positive and the rotation is supported. */
	bool IsValid() const;
	/** Converts Coord to its world-space cell center. */
	FVector CellToWorld(const FGridCellCoord& Coord) const;
	/** Quantizes WorldLocation relative to Origin and Rotation. */
	FGridCellCoord WorldToCell(const FVector& WorldLocation) const;
	/** Removes Origin and Rotation from WorldLocation. */
	FVector WorldToLocal(const FVector& WorldLocation) const;
	/** Applies Rotation and Origin to LocalLocation. */
	FVector LocalToWorld(const FVector& LocalLocation) const;
};

/** Read-only description returned when projecting or inspecting one cell. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellQueryResult
{
	GENERATED_BODY()

	/** Explicit success or failure reason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	EGridQueryStatus Status = EGridQueryStatus::InvalidInput;

	/** Persistent cell identity, populated on success. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGridCellId CellId;

	/** Revision-sensitive fast handle, populated on success. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGridCellHandle Handle;

	/** Generated floor-center position. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FVector WorldCenter = FVector::ZeroVector;

	/** World-space normal of the walkable floor sampled for this cell. */
	/** True when topology and traversal overlays allow the cell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FVector FloorNormal = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	bool bWalkable = false;
};

/** Blueprint-safe path result. Cost excludes synthetic turn penalties used during selection. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridPathQueryResult
{
	GENERATED_BODY()

	/** Complete/partial outcome or explicit failure reason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	EGridQueryStatus Status = EGridQueryStatus::InvalidInput;

	/** Ordered persistent cells from projected start to projected goal. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	TArray<FGridCellId> Cells;

	/** Ordered world-space following points. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	TArray<FVector> WorldPoints;

	/** Geometric world length in centimetres. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	double Length = 0.0;

	/** Real traversal cost, excluding Balanced turn penalties. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	double Cost = 0.0;

	/** Optimization strategy actually used by the query filter. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	EGridPathOptimizationMode OptimizationMode = EGridPathOptimizationMode::ShortestPath;

	/** Number of ordinary direction changes in the selected path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	int32 TurnCount = 0;

	/** Number of cells or directional states expanded by A*. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	int32 VisitedNodes = 0;

	/** Immutable snapshot revisions retained by this result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGridRevisionSet Revisions;
};

/** Blueprint-safe result of a bounded reachability query. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridReachabilityResult
{
	GENERATED_BODY()

	/** Explicit success or failure reason. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	EGridQueryStatus Status = EGridQueryStatus::InvalidInput;

	/** Reachable persistent cells in deterministic order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	TArray<FGridCellId> Cells;

	/** World centers matching Cells. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	TArray<FVector> WorldPoints;

	/** Number of graph nodes expanded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	int32 VisitedNodes = 0;

	/** Snapshot revisions used by the query. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGridRevisionSet Revisions;
};

/** Localized difference between two immutable snapshot publications. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridChangeSet
{
	GENERATED_BODY()

	/** Cells whose topology or overlay data changed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	TArray<FGridCellId> ChangedCells;

	/** Links whose enabled or cost state changed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	TArray<FGuid> ChangedLinks;

	/** Revision tuple before publication. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGridRevisionSet PreviousRevisions;

	/** Revision tuple after publication. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World")
	FGridRevisionSet NewRevisions;
};
