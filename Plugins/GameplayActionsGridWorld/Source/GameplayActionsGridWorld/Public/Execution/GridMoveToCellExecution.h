#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "GridWorldTypes.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "Navigation/GridTrafficReservation.h"
#include "Navigation/PathFollowingComponent.h"
#include "UObject/Object.h"
#include "GridMoveToCellExecution.generated.h"

class AActor;
class AController;
class AGridNavigationData;
class UGridMoveToCellTask;
class UNavigationQueryFilter;
class UPathFollowingComponent;

/** Immutable input used by one controller-aware GridWorld movement execution. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONSGRIDWORLD_API FGridMoveToCellExecutionRequest
{
	GENERATED_BODY()

	UPROPERTY(Transient, BlueprintReadWrite, Category = "Grid World|Movement")
	TObjectPtr<AController> Controller = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	EGridMovePathSource PathSource = EGridMovePathSource::Destination;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	FGridInjectedPath InjectedPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	FVector GoalLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	TWeakObjectPtr<AActor> GoalActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	float AcceptanceRadius = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	TEnumAsByte<EAIOptionFlag::Type> StopOnOverlap = EAIOptionFlag::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	TEnumAsByte<EAIOptionFlag::Type> AcceptPartialPath = EAIOptionFlag::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	bool bUsePathfinding = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	bool bLockAILogic = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	bool bTrackMovingGoal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	TEnumAsByte<EAIOptionFlag::Type> RequireNavigableEndLocation = EAIOptionFlag::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	TSubclassOf<UNavigationQueryFilter> FilterClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	bool bAllowStrafe = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	EGridGoalContentionPolicy GoalContentionPolicy = EGridGoalContentionPolicy::StopBeforeOccupied;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	int32 MaxAlternativeSearchRadius = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	float AdditionalGoalSeparation = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	bool bAutoRegisterPawnOccupancy = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	float GoalAvailabilityTimeout = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	float GoalWaitWarningInterval = 1.0f;
};

/** Non-mutating controller-aware path assessment used to rank interaction destinations. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONSGRIDWORLD_API FGridMoveToCellEvaluationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	bool bCanExecute = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	bool bAlreadyAtGoal = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	FGridCellId GoalCell;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	FVector GoalLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	double PathCost = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	FString DiagnosticMessage;
};

/** Terminal snapshot produced exactly once by UGridMoveToCellExecution. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONSGRIDWORLD_API FGridMoveToCellExecutionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	TEnumAsByte<EPathFollowingResult::Type> Result = EPathFollowingResult::Invalid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	TWeakObjectPtr<AController> Controller;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Movement")
	FString DiagnosticMessage;

	bool IsSuccess() const { return Result == EPathFollowingResult::Success; }
};

DECLARE_MULTICAST_DELEGATE_OneParam(
	FGridMoveToCellExecutionFinishedNative,
	const FGridMoveToCellExecutionResult&);

/**
 * Reusable one-shot movement executor shared by GridWorld Gameplay Actions and composed actions.
 *
 * It is deliberately not a Gameplay Action: the owning action retains scheduling, locks and
 * journaling while this object owns only navigation task/request lifetime.
 */
UCLASS(Transient)
class GAMEPLAYACTIONSGRIDWORLD_API UGridMoveToCellExecution : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;
	virtual void BeginDestroy() override;

	/** Performs a read-only full-path assessment without acquiring traffic claims. */
	static FGridMoveToCellEvaluationResult Evaluate(
		const FGridMoveToCellExecutionRequest& Request);

	/** Starts one execution. Bind OnFinished before calling because completion may be synchronous. */
	bool Start(const FGridMoveToCellExecutionRequest& Request, FString& OutDiagnostic);
	void Pause();
	void Resume();
	void Cancel();

	bool IsRunning() const { return bStarted && !bFinished; }
	FGridMoveToCellExecutionFinishedNative& OnFinishedNative() { return OnFinished; }

private:
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
		class AAIController* Controller);
	void HandleControllerMoveFinished(
		FAIRequestID RequestId,
		const FPathFollowingResult& Result);
	void Finish(EPathFollowingResult::Type Result, const AController* Controller, FString Diagnostic = FString());
	void ReleaseMoveTask(bool bCancelActiveTask);
	void ReleaseControllerMove(bool bCancelActiveMove);

	UPROPERTY(Transient)
	FGridMoveToCellExecutionRequest ActiveRequest;

	UPROPERTY(Transient)
	TObjectPtr<UGridMoveToCellTask> MoveTask;

	UPROPERTY(Transient)
	TObjectPtr<UPathFollowingComponent> ControllerPathFollowingComponent;

	FGridMoveToCellExecutionFinishedNative OnFinished;
	FDelegateHandle MoveFinishedHandle;
	FDelegateHandle ControllerMoveFinishedHandle;
	FAIRequestID ControllerMoveRequestId;
	FAIRequestID DeferredControllerMoveRequestId;
	FPathFollowingResult DeferredControllerMoveResult;
	FGridTrafficGoalClaimRequest ControllerGoalClaim;
	TWeakObjectPtr<AGridNavigationData> ControllerClaimNavigationData;
	bool bStarted = false;
	bool bFinished = false;
	bool bAcceptMoveCallbacks = false;
	bool bStartingControllerMove = false;
	bool bHasDeferredControllerMoveResult = false;
	bool bHasControllerGoalClaim = false;
	bool bControllerMovePathIsPartial = false;
};
