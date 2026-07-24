#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Actions/GameplayActionInstance.h"
#include "GridWorldTypes.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/GridTrafficReservation.h"
#include "GridMoveToCellAction.generated.h"

class AAIController;
class AActor;
class AController;
class AGridNavigationData;
class UGridMoveToCellTask;
class UNavigationQueryFilter;
class UPathFollowingComponent;

/**
 * Executes GridWorld movement under GameplayActions lifecycle ownership.
 *
 * AIControllers use GridWorld's existing UGridMoveToCellTask. Other Controllers use their
 * UPathFollowingComponent after the requested destination is projected to a GridWorld cell.
 * Init only validates and snapshots parameters; movement starts after the Movement lock is acquired.
 * Cleanup removes this instance's delegate before cancellation so a late or teardown callback can
 * never finish a released action.
 */
UCLASS(Blueprintable)
class GAMEPLAYACTIONSGRIDWORLD_API UGridMoveToCellAction : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

protected:
	virtual bool CanStartAction_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void OnActionInit_Implementation() override;
	virtual void OnActionStarted_Implementation() override;
	virtual void OnActionPaused_Implementation() override;
	virtual void OnActionResumed_Implementation() override;
	virtual void OnActionCleanup_Implementation() override;

private:
	struct FMoveSettings
	{
		EGridMovePathSource PathSource = EGridMovePathSource::Destination;
		FGridInjectedPath InjectedPath;
		FVector GoalLocation = FVector::ZeroVector;
		TWeakObjectPtr<AActor> GoalActor;
		float AcceptanceRadius = -1.0f;
		EAIOptionFlag::Type StopOnOverlap = EAIOptionFlag::Default;
		EAIOptionFlag::Type AcceptPartialPath = EAIOptionFlag::Default;
		bool bUsePathfinding = true;
		bool bLockAILogic = false;
		bool bTrackMovingGoal = true;
		EAIOptionFlag::Type RequireNavigableEndLocation = EAIOptionFlag::Default;
		TSubclassOf<UNavigationQueryFilter> FilterClass;
		bool bAllowStrafe = false;
		EGridGoalContentionPolicy GoalContentionPolicy = EGridGoalContentionPolicy::StopBeforeOccupied;
		int32 MaxAlternativeSearchRadius = 3;
		float AdditionalGoalSeparation = 5.0f;
		bool bAutoRegisterPawnOccupancy = true;
		float GoalAvailabilityTimeout = 5.0f;
		float GoalWaitWarningInterval = 1.0f;
	};

	bool ReadSettings(FMoveSettings& OutSettings, FString& OutDiagnostic) const;
	AController* ResolveController() const;
	bool StartControllerMove(AController& Controller, FString& OutDiagnostic);
	bool TryClaimControllerGoal(
		AController& Controller,
		AGridNavigationData& NavigationData,
		const FGridCellId& GoalCell,
		const FVector& GoalLocation,
		FString& OutDiagnostic);
	bool BuildStopBeforeOccupiedPath(
		AController& Controller,
		AGridNavigationData& NavigationData,
		TSubclassOf<UNavigationQueryFilter> FilterClass,
		const FVector& StartLocation,
		const FGridCellId& RequestedGoalCell,
		const FPathFindingResult& FullPath,
		FPathFindingResult& OutAdjustedPath,
		FGridCellId& OutEffectiveGoalCell,
		FVector& OutEffectiveGoalLocation,
		FString& OutDiagnostic) const;
	void ReleaseControllerGoalClaim(bool bCommitParking);
	void HandleMoveFinished(
		TEnumAsByte<EPathFollowingResult::Type> Result,
		AAIController* Controller);
	void HandleControllerMoveFinished(
		FAIRequestID RequestId,
		const FPathFollowingResult& Result);
	void CompleteMove(
		EPathFollowingResult::Type Result,
		const AController* Controller);
	void ReleaseMoveTask(bool bCancelActiveTask);
	void ReleaseControllerMove(bool bCancelActiveMove);

	FMoveSettings CachedSettings;

	UPROPERTY(Transient)
	TObjectPtr<UGridMoveToCellTask> MoveTask;

	UPROPERTY(Transient)
	TObjectPtr<UPathFollowingComponent> ControllerPathFollowingComponent;

	FDelegateHandle MoveFinishedHandle;
	FDelegateHandle ControllerMoveFinishedHandle;
	FAIRequestID ControllerMoveRequestId;
	FAIRequestID DeferredControllerMoveRequestId;
	FPathFollowingResult DeferredControllerMoveResult;
	FGridTrafficGoalClaimRequest ControllerGoalClaim;
	TWeakObjectPtr<AGridNavigationData> ControllerClaimNavigationData;
	bool bAcceptMoveCallbacks = false;
	bool bStartingControllerMove = false;
	bool bHasDeferredControllerMoveResult = false;
	bool bHasControllerGoalClaim = false;
	bool bControllerMovePathIsPartial = false;
};
