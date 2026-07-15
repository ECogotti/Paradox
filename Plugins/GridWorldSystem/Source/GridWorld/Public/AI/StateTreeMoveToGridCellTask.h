// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "Tasks/StateTreeMoveToTask.h"
#include "StateTreeMoveToGridCellTask.generated.h"

/** StateTree MoveTo variant whose destination is the nearest GridWorld cell center. */
USTRUCT(meta = (DisplayName = "Move To Grid Cell", Category = "AI|Action"))
struct GRIDWORLD_API FStateTreeMoveToGridCellTask : public FStateTreeMoveToTask
{
	GENERATED_BODY()

	/** Optional arbitration applied when several agents select the same destination cell. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention")
	EGridGoalContentionPolicy GoalContentionPolicy = EGridGoalContentionPolicy::Ignore;

	/** Maximum ordinary-adjacency distance searched for a separated alternative. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention", meta = (ClampMin = "1", UIMin = "1", EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	int32 MaxAlternativeSearchRadius = 3;

	/** Extra clearance added to the two agent radii while selecting a destination. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm", EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	float AdditionalGoalSeparation = 5.0f;

	/** Adds runtime occupancy to the controlled Pawn when needed. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention", meta = (EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	bool bAutoRegisterPawnOccupancy = true;

	/** Continuous time with no separated goal before the task finishes as Blocked. */
	UPROPERTY(EditAnywhere, Category = "Grid World|Goal Contention", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s", EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	float GoalAvailabilityTimeout = 5.0f;

	/** Rate-limited warning interval while the task waits for a destination. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Grid World|Goal Contention", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s", EditCondition = "GoalContentionPolicy != EGridGoalContentionPolicy::Ignore", EditConditionHides))
	float GoalWaitWarningInterval = 1.0f;

	/** Creates or reuses the shared Grid Move task while retaining StateTree bindings and callbacks. */
	virtual UAITask_MoveTo* PrepareMoveToTask(
		FStateTreeExecutionContext& Context,
		AAIController& Controller,
		UAITask_MoveTo* ExistingTask,
		FAIMoveRequest& MoveRequest) const override;
};
