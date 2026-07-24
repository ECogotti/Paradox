#include "Subsystems/EntityRelationsWorldSubsystem.h"

#include "Algo/Sort.h"
#include "Components/EntityIdentityComponent.h"
#include "Components/EntityRelationStateComponent.h"
#include "Data/EntityRelationPolicySet.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EntityRelations.h"
#include "GameFramework/Actor.h"
#include "Policies/EntityRelationPolicy.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Settings/EntityRelationsDeveloperSettings.h"

namespace UE::EntityRelations::Private
{
	uint32 HashTagContainer(const FGameplayTagContainer& Container)
	{
		TArray<FGameplayTag> Tags;
		Container.GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		uint32 Hash = 0;
		for (const FGameplayTag& Tag : Tags)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Tag));
		}
		return Hash;
	}

	uint32 HashNumericContext(const TMap<FGameplayTag, float>& Values)
	{
		TArray<FGameplayTag> Keys;
		Values.GetKeys(Keys);
		Keys.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		uint32 Hash = 0;
		for (const FGameplayTag& Key : Keys)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Key));
			Hash = HashCombineFast(Hash, GetTypeHash(Values.FindChecked(Key)));
		}
		return Hash;
	}

	bool IsContextValid(const FEntityRelationQueryContext& Context)
	{
		if (!Context.Domain.IsValid())
		{
			return false;
		}
		for (const TPair<FGameplayTag, float>& Pair : Context.NumericContext)
		{
			if (!Pair.Key.IsValid() || !FMath::IsFinite(Pair.Value))
			{
				return false;
			}
		}
		return true;
	}
}

struct FEntityRelationsRegistryEntry
{
	TWeakObjectPtr<UEntityIdentityComponent> Identity;
	TWeakObjectPtr<UEntityRelationStateComponent> StateComponent;
};

struct FEntityRelationsResolvedPolicy
{
	TWeakObjectPtr<UEntityRelationPolicy> Policy;
	int32 SerializedIndex = INDEX_NONE;
};

struct FEntityRelationsCacheKey
{
	FEntityRelationId SourceId;
	FEntityRelationId TargetId;
	FGameplayTag Domain;
	uint32 ContextTagsHash = 0;
	uint32 NumericContextHash = 0;
	int64 SourceRevision = 0;
	int64 TargetRevision = 0;
	int64 PairRevision = 0;
	int64 PolicySetRevision = 0;

	friend bool operator==(const FEntityRelationsCacheKey& Left, const FEntityRelationsCacheKey& Right)
	{
		return Left.SourceId == Right.SourceId
			&& Left.TargetId == Right.TargetId
			&& Left.Domain == Right.Domain
			&& Left.ContextTagsHash == Right.ContextTagsHash
			&& Left.NumericContextHash == Right.NumericContextHash
			&& Left.SourceRevision == Right.SourceRevision
			&& Left.TargetRevision == Right.TargetRevision
			&& Left.PairRevision == Right.PairRevision
			&& Left.PolicySetRevision == Right.PolicySetRevision;
	}

	friend uint32 GetTypeHash(const FEntityRelationsCacheKey& Key)
	{
		uint32 Hash = HashCombineFast(GetTypeHash(Key.SourceId), GetTypeHash(Key.TargetId));
		Hash = HashCombineFast(Hash, GetTypeHash(Key.Domain));
		Hash = HashCombineFast(Hash, Key.ContextTagsHash);
		Hash = HashCombineFast(Hash, Key.NumericContextHash);
		Hash = HashCombineFast(Hash, GetTypeHash(Key.SourceRevision));
		Hash = HashCombineFast(Hash, GetTypeHash(Key.TargetRevision));
		Hash = HashCombineFast(Hash, GetTypeHash(Key.PairRevision));
		return HashCombineFast(Hash, GetTypeHash(Key.PolicySetRevision));
	}
};

struct FEntityRelationsCacheEntry
{
	FEntityRelationResult Result;
	uint64 LastAccessSequence = 0;
};

struct FEntityRelationsSubsystemRuntime
{
	TMap<FEntityRelationId, FEntityRelationsRegistryEntry> Registry;
	TArray<FEntityRelationsResolvedPolicy> ResolvedPolicies;
	TMap<FEntityRelationsCacheKey, FEntityRelationsCacheEntry> Cache;
	uint64 CacheAccessSequence = 0;
	int64 PolicySetRevision = 0;
	int64 QueryCount = 0;
	int64 BatchCount = 0;
	int64 CacheHits = 0;
	int64 CacheMisses = 0;
	int64 PoliciesEvaluated = 0;
	bool bShuttingDown = false;
	bool bMissingPolicyWarningIssued = false;
	FEntityRelationsRegistryNativeEvent EntityRegisteredNative;
	FEntityRelationsRegistryNativeEvent EntityUnregisteredNative;
	FEntityRelationsInvalidationNativeEvent EntityIdentityChangedNative;
	FEntityRelationsInvalidationNativeEvent DirectedRelationStateChangedNative;
	FEntityRelationsInvalidationNativeEvent RelationsInvalidatedForEntityNative;
	FEntityRelationsInvalidationNativeEvent RelationsInvalidatedForPairNative;
	FEntityRelationsPolicySetChangedNativeEvent PolicySetChangedNative;
};

