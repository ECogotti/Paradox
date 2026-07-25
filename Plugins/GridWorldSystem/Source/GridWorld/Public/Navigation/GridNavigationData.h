// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "Navigation/GridTrafficReservation.h"
#include "Navigation/GridWorldSnapshot.h"
#include "GridNavigationData.generated.h"

class AAIController;
class UPathFollowingComponent;
class UNavigationQueryFilter;
struct FAIRequestID;
struct FPathFollowingResult;
class FGridTrafficReservationManager;
struct FGridAStarQuery;
struct FGridAStarResult;

/** Game-Thread notification emitted after traffic reservations or relevant occupancy change. */
DECLARE_MULTICAST_DELEGATE(FOnGridTrafficReservationsChanged);

/** Navigation Data implementation that owns and queries the authoritative GridWorld graph. */
UCLASS(Config = Engine, DefaultConfig, NotBlueprintable)
class GRIDWORLD_API AGridNavigationData : public ANavigationData
{
	GENERATED_BODY()

public:
	AGridNavigationData(const FObjectInitializer& ObjectInitializer);
	virtual void Serialize(FArchive& Ar) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual FBox GetBounds() const override;
	virtual bool SupportsRuntimeGeneration() const override;
	virtual bool IsNodeRefValid(NavNodeRef NodeRef) const override;
	virtual void BatchRaycast(TArray<FNavigationRaycastWork>& Workload, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier = nullptr) const override;
	virtual bool FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual void BatchFindMoveAlongSurface(TArray<FNavigationFindMoveAlongSurfaceWork>& Workload, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual bool FindOverlappingEdges(const FNavLocation& StartLocation, TConstArrayView<FVector> ConvexPolygon, TArray<FVector>& OutEdges, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual bool GetPathSegmentBoundaryEdges(const FNavigationPath& Path, const FNavPathPoint& StartPoint, const FNavPathPoint& EndPoint, TConstArrayView<FVector> SearchArea, TArray<FVector>& OutEdges, float MaxAreaEnterCost, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual FNavLocation GetRandomPoint(FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual bool GetRandomReachablePointInRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual bool GetRandomPointInNavigableRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual bool ProjectPoint(const FVector& Point, FNavLocation& OutLocation, const FVector& Extent, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual void BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, const FVector& Extent, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual void BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, FSharedConstNavQueryFilter Filter = nullptr, const UObject* Querier = nullptr) const override;
	virtual ENavigationQueryResult::Type CalcPathCost(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathCost, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;
	virtual ENavigationQueryResult::Type CalcPathLength(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathLength, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;
	virtual ENavigationQueryResult::Type CalcPathLengthAndCost(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathLength, FVector::FReal& OutPathCost, FSharedConstNavQueryFilter QueryFilter = nullptr, const UObject* Querier = nullptr) const override;
	virtual bool DoesNodeContainLocation(NavNodeRef NodeRef, const FVector& WorldSpaceLocation) const override;
	virtual UPrimitiveComponent* ConstructRenderingComponent() override;
	virtual void ConditionalConstructGenerator() override;
	virtual bool NeedsRebuild() const override;

	/** Publishes a fully built immutable snapshot. Invalid input leaves the last valid snapshot active. @param OutError Optional validation reason. */
	bool PublishSnapshot(TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> NewSnapshot, FString* OutError = nullptr);
	/** @return Shared immutable topology/overlay view safe to retain on worker threads. */
	FGridWorldSnapshotPtr GetSnapshot() const;
	/** @return Independent revision tuple of the currently published snapshot. */
	FGridRevisionSet GetPublishedRevisions() const;
	/** @return Non-serialized identity of this authoritative NavData instance for runtime path handoff. */
	const FGuid& GetRuntimeNavigationDataId() const { return RuntimeNavigationDataId; }
	/** Stable runtime hash of a fully initialized navigation filter and its GridWorld query context. */
	static int64 BuildFilterSignature(
		const FSharedConstNavQueryFilter& QueryFilter,
		TSubclassOf<UNavigationQueryFilter> FilterClass);
	/** Validates and stamps a Blueprint-safe exact path description without creating movement state. */
	FGridInjectedPathValidationResult CreateExactInjectedPath(
		const UObject* Querier,
		const FNavAgentProperties& AgentProperties,
		TSubclassOf<UNavigationQueryFilter> FilterClass,
		const FVector& StartLocation,
		TConstArrayView<FGridCellId> Cells,
		const FGridCellId& OriginalGoalCell,
		bool bAllowPartialPath,
		bool bIsPartial,
		EGridInjectedPathInvalidationPolicy InvalidationPolicy,
		const FGuid& SourcePreviewId,
		FGridInjectedPath& OutInjectedPath,
		bool bAllowDynamicAgentConflictsDuringValidation = false) const;
	/** Revalidates a previously stamped path against the current snapshot, agent and querier context. */
	FGridInjectedPathValidationResult ValidateInjectedPath(
		const FGridInjectedPath& InjectedPath,
		const UObject* Querier,
		const FNavAgentProperties& AgentProperties,
		const FVector& StartLocation) const;
	/** Builds the normal engine-compatible FGridNavigationPath used by path following. */
	FPathFindingResult MaterializeInjectedPath(
		const FGridInjectedPath& InjectedPath,
		const UObject* Querier,
		const FNavAgentProperties& AgentProperties,
		const FVector& StartLocation,
		FNavPathSharedPtr PathInstanceToFill = nullptr) const;
	/** Synchronously samples every valid bounds region on the Game Thread. @return True after a new valid topology is published. */
	bool BuildFromWorld();
	/** Clears generated topology, runtime overlays, traffic state, and debug data. */
	void ClearGridWorld();
	/** Recomposes runtime modifiers/links/occupancy without rebuilding collision topology. */
	void RefreshRuntimeOverlay(bool bOccupancyOnly);
	/** Applies one path follower's rolling short-corridor request. @param OutResult Receives granted/waiting details. */
	bool UpdateTrafficCorridor(const FGridTrafficCorridorRequest& Request, FGridTrafficCorridorResult& OutResult);
	/** Releases future traffic cells while optionally preserving the owner's current parking cell. Source prevents stale callers releasing replacements. */
	void ReleaseTrafficCorridor(const FGuid& OwnerId, const UObject* Source, bool bKeepCurrentCell);
	/** Tests whether a Move To Grid Cell destination can be claimed without mutation. @param OutBlockingOwnerId Optional conflicting owner. */
	bool CanClaimTrafficGoal(const FGridTrafficGoalClaimRequest& Request, FGuid* OutBlockingOwnerId = nullptr) const;
	/** Atomically claims one separated Move To Grid Cell destination. */
	bool TryClaimTrafficGoal(const FGridTrafficGoalClaimRequest& Request);
	/** Returns whether a destination belongs to another traffic owner; Claimant distinguishes claims when OwnerId is unavailable. */
	bool IsTrafficGoalClaimedByOther(const FGridCellId& CellId, const UObject* Claimant, const FGuid& OwnerId = FGuid()) const;
	/** Releases every temporary destination claim owned by Claimant. */
	void ReleaseTrafficGoalClaims(const UObject* Claimant);
	/** Converts a reached destination into parking protection tied to its Pawn. */
	void CommitTrafficParking(const FGridTrafficGoalClaimRequest& Request);
	/** Removes all traffic state owned by one occupancy identity. */
	void RemoveTrafficOwner(const FGuid& OwnerId);
	/** Returns the immutable runtime traffic view retained by asynchronous queries. */
	FGridTrafficReservationSnapshotPtr GetTrafficReservationSnapshot() const;
	/** Delegate used by waiting tasks to retry immediately after availability changes. */
	FOnGridTrafficReservationsChanged& OnTrafficReservationsChanged() { return TrafficReservationsChanged; }
	/** @return Localized difference generated by the most recent publication. */
	const FGridChangeSet& GetLastChangeSet() const { return LastChangeSet; }
	/** @return Designer-facing validation failures from the most recent rejected build. */
	const TArray<FString>& GetLastValidationErrors() const { return LastValidationErrors; }
	/** @return Chunks touched by the most recent incremental build. */
	const TSet<FGridChunkCoord>& GetLastDirtyChunks() const { return LastDirtyChunks; }
	/** Rebuilds dirty chunks. Native generator calls may respect the owning bounds' automatic geometry policy; explicit calls bypass it by default. */
	bool BuildDirtyAreas(const TArray<FNavigationDirtyArea>& DirtyAreas, bool bRespectGeometryAutoRebuild = false);

	/** Draws valid projected cell quads, borders, and cell bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawCells = true;

	/** Colors cell bounds by composed traversal cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawCosts = false;

	/** Draws ordinary occupancy and parked-agent indicators. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawOccupancy = true;

	/** Draws authored special links. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawLinks = true;

	/** Draws chunk boundaries and identities. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawChunks = false;

	/** Draws chunks/regions touched by the last incremental rebuild. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawDirtyRegions = true;

	/** Draws build validation errors in world space when possible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawErrors = true;

	/** Draws active AI/query paths and precise center gates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawPaths = true;

	/** Draws the most recent reachability query. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawReachability = true;

	/** Draws every granted short-corridor cell and a connection to its owning Pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bDrawTrafficReservations = true;

	/** Replaces reachability debug points and invalidates the scene proxy. */
	void SetDebugReachability(TConstArrayView<FVector> WorldPoints) const;
	/** Copies active paths/reachability under the debug lock. */
	void GetDebugQueryData(TArray<TArray<FVector>>& OutPathPointSets, TArray<FVector>& OutReachablePoints) const;
	/** Copies active paths, reachability, and required stop points. */
	void GetDebugQueryData(TArray<TArray<FVector>>& OutPathPointSets, TArray<FVector>& OutReachablePoints, TArray<FVector>& OutRequiredStopPoints) const;
	/** Copies active debug paths plus one-way center-gate geometry. */
	void GetDebugQueryData(
		TArray<TArray<FVector>>& OutPathPointSets,
		TArray<FVector>& OutReachablePoints,
		TArray<FVector>& OutRequiredStopPoints,
		TArray<FGridCenterGateDebugData>& OutCenterGates) const;
	void GetDebugQueryData(
		TArray<TArray<FVector>>& OutPathPointSets,
		TArray<FVector>& OutReachablePoints,
		TArray<FVector>& OutRequiredStopPoints,
		TArray<FGridCenterGateDebugData>& OutCenterGates,
		TArray<FGridPathDriveDebugData>& OutDriveData) const;
	void GetDebugQueryData(
		TArray<TArray<FVector>>& OutPathPointSets,
		TArray<FVector>& OutReachablePoints,
		TArray<FVector>& OutRequiredStopPoints,
		TArray<FGridCenterGateDebugData>& OutCenterGates,
		TArray<FGridPathDriveDebugData>& OutDriveData,
		TArray<FGridAgentAvoidanceDebugData>& OutAgentAvoidanceData) const;
	/** Publishes one source's immutable avoidance debug record. Source is weakly tracked. */
	void SetAgentAvoidanceDebug(UObject* Source, const FGridAgentAvoidanceDebugData& DebugData) const;
	/** Removes Source's avoidance debug record and invalidates the renderer. */
	void ClearAgentAvoidanceDebug(UObject* Source) const;

	/** Unreal pathfinding entry point backed by deterministic GridWorld A*. */
	static FPathFindingResult FindPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query);
	/** Tests connectivity without returning path ownership. @param NumVisitedNodes Optional expanded-state count. */
	static bool TestPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes);
	/** GridWorld implementation of Unreal navigation raycast. */
	static bool NavigationRaycast(const ANavigationData* NavDataInstance, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FNavigationRaycastAdditionalResults* AdditionalResults, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier);

private:
	/** Protects only shared-pointer exchange; queries retain immutable snapshots after unlocking. */
	mutable FRWLock SnapshotLock;
	/** Current immutable topology plus runtime overlays. */
	FGridWorldSnapshotPtr PublishedSnapshot;
	/** World-local identity used to reject cross-NavData injected paths. Never serialized. */
	FGuid RuntimeNavigationDataId;
	/** Last valid generated topology before runtime overlay composition. */
	FGridWorldSnapshotPtr BaseTopologySnapshot;
	/** Errors from the most recent rejected generation. */
	TArray<FString> LastValidationErrors;
	/** Localized change data from the most recent publication. */
	FGridChangeSet LastChangeSet;
	/** Incremental chunks touched by the last dirty-area request. */
	TSet<FGridChunkCoord> LastDirtyChunks;
	/** Protects plain debug copies shared with the scene proxy. */
	mutable FRWLock DebugDataLock;
	/** Last non-controller query path. */
	mutable TArray<FVector> LastDebugPathPoints;
	/** Flattened active-controller path copies. */
	mutable TArray<TArray<FVector>> ActiveDebugPathPointSets;
	/** Final/curve points requiring a stop under precise following. */
	mutable TArray<FVector> ActiveDebugRequiredStopPoints;
	/** One-way center gate planes for all active precise paths. */
	mutable TArray<FGridCenterGateDebugData> ActiveDebugCenterGates;
	/** Drive-mode labels for active precise paths. */
	mutable TArray<FGridPathDriveDebugData> ActiveDebugDriveData;
	/** Weak-source dynamic-agent look-ahead and wait labels. */
	mutable TMap<TWeakObjectPtr<UObject>, FGridAgentAvoidanceDebugData> ActiveAgentAvoidanceDebugData;
	/** Last reachability result centers. */
	mutable TArray<FVector> LastDebugReachablePoints;
	/** Game-Thread authority for runtime-only traffic reservations and goal claims. */
	TSharedPtr<FGridTrafficReservationManager> TrafficReservationManager;
	/** Wakes tasks/followers after traffic or relevant occupancy changes. */
	FOnGridTrafficReservationsChanged TrafficReservationsChanged;

	/** Delegate ownership and immutable draw data for one controller's active path. */
	struct FActiveDebugPath
	{
		/** Path following component whose request-finished delegate clears this entry. */
		TWeakObjectPtr<UPathFollowingComponent> PathFollowingComponent;
		/** Weak path observed for update/invalidation events. */
		FNavPathWeakPtr Path;
		/** Symmetric FNavigationPath observer handle. */
		FDelegateHandle PathObserverHandle;
		/** Symmetric UPathFollowingComponent completion handle. */
		FDelegateHandle MoveFinishedHandle;
		/** Latest ordered world points. */
		TArray<FVector> Points;
		/** Latest precise stop points. */
		TArray<FVector> RequiredStopPoints;
		/** Latest one-way center gates. */
		TArray<FGridCenterGateDebugData> CenterGates;
		/** Latest drive-mode label. */
		FGridPathDriveDebugData DriveData;
		/** Distinguishes Standard paths from a default-valued DriveData. */
		bool bHasDriveData = false;
	};
	/** One independently tracked active path per AI controller. */
	TMap<TWeakObjectPtr<AAIController>, FActiveDebugPath> ActiveDebugPaths;
	/** Controllers already warned about precise paths without Grid path following. */
	TSet<TWeakObjectPtr<AAIController>> PrecisePathWarningControllers;
	/** Controllers already warned that Character Movement cannot walk a generated slope. */
	TSet<TWeakObjectPtr<AAIController>> SlopeWarningControllers;
	/** Controllers already warned that a directional search hit its state budget. */
	TSet<TWeakObjectPtr<AAIController>> SearchLimitWarningControllers;

	/** Invalidates only active paths whose cells/links intersect ChangeSet. */
	void InvalidateAffectedPaths(const FGridChangeSet& ChangeSet);
	/** Converts a fully initialized Unreal filter and agent into the pure-data A* query. */
	FGridAStarQuery BuildAStarQuery(
		const FNavAgentProperties& AgentProperties,
		const FPathFindingQuery& Query,
		int32 StartIndex,
		int32 GoalIndex) const;
	/** Shared materializer for A* results and validated injected paths. */
	FPathFindingResult MaterializeGridPath(
		const FNavAgentProperties& AgentProperties,
		const FPathFindingQuery& Query,
		const FGridWorldSnapshot& Snapshot,
		const FGridAStarQuery& AStarQuery,
		const FGridAStarResult& SearchResult,
		EGridDynamicAgentPolicy RequestedDynamicAgentPolicy,
		bool bUsedDynamicAgentFallback,
		EGridNavigationPathOrigin Origin,
		const FGuid& SourcePreviewId,
		EGridInjectedPathInvalidationPolicy InvalidationPolicy) const;
	/** Replaces the generic last-query path used when no controller owns it. */
	void SetDebugPath(TConstArrayView<FNavPathPoint> PathPoints) const;
	/** Registers symmetric path/move/controller observers and captures draw data. */
	void TrackActiveDebugPath(AAIController& Controller, const FNavPathSharedPtr& Path);
	/** Removes every observer owned for Controller and its draw data. */
	void RemoveActiveDebugPath(TWeakObjectPtr<AAIController> Controller);
	/** Removes all path observers during clear/teardown. */
	void ClearActiveDebugPaths();
	/** Rebuilds the flattened scene-proxy copies under DebugDataLock. */
	void RefreshActiveDebugPathPointSets();
	/** Invalidates the rendering component without creating one when debug is absent. */
	void MarkDebugRenderStateDirty() const;
	/** Marks generated level data dirty after explicit editor build/clear. */
	void MarkGeneratedDataPackageDirty() const;
	/** Handles path update/invalidation events and atomically refreshes or clears debug data. */
	void OnActiveDebugPathEvent(FNavigationPath* Path, ENavPathEvent::Type Event, TWeakObjectPtr<AAIController> Controller);
	/** Defers completion cleanup to avoid an old abort clearing a newly replaced path. */
	void OnActiveDebugMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result, TWeakObjectPtr<AAIController> Controller);
	/** Rebuilds one entry after a path event and removes it when controller/path is invalid. */
	void ReconcileActiveDebugPath(TWeakObjectPtr<AAIController> Controller);

	UFUNCTION()
	void OnTrackedControllerEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);
};
	/** Copies active debug paths, center gates, and drive-mode labels. */
	/** Copies every active path and dynamic-agent wait/repath label for batched rendering. */
