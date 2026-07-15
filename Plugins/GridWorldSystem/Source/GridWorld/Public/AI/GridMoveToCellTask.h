// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "GridWorldTypes.h"
#include "Tasks/AITask_MoveTo.h"
#include "GridMoveToCellTask.generated.h"

class AAIController;
class UNavigationQueryFilter;
class UGridNavigationOccupancyComponent;
class USceneComponent;
class AGridNavigationData;
struct FGridTrafficGoalClaimRequest;

/** Native MoveTo task that resolves its Actor or location goal to the nearest GridWorld cell center. */
UCLASS()
class GRIDWORLD_API UGridMoveToCellTask : public UAITask_MoveTo
{
	GENERATED_BODY()

public:
	UGridMoveToCellTask(const FObjectInitializer& ObjectInitializer);

	/**
	 * Moves an AI to the center of the nearest GridWorld cell.
	 * GoalActor takes precedence over GoalLocation. When tracking is enabled, crossing a cell boundary updates the move.
	 * @param Controller Controller whose path-following component owns the request.
	 * @param GoalLocation Acquired vector goal used when GoalActor is null.
	 * @param GoalActor Optional tracked Actor goal; its NavAgent offset is respected.
	 * @param AcceptanceRadius Native MoveTo acceptance radius; precise paths may impose stricter final-center rules.
	 * @param StopOnOverlap Native overlap policy.
	 * @param AcceptPartialPath Whether an unreachable goal may return the best reachable prefix.
	 * @param bUsePathfinding False requests direct native movement to the projected center.
	 * @param bLockAILogic Whether the gameplay task resource locks AI logic.
	 * @param bTrackMovingGoal Reprojects an Actor only after it changes GridWorld cell.
	 * @param RequireNavigableEndLocation Native navigable-end requirement.
	 * @param FilterClass Query filter controlling optimization, occupancy, and Reserved Corridor.
	 * @param bAllowStrafe Native controller strafe option.
	 * @param GoalContentionPolicy Optional destination arbitration policy.
	 * @param MaxAlternativeSearchRadius Graph radius for alternative destinations.
	 * @param AdditionalGoalSeparation Extra clearance around destination agents, in centimetres.
	 * @param bAutoRegisterPawnOccupancy Adds occupancy to the Pawn when required.
	 * @param GoalAvailabilityTimeout Continuous wait before returning Blocked.
	 * @param GoalWaitWarningInterval Rate limit for observable wait warnings.
	 * @return New task owned by Controller, or nullptr when Controller is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Tasks", meta = (
		AdvancedDisplay = "AcceptanceRadius,StopOnOverlap,AcceptPartialPath,bUsePathfinding,bLockAILogic,bTrackMovingGoal,RequireNavigableEndLocation,FilterClass,bAllowStrafe,GoalContentionPolicy,MaxAlternativeSearchRadius,AdditionalGoalSeparation,bAutoRegisterPawnOccupancy,GoalAvailabilityTimeout,GoalWaitWarningInterval",
		DefaultToSelf = "Controller",
		BlueprintInternalUseOnly = "TRUE",
		DisplayName = "Move To Grid Cell"))
	static UGridMoveToCellTask* MoveToGridCell(
		AAIController* Controller,
		FVector GoalLocation,
		AActor* GoalActor = nullptr,
		float AcceptanceRadius = -1.0f,
		EAIOptionFlag::Type StopOnOverlap = EAIOptionFlag::Default,
		EAIOptionFlag::Type AcceptPartialPath = EAIOptionFlag::Default,
		bool bUsePathfinding = true,
		bool bLockAILogic = true,
		bool bTrackMovingGoal = true,
		EAIOptionFlag::Type RequireNavigableEndLocation = EAIOptionFlag::Default,
		TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr,
		bool bAllowStrafe = false,
		EGridGoalContentionPolicy GoalContentionPolicy = EGridGoalContentionPolicy::Ignore,
		int32 MaxAlternativeSearchRadius = 3,
		float AdditionalGoalSeparation = 5.0f,
		bool bAutoRegisterPawnOccupancy = true,
		float GoalAvailabilityTimeout = 5.0f,
		float GoalWaitWarningInterval = 1.0f);

	/** Configures this task from an existing native MoveTo request. The task retains no ownership of SourceGoalActor. */
	void SetUpGridMove(
		AAIController* Controller,
		const FAIMoveRequest& InMoveRequest,
		AActor* SourceGoalActor = nullptr,
		bool bTrackSourceGoalActor = true);

