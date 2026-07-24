#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Types/GameplayActionTypes.h"
#include "BTTask_CancelGameplayAction.generated.h"

/** Cancels one Gameplay Action handle resolved from a Blackboard Struct key. */
UCLASS(meta = (DisplayName = "Cancel Gameplay Action"))
class GAMEPLAYACTIONSAI_API UBTTask_CancelGameplayAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CancelGameplayAction();

	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	FBlackboardKeySelector Handle;

	/** Optional Actor containing the component. None resolves controlled Pawn, then AIController. */
	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	FBlackboardKeySelector ActionOwnerActor;

	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	FGameplayTag ReasonTag;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
