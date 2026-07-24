#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "Types/GameplayActionExecutionSpec.h"
#include "BTDecorator_CanSubmitGameplayAction.generated.h"

/** Side-effect-free preflight for the current action spec and Blackboard parameter bindings. */
UCLASS(meta = (DisplayName = "Can Submit Gameplay Action"))
class GAMEPLAYACTIONSAI_API UBTDecorator_CanSubmitGameplayAction : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_CanSubmitGameplayAction();

	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	FGameplayActionExecutionSpec ExecutionSpec;

	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	TArray<FGameplayActionBlackboardParameterBinding> ParameterBindings;

	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	FBlackboardKeySelector ActionOwnerActor;

	/** When true, AcceptedQueued fails the condition even though the request is otherwise acceptable. */
	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	bool bRequireImmediateStart = false;

protected:
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) const override;
};
