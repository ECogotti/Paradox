#include "Policies/EntityRelationPolicy.h"

bool UEntityRelationPolicy::EvaluatePolicy(
	const FEntityRelationPolicyContext& Context,
	FEntityRelationContribution& OutContribution,
	FString& OutError) const
{
	OutContribution = FEntityRelationContribution();
	OutError.Reset();
	if (PolicyId.IsNone())
	{
		OutError = TEXT("PolicyId is required.");
		return false;
	}
	if (!bEnabled)
	{
		OutError = TEXT("Disabled policies cannot be evaluated directly.");
		return false;
	}
	if (!Context.Source.EntityId.IsValid() || !Context.Target.EntityId.IsValid() || !Context.QueryContext.Domain.IsValid())
	{
		OutError = TEXT("Policy context contains an invalid identity or domain.");
		return false;
	}
	for (const TPair<FGameplayTag, float>& Pair : Context.QueryContext.NumericContext)
	{
		if (!Pair.Key.IsValid() || !FMath::IsFinite(Pair.Value))
		{
			OutError = TEXT("Policy context contains an invalid numeric value.");
			return false;
		}
	}
	if (!SupportsDomain(Context.QueryContext.Domain))
	{
		OutError = TEXT("Policy does not support this domain.");
		return false;
	}

	OutContribution = EvaluateRelation(Context);
	return true;
}

FEntityRelationContribution UEntityRelationPolicy::EvaluateRelation_Implementation(
	const FEntityRelationPolicyContext& Context) const
{
	return FEntityRelationContribution();
}
