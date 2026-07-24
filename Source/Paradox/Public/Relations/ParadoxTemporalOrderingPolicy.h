#pragma once

#include "Policies/EntityRelationPolicy.h"
#include "ParadoxTemporalOrderingPolicy.generated.h"

/** Project-owned Visual Perception rule for past observers seeing future temporal entities. */
UCLASS(BlueprintType, EditInlineNew)
class PARADOX_API UParadoxTemporalOrderingPolicy : public UEntityRelationPolicy
{
	GENERATED_BODY()

public:
	UParadoxTemporalOrderingPolicy();

protected:
	virtual FEntityRelationContribution EvaluateRelation_Implementation(
		const FEntityRelationPolicyContext& Context) const override;
};
