#include "AI/BehaviorTree/BTTask_ParadoxInvestigateObservation.h"

#include "AIController.h"
#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "GameplayActionTags.h"
#include "Investigation/ParadoxCloneInvestigationComponent.h"

UBTTask_ParadoxInvestigateObservation::
	UBTTask_ParadoxInvestigateObservation()
{
	NodeName = TEXT("Paradox Investigate Observation");
	bCreateNodeInstance = true;
	bNotifyTaskFinished = true;
	bIgnoreRestartSelf = true;
}

EBTNodeResult::Type
UBTTask_ParadoxInvestigateObservation::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	CleanupBindings();
	AAIController* Controller = OwnerComp.GetAIOwner();
	AParadoxCloneCharacter* Clone = Controller
		? Cast<AParadoxCloneCharacter>(Controller->GetPawn())
		: nullptr;
	UParadoxCloneBehaviorCoordinatorComponent* ResolvedCoordinator =
		Clone ? Clone->GetBehaviorCoordinator() : nullptr;
	UParadoxCloneInvestigationComponent* ResolvedInvestigation =
		Clone ? Clone->GetInvestigationComponent() : nullptr;
	if (!ResolvedCoordinator || !ResolvedInvestigation
		|| ResolvedCoordinator->GetCurrentMode()
			!= EParadoxCloneBehaviorMode::Investigating)
	{
		return EBTNodeResult::Failed;
	}
	const FParadoxInvestigationContext Context =
		ResolvedCoordinator->GetCurrentInvestigation();
	if (!Context.IsValid())
	{
		return EBTNodeResult::Failed;
	}

	ActiveTree = &OwnerComp;
	Coordinator = ResolvedCoordinator;
	Investigation = ResolvedInvestigation;
	ExpectedRevision = Context.InvestigationRevision;
	InvestigationFinishedHandle =
		ResolvedInvestigation->OnInvestigationFinishedNative().AddUObject(
			this,
			&UBTTask_ParadoxInvestigateObservation::HandleInvestigationFinished);
	InvestigationRetargetedHandle =
		ResolvedInvestigation->OnInvestigationRetargetedNative().AddUObject(
			this,
			&UBTTask_ParadoxInvestigateObservation::HandleInvestigationRetargeted);
	ReplayContinuityHandle =
		ResolvedCoordinator->OnReplayContinuityNative().AddUObject(
			this,
			&UBTTask_ParadoxInvestigateObservation::HandleReplayContinuity);

	const FParadoxCloneBehaviorOperationResult Start =
		ResolvedInvestigation->StartInvestigation(Context);
	if (!Start.IsSuccess())
	{
		CleanupBindings();
		return EBTNodeResult::Failed;
	}
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type
UBTTask_ParadoxInvestigateObservation::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	if (UParadoxCloneInvestigationComponent* Resolved =
		Investigation.Get();
		Resolved && Resolved->IsInvestigationActive())
	{
		Resolved->CancelInvestigation(
			GameplayActionTags::Result_Cancelled_ByRequester);
	}
	CleanupBindings();
	return EBTNodeResult::Aborted;
}

void UBTTask_ParadoxInvestigateObservation::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	CleanupBindings();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTTask_ParadoxInvestigateObservation::HandleInvestigationFinished(
	const FParadoxInvestigationContext& Context,
	const FGameplayActionResult& Result)
{
	if (Context.InvestigationRevision != ExpectedRevision)
	{
		return;
	}
	UParadoxCloneBehaviorCoordinatorComponent* Resolved =
		Coordinator.Get();
	if (!Resolved)
	{
		return;
	}
	const FParadoxCloneBehaviorOperationResult Completion =
		Resolved->CompleteInvestigation(Context, Result);
	if (Completion.IsSuccess()
		&& Resolved->GetCurrentMode()
			== EParadoxCloneBehaviorMode::Replay)
	{
		HandleReplayContinuity(true);
	}
}

void UBTTask_ParadoxInvestigateObservation::HandleInvestigationRetargeted(
	const FParadoxInvestigationContext& Context)
{
	ExpectedRevision = Context.InvestigationRevision;
}

void UBTTask_ParadoxInvestigateObservation::HandleReplayContinuity(
	const bool bRestored)
{
	if (!bRestored)
	{
		return;
	}
	UBehaviorTreeComponent* Tree = ActiveTree.Get();
	CleanupBindings();
	if (Tree)
	{
		FinishLatentTask(*Tree, EBTNodeResult::Succeeded);
	}
}

void UBTTask_ParadoxInvestigateObservation::CleanupBindings()
{
	if (UParadoxCloneInvestigationComponent* Resolved =
		Investigation.Get())
	{
		if (InvestigationFinishedHandle.IsValid())
		{
			Resolved->OnInvestigationFinishedNative().Remove(
				InvestigationFinishedHandle);
		}
		if (InvestigationRetargetedHandle.IsValid())
		{
			Resolved->OnInvestigationRetargetedNative().Remove(
				InvestigationRetargetedHandle);
		}
	}
	if (UParadoxCloneBehaviorCoordinatorComponent* Resolved =
		Coordinator.Get();
		Resolved && ReplayContinuityHandle.IsValid())
	{
		Resolved->OnReplayContinuityNative().Remove(
			ReplayContinuityHandle);
	}
	InvestigationFinishedHandle.Reset();
	InvestigationRetargetedHandle.Reset();
	ReplayContinuityHandle.Reset();
	ActiveTree.Reset();
	Coordinator.Reset();
	Investigation.Reset();
	ExpectedRevision = 0;
}

