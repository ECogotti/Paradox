#include "BehaviorTree/BTDecorator_CanSubmitGameplayAction.h"

#include "AIController.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Components/GameplayActionComponent.h"

UBTDecorator_CanSubmitGameplayAction::UBTDecorator_CanSubmitGameplayAction()
{
	NodeName = TEXT("Can Submit Gameplay Action");
	ActionOwnerActor.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_CanSubmitGameplayAction, ActionOwnerActor),
		AActor::StaticClass());
	ActionOwnerActor.AllowNoneAsValue(true);
}

bool UBTDecorator_CanSubmitGameplayAction::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* ExplicitActor = Blackboard && !ActionOwnerActor.SelectedKeyName.IsNone()
		? Cast<AActor>(Blackboard->GetValueAsObject(ActionOwnerActor.SelectedKeyName))
		: nullptr;

	FString Diagnostic;
	UGameplayActionComponent* Component =
		GameplayActionsAI::ResolveActionComponent(ExplicitActor, OwnerComp.GetAIOwner(), Diagnostic);
	if (!Component)
	{
		return false;
	}

	const FGameplayActionRequestBuildResult Build =
		GameplayActionsAI::BuildRequest(ExecutionSpec, Blackboard, ParameterBindings);
	if (!Build.bSucceeded)
	{
		return false;
	}

	const FGameplayActionSubmissionResult Preflight = Component->PreflightAction(Build.Request);
	return Preflight.IsAccepted()
		&& (!bRequireImmediateStart
			|| Preflight.Status == EGameplayActionSubmissionStatus::AcceptedStarted);
}
