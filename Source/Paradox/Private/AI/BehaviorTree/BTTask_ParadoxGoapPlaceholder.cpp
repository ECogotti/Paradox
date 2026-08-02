#include "AI/BehaviorTree/BTTask_ParadoxGoapPlaceholder.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Paradox.h"

UBTTask_ParadoxGoapPlaceholder::UBTTask_ParadoxGoapPlaceholder()
{
	NodeName = TEXT("Paradox GOAP Placeholder (Inert)");
	bCreateNodeInstance = true;
	bIgnoreRestartSelf = true;
}

EBTNodeResult::Type UBTTask_ParadoxGoapPlaceholder::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	PARADOX_LOG_WARNING(
		TEXT("GOAP placeholder task became active in Behavior Tree '%s'. Milestone 3 provides no GOAP behavior or transition trigger."),
		*GetNameSafe(OwnerComp.GetRootTree()));
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_ParadoxGoapPlaceholder::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	return EBTNodeResult::Aborted;
}
