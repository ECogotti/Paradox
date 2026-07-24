#include "Tests/GameplayActionsAITestTypes.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameplayActionTags.h"

bool UGameplayActionsAITestAction::bCompleteSynchronously = false;

void UGameplayActionsAITestAction::CompleteForTest(const bool bSuccess)
{
	if (bSuccess)
	{
		SucceedAction(
			GameplayActionTags::Result_Success,
			TEXT("Completed successfully by GameplayActionsAI automation test."));
	}
	else
	{
		FailAction(
			GameplayActionTags::Result_Failure_Unspecified,
			TEXT("Failed by GameplayActionsAI automation test."));
	}
}

void UGameplayActionsAITestAction::OnActionStarted_Implementation()
{
	if (bCompleteSynchronously)
	{
		SucceedAction(
			GameplayActionTags::Result_Success,
			TEXT("Completed synchronously by GameplayActionsAI automation test."));
	}
}

EBTNodeResult::Type UGameplayActionsAITestBTTask::ExecuteForTest(
	UBehaviorTreeComponent& OwnerComp)
{
	return ExecuteTask(OwnerComp, nullptr);
}

EBTNodeResult::Type UGameplayActionsAITestBTTask::AbortForTest(
	UBehaviorTreeComponent& OwnerComp)
{
	return AbortTask(OwnerComp, nullptr);
}
