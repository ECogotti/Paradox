#include "Relations/ParadoxTemporalOrderingPolicy.h"

#include "EntityRelationTags.h"
#include "GameFramework/Actor.h"
#include "Paradox.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"

UParadoxTemporalOrderingPolicy::UParadoxTemporalOrderingPolicy()
{
	PolicyId = TEXT("ParadoxTemporalOrdering");
	Priority = 1000;
	SupportedDomains.AddTag(EntityRelationTags::Domain_VisualPerception);
	bStopEvaluationAfterContribution = true;
	bCacheable = false;
	bEnabled = true;
}

FEntityRelationContribution
UParadoxTemporalOrderingPolicy::EvaluateRelation_Implementation(
	const FEntityRelationPolicyContext& Context) const
{
	FEntityRelationContribution Contribution;
	const AActor* Observer = Context.Source.Actor;
	const AActor* Target = Context.Target.Actor;
	if (!Context.Source.EntityId.IsValid()
		|| !Context.Target.EntityId.IsValid())
	{
		Contribution.DebugMessage =
			TEXT("Paradox temporal ordering requires two valid Entity Relations identities.");
		return Contribution;
	}
	const UParadoxTemporalEntityComponent* ObserverTemporal =
		Observer
			? Observer->FindComponentByClass<UParadoxTemporalEntityComponent>()
			: nullptr;
	const UParadoxTemporalEntityComponent* TargetTemporal =
		Target
			? Target->FindComponentByClass<UParadoxTemporalEntityComponent>()
			: nullptr;
	if (!ObserverTemporal
		|| !TargetTemporal
		|| !ObserverTemporal->HasValidTemporalIndex()
		|| !TargetTemporal->HasValidTemporalIndex())
	{
		Contribution.DebugMessage =
			TEXT("Paradox temporal ordering requires two valid temporal identities.");
		return Contribution;
	}

	const int32 ObserverIndex = ObserverTemporal->GetTemporalIndex();
	const int32 TargetIndex = TargetTemporal->GetTemporalIndex();
	const bool bFutureObserved = ObserverIndex < TargetIndex;
	Contribution.Decision = bFutureObserved
		? EEntityRelationDecision::Deny
		: EEntityRelationDecision::Allow;
	Contribution.ReasonTags.AddTag(
		bFutureObserved
			? ParadoxGameplayTags::Relation_Reason_FutureTemporalOrder
			: ParadoxGameplayTags::Relation_Reason_SafeTemporalOrder);
	if (bFutureObserved)
	{
		Contribution.OutcomeTags.AddTag(
			ParadoxGameplayTags::Relation_Outcome_FutureObserved);
	}
	Contribution.DebugMessage = FString::Printf(
		TEXT("Temporal observer T%d evaluated target T%d as %s."),
		ObserverIndex,
		TargetIndex,
		bFutureObserved ? TEXT("future") : TEXT("safe"));
	Contribution.bStopEvaluation = true;
	return Contribution;
}