	/** Applies optional destination arbitration without changing the query filter's traversal policy. */
	void SetGoalContentionSettings(
		EGridGoalContentionPolicy InPolicy,
		int32 InMaxAlternativeSearchRadius = 3,
		float InAdditionalGoalSeparation = 5.0f,
		bool bInAutoRegisterPawnOccupancy = true,
		float InGoalAvailabilityTimeout = 5.0f,
		float InGoalWaitWarningInterval = 1.0f);

	/** @return Persistent destination selected for the currently active internal request. */
	const FGridCellId& GetProjectedGoalCell() const { return ProjectedGoalCell; }
	/** @return World center matching GetProjectedGoalCell. */
	const FVector& GetProjectedGoalLocation() const { return ProjectedGoalLocation; }

protected:
	virtual void PerformMove() override;
	virtual void OnRequestFinished(FAIRequestID RequestID, const FPathFollowingResult& Result) override;
	virtual void ResetObservers() override;
	virtual void ResetTimers() override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:
	/** Unprojected options copied from the native caller and reused across target-cell changes. */
	FAIMoveRequest SourceMoveRequest;
	/** Optional Actor whose cell is observed without Tick. */
	TWeakObjectPtr<AActor> SourceGoalActor;
	/** Acquired vector goal used when no Actor is supplied. */
	FVector SourceGoalLocation = FVector::ZeroVector;
	/** Current projected/claimed destination used by the internal MoveTo. */
	FGridCellId ProjectedGoalCell;
	/** Desired cell before contention redirects. */
	FGridCellId RequestedGoalCell;
	/** Redirect selected while replacing an already completed internal request. */
	FGridCellId PendingGoalCell;
	/** Goal currently owned in the central traffic registry. */
	FGridCellId ClaimedGoalCell;
	/** World center matching ProjectedGoalCell. */
	FVector ProjectedGoalLocation = FVector::ZeroVector;
	/** Root component providing TransformUpdated for a tracked goal Actor. */
	TWeakObjectPtr<USceneComponent> ObservedGoalRootComponent;
	/** Auto-resolved/created Pawn occupancy used for contention geometry. */
	TWeakObjectPtr<UGridNavigationOccupancyComponent> PawnOccupancyComponent;
	/** NavData that owns the current claim and wait delegate. */
	TWeakObjectPtr<AGridNavigationData> ContentionNavigationData;
	/** Handle for symmetric TransformUpdated unbinding. */
	FDelegateHandle GoalTransformUpdatedHandle;
	/** Handle for symmetric traffic/occupancy availability unbinding. */
	FDelegateHandle TrafficReservationsChangedHandle;
	/** Periodic warning and timeout timer for continuous contention. */
	FTimerHandle GoalAvailabilityTimerHandle;
	/** Candidate cells rejected during repeated second/third blocker redirects. */
	TSet<FGridCellId> RejectedGoalCells;
	/** Selected destination arbitration policy. */
	EGridGoalContentionPolicy GoalContentionPolicy = EGridGoalContentionPolicy::Ignore;
	/** Maximum candidate graph distance. */
	int32 MaxAlternativeSearchRadius = 3;
	/** Extra destination clearance in centimetres. */
	float AdditionalGoalSeparation = 5.0f;
	/** Maximum continuous wait in seconds. */
	float GoalAvailabilityTimeout = 5.0f;
	/** Rate-limited wait warning interval in seconds. */
	float GoalWaitWarningInterval = 1.0f;
	/** World time at which the current uninterrupted no-goal episode began. */
	double GoalWaitStartedAt = -1.0;
	/** Coalesces tracked-Actor TransformUpdated callbacks into one next-tick check. */
	bool bGoalUpdateScheduled = false;
	/** Coalesces traffic/occupancy notifications into one next-tick retry. */
	bool bGoalAvailabilityRetryScheduled = false;
	/** Distinguishes explicit Actor input from vector input. */
	bool bHasSourceGoalActor = false;
	/** Enables Actor cell-boundary tracking. */
	bool bTrackSourceGoalActor = true;
	/** Allows creation of missing Pawn occupancy. */
	bool bAutoRegisterPawnOccupancy = true;
	/** True while this task owns ClaimedGoalCell. */
	bool bHasGoalClaim = false;
	/** Uses PendingGoalCell for the next internal request after redirect. */
	bool bUsePendingGoalCell = false;
	/** True while no separated destination is currently available. */
	bool bWaitingForGoalAvailability = false;
	/** Distinguishes an alternative-goal wait from initial desired-goal waiting. */
	bool bWaitingForAlternativeGoal = false;

