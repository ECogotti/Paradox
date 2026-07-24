#include "BehaviorTree/BTTask_ExecuteGameplayAction.h"

#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Components/GameplayActionComponent.h"
#include "GameplayActionTags.h"
#include "GameplayActionsAIModule.h"

UBTTask_ExecuteGameplayAction::UBTTask_ExecuteGameplayAction()
{
	NodeName = TEXT("Execute Gameplay Action");
	bCreateNodeInstance = true;
	bNotifyTaskFinished = true;

	ActionOwnerActor.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTTask_ExecuteGameplayAction, ActionOwnerActor),
		AActor::StaticClass());
	ActionOwnerActor.AllowNoneAsValue(true);

	HandleOutput.AddStructFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTTask_ExecuteGameplayAction, HandleOutput),
		FGameplayActionHandle::StaticStruct());
	SubmissionResultOutput.AddStructFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTTask_ExecuteGameplayAction, SubmissionResultOutput),
		FGameplayActionSubmissionResult::StaticStruct());
	TerminalResultOutput.AddStructFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTTask_ExecuteGameplayAction, TerminalResultOutput),
		FGameplayActionResult::StaticStruct());
	HandleOutput.AllowNoneAsValue(true);
	SubmissionResultOutput.AllowNoneAsValue(true);
	TerminalResultOutput.AllowNoneAsValue(true);
}

EBTNodeResult::Type UBTTask_ExecuteGameplayAction::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	RemoveEndedObserver();
	ActiveBehaviorTree = &OwnerComp;
	ActiveHandle = FGameplayActionHandle();
	DeferredEndedEvents.Reset();
	bSubmitting = false;

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	AActor* ExplicitActor = Blackboard && !ActionOwnerActor.SelectedKeyName.IsNone()
		? Cast<AActor>(Blackboard->GetValueAsObject(ActionOwnerActor.SelectedKeyName))
		: nullptr;

	FString Diagnostic;
	UGameplayActionComponent* ActionComponent =
		GameplayActionsAI::ResolveActionComponent(ExplicitActor, Controller, Diagnostic);
	if (!ActionComponent)
	{
		GAMEPLAYACTIONSAI_LOG_WARNING(
			TEXT("%s failed component resolution: %s"),
			*GetNameSafe(this),
			*Diagnostic);
		return EBTNodeResult::Failed;
	}

	FGameplayActionRequestBuildResult Build =
		GameplayActionsAI::BuildRequest(ExecutionSpec, Blackboard, ParameterBindings);
	if (!Build.bSucceeded)
	{
		GAMEPLAYACTIONSAI_LOG_WARNING(
			TEXT("%s could not build its request: %s"),
			*GetNameSafe(this),
			*Build.DiagnosticMessage);
		return EBTNodeResult::Failed;
	}

	ActiveActionComponent = ActionComponent;
	EndedObserverHandle =
		ActionComponent->OnActionEndedNative().AddUObject(this, &ThisClass::HandleActionEnded);

	// Ended can be emitted before SubmitAction returns when Action Start completes synchronously.
	// Buffering preserves the handle-based identity once the submission result becomes available.
	bSubmitting = true;
	const FGameplayActionSubmissionResult Submission = ActionComponent->SubmitAction(Build.Request);
	bSubmitting = false;
	ActiveHandle = Submission.Handle;

	WriteOutputs(Blackboard, &Submission, nullptr);
	if (!Submission.IsAccepted())
	{
		RemoveEndedObserver();
		return EBTNodeResult::Failed;
	}

	for (const FGameplayActionEvent& Event : DeferredEndedEvents)
	{
		if (Event.Handle == ActiveHandle && Event.bHasResult)
		{
			WriteOutputs(Blackboard, nullptr, &Event.Result);
			const EBTNodeResult::Type TaskResult = ResultToTaskResult(Event.Result);
			RemoveEndedObserver();
			return TaskResult;
		}
	}
	DeferredEndedEvents.Reset();
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_ExecuteGameplayAction::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UGameplayActionComponent* ActionComponent = ActiveActionComponent.Get();
	const FGameplayActionHandle Handle = ActiveHandle;
	RemoveEndedObserver();

	if (bCancelActionOnAbort && ActionComponent && Handle.IsValid())
	{
		ActionComponent->CancelAction(Handle, GameplayActionTags::Result_Cancelled_ByRequester);
	}
	return EBTNodeResult::Aborted;
}

void UBTTask_ExecuteGameplayAction::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	RemoveEndedObserver();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTTask_ExecuteGameplayAction::HandleActionEnded(const FGameplayActionEvent& Event)
{
	if (bSubmitting)
	{
		DeferredEndedEvents.Add(Event);
		return;
	}
	if (Event.Handle != ActiveHandle || !Event.bHasResult)
	{
		return;
	}

	UBehaviorTreeComponent* BehaviorTree = ActiveBehaviorTree.Get();
	if (!BehaviorTree)
	{
		RemoveEndedObserver();
		return;
	}

	WriteOutputs(BehaviorTree->GetBlackboardComponent(), nullptr, &Event.Result);
	const EBTNodeResult::Type TaskResult = ResultToTaskResult(Event.Result);
	RemoveEndedObserver();
	FinishLatentTask(*BehaviorTree, TaskResult);
}

void UBTTask_ExecuteGameplayAction::RemoveEndedObserver()
{
	if (UGameplayActionComponent* Component = ActiveActionComponent.Get();
		Component && EndedObserverHandle.IsValid())
	{
		Component->OnActionEndedNative().Remove(EndedObserverHandle);
	}
	EndedObserverHandle.Reset();
	ActiveActionComponent.Reset();
	ActiveBehaviorTree.Reset();
	bSubmitting = false;
}

bool UBTTask_ExecuteGameplayAction::WriteOutputs(
	UBlackboardComponent* Blackboard,
	const FGameplayActionSubmissionResult* Submission,
	const FGameplayActionResult* TerminalResult)
{
	FString Diagnostic;
	bool bSucceeded = true;
	if (Submission)
	{
		bSucceeded &= GameplayActionsAI::WriteBlackboardStruct(
			Blackboard,
			HandleOutput,
			FConstStructView::Make(Submission->Handle),
			Diagnostic);
		bSucceeded &= GameplayActionsAI::WriteBlackboardStruct(
			Blackboard,
			SubmissionResultOutput,
			FConstStructView::Make(*Submission),
			Diagnostic);
	}
	if (TerminalResult)
	{
		bSucceeded &= GameplayActionsAI::WriteBlackboardStruct(
			Blackboard,
			TerminalResultOutput,
			FConstStructView::Make(*TerminalResult),
			Diagnostic);
	}
	if (!bSucceeded)
	{
		GAMEPLAYACTIONSAI_LOG_WARNING(TEXT("%s output failed: %s"), *GetNameSafe(this), *Diagnostic);
	}
	return bSucceeded;
}

EBTNodeResult::Type UBTTask_ExecuteGameplayAction::ResultToTaskResult(
	const FGameplayActionResult& Result) const
{
	return Result.TerminalState == EGameplayActionState::Succeeded
		? EBTNodeResult::Succeeded
		: EBTNodeResult::Failed;
}
