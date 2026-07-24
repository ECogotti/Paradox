#pragma once

#include "Components/ActorComponent.h"
#include "Types/EntityRelationTypes.h"
#include "EntityIdentityComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FEntityIdentityChangedEvent,
	FEntityRelationId, EntityId,
	int64, NewRevision);

/** Owns the logical identity and query-relevant tags of one runtime Actor. */
UCLASS(ClassGroup = (EntityRelations), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class ENTITYRELATIONS_API UEntityIdentityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEntityIdentityComponent();

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Identity|Events")
	FEntityIdentityChangedEvent OnEntityIdentityChanged;

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	FEntityRelationId GetEntityId() const;

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	EEntityRelationIdMode GetEntityIdMode() const { return EntityIdMode; }

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	FName GetDebugName() const { return DebugName; }

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	FGameplayTagContainer GetIdentityTags() const { return IdentityTags; }

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	FGameplayTagContainer GetAffiliationTags() const { return AffiliationTags; }

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	int64 GetIdentityRevision() const { return IdentityRevision; }

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	FEntityRelationRegistrationResult GetLastRegistrationResult() const { return LastRegistrationResult; }

	/** Switches to an explicit stable ID. Rejected while this component owns a live registry entry. */
	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	bool SetExplicitEntityId(FEntityRelationId NewEntityId);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	bool SetDebugName(FName NewDebugName);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	bool SetIdentityTags(const FGameplayTagContainer& NewTags);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	bool AddIdentityTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	bool RemoveIdentityTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	bool SetAffiliationTags(const FGameplayTagContainer& NewTags);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	bool AddAffiliationTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	bool RemoveAffiliationTag(FGameplayTag Tag);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void PublishIdentityChange();
	bool IsRegistered() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Entity Relations|Identity", meta = (AllowPrivateAccess = "true"))
	EEntityRelationIdMode EntityIdMode = EEntityRelationIdMode::RuntimeGenerated;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, SaveGame, Category = "Entity Relations|Identity", meta = (AllowPrivateAccess = "true", EditCondition = "EntityIdMode == EEntityRelationIdMode::Explicit", EditConditionHides))
	FEntityRelationId ExplicitEntityId;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Entity Relations|Identity", meta = (AllowPrivateAccess = "true"))
	FEntityRelationId RuntimeEntityId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Entity Relations|Identity", meta = (AllowPrivateAccess = "true"))
	FName DebugName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Entity Relations|Identity", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer IdentityTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Entity Relations|Identity", meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer AffiliationTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Debug", meta = (AllowPrivateAccess = "true"))
	bool bEnableDebug = false;

	UPROPERTY(Transient)
	int64 IdentityRevision = 0;

	UPROPERTY(Transient)
	FEntityRelationRegistrationResult LastRegistrationResult;

	friend class UEntityRelationsWorldSubsystem;
};