UEntityRelationsWorldSubsystem::UEntityRelationsWorldSubsystem()
	: Runtime(new FEntityRelationsSubsystemRuntime())
{
}

UEntityRelationsWorldSubsystem::~UEntityRelationsWorldSubsystem()
{
	delete Runtime;
	Runtime = nullptr;
}

void UEntityRelationsWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	delete Runtime;
	Runtime = new FEntityRelationsSubsystemRuntime();
	const UEntityRelationsDeveloperSettings* Settings = GetDefault<UEntityRelationsDeveloperSettings>();
	ConfiguredDefaultPolicySet = Settings ? Settings->DefaultPolicySet.LoadSynchronous() : nullptr;
	ActivatePolicySet(ConfiguredDefaultPolicySet);
}

void UEntityRelationsWorldSubsystem::Deinitialize()
{
	Runtime->bShuttingDown = true;
	Runtime->Registry.Reset();
	Runtime->ResolvedPolicies.Reset();
	Runtime->Cache.Reset();
	Runtime->EntityRegisteredNative.Clear();
	Runtime->EntityUnregisteredNative.Clear();
	Runtime->EntityIdentityChangedNative.Clear();
	Runtime->DirectedRelationStateChangedNative.Clear();
	Runtime->RelationsInvalidatedForEntityNative.Clear();
	Runtime->RelationsInvalidatedForPairNative.Clear();
	Runtime->PolicySetChangedNative.Clear();
	OnEntityRegistered.Clear();
	OnEntityUnregistered.Clear();
	OnEntityIdentityChanged.Clear();
	OnDirectedRelationStateChanged.Clear();
	OnRelationsInvalidatedForEntity.Clear();
	OnRelationsInvalidatedForPair.Clear();
	OnPolicySetChanged.Clear();
	PolicySetOverride = nullptr;
	ConfiguredDefaultPolicySet = nullptr;
	ActivePolicySet = nullptr;
	Super::Deinitialize();
}

bool UEntityRelationsWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

FEntityRelationRegistrationResult UEntityRelationsWorldSubsystem::RegisterIdentity(UEntityIdentityComponent* Identity)
{
	FEntityRelationRegistrationResult Result;
	Result.EntityId = Identity ? Identity->GetEntityId() : FEntityRelationId();
	if (!IsInGameThread())
	{
		Result.Status = EEntityRelationRegistrationStatus::WrongThread;
		Result.Message = TEXT("Entity registration may only occur on the Game Thread.");
		return Result;
	}
	if (Runtime->bShuttingDown)
	{
		Result.Status = EEntityRelationRegistrationStatus::ShuttingDown;
		Result.Message = TEXT("The Entity Relations subsystem is shutting down.");
		return Result;
	}
	if (!Identity)
	{
		Result.Status = EEntityRelationRegistrationStatus::InvalidComponent;
		Result.Message = TEXT("Identity component is null.");
		return Result;
	}
	if (!Result.EntityId.IsValid())
	{
		Result.Status = EEntityRelationRegistrationStatus::InvalidId;
		Result.Message = TEXT("Entity ID is invalid.");
		return Result;
	}
	if (FEntityRelationsRegistryEntry* Existing = Runtime->Registry.Find(Result.EntityId))
	{
		if (Existing->Identity.Get() == Identity)
		{
			Result.Status = EEntityRelationRegistrationStatus::AlreadyRegistered;
			return Result;
		}
		if (Existing->Identity.IsValid())
		{
			Result.Status = EEntityRelationRegistrationStatus::DuplicateId;
			Result.Message = TEXT("A live entity already owns this logical ID.");
			ENTITYRELATIONS_LOG_ERROR(
				"Duplicate EntityId %s on %s; already owned by %s in World %s.",
				*Result.EntityId.ToString(),
				*GetNameSafe(Identity->GetOwner()),
				*GetNameSafe(Existing->Identity->GetOwner()),
				*GetNameSafe(GetWorld()));
			return Result;
		}
		Runtime->Registry.Remove(Result.EntityId);
		PurgeCacheForEntity(Result.EntityId);
	}

	FEntityRelationsRegistryEntry& Entry = Runtime->Registry.Add(Result.EntityId);
	Entry.Identity = Identity;
	Entry.StateComponent = Identity->GetOwner() ? Identity->GetOwner()->FindComponentByClass<UEntityRelationStateComponent>() : nullptr;
	Result.Status = EEntityRelationRegistrationStatus::Registered;

	FEntityRelationRegistryEvent Event;
	Event.EntityId = Result.EntityId;
	Event.IdentityComponent = Identity;
	OnEntityRegistered.Broadcast(Event);
	Runtime->EntityRegisteredNative.Broadcast(Event);
	return Result;
}

void UEntityRelationsWorldSubsystem::UnregisterIdentity(UEntityIdentityComponent* Identity)
{
	if (!Identity || !Runtime || Runtime->bShuttingDown)
	{
		return;
	}
	const FEntityRelationId EntityId = Identity->GetEntityId();
	FEntityRelationsRegistryEntry* Existing = Runtime->Registry.Find(EntityId);
	if (!Existing || Existing->Identity.Get() != Identity)
	{
		return;
	}
	Runtime->Registry.Remove(EntityId);
	PurgeCacheForEntity(EntityId);

	FEntityRelationRegistryEvent RegistryEvent;
	RegistryEvent.EntityId = EntityId;
	RegistryEvent.IdentityComponent = Identity;
	OnEntityUnregistered.Broadcast(RegistryEvent);
	Runtime->EntityUnregisteredNative.Broadcast(RegistryEvent);

	FEntityRelationInvalidationEvent Invalidation;
	Invalidation.SourceId = EntityId;
	Invalidation.bAffectsAllRelationsForEntity = true;
	OnRelationsInvalidatedForEntity.Broadcast(Invalidation);
	Runtime->RelationsInvalidatedForEntityNative.Broadcast(Invalidation);
}

