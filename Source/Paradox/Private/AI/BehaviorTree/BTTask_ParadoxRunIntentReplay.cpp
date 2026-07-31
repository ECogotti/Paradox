#include "AI/BehaviorTree/BTTask_ParadoxRunIntentReplay.h"

#include "AIController.h"
#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Components/IntentReplayComponent.h"
#include "Playback/IntentReplayPlaybackSession.h"

UBTTask_ParadoxRunIntentReplay::UBTTask_ParadoxRunIntentReplay()
{
	NodeName = TEXT("Paradox Run Intent Replay");
	bCreateNodeInstance = true;
	bNotifyTaskFinished = true;
	bIgnoreRestartSelf = true;
}

EBTNodeResult::Type UBTTask_ParadoxRunIntentReplay::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	CleanupBindings();
	AAIController* Controller = OwnerComp.GetAIOwner();
	AParadoxCloneCharacter* Clone = Controller
		? Cast<AParadoxCloneCharacter>(Controller->GetPawn())
		: nullptr;
	UParadoxCloneBehaviorCoordinatorComponent* Resolved =
		Clone ? Clone->GetBehaviorCoordinator() : nullptr;
	if (!Resolved
		|| Resolved->GetCurrentMode()
			!= EParadoxCloneBehaviorMode::Replay
		|| !Resolved->GetReplayComponent()
		|| !Resolved->GetReplayComponent()->GetActivePlaybackSession())
	{
		return EBTNodeResult::Failed;
	}

	ActiveTree = &OwnerComp;
	Coordinator = Resolved;
	ExpectedSessionId = Resolved->GetReplayComponent()
		->GetActivePlaybackSession()->GetSessionId();
	ReplayAuthorizedHandle =
		Resolved->OnReplayAuthorizedNative().AddUObject(
			this,
			&UBTTask_ParadoxRunIntentReplay::HandleReplayAuthorized);
	TimelineLifecycleHandle =
		Resolved->GetReplayComponent()
			->OnTimelineLifecycleChangedNative()
			.AddUObject(
				this,
				&UBTTask_ParadoxRunIntentReplay::HandleTimelineLifecycle);
	if (!TryStartOrResume())
	{
		const FParadoxCloneBehaviorOperationResult Result =
			Resolved->StartAuthorizedReplayFromBehaviorTree();
		if (Result.Status
			!= EParadoxCloneBehaviorOperationStatus::NotAuthorized)
		{
			CleanupBindings();
			return EBTNodeResult::Failed;
		}
	}
	if (Resolved->GetReplayComponent()->GetPlaybackState()
		== EIntentReplayPlaybackState::Completed)
	{
		CleanupBindings();
		return EBTNodeResult::Succeeded;
	}
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_ParadoxRunIntentReplay::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	CleanupBindings();
	return EBTNodeResult::Aborted;
}

void UBTTask_ParadoxRunIntentReplay::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	CleanupBindings();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTTask_ParadoxRunIntentReplay::HandleReplayAuthorized()
{
	TryStartOrResume();
}

void UBTTask_ParadoxRunIntentReplay::HandleTimelineLifecycle(
	const FIntentReplayTimelineLifecycleEvent& Event)
{
	if (Event.Domain != EIntentReplayTimelineDomain::Playback
		|| Event.Clock.PlaybackSessionId != ExpectedSessionId)
	{
		return;
	}
	if (Event.NewPlaybackState == EIntentReplayPlaybackState::Completed
		|| Event.NewPlaybackState == EIntentReplayPlaybackState::Failed
		|| Event.NewPlaybackState == EIntentReplayPlaybackState::Cancelled)
	{
		UBehaviorTreeComponent* Tree = ActiveTree.Get();
		const EBTNodeResult::Type Result =
			Event.NewPlaybackState == EIntentReplayPlaybackState::Completed
				? EBTNodeResult::Succeeded
				: EBTNodeResult::Failed;
		CleanupBindings();
		if (Tree)
		{
			FinishLatentTask(*Tree, Result);
		}
	}
}

bool UBTTask_ParadoxRunIntentReplay::TryStartOrResume()
{
	UParadoxCloneBehaviorCoordinatorComponent* Resolved = Coordinator.Get();
	if (!Resolved || !Resolved->IsReplayStartAuthorized())
	{
		return false;
	}
	const FParadoxCloneBehaviorOperationResult Result =
		Resolved->StartAuthorizedReplayFromBehaviorTree();
	return Result.IsSuccess();
}

void UBTTask_ParadoxRunIntentReplay::CleanupBindings()
{
	if (UParadoxCloneBehaviorCoordinatorComponent* Resolved =
		Coordinator.Get())
	{
		if (ReplayAuthorizedHandle.IsValid())
		{
			Resolved->OnReplayAuthorizedNative().Remove(
				ReplayAuthorizedHandle);
		}
		if (TimelineLifecycleHandle.IsValid()
			&& Resolved->GetReplayComponent())
		{
			Resolved->GetReplayComponent()
				->OnTimelineLifecycleChangedNative()
				.Remove(TimelineLifecycleHandle);
		}
	}
	ReplayAuthorizedHandle.Reset();
	TimelineLifecycleHandle.Reset();
	ActiveTree.Reset();
	Coordinator.Reset();
	ExpectedSessionId = FIntentReplayPlaybackSessionId();
}

