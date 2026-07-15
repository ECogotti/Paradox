// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GridWorldTypes.h"
#include "Navigation/PathFollowingComponent.h"

#include "GridWorldPathFollowingComponent.generated.h"

struct FGridNavigationPath;
struct FGridPathPointFollowingData;
struct FGridTrafficCorridorResult;
struct FGridWorldSnapshot;
class UCharacterMovementComponent;
class UGridNavigationOccupancyComponent;
class AGridNavigationData;

/**
 * Path following component that optionally enforces GridWorld cell-center waypoints.
 * Standard GridWorld paths retain the native UPathFollowingComponent behavior.
 */
UCLASS(BlueprintType, ClassGroup = (GridWorld), meta = (DisplayName = "GridWorld Path Following"))
class GRIDWORLD_API UGridWorldPathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()

public:
	virtual void Cleanup() override;
	virtual void OnPathFinished(const FPathFollowingResult& Result) override;
	virtual void OnPathUpdated() override;
	virtual void SetNavMovementInterface(INavMovementInterface* NavMoveInterface) override;

protected:
	virtual void Reset() override;
	virtual void OnNewPawn(APawn* NewPawn) override;
	virtual void SetMoveSegment(int32 SegmentStartIndex) override;
	virtual void FollowPathSegment(float DeltaTime) override;
	virtual void UpdatePathSegment() override;
	virtual bool UpdateBlockDetection() override;
	virtual bool HasReachedDestination(const FVector& CurrentLocation) const override;
	virtual bool HasReachedCurrentTarget(const FVector& CurrentLocation) const override;
	virtual int32 DetermineStartingPathPoint(const FNavigationPath* ConsideredPath) const override;
	virtual int32 DetermineCurrentTargetPathPoint(int32 StartIndex) override;

