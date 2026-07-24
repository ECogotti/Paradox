#pragma once

#include "Components/ActorComponent.h"
#include "Types/EntityRelationTypes.h"
#include "EntityRelationStateComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FDirectedRelationStateChangedEvent,
	const FEntityRelationStateMutationResult&, Result);

/** Optional Source-owned sparse state for explicit directional relationships. */
UCLASS(ClassGroup = (EntityRelations), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class ENTITYRELATIONS_API UEntityRelationStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEntityRelationStateComponent();

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Directed State|Events")
	FDirectedRelationStateChangedEvent OnDirectedRelationStateChanged;

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Directed State")
	bool HasStateForTarget(FEntityRelationId TargetId) const;

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Directed State")
	bool GetStateForTarget(FEntityRelationId TargetId, FEntityDirectedRelationState& OutState) const;

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Directed State")
	FEntityRelationStateMutationResult SetStateTagsForTarget(FEntityRelationId TargetId, const FGameplayTagContainer& NewTags);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Directed State")
	FEntityRelationStateMutationResult AddStateTagForTarget(FEntityRelationId TargetId, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Directed State")
	FEntityRelationStateMutationResult RemoveStateTagForTarget(FEntityRelationId TargetId, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Directed State")
	FEntityRelationStateMutationResult SetNumericValueForTarget(FEntityRelationId TargetId, FGameplayTag ValueTag, float Value);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Directed State")
	FEntityRelationStateMutationResult RemoveNumericValueForTarget(FEntityRelationId TargetId, FGameplayTag ValueTag);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Directed State")
	FEntityRelationStateMutationResult ClearStateForTarget(FEntityRelationId TargetId);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Directed State")
	FEntityRelationStateMutationResult ClearAllDirectedState();

	int64 GetRevisionForTarget(FEntityRelationId TargetId) const;
	int32 GetDirectedStateEntryCount() const { return DirectedStates.Num(); }
	const TMap<FEntityRelationId, FEntityDirectedRelationState>& GetDirectedStates() const { return DirectedStates; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FEntityRelationStateMutationResult MakeFailure(EEntityRelationStateMutationStatus Status, FEntityRelationId TargetId, FString Message) const;
	bool ResolveMutationAuthority(FEntityRelationId TargetId, class UEntityIdentityComponent*& OutIdentity, class UEntityRelationsWorldSubsystem*& OutSubsystem, FEntityRelationStateMutationResult& OutFailure) const;
	FEntityRelationStateMutationResult CommitChange(FEntityRelationId TargetId, bool bEntryRemoved);

	UPROPERTY(EditInstanceOnly, SaveGame, Category = "Entity Relations|Directed State")
	TMap<FEntityRelationId, FEntityDirectedRelationState> DirectedStates;
};
