#pragma once

#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ParadoxGoapPlaceholder.generated.h"

/** Inert latent authoring placeholder. Real GOAP starts only after the coordinator stops the BT. */
UCLASS()
class PARADOX_API UBTTask_ParadoxGoapPlaceholder : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ParadoxGoapPlaceholder();

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};