void UEntityRelationsWorldSubsystem::RefreshStateComponent(UEntityRelationStateComponent* StateComponent)
{
	if (!StateComponent || Runtime->bShuttingDown)
	{
		return;
	}
	AActor* Owner = StateComponent->GetOwner();
	UEntityIdentityComponent* Identity = Owner ? Owner->FindComponentByClass<UEntityIdentityComponent>() : nullptr;
	if (!Identity)
	{
		return;
	}
	if (FEntityRelationsRegistryEntry* Entry = Runtime->Registry.Find(Identity->GetEntityId()); Entry && Entry->Identity.Get() == Identity)
	{
		if (Entry->StateComponent.Get() != StateComponent)
		{
			Entry->StateComponent = StateComponent;
			PurgeCacheForEntity(Identity->GetEntityId());
		}
	}
}

void UEntityRelationsWorldSubsystem::UnregisterStateComponent(UEntityRelationStateComponent* StateComponent)
{
	if (!StateComponent || !Runtime)
	{
		return;
	}
	AActor* Owner = StateComponent->GetOwner();
	UEntityIdentityComponent* Identity = Owner ? Owner->FindComponentByClass<UEntityIdentityComponent>() : nullptr;
	if (!Identity)
	{
		return;
	}
	if (FEntityRelationsRegistryEntry* Entry = Runtime->Registry.Find(Identity->GetEntityId()); Entry && Entry->StateComponent.Get() == StateComponent)
	{
		Entry->StateComponent.Reset();
		PurgeCacheForEntity(Identity->GetEntityId());
		FEntityRelationInvalidationEvent Event;
		Event.SourceId = Identity->GetEntityId();
		Event.bAffectsAllRelationsForEntity = true;
		OnRelationsInvalidatedForEntity.Broadcast(Event);
		Runtime->RelationsInvalidatedForEntityNative.Broadcast(Event);
	}
}

void UEntityRelationsWorldSubsystem::NotifyIdentityChanged(UEntityIdentityComponent* Identity)
{
	if (!Identity || !IsEntityRegistered(Identity))
	{
		return;
	}
	FEntityRelationInvalidationEvent Event;
	Event.SourceId = Identity->GetEntityId();
	Event.bAffectsAllRelationsForEntity = true;
	OnEntityIdentityChanged.Broadcast(Event);
	Runtime->EntityIdentityChangedNative.Broadcast(Event);
	OnRelationsInvalidatedForEntity.Broadcast(Event);
	Runtime->RelationsInvalidatedForEntityNative.Broadcast(Event);
}

void UEntityRelationsWorldSubsystem::NotifyDirectedStateChanged(
	UEntityRelationStateComponent* StateComponent,
	FEntityRelationId TargetId,
	bool bEntryRemoved)
{
	if (!StateComponent || !StateComponent->GetOwner())
	{
		return;
	}
	UEntityIdentityComponent* Identity = StateComponent->GetOwner()->FindComponentByClass<UEntityIdentityComponent>();
	if (!Identity || !IsEntityRegistered(Identity))
	{
		return;
	}
	if (bEntryRemoved)
	{
		PurgeCacheForPair(Identity->GetEntityId(), TargetId);
	}
	FEntityRelationInvalidationEvent Event;
	Event.SourceId = Identity->GetEntityId();
	Event.TargetId = TargetId;
	OnDirectedRelationStateChanged.Broadcast(Event);
	Runtime->DirectedRelationStateChangedNative.Broadcast(Event);
	OnRelationsInvalidatedForPair.Broadcast(Event);
	Runtime->RelationsInvalidatedForPairNative.Broadcast(Event);
}

bool UEntityRelationsWorldSubsystem::IsEntityRegistered(const UEntityIdentityComponent* Identity) const
{
	if (!Identity || !Runtime)
	{
		return false;
	}
	const FEntityRelationsRegistryEntry* Entry = Runtime->Registry.Find(Identity->GetEntityId());
	return Entry && Entry->Identity.Get() == Identity;
}

UEntityIdentityComponent* UEntityRelationsWorldSubsystem::ResolveIdentity(
	FEntityRelationId EntityId,
	EEntityRelationQueryStatus MissingStatus,
	FEntityRelationResult& OutFailure)
{
	FEntityRelationsRegistryEntry* Entry = Runtime->Registry.Find(EntityId);
	if (!Entry || !Entry->Identity.IsValid())
	{
		if (Entry)
		{
			Runtime->Registry.Remove(EntityId);
			PurgeCacheForEntity(EntityId);
		}
		OutFailure.Status = MissingStatus;
		return nullptr;
	}
	return Entry->Identity.Get();
}

UEntityIdentityComponent* UEntityRelationsWorldSubsystem::FindRegisteredEntity(FEntityRelationId EntityId)
{
	FEntityRelationResult Failure;
	return EntityId.IsValid() ? ResolveIdentity(EntityId, EEntityRelationQueryStatus::SourceNotRegistered, Failure) : nullptr;
}

