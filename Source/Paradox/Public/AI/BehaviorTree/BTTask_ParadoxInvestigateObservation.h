#pragma once

#include "Behavior/ParadoxCloneBehaviorTypes.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Types/GameplayActionTypes.h"
#include "BTTask_ParadoxInvestigateObservation.generated.h"

class UParadoxCloneBehaviorCoordinatorComponent;
class UParadoxCloneInvestigationComponent;

/** Latent node-instanced task that survives higher-priority retargets without duplication. */
UCLASS()
class PARADOX_API UBTTask_ParadoxInvestigateObservation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ParadoxInvestigateObservation();

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
	void HandleInvestigationFinished(
		const FParadoxInvestigationContext& Context,
		const FGameplayActionResult& Result);
	void HandleInvestigationRetargeted(
		const FParadoxInvestigationContext& Context);
	void HandleReplayContinuity(bool bRestored);
	void CleanupBindings();

	TWeakObjectPtr<UBehaviorTreeComponent> ActiveTree;
	TWeakObjectPtr<UParadoxCloneBehaviorCoordinatorComponent> Coordinator;
	TWeakObjectPtr<UParadoxCloneInvestigationComponent> Investigation;
	FDelegateHandle InvestigationFinishedHandle;
	FDelegateHandle InvestigationRetargetedHandle;
	FDelegateHandle ReplayContinuityHandle;
	int32 ExpectedRevision = 0;
};

