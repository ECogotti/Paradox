// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NavigationData.h"
#include "GridWorldTypes.h"
#include "Navigation/GridPathInjectionTypes.h"

class AGridNavigationData;

/** Immutable geometry used by the batched path debug renderer. */
struct GRIDWORLD_API FGridCenterGateDebugData
{
	/** World-space cell center crossed by the gate. */
	FVector Center = FVector::ZeroVector;
	/** Required forward crossing direction. */
	FVector Forward = FVector::ForwardVector;
	/** Horizontal gate-plane axis. */
	FVector Tangent = FVector::RightVector;
	/** Grid up axis used by the debug plane. */
	FVector Up = FVector::UpVector;
	/** Allowed lateral distance from Center. */
	float HalfWidth = 0.0f;
	/** Path style that requested this gate. */
	EGridPathFollowingStyle Style = EGridPathFollowingStyle::Standard;
};

/** Drive-mode label aligned with one active debug path. */
struct GRIDWORLD_API FGridPathDriveDebugData
{
	/** Label anchor near the active path start. */
	FVector Location = FVector::ZeroVector;
	/** Drive mode captured when the path was published. */
	EGridPathDriveMode DriveMode = EGridPathDriveMode::Accelerated;
	/** Whether the final Direct Velocity segment switches to acceleration. */
	bool bUseAcceleratedFinalApproach = false;
};

/** Debug-only lifecycle state for reactive or reserved dynamic-agent following. */
enum class EGridAgentAvoidanceDebugState : uint8
{
	/** Look-ahead is clear. */
	Monitoring,
	/** Movement is held for ordinary occupancy. */
	Yielding,
	/** Movement is held because the next safe reservation prefix was not granted. */
	WaitingReservation,
	/** A localized replacement path is being requested. */
	Repathing
};

/** Immutable snapshot of one path follower's dynamic-agent look-ahead plan. */
struct GRIDWORLD_API FGridAgentAvoidanceDebugData
{
	/** Ordered centers currently inspected or requested by the follower. */
	TArray<FVector> LookAheadCellCenters;
	/** World position used for the avoidance-state label. */
	FVector LabelLocation = FVector::ZeroVector;
	/** First blocked center highlighted by the renderer. */
	FVector BlockingCellCenter = FVector::ZeroVector;
	/** Occupancy/traffic owner causing the wait. */
	FGuid BlockingOccupantId;
	/** Current debug lifecycle state. */
	EGridAgentAvoidanceDebugState State = EGridAgentAvoidanceDebugState::Monitoring;

	bool operator==(const FGridAgentAvoidanceDebugData& Other) const
	{
		return LookAheadCellCenters == Other.LookAheadCellCenters
			&& LabelLocation == Other.LabelLocation
			&& BlockingCellCenter == Other.BlockingCellCenter
			&& BlockingOccupantId == Other.BlockingOccupantId
			&& State == Other.State;
	}
};

/** Immutable movement policy aligned with one world-space point in a GridWorld path. */
struct GRIDWORLD_API FGridPathPointFollowingData
{
	/** Persistent cell represented by this point. */
	FGridCellId CellId;
	/** Region yaw used to evaluate local center planes. */
	FRotator GridRotation = FRotator::ZeroRotator;
	/** Physical waypoint policy. */
	EGridPathFollowingStyle Style = EGridPathFollowingStyle::Standard;
	/** Movement drive policy. */
	EGridPathDriveMode DriveMode = EGridPathDriveMode::Accelerated;
	/** Switches only the final Direct Velocity segment to braking. */
	bool bUseAcceleratedFinalApproach = false;
	/** Horizontal radial center acceptance in centimetres. */
	float CellCenterTolerance = 2.0f;
	/** Final horizontal speed tolerance in centimetres per second. */
	float StopSpeedTolerance = 5.0f;
	/** Lateral half width of the one-way center gate. */
	float CenterGateHalfWidth = 12.5f;
	/** True when the point is a generated logical cell center. */
	bool bIsCellCenter = false;
	/** True for final arrival or other explicit stop boundaries. */
	bool bRequiresStop = false;
	/** True when the follower must observe a one-way center-plane crossing. */
	bool bRequiresCenterGate = false;
	/** True before/after an authored link whose internal traversal remains native. */
	bool bIsLinkBoundary = false;
};

/** Unreal navigation path enriched with immutable GridWorld cells, costs, policies, and revisions. */
struct GRIDWORLD_API FGridNavigationPath : public FNavigationPath
{
	using Super = FNavigationPath;

	FGridNavigationPath();
	virtual void ResetForRepath() override;
	virtual FVector::FReal GetCostFromIndex(int32 PathPointIndex) const override;
	virtual FVector::FReal GetCostFromNode(NavNodeRef PathNode) const override;
	virtual bool ContainsNode(NavNodeRef NodeRef) const override;