void UEntityRelationsWorldSubsystem::ActivatePolicySet(UEntityRelationPolicySet* PolicySet)
{
	ActivePolicySet = nullptr;
	Runtime->ResolvedPolicies.Reset();
	if (PolicySet)
	{
		const FEntityRelationValidationResult Validation = PolicySet->ValidatePolicySet();
		if (Validation.IsValid())
		{
			ActivePolicySet = PolicySet;
			const TArray<TObjectPtr<UEntityRelationPolicy>>& Policies = PolicySet->GetPolicies();
			for (int32 Index = 0; Index < Policies.Num(); ++Index)
			{
				if (Policies[Index])
				{
					FEntityRelationsResolvedPolicy& Resolved = Runtime->ResolvedPolicies.AddDefaulted_GetRef();
					Resolved.Policy = Policies[Index];
					Resolved.SerializedIndex = Index;
				}
			}
			Runtime->ResolvedPolicies.Sort([](const FEntityRelationsResolvedPolicy& Left, const FEntityRelationsResolvedPolicy& Right)
			{
				const UEntityRelationPolicy* LeftPolicy = Left.Policy.Get();
				const UEntityRelationPolicy* RightPolicy = Right.Policy.Get();
				if (!LeftPolicy || !RightPolicy || LeftPolicy->GetPriority() == RightPolicy->GetPriority())
				{
					return Left.SerializedIndex < Right.SerializedIndex;
				}
				return LeftPolicy->GetPriority() > RightPolicy->GetPriority();
			});
		}
		else
		{
			ENTITYRELATIONS_LOG_ERROR("Policy Set %s is invalid and was not activated in World %s.", *GetNameSafe(PolicySet), *GetNameSafe(GetWorld()));
		}
	}
	++Runtime->PolicySetRevision;
	Runtime->bMissingPolicyWarningIssued = false;
	ClearCache();
	OnPolicySetChanged.Broadcast(ActivePolicySet);
	Runtime->PolicySetChangedNative.Broadcast(ActivePolicySet);
}

bool UEntityRelationsWorldSubsystem::SetPolicySetOverride(UEntityRelationPolicySet* PolicySet)
{
	if (!PolicySet || !PolicySet->ValidatePolicySet().IsValid())
	{
		return false;
	}
	PolicySetOverride = PolicySet;
	ActivatePolicySet(PolicySetOverride);
	return ActivePolicySet == PolicySet;
}

void UEntityRelationsWorldSubsystem::ClearPolicySetOverride()
{
	PolicySetOverride = nullptr;
	ActivatePolicySet(ConfiguredDefaultPolicySet);
}

FEntityRelationResult UEntityRelationsWorldSubsystem::EvaluateRelationById(
	FEntityRelationId SourceId,
	FEntityRelationId TargetId,
	const FEntityRelationQueryContext& Context)
{
	FEntityRelationQuery Query;
	Query.SourceId = SourceId;
	Query.TargetId = TargetId;
	Query.Context = Context;
	return EvaluateRelation(Query);
}

FEntityRelationResult UEntityRelationsWorldSubsystem::EvaluateRelationByComponent(
	UEntityIdentityComponent* Source,
	UEntityIdentityComponent* Target,
	const FEntityRelationQueryContext& Context)
{
	if (!Source)
	{
		FEntityRelationResult Result;
		Result.Status = EEntityRelationQueryStatus::InvalidSource;
		return Result;
	}
	if (!Target)
	{
		FEntityRelationResult Result;
		Result.Status = EEntityRelationQueryStatus::InvalidTarget;
		return Result;
	}
	return EvaluateRelationById(Source->GetEntityId(), Target->GetEntityId(), Context);
}

FEntityRelationResult UEntityRelationsWorldSubsystem::EvaluateRelationByActor(
	AActor* Source,
	AActor* Target,
	const FEntityRelationQueryContext& Context)
{
	if (!Source)
	{
		FEntityRelationResult Result;
		Result.Status = EEntityRelationQueryStatus::InvalidSource;
		return Result;
	}
	if (!Target)
	{
		FEntityRelationResult Result;
		Result.Status = EEntityRelationQueryStatus::InvalidTarget;
		return Result;
	}
	return EvaluateRelationByComponent(
		Source->FindComponentByClass<UEntityIdentityComponent>(),
		Target->FindComponentByClass<UEntityIdentityComponent>(),
		Context);
}

TArray<FEntityRelationResult> UEntityRelationsWorldSubsystem::EvaluateRelationsFromSource(
	FEntityRelationId SourceId,
	const TArray<FEntityRelationId>& TargetIds,
	const FEntityRelationQueryContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(EntityRelations_EvaluateBatch);
	++Runtime->BatchCount;
	TArray<FEntityRelationResult> Results;
	Results.Reserve(TargetIds.Num());
	for (const FEntityRelationId& TargetId : TargetIds)
	{
		Results.Add(EvaluateRelationById(SourceId, TargetId, Context));
	}
	return Results;
}

