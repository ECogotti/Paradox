#pragma once

#include "GameFramework/Actor.h"
#include "Policies/EntityRelationPolicy.h"
#include "EntityRelationsTestTypes.generated.h"

class UEntityIdentityComponent;
class UEntityRelationStateComponent;

/** Native Actor fixture exercising the same component lifecycle as plugin consumers. */
UCLASS()
class AEntityRelationsTestActor : public AActor
{
	GENERATED_BODY()

public:
	AEntityRelationsTestActor();

	UPROPERTY()
	TObjectPtr<UEntityIdentityComponent> Identity;

	UPROPERTY()
	TObjectPtr<UEntityRelationStateComponent> RelationState;
};

/** Configurable native policy fixture used to verify resolver ordering and cache behavior. */
UCLASS()
class UEntityRelationsTestPolicy : public UEntityRelationPolicy
{
	GENERATED_BODY()

public:
	void Configure(
		FName InPolicyId,
		int32 InPriority,
		FGameplayTag Domain,
		EEntityRelationDecision InDecision,
		FGameplayTag ClassificationTag = FGameplayTag(),
		FGameplayTag OutcomeTag = FGameplayTag(),
		FGameplayTag ReasonTag = FGameplayTag(),
		bool bInEnabled = true,
		bool bInCacheable = true,
		bool bInStopAfterContribution = false,
		bool bInContributionStops = false);

	int32 GetEvaluationCount() const { return EvaluationCount; }
	bool DidLastEvaluationHaveDirectedState() const { return bLastHadDirectedState; }
	AActor* GetLastSourceActor() const { return LastSourceActor.Get(); }
	AActor* GetLastTargetActor() const { return LastTargetActor.Get(); }

protected:
	virtual FEntityRelationContribution EvaluateRelation_Implementation(const FEntityRelationPolicyContext& Context) const override;

private:
	FEntityRelationContribution TestContribution;
	mutable int32 EvaluationCount = 0;
	mutable bool bLastHadDirectedState = false;
	mutable TWeakObjectPtr<AActor> LastSourceActor;
	mutable TWeakObjectPtr<AActor> LastTargetActor;
};