	/** Resolves Actor/vector input and contention to a usable center. @return False with optional OutError on failure. */
	bool ResolveGridGoal(FGridCellId& OutCellId, FVector& OutCellCenter, FString* OutError = nullptr) const;
	/** Resolves CellId in the current snapshot. @param OutCellCenter Receives its floor center. */
	bool ResolveCellCenter(const FGridCellId& CellId, FVector& OutCellCenter) const;
	/** Claims the best deterministic separated candidate. @return True after an atomic claim succeeds. */
	bool SelectAndClaimAlternative(const FGridCellId& DesiredCell, bool bIncludeDesiredCell, FGridCellId& OutCellId);
	/** @return True when current goal occupancy/claims require another destination. */
	bool IsProjectedGoalContested() const;
	/** Starts a repeatable redirect after contention at completion. */
	bool StartContentionRedirect();
	/** @return Borrowed authoritative Grid nav data selected for the controlled Pawn. */
	AGridNavigationData* ResolveNavigationData() const;
	/** Projects the Pawn feet to its current persistent cell. */
	bool ResolveCurrentPawnCell(FGridCellId& OutCellId, FVector& OutCellCenter) const;
	/** Populates a traffic goal request using current occupancy geometry. */
	bool BuildGoalClaimRequest(const FGridCellId& CellId, FGridTrafficGoalClaimRequest& OutRequest) const;
	/** Converts the reached claim into Pawn-lifetime parking protection. */
	void CommitCurrentGoalAsParking();
	/** Completes successfully without issuing another MoveTo when the Pawn already occupies the best cell. */
	void FinishAlreadyAtBestCell(const FGridCellId& CellId, const FVector& CellCenter);
	/** Starts event-driven availability waiting and its warning/timeout timer. */
	void BeginGoalAvailabilityWait();
	/** Removes wait delegates/timers and resets the continuous contention episode. */
	void EndGoalAvailabilityWait();
	/** Emits rate-limited warnings and fails Blocked at the configured timeout. */
	void HandleGoalAvailabilityTimer();
	/** Schedules one retry after traffic or occupancy changes. */
	void HandleTrafficReservationsChanged();
	/** Retries projection/claim on the next tick to avoid re-entrant registry mutation. */
	void HandleTrafficReservationRetry();
	/** Resolves or creates the Pawn's runtime occupancy component. */
	void EnsurePawnOccupancy();
	/** Releases every temporary claim owned by this task. */
	void ReleaseGoalClaim();
	/** Resets redirect, wait, and claim state without changing native MoveTo options. */
	void ResetContentionState();
	/** Binds TransformUpdated and destruction callbacks for a tracked Actor. */
	void BindTrackedGoal();
	/** Removes all tracked-Actor callbacks symmetrically. */
	void UnbindTrackedGoal();
	/** Defers a tracked Actor transform update so multiple changes coalesce. */
	void HandleGoalTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);
	/** Replaces the internal request only when the tracked Actor changed cell. */
	void HandleTrackedGoalCellChange();

	UFUNCTION()
	void HandleGoalActorDestroyed(AActor* DestroyedActor);
};
