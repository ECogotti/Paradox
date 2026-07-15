// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BTTask_MoveToGridCell.h"

#include "AI/GridMoveToCellTask.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTTask_MoveToGridCell::UBTTask_MoveToGridCell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("Move To Grid Cell");
}

UAITask_MoveTo* UBTTask_MoveToGridCell::PrepareMoveTask(
	UBehaviorTreeComponent& OwnerComp,
	UAITask_MoveTo* ExistingTask,
	FAIMoveRequest& MoveRequest)
{
	UGridMoveToCellTask* GridMoveTask = Cast<UGridMoveToCellTask>(ExistingTask);
	if (GridMoveTask == nullptr)
	{
		GridMoveTask = NewBTAITask<UGridMoveToCellTask>(OwnerComp);
	}
	if (GridMoveTask != nullptr)
	{
		AActor* SourceGoalActor = nullptr;
		if (const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent())
		{
			SourceGoalActor = Cast<AActor>(Blackboard->GetValueAsObject(BlackboardKey.SelectedKeyName));
		}
		GridMoveTask->SetUpGridMove(
			OwnerComp.GetAIOwner(),
			MoveRequest,
			SourceGoalActor,
			MoveRequest.IsMoveToActorRequest());
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
