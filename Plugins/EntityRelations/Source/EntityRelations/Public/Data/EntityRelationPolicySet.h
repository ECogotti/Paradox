#pragma once

#include "Engine/DataAsset.h"
#include "Types/EntityRelationTypes.h"
#include "EntityRelationPolicySet.generated.h"

class UEntityRelationPolicy;
class FDataValidationContext;

/** Ordered, immutable-at-runtime collection of instanced relation policies. */
UCLASS(BlueprintType, meta = (DisplayName = "Entity Relation Policy Set"))
class ENTITYRELATIONS_API UEntityRelationPolicySet : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Policy Set")
	FEntityRelationValidationResult ValidatePolicySet() const;

	const TArray<TObjectPtr<UEntityRelationPolicy>>& GetPolicies() const { return Policies; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

#if WITH_DEV_AUTOMATION_TESTS
	void SetPoliciesForTests(TArray<TObjectPtr<UEntityRelationPolicy>> InPolicies) { Policies = MoveTemp(InPolicies); }
#endif

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Entity Relations|Policy Set", meta = (AllowPrivateAccess = "true", EditInline, AllowEditInlineCustomization, MaxPropertyDepth = "8"))
	TArray<TObjectPtr<UEntityRelationPolicy>> Policies;
};
