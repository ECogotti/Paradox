// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_MoveTo.h"
#include "GridWorldTypes.h"
#include "BTTask_MoveToGridCell.generated.h"

/** Behavior Tree MoveTo variant whose destination is the nearest GridWorld cell center. */
UCLASS()
class GRIDWORLD_API UBTTask_MoveToGridCell : public UBTTask_MoveTo
{
	GENERATED_BODY()

public:
	UBTTask_MoveToGridCell(const FObjectInitializer& ObjectInitializer);

	/** Optional arbitration applied when several agents select the same destination cell. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention")
	EGridGoalContentionPolicy GoalContentionPolicy = EGridGoalContentionPolicy::StopBeforeOccupied;

	/** Maximum ordinary-adjacency distance searched for a separated alternative goal. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention", meta = (ClampMin = "1", UIMin = "1", EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	int32 MaxAlternativeSearchRadius = 3;

	/** Extra clearance added to the two agent radii while selecting a destination. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm", EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	float AdditionalGoalSeparation = 5.0f;

	/** Adds a runtime occupancy component to the controlled Pawn when one is not already active. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention", meta = (EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	bool bAutoRegisterPawnOccupancy = true;

	/** Continuous time with no separated goal before the task finishes as Blocked. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s", EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	float GoalAvailabilityTimeout = 5.0f;

	/** Rate-limited warning interval while the task waits for a destination. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Grid World|Goal Contention", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s", EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	float GoalWaitWarningInterval = 1.0f;

protected:
	/** Creates or reuses UGridMoveToCellTask while preserving all native BT MoveTo lifecycle semantics. */
	virtual UAITask_MoveTo* PrepareMoveTask(UBehaviorTreeComponent& OwnerComp, UAITask_MoveTo* ExistingTask, FAIMoveRequest& MoveRequest) override;
};
