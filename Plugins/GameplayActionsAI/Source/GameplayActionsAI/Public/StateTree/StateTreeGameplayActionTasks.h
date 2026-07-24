#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "Tasks/StateTreeAITask.h"
#include "Types/GameplayActionExecutionSpec.h"
#include "StateTreeGameplayActionTasks.generated.h"

class AAIController;
class UGameplayActionStateTreeObserver;

USTRUCT()
struct GAMEPLAYACTIONSAI_API FStateTreeExecuteGameplayActionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController;

	/** Optional explicit component owner. Null resolves controlled Pawn, then AIController. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	TObjectPtr<AActor> ActionOwnerActor;

	/** Fixed-layout fields inside Parameters are individually bindable by StateTree. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FGameplayActionExecutionSpec ExecutionSpec;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bCancelActionOnExit = true;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	FGameplayActionHandle Handle;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	FGameplayActionSubmissionResult SubmissionResult;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	FGameplayActionResult TerminalResult;

	UPROPERTY(Transient)
	TObjectPtr<UGameplayActionStateTreeObserver> Observer;
};

/** StateTree counterpart of Execute Gameplay Action; queued actions remain Running until Ended. */
USTRUCT(meta = (DisplayName = "Execute Gameplay Action", Category = "AI|Action"))
struct GAMEPLAYACTIONSAI_API FStateTreeExecuteGameplayActionTask : public FStateTreeAIActionTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeExecuteGameplayActionInstanceData;

	FStateTreeExecuteGameplayActionTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(
		FStateTreeExecutionContext& Context,
		float DeltaTime) const override;
	virtual void ExitState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct GAMEPLAYACTIONSAI_API FStateTreeCancelGameplayActionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	TObjectPtr<AActor> ActionOwnerActor;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FGameplayActionHandle Handle;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FGameplayTag ReasonTag;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	EGameplayActionOperationResult OperationResult = EGameplayActionOperationResult::HandleNotFound;
};

/** Cancels one bound handle and succeeds only when the core component accepts the operation. */
USTRUCT(meta = (DisplayName = "Cancel Gameplay Action", Category = "AI|Action"))
struct GAMEPLAYACTIONSAI_API FStateTreeCancelGameplayActionTask : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeCancelGameplayActionInstanceData;

	FStateTreeCancelGameplayActionTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(
		FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct GAMEPLAYACTIONSAI_API FStateTreeCanSubmitGameplayActionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<AActor> ActionOwnerActor;

	UPROPERTY(EditAnywhere, Category = "Input")
	FGameplayActionExecutionSpec ExecutionSpec;

	UPROPERTY(EditAnywhere, Category = "Input")
	bool bRequireImmediateStart = false;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	FGameplayActionSubmissionResult PreflightResult;
};

/** StateTree condition performing core preflight without handles, Init, journal writes, or preemption. */
USTRUCT(meta = (DisplayName = "Can Submit Gameplay Action", Category = "AI"))
struct GAMEPLAYACTIONSAI_API FStateTreeCanSubmitGameplayActionCondition
	: public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FStateTreeCanSubmitGameplayActionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
