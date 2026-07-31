#pragma once

#include "BehaviorTree/BTTaskNode.h"
#include "Types/IntentReplayTypes.h"
#include "BTTask_ParadoxRunIntentReplay.generated.h"

class UParadoxCloneBehaviorCoordinatorComponent;

/** Latent, node-instanced Replay branch task; only this node starts or resumes IntentReplay. */
UCLASS()
class PARADOX_API UBTTask_ParadoxRunIntentReplay : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ParadoxRunIntentReplay();

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
	virtual void OnTaskFinished(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		EBTNodeResult::Type TaskResult) override;

private:
	void HandleReplayAuthorized();
	void HandleTimelineLifecycle(
		const FIntentReplayTimelineLifecycleEvent& Event);
	void CleanupBindings();
	bool TryStartOrResume();

	TWeakObjectPtr<UBehaviorTreeComponent> ActiveTree;
	TWeakObjectPtr<UParadoxCloneBehaviorCoordinatorComponent> Coordinator;
	FDelegateHandle ReplayAuthorizedHandle;
	FDelegateHandle TimelineLifecycleHandle;
	FIntentReplayPlaybackSessionId ExpectedSessionId;
};