	/** @return Borrowed metadata for PathPointIndex, or nullptr when it is not a generated point. */
	const FGridPathPointFollowingData* GetFollowingData(int32 PathPointIndex) const;
	/** @return True when any point requires Center-Constrained or Cell-by-Cell following. */
	bool RequiresPrecisePathFollowing() const;
	/** Selects the next physical target, skipping only permitted collinear centers. */
	int32 GetNextFollowingTargetIndex(int32 StartIndex) const;
	/** Builds one debug gate from path points. @return False for invalid/non-gate indices. */
	bool GetCenterGateGeometry(int32 StartIndex, int32 GateIndex, FGridCenterGateDebugData& OutGate) const;
	/** Appends every required gate to OutGates. */
	void GetCenterGateDebugData(TArray<FGridCenterGateDebugData>& OutGates) const;
	/** Populates drive-mode label data. @return False for a Standard path. */
	bool GetDriveDebugData(FGridPathDriveDebugData& OutDriveData) const;
	/** Appends points that require zero-speed completion. */
	void GetRequiredStopLocations(TArray<FVector>& OutLocations) const;
	/** @return Offset mapping CellPath indices to Unreal path points. */
	int32 GetCellPathPointOffset() const { return CellPathPointOffset; }

	/** Ordered persistent cells selected by A*. */
	TArray<FGridCellId> CellPath;
	/** Ordered generation-sensitive node references. */
	TArray<NavNodeRef> NodePath;
	/** Real fixed-point traversal cost for each path segment. */
	TArray<FVector::FReal> SegmentCosts;
	/** Authored link identities crossed by the path. */
	TArray<FGuid> TraversedLinks;
	/** Snapshot revisions used to create the path. */
	FGridRevisionSet Revisions;
	/** Geometric path length in centimetres. */
	FVector::FReal TotalLength = 0.0;
	/** Steepest floor slope, in degrees from world-up, crossed by this path. */
	float MaximumFloorSlopeDegrees = 0.0f;
	/** Selection strategy copied from the query filter. */
	EGridPathOptimizationMode OptimizationMode = EGridPathOptimizationMode::ShortestPath;
	/** Ordinary direction changes counted by the selected path. */
	int32 TurnCount = 0;
	/** Cells or directional states expanded by A*. */
	int32 VisitedNodes = 0;
	/** Ordinary occupancy policy used by this path. */
	EGridOccupancyPolicy OccupancyPolicy = EGridOccupancyPolicy::Ignore;
	/** Dynamic-agent following policy used by this path. */
	EGridDynamicAgentPolicy DynamicAgentPolicy = EGridDynamicAgentPolicy::Ignore;
	/** Reactive occupancy look-ahead count. */
	int32 MinimumAgentLookAheadCells = 3;
	/** Designer-authored minimum number of cells in a Reserved Corridor prefix. */
	int32 ReservedLookAheadCells = 3;
	/** Extra clearance between traffic capsules in centimetres. */
	float AdditionalAgentSeparation = 5.0f;
	/** Speed at/below which a blocker may trigger delayed repathing. */
	float StationaryAgentSpeedThreshold = 5.0f;
	/** Continuous stationary-blocker delay before localized repath. */
	float DynamicAgentRepathDelay = 0.35f;
	/** Occupancy identity ignored as the query's own Pawn. */
	FGuid IgnoredOccupancyOwnerId;
	/** Runtime traffic revision observed by the A* query that produced this path. */
	int64 TrafficReservationRevision = 0;
	/** True when no route avoided every occupied intermediate cell and the safe waiting corridor was retained. */
	bool bUsedDynamicAgentFallback = false;
	/** Generic source of the latest materialization of this path. */
	EGridNavigationPathOrigin Origin = EGridNavigationPathOrigin::Computed;
	/** Identity of this materialization; refreshed after successful repath. */
	FGuid PathInstanceId;
	/** Previous materialization when Origin is Recalculated. */
	FGuid ParentPathInstanceId;
	/** Preview correlation retained when this path was committed from prediction. */
	FGuid SourcePreviewId;
	/** Additional exact-path behavior layered over the normal invalidation pipeline. */
	EGridInjectedPathInvalidationPolicy InjectedInvalidationPolicy =
		EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;
	/**
	 * Keeps a replay exact path stable across transient live-agent conflicts.
	 * Reserved Corridor still yields, and a persistent final-cell conflict is reported
	 * as Blocked from the predecessor so the owning goal-contention policy can redirect.
	 */
	bool bAllowDynamicAgentConflictsDuringValidation = false;

	static const FNavPathType Type;

private:
	friend class AGridNavigationData;
	TArray<FGridPathPointFollowingData> PathPointFollowingData;
	int32 CellPathPointOffset = 0;
};
