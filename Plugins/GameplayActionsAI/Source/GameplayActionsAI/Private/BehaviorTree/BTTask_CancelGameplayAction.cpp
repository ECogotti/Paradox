#include "BehaviorTree/BTTask_CancelGameplayAction.h"

#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Components/GameplayActionComponent.h"
#include "GameplayActionTags.h"
#include "GameplayActionsAIModule.h"
#include "Types/GameplayActionExecutionSpec.h"

UBTTask_CancelGameplayAction::UBTTask_CancelGameplayAction()
{
	NodeName = TEXT("Cancel Gameplay Action");
	Handle.AddStructFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTTask_CancelGameplayAction, Handle),
		FGameplayActionHandle::StaticStruct());
	ActionOwnerActor.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTTask_CancelGameplayAction, ActionOwnerActor),
		AActor::StaticClass());
	ActionOwnerActor.AllowNoneAsValue(true);
	ReasonTag = GameplayActionTags::Result_Cancelled_ByRequester;
}

EBTNodeResult::Type UBTTask_CancelGameplayAction::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard || Handle.SelectedKeyName.IsNone())
	{
		GAMEPLAYACTIONSAI_LOG_WARNING(
			TEXT("%s requires a Blackboard Struct key containing FGameplayActionHandle."),
			*GetNameSafe(this));
		return EBTNodeResult::Failed;
	}

	const FBlackboard::FKey HandleKey = Blackboard->GetKeyID(Handle.SelectedKeyName);
	const FConstStructView HandleView =
		Blackboard->GetValue<UBlackboardKeyType_Struct>(HandleKey);
	if (!HandleView.IsValid()
		|| HandleView.GetScriptStruct() != FGameplayActionHandle::StaticStruct())
	{
		GAMEPLAYACTIONSAI_LOG_WARNING(
			TEXT("%s Blackboard key '%s' is not an FGameplayActionHandle Struct."),
			*GetNameSafe(this),
			*Handle.SelectedKeyName.ToString());
		return EBTNodeResult::Failed;
	}

	AActor* ExplicitActor = !ActionOwnerActor.SelectedKeyName.IsNone()
		? Cast<AActor>(Blackboard->GetValueAsObject(ActionOwnerActor.SelectedKeyName))
		: nullptr;
	FString Diagnostic;
	UGameplayActionComponent* Component =
		GameplayActionsAI::ResolveActionComponent(ExplicitActor, OwnerComp.GetAIOwner(), Diagnostic);
	if (!Component)
	{
		GAMEPLAYACTIONSAI_LOG_WARNING(
			TEXT("%s failed component resolution: %s"),
			*GetNameSafe(this),
			*Diagnostic);
		return EBTNodeResult::Failed;
	}

	const FGameplayTag EffectiveReason =
		ReasonTag.IsValid() ? ReasonTag : GameplayActionTags::Result_Cancelled_ByRequester;
	const EGameplayActionOperationResult Result =
		Component->CancelAction(HandleView.Get<FGameplayActionHandle>(), EffectiveReason);
	return Result == EGameplayActionOperationResult::Succeeded
		? EBTNodeResult::Succeeded
		: EBTNodeResult::Failed;
}
