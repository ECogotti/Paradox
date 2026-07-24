#include "Policies/EntityRelationTagQueryPolicy.h"

namespace
{
	bool Matches(const FGameplayTagQuery& Query, const FGameplayTagContainer& Tags)
	{
		return Query.IsEmpty() || Query.Matches(Tags);
	}
}

FEntityRelationContribution UEntityRelationTagQueryPolicy::EvaluateRelation_Implementation(
	const FEntityRelationPolicyContext& Context) const
{
	if (!Matches(SourceIdentityQuery, Context.Source.IdentityTags)
		|| !Matches(SourceAffiliationQuery, Context.Source.AffiliationTags)
		|| !Matches(TargetIdentityQuery, Context.Target.IdentityTags)
		|| !Matches(TargetAffiliationQuery, Context.Target.AffiliationTags)
		|| !Matches(ContextTagsQuery, Context.QueryContext.ContextTags)
		|| (!DirectedStateQuery.IsEmpty() && (!Context.bHasDirectedState || !DirectedStateQuery.Matches(Context.DirectedState.StateTags))))
	{
		return FEntityRelationContribution();
	}

	FEntityRelationContribution Contribution;
	Contribution.Decision = MatchingDecision;
	Contribution.ClassificationTags = MatchingClassificationTags;
	Contribution.OutcomeTags = MatchingOutcomeTags;
	Contribution.ReasonTags = MatchingReasonTags;
	Contribution.DebugMessage = MatchingDebugMessage;
	return Contribution;
}