FEntityRelationResult UEntityRelationsWorldSubsystem::EvaluateRelation(const FEntityRelationQuery& Query)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(EntityRelations_EvaluateRelation);
	++Runtime->QueryCount;
	FEntityRelationResult Result;
	if (!IsInGameThread())
	{
		Result.Status = EEntityRelationQueryStatus::WrongThread;
		return Result;
	}
	if (!Query.SourceId.IsValid())
	{
		Result.Status = EEntityRelationQueryStatus::InvalidSource;
		return Result;
	}
	if (!Query.TargetId.IsValid())
	{
		Result.Status = EEntityRelationQueryStatus::InvalidTarget;
		return Result;
	}
	if (!UE::EntityRelations::Private::IsContextValid(Query.Context))
	{
		Result.Status = EEntityRelationQueryStatus::InvalidContext;
		return Result;
	}

	UEntityIdentityComponent* Source = ResolveIdentity(Query.SourceId, EEntityRelationQueryStatus::SourceNotRegistered, Result);
	if (!Source)
	{
		return Result;
	}
	UEntityIdentityComponent* Target = ResolveIdentity(Query.TargetId, EEntityRelationQueryStatus::TargetNotRegistered, Result);
	if (!Target)
	{
		return Result;
	}
	if (!ActivePolicySet)
	{
		Result.Status = EEntityRelationQueryStatus::MissingPolicySet;
		if (!Runtime->bMissingPolicyWarningIssued)
		{
			Runtime->bMissingPolicyWarningIssued = true;
			ENTITYRELATIONS_LOG_WARNING("No active Policy Set in World %s; relation queries return MissingPolicySet.", *GetNameSafe(GetWorld()));
		}
		return Result;
	}

	FEntityRelationsRegistryEntry* SourceEntry = Runtime->Registry.Find(Query.SourceId);
	UEntityRelationStateComponent* StateComponent = SourceEntry ? SourceEntry->StateComponent.Get() : nullptr;
	if (!StateComponent && Source->GetOwner())
	{
		StateComponent = Source->GetOwner()->FindComponentByClass<UEntityRelationStateComponent>();
		if (SourceEntry)
		{
			SourceEntry->StateComponent = StateComponent;
		}
	}

	FEntityRelationPolicyContext PolicyContext;
	PolicyContext.Source.EntityId = Source->GetEntityId();
	PolicyContext.Source.DebugName = Source->GetDebugName();
	PolicyContext.Source.IdentityTags = Source->GetIdentityTags();
	PolicyContext.Source.AffiliationTags = Source->GetAffiliationTags();
	PolicyContext.Source.Revision = Source->GetIdentityRevision();
	PolicyContext.Source.Actor = Source->GetOwner();
	PolicyContext.Target.EntityId = Target->GetEntityId();
	PolicyContext.Target.DebugName = Target->GetDebugName();
	PolicyContext.Target.IdentityTags = Target->GetIdentityTags();
	PolicyContext.Target.AffiliationTags = Target->GetAffiliationTags();
	PolicyContext.Target.Revision = Target->GetIdentityRevision();
	PolicyContext.Target.Actor = Target->GetOwner();
	PolicyContext.QueryContext = Query.Context;
	PolicyContext.bHasDirectedState = StateComponent && StateComponent->GetStateForTarget(Query.TargetId, PolicyContext.DirectedState);

	bool bHasApplicablePolicy = false;
	bool bAllApplicablePoliciesCacheable = true;
	for (const FEntityRelationsResolvedPolicy& Resolved : Runtime->ResolvedPolicies)
	{
		const UEntityRelationPolicy* Policy = Resolved.Policy.Get();
		if (Policy && Policy->IsPolicyEnabled() && Policy->SupportsDomain(Query.Context.Domain))
		{
			bHasApplicablePolicy = true;
			bAllApplicablePoliciesCacheable &= Policy->IsCacheable();
		}
	}
	if (!bHasApplicablePolicy)
	{
		Result.Status = EEntityRelationQueryStatus::UnsupportedDomain;
		return Result;
	}

	const UEntityRelationsDeveloperSettings* Settings = GetDefault<UEntityRelationsDeveloperSettings>();
	const bool bCanUseCache = Settings && Settings->bEnableQueryCache && Settings->MaxCacheEntries > 0
		&& Query.Context.bAllowCache && !Query.Context.bRequestExplanation && bAllApplicablePoliciesCacheable;
	FEntityRelationsCacheKey CacheKey;
	CacheKey.SourceId = Query.SourceId;
	CacheKey.TargetId = Query.TargetId;
	CacheKey.Domain = Query.Context.Domain;
	CacheKey.ContextTagsHash = UE::EntityRelations::Private::HashTagContainer(Query.Context.ContextTags);
	CacheKey.NumericContextHash = UE::EntityRelations::Private::HashNumericContext(Query.Context.NumericContext);
	CacheKey.SourceRevision = Source->GetIdentityRevision();
	CacheKey.TargetRevision = Target->GetIdentityRevision();
	CacheKey.PairRevision = StateComponent ? StateComponent->GetRevisionForTarget(Query.TargetId) : 0;
	CacheKey.PolicySetRevision = Runtime->PolicySetRevision;

	if (bCanUseCache)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EntityRelations_CacheLookup);
		if (FEntityRelationsCacheEntry* Cached = Runtime->Cache.Find(CacheKey))
		{
			Cached->LastAccessSequence = ++Runtime->CacheAccessSequence;
			Result = Cached->Result;
			Result.bWasCacheHit = true;
			++Runtime->CacheHits;
			return Result;
		}
		++Runtime->CacheMisses;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(EntityRelations_ResolvePolicies);
	Result.Status = EEntityRelationQueryStatus::Success;
	bool bDecisionChosen = false;
	const bool bIncludeMessages = Query.Context.bRequestExplanation || (Settings && Settings->bEnableVerboseExplanation) || IsEntityRelationsGlobalDebugEnabled();
	for (const FEntityRelationsResolvedPolicy& Resolved : Runtime->ResolvedPolicies)
	{
		UEntityRelationPolicy* Policy = Resolved.Policy.Get();
		FEntityRelationPolicyTrace Trace;
		Trace.PolicyId = Policy ? Policy->GetPolicyId() : NAME_None;
		Trace.Priority = Policy ? Policy->GetPriority() : 0;
		Trace.SerializedIndex = Resolved.SerializedIndex;
		if (!Policy)
		{
			Trace.Status = EEntityRelationPolicyTraceStatus::InvalidPolicy;
			Trace.Message = TEXT("Policy object is no longer valid.");
			if (Query.Context.bRequestExplanation)
			{
				Result.Explanation.PolicyTrace.Add(Trace);
			}
			Result.Status = EEntityRelationQueryStatus::EvaluationFailed;
			return Result;
		}
		if (!Policy->IsPolicyEnabled())
		{
			Trace.Status = EEntityRelationPolicyTraceStatus::SkippedDisabled;
			if (Query.Context.bRequestExplanation)
			{
				Result.Explanation.PolicyTrace.Add(Trace);
			}
			continue;
		}
		if (!Policy->SupportsDomain(Query.Context.Domain))
		{
			Trace.Status = EEntityRelationPolicyTraceStatus::SkippedUnsupportedDomain;
			if (Query.Context.bRequestExplanation)
			{
				Result.Explanation.PolicyTrace.Add(Trace);
			}
			continue;
		}

		FString Error;
		FEntityRelationContribution Contribution;
		++Runtime->PoliciesEvaluated;
		if (!Policy->EvaluatePolicy(PolicyContext, Contribution, Error))
		{
			Trace.Status = EEntityRelationPolicyTraceStatus::EvaluationFailed;
			Trace.Message = Error;
			if (Query.Context.bRequestExplanation)
			{
				Result.Explanation.PolicyTrace.Add(Trace);
			}
			Result.Status = EEntityRelationQueryStatus::EvaluationFailed;
			ENTITYRELATIONS_LOG_ERROR(
				"Policy %s failed Source=%s Target=%s Domain=%s World=%s: %s",
				*Policy->GetPolicyId().ToString(),
				*Query.SourceId.ToString(),
				*Query.TargetId.ToString(),
				*Query.Context.Domain.ToString(),
				*GetNameSafe(GetWorld()),
				*Error);
			return Result;
		}

		Trace.Status = EEntityRelationPolicyTraceStatus::Evaluated;
		Trace.Contribution = Contribution;
		if (Query.Context.bRequestExplanation)
		{
			Result.Explanation.PolicyTrace.Add(Trace);
		}
		Result.ClassificationTags.AppendTags(Contribution.ClassificationTags);
		Result.OutcomeTags.AppendTags(Contribution.OutcomeTags);
		for (const FGameplayTag& ReasonTag : Contribution.ReasonTags)
		{
			if (Result.Reasons.ContainsByPredicate([ReasonTag](const FEntityRelationReason& ExistingReason)
			{
				return ExistingReason.ReasonTag == ReasonTag;
			}))
			{
				continue;
			}
			FEntityRelationReason& Reason = Result.Reasons.AddDefaulted_GetRef();
			Reason.PolicyId = Policy->GetPolicyId();
			Reason.ReasonTag = ReasonTag;
			if (bIncludeMessages)
			{
				Reason.DebugMessage = Contribution.DebugMessage;
			}
		}
		if (!bDecisionChosen && Contribution.Decision != EEntityRelationDecision::NoOpinion)
		{
			Result.Decision = Contribution.Decision;
			Result.WinningPolicyId = Policy->GetPolicyId();
			bDecisionChosen = true;
		}
		if (Contribution.bStopEvaluation || (Policy->ShouldStopAfterContribution() && Contribution.HasContribution()))
		{
			break;
		}
	}

	if (Query.Context.bRequestExplanation)
	{
		Result.Explanation.SourceRevision = CacheKey.SourceRevision;
		Result.Explanation.TargetRevision = CacheKey.TargetRevision;
		Result.Explanation.PairRevision = CacheKey.PairRevision;
		Result.Explanation.PolicySetRevision = CacheKey.PolicySetRevision;
	}
	if (bCanUseCache)
	{
		const int32 MaxEntries = FMath::Max(0, Settings->MaxCacheEntries);
		if (Runtime->Cache.Num() >= MaxEntries)
		{
			TOptional<FEntityRelationsCacheKey> OldestKey;
			uint64 OldestSequence = MAX_uint64;
			for (const TPair<FEntityRelationsCacheKey, FEntityRelationsCacheEntry>& Pair : Runtime->Cache)
			{
				if (Pair.Value.LastAccessSequence < OldestSequence)
				{
					OldestSequence = Pair.Value.LastAccessSequence;
					OldestKey = Pair.Key;
				}
			}
			if (OldestKey.IsSet())
			{
				Runtime->Cache.Remove(OldestKey.GetValue());
			}
		}
		FEntityRelationsCacheEntry& Entry = Runtime->Cache.Add(CacheKey);
		Entry.Result = Result;
		Entry.Result.bWasCacheHit = false;
		Entry.LastAccessSequence = ++Runtime->CacheAccessSequence;
	}
	DrawQueryDebug(PolicyContext, Result);
	return Result;
}

