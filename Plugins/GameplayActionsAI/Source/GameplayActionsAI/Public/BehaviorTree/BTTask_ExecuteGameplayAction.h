#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Types/GameplayActionExecutionSpec.h"
#include "BTTask_ExecuteGameplayAction.generated.h"

class UGameplayActionComponent;

/**
 * Submits one Gameplay Action and remains latent through queue residence and execution.
 *
 * The task listens before submission and buffers synchronous Ended notifications until SubmitAction
 * returns the authoritative handle. This covers actions that complete inside Action Start without
 * confusing their event with preempted actions ended by the same submission.
 */
UCLASS(meta = (DisplayName = "Execute Gameplay Action"))
class GAMEPLAYACTIONSAI_API UBTTask_ExecuteGameplayAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ExecuteGameplayAction();

	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	FGameplayActionExecutionSpec ExecutionSpec;

	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	TArray<FGameplayActionBlackboardParameterBinding> ParameterBindings;

	/** Optional Actor containing the component. None resolves controlled Pawn, then AIController. */
	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	FBlackboardKeySelector ActionOwnerActor;

	/** Optional Struct Blackboard output of type FGameplayActionHandle. */
	UPROPERTY(EditAnywhere, Category = "Output")
	FBlackboardKeySelector HandleOutput;

	/** Optional Struct Blackboard output of type FGameplayActionSubmissionResult. */
	UPROPERTY(EditAnywhere, Category = "Output")
	FBlackboardKeySelector SubmissionResultOutput;

	/** Optional Struct Blackboard output of type FGameplayActionResult. */
	UPROPERTY(EditAnywhere, Category = "Output")
	FBlackboardKeySelector TerminalResultOutput;

	/** Abort cancels only the handle created by this task; no component-wide abort is performed. */
	UPROPERTY(EditAnywhere, Category = "Gameplay Action")
	bool bCancelActionOnAbort = true;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTNodeResult::Type TaskResult) override;

private:
	void HandleActionEnded(const FGameplayActionEvent& Event);
	void RemoveEndedObserver();
	bool WriteOutputs(
		class UBlackboardComponent* Blackboard,
		const FGameplayActionSubmissionResult* Submission,
		const FGameplayActionResult* TerminalResult);
	EBTNodeResult::Type ResultToTaskResult(const FGameplayActionResult& Result) const;

	TWeakObjectPtr<UBehaviorTreeComponent> ActiveBehaviorTree;
	TWeakObjectPtr<UGameplayActionComponent> ActiveActionComponent;
	FGameplayActionHandle ActiveHandle;
	FDelegateHandle EndedObserverHandle;
	TArray<FGameplayActionEvent> DeferredEndedEvents;
	bool bSubmitting = false;
};
