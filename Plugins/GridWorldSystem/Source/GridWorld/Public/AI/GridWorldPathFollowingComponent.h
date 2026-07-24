// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GridWorldTypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "Presentation/GridPathPresentationTypes.h"

#include "GridWorldPathFollowingComponent.generated.h"

struct FGridNavigationPath;
struct FGridPathPointFollowingData;
struct FGridTrafficCorridorResult;
struct FGridWorldSnapshot;
class UCharacterMovementComponent;
class UGridNavigationOccupancyComponent;
class AGridNavigationData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnGridFollowedPathChanged,
	EGridPathFollowingPresentationChange,
	Change,
	const TArray<FGridCellId>&,
	Cells,
	const FGridRevisionSet&,
	SourceRevisions);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnGridFollowedPathProgressChanged,
	int32,
	CurrentCellIndex,
	FGridCellId,
	CurrentCell);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnGridFollowedPathInvalidated,
	EGridPathPresentationInvalidationReason,
	Reason);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnGridFollowedPathPresentationFinished,
	TEnumAsByte<EPathFollowingResult::Type>,
	Result,
	int32,
	ResultFlags);

/**
 * Path following component that optionally enforces GridWorld cell-center waypoints.
 * Standard GridWorld paths retain the native UPathFollowingComponent behavior.
 */
UCLASS(BlueprintType, ClassGroup = (GridWorld), meta = (DisplayName = "GridWorld Path Following"))
class GRIDWORLD_API UGridWorldPathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()

public:
	/**
	 * Publishes the controlled Pawn as non-blocking occupancy for owner-aware traffic coordination.
	 * Enabled for AI and player controllers; ordinary A* occupancy remains controlled by the query filter.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Dynamic Agents")
	bool bAutoRegisterPawnOccupancy = true;

	/** Master opt-in for local presentation of this follower's authoritative GridWorld path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	bool bPresentActivePath = false;

	/** Sends the active path to the cell-overlay backend when presentation is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation", meta = (EditCondition = "bPresentActivePath"))
	bool bPresentActivePathAsCellOverlay = true;

	/** Sends the active path to the independent strict-line backend when presentation is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation", meta = (EditCondition = "bPresentActivePath"))
	bool bPresentActivePathAsLine = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	EGridPathProgressPresentationMode ActivePathPresentationMode =
		EGridPathProgressPresentationMode::TraversedAndRemaining;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	int32 ActivePathPresentationPriority = 0;

	/** Emitted for the first path and for accepted replacement/recalculation updates. */
	UPROPERTY(BlueprintAssignable, Category = "Grid World|Presentation")
	FOnGridFollowedPathChanged OnGridPathChanged;

	/** Emitted only when the nearest logical cell index changes. */
	UPROPERTY(BlueprintAssignable, Category = "Grid World|Presentation")
	FOnGridFollowedPathProgressChanged OnGridPathProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "Grid World|Presentation")
	FOnGridFollowedPathInvalidated OnGridPathInvalidated;

	UPROPERTY(BlueprintAssignable, Category = "Grid World|Presentation")
	FOnGridFollowedPathPresentationFinished OnGridPathPresentationFinished;

	/** Enables/disables the opt-in active session immediately, including during an active move. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void SetActivePathPresentationEnabled(bool bEnabled);

	/** Changes active-session mode and overlap priority without replacing the movement path. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void SetActivePathPresentationSettings(
		EGridPathProgressPresentationMode ProgressMode,
		int32 Priority);

	/** Selects cell overlay, strict line, both, or neither for the current/future active session. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void SetActivePathPresentationRenderers(bool bCellOverlay, bool bLine);

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
	friend class FGridPathFollowerPresentationTest;

	/** Internal one-way center-plane classification for the active waypoint. */
	enum class ECenterGateResult : uint8
	{
		Pending,
		Passed,
		Missed
	};

	/** @return Borrowed active path when it is an FGridNavigationPath, otherwise nullptr. */
	const FGridNavigationPath* GetGridPath() const;
	/** Binds an additional non-authoritative observer without replacing Unreal's path observer. */
	void BindPresentationPathObserver(const FNavPathSharedPtr& InPath);
	/** Removes only this component's presentation observer. */
	void UnbindPresentationPathObserver();
	/** Converts native invalidation/repath failure into presentation state and Blueprint events. */
	void HandlePresentationPathEvent(FNavigationPath* InPath, ENavPathEvent::Type Event);
	/** Creates or updates the opt-in active session and emits the path-change event. */
	void SynchronizePathPresentation(EGridPathFollowingPresentationChange Change, bool bBroadcastChange);
	/** Publishes a new logical index only when it differs from the previous value. */
	void UpdatePathPresentationProgress();
	/** Releases the active session and optionally clears all event/progress correlation state. */
	void ReleasePathPresentation(bool bResetCorrelationState);
	/** @return Current logical cell index or zero while a newly accepted segment is not initialized. */
	int32 ResolvePresentationCellPathIndex(const FGridNavigationPath& GridPath) const;
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
	/** Opaque active presentation session owned by the world subsystem. */
	FGridPathPresentationHandle ActivePathPresentationHandle;
	/** Native path observed only for invalidation/repath-failure presentation events. */
	FNavPathWeakPtr PresentationObservedPath;
	FDelegateHandle PresentationPathObserverHandle;
	/** Last logical index emitted to presentation/listeners. */
	int32 PresentationCurrentCellPathIndex = INDEX_NONE;
	/** Previous accepted snapshot used to classify replacement versus recalculation. */
	TArray<FGridCellId> LastPresentedPathCells;
	FGridRevisionSet LastPresentedPathRevisions;
	/** Rate limits the warning that Standard following is best effort for Reserved Corridor. */
	bool bWarnedStandardTrafficFollowing = false;
};
