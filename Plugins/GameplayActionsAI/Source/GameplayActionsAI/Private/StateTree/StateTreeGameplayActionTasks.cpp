#include "StateTree/StateTreeGameplayActionTasks.h"

#include "AIController.h"
#include "Components/GameplayActionComponent.h"
#include "GameplayActionTags.h"
#include "StateTree/GameplayActionStateTreeObserver.h"
#include "StateTreeExecutionContext.h"

FStateTreeExecuteGameplayActionTask::FStateTreeExecuteGameplayActionTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

EStateTreeRunStatus FStateTreeExecuteGameplayActionTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	InstanceData.Handle = FGameplayActionHandle();
	InstanceData.SubmissionResult = FGameplayActionSubmissionResult();
	InstanceData.TerminalResult = FGameplayActionResult();

	FString Diagnostic;
	UGameplayActionComponent* Component = GameplayActionsAI::ResolveActionComponent(
		InstanceData.ActionOwnerActor.Get(),
		InstanceData.AIController.Get(),
		Diagnostic);
	if (!Component)
	{
		return EStateTreeRunStatus::Failed;
	}

	FGameplayActionRequestBuildResult Build =
		GameplayActionsAI::BuildRequest(InstanceData.ExecutionSpec, nullptr, {});
	if (!Build.bSucceeded)
	{
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.Observer = NewObject<UGameplayActionStateTreeObserver>(Context.GetOwner());
	InstanceData.Observer->Bind(*Component);
	InstanceData.Observer->BeginSubmission();

	InstanceData.SubmissionResult = Component->SubmitAction(Build.Request);
	InstanceData.Handle = InstanceData.SubmissionResult.Handle;
	InstanceData.Observer->CompleteSubmission(InstanceData.Handle);

	if (!InstanceData.SubmissionResult.IsAccepted())
	{
		InstanceData.Observer->Unbind();
		return EStateTreeRunStatus::Failed;
	}
	if (InstanceData.Observer->HasTerminalResult())
	{
		InstanceData.TerminalResult = InstanceData.Observer->GetTerminalResult();
		const EStateTreeRunStatus Status =
			InstanceData.TerminalResult.TerminalState == EGameplayActionState::Succeeded
				? EStateTreeRunStatus::Succeeded
				: EStateTreeRunStatus::Failed;
		InstanceData.Observer->Unbind();
		return Status;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeExecuteGameplayActionTask::Tick(
	FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.Observer)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (!InstanceData.Observer->HasTerminalResult())
	{
		return EStateTreeRunStatus::Running;
	}

	InstanceData.TerminalResult = InstanceData.Observer->GetTerminalResult();
	InstanceData.Observer->Unbind();
	return InstanceData.TerminalResult.TerminalState == EGameplayActionState::Succeeded
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Failed;
}

void FStateTreeExecuteGameplayActionTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	UGameplayActionStateTreeObserver* Observer = InstanceData.Observer;
	// Cancellation must target the component that created the handle. Re-running component
	// resolution here could select a different Actor after possession or component changes.
	UGameplayActionComponent* ActionComponent =
		Observer ? Observer->GetActionComponent() : nullptr;
	if (Observer)
	{
		Observer->Unbind();
	}

	if (InstanceData.bCancelActionOnExit
		&& ActionComponent
		&& InstanceData.Handle.IsValid())
	{
		EGameplayActionState State = EGameplayActionState::Created;
		if (ActionComponent->GetActionState(InstanceData.Handle, State)
			&& State != EGameplayActionState::Succeeded
			&& State != EGameplayActionState::Failed
			&& State != EGameplayActionState::Cancelled
			&& State != EGameplayActionState::Interrupted
			&& State != EGameplayActionState::Aborted)
		{
			ActionComponent->CancelAction(
				InstanceData.Handle,
				GameplayActionTags::Result_Cancelled_ByRequester);
		}
	}
	InstanceData.Observer = nullptr;
}

FStateTreeCancelGameplayActionTask::FStateTreeCancelGameplayActionTask()
{
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FStateTreeCancelGameplayActionTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.ReasonTag.IsValid())
	{
		InstanceData.ReasonTag = GameplayActionTags::Result_Cancelled_ByRequester;
	}

	FString Diagnostic;
	UGameplayActionComponent* Component = GameplayActionsAI::ResolveActionComponent(
		InstanceData.ActionOwnerActor.Get(),
		InstanceData.AIController.Get(),
		Diagnostic);
	if (!Component)
	{
		InstanceData.OperationResult = EGameplayActionOperationResult::HandleNotFound;
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.OperationResult =
		Component->CancelAction(InstanceData.Handle, InstanceData.ReasonTag);
	return InstanceData.OperationResult == EGameplayActionOperationResult::Succeeded
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Failed;
}

bool FStateTreeCanSubmitGameplayActionCondition::TestCondition(
	FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	// Conditions can be evaluated repeatedly; never leave a previous successful preflight visible
	// when the current component resolution or request construction fails.
	InstanceData.PreflightResult = FGameplayActionSubmissionResult();
	FString Diagnostic;
	UGameplayActionComponent* Component = GameplayActionsAI::ResolveActionComponent(
		InstanceData.ActionOwnerActor.Get(),
		InstanceData.AIController.Get(),
		Diagnostic);
	if (!Component)
	{
		return false;
	}

	const FGameplayActionRequestBuildResult Build =
		GameplayActionsAI::BuildRequest(InstanceData.ExecutionSpec, nullptr, {});
	if (!Build.bSucceeded)
	{
		return false;
	}

	InstanceData.PreflightResult = Component->PreflightAction(Build.Request);
	return InstanceData.PreflightResult.IsAccepted()
		&& (!InstanceData.bRequireImmediateStart
			|| InstanceData.PreflightResult.Status == EGameplayActionSubmissionStatus::AcceptedStarted);
}