void UEntityRelationsWorldSubsystem::PurgeCacheForEntity(FEntityRelationId EntityId)
{
	for (auto It = Runtime->Cache.CreateIterator(); It; ++It)
	{
		if (It.Key().SourceId == EntityId || It.Key().TargetId == EntityId)
		{
			It.RemoveCurrent();
		}
	}
}

void UEntityRelationsWorldSubsystem::PurgeCacheForPair(FEntityRelationId SourceId, FEntityRelationId TargetId)
{
	for (auto It = Runtime->Cache.CreateIterator(); It; ++It)
	{
		if (It.Key().SourceId == SourceId && It.Key().TargetId == TargetId)
		{
			It.RemoveCurrent();
		}
	}
}

void UEntityRelationsWorldSubsystem::ClearCache()
{
	if (Runtime)
	{
		Runtime->Cache.Reset();
	}
}

FEntityRelationsRuntimeStats UEntityRelationsWorldSubsystem::GetRuntimeStats() const
{
	FEntityRelationsRuntimeStats Stats;
	Stats.QueryCount = Runtime->QueryCount;
	Stats.BatchCount = Runtime->BatchCount;
	Stats.CacheHits = Runtime->CacheHits;
	Stats.CacheMisses = Runtime->CacheMisses;
	Stats.PoliciesEvaluated = Runtime->PoliciesEvaluated;
	Stats.RegisteredEntities = Runtime->Registry.Num();
	Stats.CacheEntries = Runtime->Cache.Num();
	for (const TPair<FEntityRelationId, FEntityRelationsRegistryEntry>& Pair : Runtime->Registry)
	{
		if (const UEntityRelationStateComponent* State = Pair.Value.StateComponent.Get())
		{
			Stats.DirectedStateEntries += State->GetDirectedStateEntryCount();
		}
	}
	return Stats;
}

