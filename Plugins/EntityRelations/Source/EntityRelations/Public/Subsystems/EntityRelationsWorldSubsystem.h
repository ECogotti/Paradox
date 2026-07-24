#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Types/EntityRelationTypes.h"
#include "EntityRelationsWorldSubsystem.generated.h"

class AActor;
class UEntityIdentityComponent;
class UEntityRelationPolicySet;
class UEntityRelationStateComponent;
struct FEntityRelationsSubsystemRuntime;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEntityRelationsRegistryEvent, const FEntityRelationRegistryEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEntityRelationsInvalidationEvent, const FEntityRelationInvalidationEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEntityRelationsPolicySetChangedEvent, UEntityRelationPolicySet*, ActivePolicySet);

DECLARE_MULTICAST_DELEGATE_OneParam(FEntityRelationsRegistryNativeEvent, const FEntityRelationRegistryEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FEntityRelationsInvalidationNativeEvent, const FEntityRelationInvalidationEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FEntityRelationsPolicySetChangedNativeEvent, UEntityRelationPolicySet*);

/** Per-world authority for entity registration, deterministic evaluation, cache and diagnostics. */
UCLASS()
class ENTITYRELATIONS_API UEntityRelationsWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UEntityRelationsWorldSubsystem();
	virtual ~UEntityRelationsWorldSubsystem() override;

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Events")
	FEntityRelationsRegistryEvent OnEntityRegistered;

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Events")
	FEntityRelationsRegistryEvent OnEntityUnregistered;

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Events")
	FEntityRelationsInvalidationEvent OnEntityIdentityChanged;

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Events")
	FEntityRelationsInvalidationEvent OnDirectedRelationStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Events")
	FEntityRelationsInvalidationEvent OnRelationsInvalidatedForEntity;

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Events")
	FEntityRelationsInvalidationEvent OnRelationsInvalidatedForPair;

	UPROPERTY(BlueprintAssignable, Category = "Entity Relations|Events")
	FEntityRelationsPolicySetChangedEvent OnPolicySetChanged;

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Query")
	FEntityRelationResult EvaluateRelationById(FEntityRelationId SourceId, FEntityRelationId TargetId, const FEntityRelationQueryContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Query")
	FEntityRelationResult EvaluateRelationByComponent(UEntityIdentityComponent* Source, UEntityIdentityComponent* Target, const FEntityRelationQueryContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Query")
	FEntityRelationResult EvaluateRelationByActor(AActor* Source, AActor* Target, const FEntityRelationQueryContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Query")
	TArray<FEntityRelationResult> EvaluateRelationsFromSource(FEntityRelationId SourceId, const TArray<FEntityRelationId>& TargetIds, const FEntityRelationQueryContext& Context);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Policy Set")
	bool SetPolicySetOverride(UEntityRelationPolicySet* PolicySet);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Policy Set")
	void ClearPolicySetOverride();

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Policy Set")
	UEntityRelationPolicySet* GetActivePolicySet() const { return ActivePolicySet; }

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Identity")
	UEntityIdentityComponent* FindRegisteredEntity(FEntityRelationId EntityId);

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	bool IsEntityRegistered(const UEntityIdentityComponent* Identity) const;

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Debug")
	void ClearCache();

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Debug")
	FEntityRelationsRuntimeStats GetRuntimeStats() const;

	void DumpRegistryToLog();
	void DumpEntityToLog(FEntityRelationId EntityId);
	void ExplainRelationToLog(FEntityRelationId SourceId, FEntityRelationId TargetId, FGameplayTag Domain);
	void DumpCacheStatsToLog() const;

	FEntityRelationsRegistryNativeEvent& OnEntityRegisteredNative();
	FEntityRelationsRegistryNativeEvent& OnEntityUnregisteredNative();
	FEntityRelationsInvalidationNativeEvent& OnEntityIdentityChangedNative();
	FEntityRelationsInvalidationNativeEvent& OnDirectedRelationStateChangedNative();
	FEntityRelationsInvalidationNativeEvent& OnRelationsInvalidatedForEntityNative();
	FEntityRelationsInvalidationNativeEvent& OnRelationsInvalidatedForPairNative();
	FEntityRelationsPolicySetChangedNativeEvent& OnPolicySetChangedNative();

#if WITH_DEV_AUTOMATION_TESTS
	FEntityRelationRegistrationResult RegisterIdentityForTests(UEntityIdentityComponent* Identity) { return RegisterIdentity(Identity); }
	void InjectStaleRegistryEntryForTests(FEntityRelationId EntityId);
#endif

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	FEntityRelationRegistrationResult RegisterIdentity(UEntityIdentityComponent* Identity);
	void UnregisterIdentity(UEntityIdentityComponent* Identity);
	void RefreshStateComponent(UEntityRelationStateComponent* StateComponent);
	void UnregisterStateComponent(UEntityRelationStateComponent* StateComponent);
	void NotifyIdentityChanged(UEntityIdentityComponent* Identity);
	void NotifyDirectedStateChanged(UEntityRelationStateComponent* StateComponent, FEntityRelationId TargetId, bool bEntryRemoved);

	FEntityRelationResult EvaluateRelation(const FEntityRelationQuery& Query);
	UEntityIdentityComponent* ResolveIdentity(FEntityRelationId EntityId, EEntityRelationQueryStatus MissingStatus, FEntityRelationResult& OutFailure);
	void ActivatePolicySet(UEntityRelationPolicySet* PolicySet);
	void PurgeCacheForEntity(FEntityRelationId EntityId);
	void PurgeCacheForPair(FEntityRelationId SourceId, FEntityRelationId TargetId);
	void DrawQueryDebug(const FEntityRelationPolicyContext& PolicyContext, const FEntityRelationResult& Result) const;

	UPROPERTY(Transient)
	TObjectPtr<UEntityRelationPolicySet> ActivePolicySet = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UEntityRelationPolicySet> ConfiguredDefaultPolicySet = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UEntityRelationPolicySet> PolicySetOverride = nullptr;

	FEntityRelationsSubsystemRuntime* Runtime = nullptr;

	friend class UEntityIdentityComponent;
	friend class UEntityRelationStateComponent;
};
