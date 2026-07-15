// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/StateTreeMoveToGridCellTask.h"

#include "AI/GridMoveToCellTask.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"

UAITask_MoveTo* FStateTreeMoveToGridCellTask::PrepareMoveToTask(
	FStateTreeExecutionContext& Context,
	AAIController& Controller,
	UAITask_MoveTo* ExistingTask,
	FAIMoveRequest& MoveRequest) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	UGridMoveToCellTask* GridMoveTask = Cast<UGridMoveToCellTask>(ExistingTask);
	if (GridMoveTask == nullptr)
	{
		GridMoveTask = UAITask::NewAITask<UGridMoveToCellTask>(
			Controller,
			*InstanceData.TaskOwner,
			EAITaskPriority::High);
	}
	if (GridMoveTask != nullptr)
	{
		GridMoveTask->SetUpGridMove(
			&Controller,
			MoveRequest,
			InstanceData.TargetActor,
			InstanceData.bTrackMovingGoal);
		GridMoveTask->SetGoalContentionSettings(
			GoalContentionPolicy,
			MaxAlternativeSearchRadius,
			AdditionalGoalSeparation,
			bAutoRegisterPawnOccupancy,
			GoalAvailabilityTimeout,
			GoalWaitWarningInterval);
	}
	return GridMoveTask;
}