void UEntityRelationsWorldSubsystem::DrawQueryDebug(
	const FEntityRelationPolicyContext& PolicyContext,
	const FEntityRelationResult& Result) const
{
	const UEntityRelationsDeveloperSettings* Settings = GetDefault<UEntityRelationsDeveloperSettings>();
	if (!PolicyContext.QueryContext.bRequestExplanation || !Settings || !Settings->bEnableDebugDraw || !IsEntityRelationsGlobalDebugEnabled())
	{
		return;
	}
	const AActor* SourceActor = PolicyContext.Source.Actor;
	const AActor* TargetActor = PolicyContext.Target.Actor;
	const UEntityIdentityComponent* SourceIdentity = SourceActor ? SourceActor->FindComponentByClass<UEntityIdentityComponent>() : nullptr;
	if (!SourceActor || !TargetActor || !SourceIdentity || !SourceIdentity->bEnableDebug)
	{
		return;
	}
	const FColor Color = Result.Decision == EEntityRelationDecision::Allow
		? FColor::Green
		: Result.Decision == EEntityRelationDecision::Deny ? FColor::Red : FColor::Yellow;
	const FVector Start = SourceActor->GetActorLocation() + FVector(0.0, 0.0, 40.0);
	const FVector End = TargetActor->GetActorLocation() + FVector(0.0, 0.0, 40.0);
	DrawDebugDirectionalArrow(GetWorld(), Start, End, 35.0f, Color, false, Settings->DebugDrawDuration, 0, 2.0f);
	DrawDebugString(
		GetWorld(),
		(Start + End) * 0.5f,
		FString::Printf(TEXT("%s | %s"), *PolicyContext.QueryContext.Domain.ToString(), *StaticEnum<EEntityRelationDecision>()->GetNameStringByValue(static_cast<int64>(Result.Decision))),
		nullptr,
		Color,
		Settings->DebugDrawDuration,
		false);
}

void UEntityRelationsWorldSubsystem::DumpRegistryToLog()
{
	ENTITYRELATIONS_LOG_INFO("EntityRelations registry World=%s Entities=%d", *GetNameSafe(GetWorld()), Runtime->Registry.Num());
	for (const TPair<FEntityRelationId, FEntityRelationsRegistryEntry>& Pair : Runtime->Registry)
	{
		ENTITYRELATIONS_LOG_INFO(
			"  Id=%s Actor=%s IdentityValid=%s StateEntries=%d",
			*Pair.Key.ToString(),
			*GetNameSafe(Pair.Value.Identity.IsValid() ? Pair.Value.Identity->GetOwner() : nullptr),
			Pair.Value.Identity.IsValid() ? TEXT("true") : TEXT("false"),
			Pair.Value.StateComponent.IsValid() ? Pair.Value.StateComponent->GetDirectedStateEntryCount() : 0);
	}
}

