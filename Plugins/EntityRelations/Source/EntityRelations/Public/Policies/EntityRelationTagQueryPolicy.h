#pragma once

#include "Policies/EntityRelationPolicy.h"
#include "EntityRelationTagQueryPolicy.generated.h"

/** Generic native policy matching Source, Target, context and directed-state tag queries. Empty queries match all. */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Entity Relation Tag Query Policy"))
class ENTITYRELATIONS_API UEntityRelationTagQueryPolicy : public UEntityRelationPolicy
{
	GENERATED_BODY()

protected:
	virtual FEntityRelationContribution EvaluateRelation_Implementation(const FEntityRelationPolicyContext& Context) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Match")
	FGameplayTagQuery SourceIdentityQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Match")
	FGameplayTagQuery SourceAffiliationQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Match")
	FGameplayTagQuery TargetIdentityQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Match")
	FGameplayTagQuery TargetAffiliationQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Match")
	FGameplayTagQuery DirectedStateQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Match")
	FGameplayTagQuery ContextTagsQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Contribution")
	EEntityRelationDecision MatchingDecision = EEntityRelationDecision::NoOpinion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Contribution")
	FGameplayTagContainer MatchingClassificationTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Contribution")
	FGameplayTagContainer MatchingOutcomeTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Contribution")
	FGameplayTagContainer MatchingReasonTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Contribution")
	FString MatchingDebugMessage;
};
