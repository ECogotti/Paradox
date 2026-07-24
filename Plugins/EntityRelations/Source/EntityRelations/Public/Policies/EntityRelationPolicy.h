#pragma once

#include "CoreMinimal.h"
#include "Types/EntityRelationTypes.h"
#include "EntityRelationPolicy.generated.h"

/** Stateless, instanced policy base. The public evaluator enforces configuration invariants. */
UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class ENTITYRELATIONS_API UEntityRelationPolicy : public UObject
{
	GENERATED_BODY()

public:
	FName GetPolicyId() const { return PolicyId; }
	int32 GetPriority() const { return Priority; }
	const FGameplayTagContainer& GetSupportedDomains() const { return SupportedDomains; }
	bool IsPolicyEnabled() const { return bEnabled; }
	bool IsCacheable() const { return bCacheable; }
	bool ShouldStopAfterContribution() const { return bStopEvaluationAfterContribution; }
	bool SupportsDomain(FGameplayTag Domain) const { return SupportedDomains.HasTagExact(Domain); }

	/** Validates the policy boundary, invokes the replaceable implementation and validates its output. */
	bool EvaluatePolicy(const FEntityRelationPolicyContext& Context, FEntityRelationContribution& OutContribution, FString& OutError) const;

protected:
	UFUNCTION(BlueprintNativeEvent, Category = "Entity Relations|Policy", meta = (DisplayName = "Evaluate Entity Relation"))
	FEntityRelationContribution EvaluateRelation(const FEntityRelationPolicyContext& Context) const;
	virtual FEntityRelationContribution EvaluateRelation_Implementation(const FEntityRelationPolicyContext& Context) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Policy")
	FName PolicyId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Policy")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Policy", meta = (Categories = "Relation.Domain"))
	FGameplayTagContainer SupportedDomains;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Policy")
	bool bStopEvaluationAfterContribution = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Policy")
	bool bCacheable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Policy")
	bool bEnabled = true;
};