void UEntityRelationsWorldSubsystem::DumpEntityToLog(FEntityRelationId EntityId)
{
	UEntityIdentityComponent* Identity = FindRegisteredEntity(EntityId);
	if (!Identity)
	{
		ENTITYRELATIONS_LOG_WARNING("Entity %s is not registered in World %s.", *EntityId.ToString(), *GetNameSafe(GetWorld()));
		return;
	}
	const FEntityRelationsRegistryEntry& Entry = Runtime->Registry.FindChecked(EntityId);
	ENTITYRELATIONS_LOG_INFO(
		"Entity Id=%s DebugName=%s Actor=%s IdentityRevision=%lld IdentityTags=%s AffiliationTags=%s DirectedEntries=%d",
		*EntityId.ToString(),
		*Identity->GetDebugName().ToString(),
		*GetNameSafe(Identity->GetOwner()),
		Identity->GetIdentityRevision(),
		*Identity->GetIdentityTags().ToStringSimple(),
		*Identity->GetAffiliationTags().ToStringSimple(),
		Entry.StateComponent.IsValid() ? Entry.StateComponent->GetDirectedStateEntryCount() : 0);
}

void UEntityRelationsWorldSubsystem::ExplainRelationToLog(
	FEntityRelationId SourceId,
	FEntityRelationId TargetId,
	FGameplayTag Domain)
{
	FEntityRelationQueryContext Context;
	Context.Domain = Domain;
	Context.bAllowCache = false;
	Context.bRequestExplanation = true;
	const FEntityRelationResult Result = EvaluateRelationById(SourceId, TargetId, Context);
	ENTITYRELATIONS_LOG_INFO(
		"Explain Source=%s Target=%s Domain=%s Status=%s Decision=%s Winner=%s CacheHit=%s Revisions=(%lld,%lld,%lld,%lld)",
		*SourceId.ToString(),
		*TargetId.ToString(),
		*Domain.ToString(),
		*StaticEnum<EEntityRelationQueryStatus>()->GetNameStringByValue(static_cast<int64>(Result.Status)),
		*StaticEnum<EEntityRelationDecision>()->GetNameStringByValue(static_cast<int64>(Result.Decision)),
		*Result.WinningPolicyId.ToString(),
		Result.bWasCacheHit ? TEXT("true") : TEXT("false"),
		Result.Explanation.SourceRevision,
		Result.Explanation.TargetRevision,
		Result.Explanation.PairRevision,
		Result.Explanation.PolicySetRevision);
	for (const FEntityRelationPolicyTrace& Trace : Result.Explanation.PolicyTrace)
	{
		ENTITYRELATIONS_LOG_INFO(
			"  Policy=%s Priority=%d Index=%d Status=%s Decision=%s Classifications=%s Outcomes=%s Message=%s",
			*Trace.PolicyId.ToString(),
			Trace.Priority,
			Trace.SerializedIndex,
			*StaticEnum<EEntityRelationPolicyTraceStatus>()->GetNameStringByValue(static_cast<int64>(Trace.Status)),
			*StaticEnum<EEntityRelationDecision>()->GetNameStringByValue(static_cast<int64>(Trace.Contribution.Decision)),
			*Trace.Contribution.ClassificationTags.ToStringSimple(),
			*Trace.Contribution.OutcomeTags.ToStringSimple(),
			*Trace.Message);
	}
}

void UEntityRelationsWorldSubsystem::DumpCacheStatsToLog() const
{
	const FEntityRelationsRuntimeStats Stats = GetRuntimeStats();
	ENTITYRELATIONS_LOG_INFO(
		"EntityRelations cache World=%s Entries=%d Hits=%lld Misses=%lld Queries=%lld Batches=%lld Policies=%lld",
		*GetNameSafe(GetWorld()),
		Stats.CacheEntries,
		Stats.CacheHits,
		Stats.CacheMisses,
		Stats.QueryCount,
		Stats.BatchCount,
		Stats.PoliciesEvaluated);
}

FEntityRelationsRegistryNativeEvent& UEntityRelationsWorldSubsystem::OnEntityRegisteredNative()
{
	return Runtime->EntityRegisteredNative;
}

FEntityRelationsRegistryNativeEvent& UEntityRelationsWorldSubsystem::OnEntityUnregisteredNative()
{
	return Runtime->EntityUnregisteredNative;
}

FEntityRelationsInvalidationNativeEvent& UEntityRelationsWorldSubsystem::OnEntityIdentityChangedNative()
{
	return Runtime->EntityIdentityChangedNative;
}

FEntityRelationsInvalidationNativeEvent& UEntityRelationsWorldSubsystem::OnDirectedRelationStateChangedNative()
{
	return Runtime->DirectedRelationStateChangedNative;
}

FEntityRelationsInvalidationNativeEvent& UEntityRelationsWorldSubsystem::OnRelationsInvalidatedForEntityNative()
{
	return Runtime->RelationsInvalidatedForEntityNative;
}

FEntityRelationsInvalidationNativeEvent& UEntityRelationsWorldSubsystem::OnRelationsInvalidatedForPairNative()
{
	return Runtime->RelationsInvalidatedForPairNative;
}

FEntityRelationsPolicySetChangedNativeEvent& UEntityRelationsWorldSubsystem::OnPolicySetChangedNative()
{
	return Runtime->PolicySetChangedNative;
}

#if WITH_DEV_AUTOMATION_TESTS
void UEntityRelationsWorldSubsystem::InjectStaleRegistryEntryForTests(FEntityRelationId EntityId)
{
	if (EntityId.IsValid())
	{
		Runtime->Registry.Add(EntityId);
	}
}
#endif