private:
	friend class AGridWorldAIController;

	/** Internal one-way center-plane classification for the active waypoint. */
	enum class ECenterGateResult : uint8
	{
		Pending,
		Passed,
		Missed
	};

	/** @return Borrowed active path when it is an FGridNavigationPath, otherwise nullptr. */
	const FGridNavigationPath* GetGridPath() const;
	/** @return Borrowed following metadata for PathPointIndex, or nullptr. */
	const FGridPathPointFollowingData* GetFollowingData(int32 PathPointIndex) const;
	/** Classifies the swept movement from the previous feet position through the active one-way gate. */
	ECenterGateResult EvaluateCurrentCenterGate(const FVector& CurrentLocation) const;
	/** Tests radial tolerance or swept one-way passage without accepting a reverse crossing. */
	bool IsWithinCenterTolerance(
		const FVector& CurrentLocation,
		const FVector& TargetLocation,
		const FGridPathPointFollowingData& FollowingData,
		bool bAllowSweptPass) const;
	/** @return True when horizontal speed satisfies FollowingData.StopSpeedTolerance. */
	bool IsBelowStopSpeed(const FGridPathPointFollowingData& FollowingData) const;
	/** @return True for Accelerated drive or Direct Velocity final approach. */
	bool ShouldUseAcceleratedDrive(const FGridPathPointFollowingData& FollowingData) const;
	/** @return Borrowed Character Movement component for the current Pawn, if any. */
	UCharacterMovementComponent* GetCharacterMovementComponent() const;
	/** Applies temporary acceleration/direct-velocity flags for the active precise path. */
	void ApplyDrivePolicy();
	/** Restores every movement flag saved by ApplyDrivePolicy. */
	void RestoreDrivePolicy();
	/** Clears the previous feet sample used by swept center gates. */
	void ResetPreviousLocation();
	/** Resolves or creates agent occupancy when the controller allows it. */
	void EnsurePawnOccupancy(APawn* Pawn);
	/** Requests the capsule/braking-aware future prefix. @return True when movement may cross the next gate. */
	bool UpdateTrafficCorridor(
		const FGridNavigationPath& GridPath,
		FGridTrafficCorridorResult& OutResult,
		TArray<FVector>& OutRequestedCellCenters);
	/** Releases future reservations. @param bKeepCurrentCell Retains current parking protection when true. */
	void ReleaseTrafficCorridor(bool bKeepCurrentCell);
	/** Forgets cell-path progress after abort, replacement, or Pawn change. */
	void ResetTrafficProgress();
	/** Advances across every center plane swept since the last frame and immediately releases cells behind it. */
	void AdvanceTrafficProgress(
		const FGridNavigationPath& GridPath,
		const FGridWorldSnapshot& Snapshot,
		const FVector& CurrentLocation);
	/** Updates Reserved Corridor or reactive occupancy policy. @return True when the normal follower must yield this frame. */
	bool UpdateDynamicAgentAvoidance();
	/** Stops requested movement while retaining the current navigation path. */
	void ApplyDynamicAgentYield();
	/** Clears blocker timers/state; optionally allows the same blocker to trigger another repath. */
	void ResetDynamicAgentAvoidance(bool bClearRepathMemory);
	/** Removes this component's avoidance label from the NavData scene proxy. */
	void ClearDynamicAgentDebug();
	/** Publishes immutable look-ahead and blocker data for batched debug rendering. */
	void PublishDynamicAgentDebug(
		TConstArrayView<FVector> LookAheadCellCenters,
		const FVector& LabelLocation,
		const FVector& BlockingCellCenter,
		const FGuid& BlockingOccupantId,
		bool bRepathing) const;
	/** @return Cell-path index containing/nearest CurrentLocation, or INDEX_NONE. */
	int32 FindCurrentCellPathIndex(const FGridNavigationPath& GridPath, const FVector& CurrentLocation) const;
	/** Finds the first blocking live agent in the configured look-ahead prefix. */
	bool FindBlockingAgent(
		const FGridNavigationPath& GridPath,
		const FVector& CurrentLocation,
		FGuid& OutOccupantId,
		FGridCellId& OutCellId,
		FVector& OutCellCenter,
		float& OutOccupantSpeed,
		TArray<FVector>& OutLookAheadCellCenters) const;

	/** Previous frame's feet location for swept gate tests. */
	FVector PreviousFeetLocation = FVector::ZeroVector;
	/** Whether PreviousFeetLocation belongs to the current segment. */
	bool bHasPreviousFeetLocation = false;
	/** Original movement-interface acceleration flag restored after a precise path. */
	bool bSavedUseAccelerationForPaths = false;
	/** Prevents restoring a flag that was never captured. */
	bool bHasSavedUseAccelerationForPaths = false;
	/** Character Movement whose requested-move flag was temporarily changed. */
	TWeakObjectPtr<UCharacterMovementComponent> SavedCharacterMovement;
	/** Original bRequestedMoveUseAcceleration value. */
	bool bSavedRequestedMoveUseAcceleration = false;
	/** Whether the Character Movement value was captured. */
	bool bHasSavedRequestedMoveUseAcceleration = false;
	/** True while precise final-center completion rules replace native acceptance radius. */
	bool bStrictFinalPath = false;
	/** Pawn occupancy source used for owner identity and dimensions. */
	TWeakObjectPtr<UGridNavigationOccupancyComponent> PawnOccupancyComponent;
	/** NavData currently owning TrafficOwnerId's corridor. */
	TWeakObjectPtr<AGridNavigationData> TrafficNavigationData;
	/** Stable occupancy identity used by traffic snapshots and A*. */
	FGuid TrafficOwnerId;
	/** First live occupancy or reservation preventing progress. */
	FGuid BlockingOccupantId;
	/** Path cell associated with BlockingOccupantId. */
	FGridCellId BlockingCellId;
	/** World center used by debug and repath diagnostics. */
	FVector BlockingCellCenter = FVector::ZeroVector;
	/** World time at which the current blocker first became stationary. */
	double StationaryBlockerSince = -1.0;
	/** True while normal following is held before an unsafe gate. */
	bool bYieldingToAgent = false;
	/** Prevents duplicate path invalidations while a replacement query is active. */
	bool bDynamicAgentRepathPending = false;
	/** Owner that triggered the most recent localized repath. */
	FGuid LastRepathBlockingOccupantId;
	/** Cell that triggered the most recent localized repath. */
	FGridCellId LastRepathBlockingCellId;
	/** Most recently crossed logical cell in GridPath.CellPath. */
	int32 TrafficCurrentCellPathIndex = INDEX_NONE;
	/** Rate limits the warning that Standard following is best effort for Reserved Corridor. */
	bool bWarnedStandardTrafficFollowing = false;
};
