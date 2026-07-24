#include "Tests/EntityRelationsTestTypes.h"

#include "Components/EntityIdentityComponent.h"
#include "Components/EntityRelationStateComponent.h"

AEntityRelationsTestActor::AEntityRelationsTestActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Identity = CreateDefaultSubobject<UEntityIdentityComponent>(TEXT("EntityIdentity"));
	RelationState = CreateDefaultSubobject<UEntityRelationStateComponent>(TEXT("EntityRelationState"));
}

void UEntityRelationsTestPolicy::Configure(
	FName InPolicyId,
	int32 InPriority,
	FGameplayTag Domain,
	EEntityRelationDecision InDecision,
	FGameplayTag ClassificationTag,
	FGameplayTag OutcomeTag,
	FGameplayTag ReasonTag,
	bool bInEnabled,
	bool bInCacheable,
	bool bInStopAfterContribution,
	bool bInContributionStops)
{
	PolicyId = InPolicyId;
	Priority = InPriority;
	SupportedDomains.Reset();
	SupportedDomains.AddTag(Domain);
	bEnabled = bInEnabled;
	bCacheable = bInCacheable;
	bStopEvaluationAfterContribution = bInStopAfterContribution;
	TestContribution = FEntityRelationContribution();
	TestContribution.Decision = InDecision;
	TestContribution.bStopEvaluation = bInContributionStops;
	if (ClassificationTag.IsValid())
	{
		TestContribution.ClassificationTags.AddTag(ClassificationTag);
	}
	if (OutcomeTag.IsValid())
	{
		TestContribution.OutcomeTags.AddTag(OutcomeTag);
	}
	if (ReasonTag.IsValid())
	{
		TestContribution.ReasonTags.AddTag(ReasonTag);
	}
}

FEntityRelationContribution UEntityRelationsTestPolicy::EvaluateRelation_Implementation(
	const FEntityRelationPolicyContext& Context) const
{
	++EvaluationCount;
	bLastHadDirectedState = Context.bHasDirectedState;
	LastSourceActor = Context.Source.Actor;
	LastTargetActor = Context.Target.Actor;
	return TestContribution;
}
