#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/EntityRelationsBlueprintLibrary.h"
#include "Components/EntityIdentityComponent.h"
#include "Components/EntityRelationStateComponent.h"
#include "Data/EntityRelationPolicySet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EntityRelationTags.h"
#include "Settings/EntityRelationsDeveloperSettings.h"
#include "Subsystems/EntityRelationsWorldSubsystem.h"
#include "Tests/EntityRelationsTestTypes.h"
#include "UObject/GarbageCollection.h"

#include <limits>

namespace UE::EntityRelations::Tests
{
	/** RAII-owned transient Game world with symmetrical engine-context teardown. */
	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			FWorldContext* Context = GEngine ? &GEngine->CreateNewWorldContext(EWorldType::Game) : nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(true);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->RemoveFromRoot();
			}
		}

		UWorld* World = nullptr;
	};

	/** Restores mutable developer defaults after cache-boundary tests. */
	struct FScopedSettingsOverride
	{
		FScopedSettingsOverride()
		{
			Settings = GetMutableDefault<UEntityRelationsDeveloperSettings>();
			if (Settings)
			{
				bOriginalCacheEnabled = Settings->bEnableQueryCache;
				OriginalMaxEntries = Settings->MaxCacheEntries;
				Settings->bEnableQueryCache = true;
				Settings->MaxCacheEntries = 2;
			}
		}

		~FScopedSettingsOverride()
		{
			if (Settings)
			{
				Settings->bEnableQueryCache = bOriginalCacheEnabled;
				Settings->MaxCacheEntries = OriginalMaxEntries;
			}
		}

		UEntityRelationsDeveloperSettings* Settings = nullptr;
		bool bOriginalCacheEnabled = true;
		int32 OriginalMaxEntries = 1024;
	};

	AEntityRelationsTestActor* SpawnTestActor(UWorld& World, FName Name, FEntityRelationId ExplicitId = FEntityRelationId())
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.bDeferConstruction = true;
		AEntityRelationsTestActor* Actor = World.SpawnActor<AEntityRelationsTestActor>(
			AEntityRelationsTestActor::StaticClass(), FTransform::Identity, Parameters);
		if (Actor && ExplicitId.IsValid())
		{
			Actor->Identity->SetExplicitEntityId(ExplicitId);
		}
		if (Actor)
		{
			Actor->FinishSpawning(FTransform::Identity);
			if (World.HasBegunPlay() && !Actor->HasActorBegunPlay())
			{
				Actor->DispatchBeginPlay();
			}
		}
		return Actor;
	}

	void StartPlay(UWorld& World)
	{
		World.InitializeActorsForPlay(FURL());
		World.BeginPlay();
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (!It->HasActorBegunPlay())
			{
				It->DispatchBeginPlay();
			}
		}
	}

	UEntityRelationsTestPolicy* MakePolicy(
		UObject& Outer,
		FName PolicyId,
		int32 Priority,
		FGameplayTag Domain,
		EEntityRelationDecision Decision,
		FGameplayTag ClassificationTag = FGameplayTag(),
		FGameplayTag OutcomeTag = FGameplayTag(),
		FGameplayTag ReasonTag = FGameplayTag(),
		bool bEnabled = true,
		bool bCacheable = true,
		bool bStopAfterContribution = false,
		bool bContributionStops = false)
	{
		UEntityRelationsTestPolicy* Policy = NewObject<UEntityRelationsTestPolicy>(&Outer);
		Policy->Configure(
			PolicyId,
			Priority,
			Domain,
			Decision,
			ClassificationTag,
			OutcomeTag,
			ReasonTag,
			bEnabled,
			bCacheable,
			bStopAfterContribution,
			bContributionStops);
		return Policy;
	}

	UEntityRelationPolicySet* MakePolicySet(UObject& Outer, const TArray<UEntityRelationPolicy*>& Policies)
	{
		UEntityRelationPolicySet* PolicySet = NewObject<UEntityRelationPolicySet>(&Outer);
		TArray<TObjectPtr<UEntityRelationPolicy>> StoredPolicies;
		StoredPolicies.Reserve(Policies.Num());
		for (UEntityRelationPolicy* Policy : Policies)
		{
			StoredPolicies.Add(Policy);
		}
		PolicySet->SetPoliciesForTests(MoveTemp(StoredPolicies));
		return PolicySet;
	}

	FEntityRelationQueryContext GeneralContext(bool bAllowCache = true, bool bExplain = false)
	{
		FEntityRelationQueryContext Context;
		Context.Domain = EntityRelationTags::Domain_General;
		Context.bAllowCache = bAllowCache;
		Context.bRequestExplanation = bExplain;
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEntityRelationsRegistryLifecycleTest,
	"EntityRelations.Runtime.Registry.IdentityDuplicateReplacementAndTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEntityRelationsRegistryLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UE::EntityRelations::Tests;
	TWeakObjectPtr<UEntityRelationsWorldSubsystem> TeardownSubsystem;
	{
		FScopedTestWorld Scope(TEXT("EntityRelationsRegistryWorld"));
		if (!Scope.World)
		{
			AddError(TEXT("Transient Game world could not be created."));
			return false;
		}
		const FEntityRelationId SharedId = FEntityRelationId::NewId();
		AEntityRelationsTestActor* First = SpawnTestActor(*Scope.World, TEXT("FirstOwner"), SharedId);
		AEntityRelationsTestActor* Duplicate = SpawnTestActor(*Scope.World, TEXT("DuplicateOwner"), SharedId);
		TestNotNull(TEXT("first fixture exists"), First);
		TestNotNull(TEXT("duplicate fixture exists"), Duplicate);
		AddExpectedError(TEXT("Duplicate EntityId"), EAutomationExpectedErrorFlags::Contains, 1);
		StartPlay(*Scope.World);
		UEntityRelationsWorldSubsystem* Subsystem = Scope.World->GetSubsystem<UEntityRelationsWorldSubsystem>();
		TeardownSubsystem = Subsystem;
		TestNotNull(TEXT("runtime subsystem exists"), Subsystem);
		UEntityIdentityComponent* InvalidIdentity = NewObject<UEntityIdentityComponent>(Scope.World);
		TestEqual(TEXT("invalid logical ID is rejected"), Subsystem->RegisterIdentityForTests(InvalidIdentity).Status, EEntityRelationRegistrationStatus::InvalidId);
		TestEqual(TEXT("first owner registers"), First->Identity->GetLastRegistrationResult().Status, EEntityRelationRegistrationStatus::Registered);
		TestEqual(TEXT("duplicate owner is rejected"), Duplicate->Identity->GetLastRegistrationResult().Status, EEntityRelationRegistrationStatus::DuplicateId);
		TestTrue(TEXT("duplicate does not replace first owner"), Subsystem && Subsystem->FindRegisteredEntity(SharedId) == First->Identity);
		const int64 InitialRevision = First->Identity->GetIdentityRevision();
		TestTrue(TEXT("changed identity value increments revision"), First->Identity->SetDebugName(TEXT("Primary")));
		TestEqual(TEXT("identity revision increments exactly once"), First->Identity->GetIdentityRevision(), InitialRevision + 1);
		TestFalse(TEXT("equal identity value is unchanged"), First->Identity->SetDebugName(TEXT("Primary")));
		TestEqual(TEXT("unchanged identity preserves revision"), First->Identity->GetIdentityRevision(), InitialRevision + 1);

		First->Destroy();
		TestNull(TEXT("EndPlay removes the first registry entry"), Subsystem ? Subsystem->FindRegisteredEntity(SharedId) : nullptr);
		const FEntityRelationRegistrationResult ReplacementResult = Subsystem->RegisterIdentityForTests(Duplicate->Identity);
		TestEqual(TEXT("same ID can be reused after deregistration"), ReplacementResult.Status, EEntityRelationRegistrationStatus::Registered);
		TestTrue(TEXT("replacement owns the registry entry"), Subsystem && Subsystem->FindRegisteredEntity(SharedId) == Duplicate->Identity);
		const FEntityRelationId StaleId = FEntityRelationId::NewId();
		Subsystem->InjectStaleRegistryEntryForTests(StaleId);
		TestNull(TEXT("stale weak registry entry resolves safely"), Subsystem->FindRegisteredEntity(StaleId));
		TestEqual(TEXT("stale weak registry entry is removed"), Subsystem->GetRuntimeStats().RegisteredEntities, 1);
	}
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("world teardown releases its Entity Relations subsystem"), TeardownSubsystem.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEntityRelationsDirectedStateTest,
	"EntityRelations.Runtime.State.DirectionRevisionEventsAndSparseRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEntityRelationsDirectedStateTest::RunTest(const FString& Parameters)
{
	using namespace UE::EntityRelations::Tests;
	FScopedTestWorld Scope(TEXT("EntityRelationsStateWorld"));
	AEntityRelationsTestActor* Source = Scope.World ? SpawnTestActor(*Scope.World, TEXT("StateSource"), FEntityRelationId::NewId()) : nullptr;
	AEntityRelationsTestActor* Target = Scope.World ? SpawnTestActor(*Scope.World, TEXT("StateTarget"), FEntityRelationId::NewId()) : nullptr;
	if (!Source || !Target)
	{
		AddError(TEXT("Directed-state fixtures could not be created."));
		return false;
	}
	StartPlay(*Scope.World);
	UEntityRelationsWorldSubsystem* Subsystem = Scope.World->GetSubsystem<UEntityRelationsWorldSubsystem>();
	int32 PairInvalidations = 0;
	Subsystem->OnRelationsInvalidatedForPairNative().AddLambda([&](const FEntityRelationInvalidationEvent&) { ++PairInvalidations; });

	const FGameplayTag StateTag = EntityRelationTags::Domain_Interaction;
	const FEntityRelationStateMutationResult EmptyNoOp = Source->RelationState->SetStateTagsForTarget(Target->Identity->GetEntityId(), FGameplayTagContainer());
	TestEqual(TEXT("empty tags on an absent pair are unchanged"), EmptyNoOp.Status, EEntityRelationStateMutationStatus::Unchanged);
	TestEqual(TEXT("empty no-op emits no pair invalidation"), PairInvalidations, 0);
	const FEntityRelationStateMutationResult Added = Source->RelationState->AddStateTagForTarget(Target->Identity->GetEntityId(), StateTag);
	TestEqual(TEXT("first state tag changes the sparse map"), Added.Status, EEntityRelationStateMutationStatus::Changed);
	TestEqual(TEXT("changed state emits one pair invalidation"), PairInvalidations, 1);
	const int64 PairRevision = Source->RelationState->GetRevisionForTarget(Target->Identity->GetEntityId());
	TestTrue(TEXT("source owns directed state"), Source->RelationState->HasStateForTarget(Target->Identity->GetEntityId()));
	TestFalse(TEXT("reverse direction remains empty"), Target->RelationState->HasStateForTarget(Source->Identity->GetEntityId()));

	const FEntityRelationStateMutationResult Unchanged = Source->RelationState->AddStateTagForTarget(Target->Identity->GetEntityId(), StateTag);
	TestEqual(TEXT("duplicate state tag is unchanged"), Unchanged.Status, EEntityRelationStateMutationStatus::Unchanged);
	TestEqual(TEXT("unchanged state preserves revision"), Source->RelationState->GetRevisionForTarget(Target->Identity->GetEntityId()), PairRevision);
	TestEqual(TEXT("unchanged state emits no event"), PairInvalidations, 1);
	TestEqual(TEXT("non-finite numeric state is rejected"), Source->RelationState->SetNumericValueForTarget(Target->Identity->GetEntityId(), EntityRelationTags::Domain_Damage, std::numeric_limits<float>::quiet_NaN()).Status, EEntityRelationStateMutationStatus::InvalidValue);

	const FEntityRelationStateMutationResult Removed = Source->RelationState->RemoveStateTagForTarget(Target->Identity->GetEntityId(), StateTag);
	TestEqual(TEXT("removing final value changes state"), Removed.Status, EEntityRelationStateMutationStatus::Changed);
	TestFalse(TEXT("empty sparse entry is removed"), Source->RelationState->HasStateForTarget(Target->Identity->GetEntityId()));
	TestEqual(TEXT("removal emits one additional event"), PairInvalidations, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEntityRelationsResolverTest,
	"EntityRelations.Runtime.Resolver.PriorityTieBreakMetadataStopAndNoOpinion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEntityRelationsResolverTest::RunTest(const FString& Parameters)
{
	using namespace UE::EntityRelations::Tests;
	FScopedTestWorld Scope(TEXT("EntityRelationsResolverWorld"));
	AEntityRelationsTestActor* Source = Scope.World ? SpawnTestActor(*Scope.World, TEXT("ResolverSource"), FEntityRelationId::NewId()) : nullptr;
	AEntityRelationsTestActor* Target = Scope.World ? SpawnTestActor(*Scope.World, TEXT("ResolverTarget"), FEntityRelationId::NewId()) : nullptr;
	if (!Source || !Target)
	{
		AddError(TEXT("Resolver fixtures could not be created."));
		return false;
	}
	StartPlay(*Scope.World);
	UEntityRelationsWorldSubsystem* Subsystem = Scope.World->GetSubsystem<UEntityRelationsWorldSubsystem>();
	AddExpectedError(TEXT("No active Policy Set"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("missing Policy Set never implies Allow"), Subsystem->EvaluateRelationByActor(Source, Target, GeneralContext(false)).Status, EEntityRelationQueryStatus::MissingPolicySet);
	UEntityRelationsTestPolicy* HighAllow = MakePolicy(*Scope.World, TEXT("HighAllow"), 100, EntityRelationTags::Domain_General, EEntityRelationDecision::Allow, EntityRelationTags::Domain_Interaction, FGameplayTag(), EntityRelationTags::Domain_Blocking);
	UEntityRelationsTestPolicy* LaterDeny = MakePolicy(*Scope.World, TEXT("LaterDeny"), 10, EntityRelationTags::Domain_General, EEntityRelationDecision::Deny, FGameplayTag(), EntityRelationTags::Domain_Damage, EntityRelationTags::Domain_Blocking);
	UEntityRelationsTestPolicy* Disabled = MakePolicy(*Scope.World, TEXT("Disabled"), 200, EntityRelationTags::Domain_General, EEntityRelationDecision::Deny, FGameplayTag(), FGameplayTag(), FGameplayTag(), false);
	UEntityRelationPolicySet* PolicySet = MakePolicySet(*Scope.World, { HighAllow, LaterDeny, Disabled });
	TestTrue(TEXT("valid policy set activates"), Subsystem->SetPolicySetOverride(PolicySet));
	FEntityRelationQueryContext Explain = GeneralContext(false, true);
	FEntityRelationResult Result = Subsystem->EvaluateRelationByActor(Source, Target, Explain);
	TestEqual(TEXT("query succeeds"), Result.Status, EEntityRelationQueryStatus::Success);
	TestEqual(TEXT("first non-NoOpinion decision is authoritative"), Result.Decision, EEntityRelationDecision::Allow);
	TestEqual(TEXT("winning policy is the highest-priority decision"), Result.WinningPolicyId, FName(TEXT("HighAllow")));
	TestTrue(TEXT("classification metadata accumulates"), Result.ClassificationTags.HasTagExact(EntityRelationTags::Domain_Interaction));
	TestTrue(TEXT("later outcome metadata accumulates"), Result.OutcomeTags.HasTagExact(EntityRelationTags::Domain_Damage));
	TestEqual(TEXT("duplicate reason tags accumulate only once"), Result.Reasons.Num(), 1);
	TestEqual(TEXT("diagnostic trace includes disabled policy"), Result.Explanation.PolicyTrace.Num(), 3);
	TestTrue(TEXT("policy context borrows the source Actor"), HighAllow->GetLastSourceActor() == Source);
	TestTrue(TEXT("policy context borrows the target Actor"), HighAllow->GetLastTargetActor() == Target);

	UEntityRelationsTestPolicy* TieDeny = MakePolicy(*Scope.World, TEXT("TieDeny"), 50, EntityRelationTags::Domain_General, EEntityRelationDecision::Deny);
	UEntityRelationsTestPolicy* TieAllow = MakePolicy(*Scope.World, TEXT("TieAllow"), 50, EntityRelationTags::Domain_General, EEntityRelationDecision::Allow);
	UEntityRelationPolicySet* TieSet = MakePolicySet(*Scope.World, { TieDeny, TieAllow });
	TestTrue(TEXT("equal priorities remain valid with a warning"), TieSet->ValidatePolicySet().IsValid());
	TestTrue(TEXT("tie-break policy set activates"), Subsystem->SetPolicySetOverride(TieSet));
	Result = Subsystem->EvaluateRelationById(Source->Identity->GetEntityId(), Target->Identity->GetEntityId(), GeneralContext(false));
	TestEqual(TEXT("serialized index breaks equal-priority ties"), Result.Decision, EEntityRelationDecision::Deny);

	UEntityRelationsTestPolicy* StopNoOpinion = MakePolicy(*Scope.World, TEXT("StopNoOpinion"), 100, EntityRelationTags::Domain_General, EEntityRelationDecision::NoOpinion, EntityRelationTags::Domain_TacticalPreview, FGameplayTag(), FGameplayTag(), true, true, false, true);
	UEntityRelationsTestPolicy* NeverReached = MakePolicy(*Scope.World, TEXT("NeverReached"), 0, EntityRelationTags::Domain_General, EEntityRelationDecision::Allow);
	UEntityRelationPolicySet* StopSet = MakePolicySet(*Scope.World, { StopNoOpinion, NeverReached });
	TestTrue(TEXT("stop policy set activates"), Subsystem->SetPolicySetOverride(StopSet));
	Result = Subsystem->EvaluateRelationById(Source->Identity->GetEntityId(), Target->Identity->GetEntityId(), GeneralContext(false));
	TestEqual(TEXT("NoOpinion remains distinct from Allow and Deny"), Result.Decision, EEntityRelationDecision::NoOpinion);
	TestEqual(TEXT("explicit stop prevents later policy evaluation"), NeverReached->GetEvaluationCount(), 0);
	TestTrue(TEXT("metadata before stop is preserved"), Result.ClassificationTags.HasTagExact(EntityRelationTags::Domain_TacticalPreview));

	FEntityRelationQueryContext Unsupported = GeneralContext(false);
	Unsupported.Domain = EntityRelationTags::Domain_AudioPerception;
	TestEqual(TEXT("unsupported domains are explicit"), Subsystem->EvaluateRelationByActor(Source, Target, Unsupported).Status, EEntityRelationQueryStatus::UnsupportedDomain);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEntityRelationsCacheBatchAndWrappersTest,
	"EntityRelations.Runtime.CacheInvalidationLruBatchErrorsAndWrappers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEntityRelationsCacheBatchAndWrappersTest::RunTest(const FString& Parameters)
{
	using namespace UE::EntityRelations::Tests;
	FScopedSettingsOverride SettingsOverride;
	FScopedTestWorld Scope(TEXT("EntityRelationsCacheWorld"));
	AEntityRelationsTestActor* Source = Scope.World ? SpawnTestActor(*Scope.World, TEXT("CacheSource"), FEntityRelationId::NewId()) : nullptr;
	AEntityRelationsTestActor* Target = Scope.World ? SpawnTestActor(*Scope.World, TEXT("CacheTarget"), FEntityRelationId::NewId()) : nullptr;
	if (!Source || !Target)
	{
		AddError(TEXT("Cache fixtures could not be created."));
		return false;
	}
	StartPlay(*Scope.World);
	UEntityRelationsWorldSubsystem* Subsystem = Scope.World->GetSubsystem<UEntityRelationsWorldSubsystem>();
	UEntityRelationsTestPolicy* Policy = MakePolicy(*Scope.World, TEXT("Cacheable"), 0, EntityRelationTags::Domain_General, EEntityRelationDecision::Allow);
	TestTrue(TEXT("cacheable policy set activates"), Subsystem->SetPolicySetOverride(MakePolicySet(*Scope.World, { Policy })));

	FEntityRelationQueryContext Context = GeneralContext();
	FEntityRelationResult First = Subsystem->EvaluateRelationByComponent(Source->Identity, Target->Identity, Context);
	FEntityRelationResult Second = Subsystem->EvaluateRelationByComponent(Source->Identity, Target->Identity, Context);
	TestFalse(TEXT("first cacheable query is a miss"), First.bWasCacheHit);
	TestTrue(TEXT("equal second query is a hit"), Second.bWasCacheHit);
	TestEqual(TEXT("cache hit avoids reevaluation"), Policy->GetEvaluationCount(), 1);

	Source->Identity->SetDebugName(TEXT("RevisionChanged"));
	FEntityRelationResult AfterIdentityChange = Subsystem->EvaluateRelationByActor(Source, Target, Context);
	TestFalse(TEXT("identity revision invalidates the cached relation"), AfterIdentityChange.bWasCacheHit);
	Target->Identity->SetDebugName(TEXT("TargetRevisionChanged"));
	TestFalse(TEXT("target identity revision invalidates the cached relation"), Subsystem->EvaluateRelationByActor(Source, Target, Context).bWasCacheHit);
	Source->RelationState->AddStateTagForTarget(Target->Identity->GetEntityId(), EntityRelationTags::Domain_Blocking);
	FEntityRelationResult AfterStateChange = Subsystem->EvaluateRelationByActor(Source, Target, Context);
	TestFalse(TEXT("pair state revision invalidates the cached relation"), AfterStateChange.bWasCacheHit);
	TestTrue(TEXT("policy receives directed state snapshot"), Policy->DidLastEvaluationHaveDirectedState());
	Source->RelationState->ClearStateForTarget(Target->Identity->GetEntityId());
	TestFalse(TEXT("state removal performs targeted purge"), Subsystem->EvaluateRelationByActor(Source, Target, Context).bWasCacheHit);

	FEntityRelationQueryContext Bypass = Context;
	Bypass.bAllowCache = false;
	TestFalse(TEXT("caller cache bypass is honored"), Subsystem->EvaluateRelationByActor(Source, Target, Bypass).bWasCacheHit);
	FEntityRelationQueryContext Diagnostic = Context;
	Diagnostic.bRequestExplanation = true;
	TestFalse(TEXT("diagnostic query bypasses cache"), Subsystem->EvaluateRelationByActor(Source, Target, Diagnostic).bWasCacheHit);

	for (int32 Index = 0; Index < 3; ++Index)
	{
		FEntityRelationQueryContext Distinct = Context;
		Distinct.NumericContext.Add(EntityRelationTags::Domain_Damage, static_cast<float>(Index));
		Subsystem->EvaluateRelationByActor(Source, Target, Distinct);
	}
	TestEqual(TEXT("LRU cache respects configured limit"), Subsystem->GetRuntimeStats().CacheEntries, 2);

	const TArray<FEntityRelationId> Targets = { Target->Identity->GetEntityId(), FEntityRelationId(), FEntityRelationId::NewId() };
	const TArray<FEntityRelationResult> Batch = Subsystem->EvaluateRelationsFromSource(Source->Identity->GetEntityId(), Targets, Context);
	TestEqual(TEXT("batch preserves input count"), Batch.Num(), Targets.Num());
	TestEqual(TEXT("batch first target succeeds"), Batch[0].Status, EEntityRelationQueryStatus::Success);
	TestEqual(TEXT("batch invalid target is independent"), Batch[1].Status, EEntityRelationQueryStatus::InvalidTarget);
	TestEqual(TEXT("batch missing target is independent"), Batch[2].Status, EEntityRelationQueryStatus::TargetNotRegistered);

	bool bBlueprintSuccess = false;
	const FEntityRelationResult BlueprintResult = UEntityRelationsBlueprintLibrary::EvaluateRelation(Scope.World, Source, Target, Context, bBlueprintSuccess);
	TestTrue(TEXT("Blueprint wrapper reports success"), bBlueprintSuccess && BlueprintResult.IsSuccess());
	TestTrue(TEXT("pure identity lookup finds component"), UEntityRelationsBlueprintLibrary::GetEntityIdentityComponent(Source) == Source->Identity);
	TestEqual(TEXT("Actor wrapper rejects null source"), Subsystem->EvaluateRelationByActor(nullptr, Target, Context).Status, EEntityRelationQueryStatus::InvalidSource);
	FEntityRelationQueryContext InvalidContext = Context;
	InvalidContext.NumericContext.Add(EntityRelationTags::Domain_Damage, std::numeric_limits<float>::infinity());
	TestEqual(TEXT("non-finite query context is rejected"), Subsystem->EvaluateRelationByActor(Source, Target, InvalidContext).Status, EEntityRelationQueryStatus::InvalidContext);
	FEntityRelationQueryContext MissingDomain;
	TestEqual(TEXT("invalid domain is rejected"), Subsystem->EvaluateRelationByActor(Source, Target, MissingDomain).Status, EEntityRelationQueryStatus::InvalidContext);

	UEntityRelationsTestPolicy* NonCacheable = MakePolicy(*Scope.World, TEXT("NonCacheable"), 0, EntityRelationTags::Domain_General, EEntityRelationDecision::Allow, FGameplayTag(), FGameplayTag(), FGameplayTag(), true, false);
	TestTrue(TEXT("non-cacheable policy set activates"), Subsystem->SetPolicySetOverride(MakePolicySet(*Scope.World, { NonCacheable })));
	TestFalse(TEXT("non-cacheable first query bypasses cache"), Subsystem->EvaluateRelationByActor(Source, Target, Context).bWasCacheHit);
	TestFalse(TEXT("non-cacheable repeated query bypasses cache"), Subsystem->EvaluateRelationByActor(Source, Target, Context).bWasCacheHit);
	TestEqual(TEXT("non-cacheable policy evaluates each time"), NonCacheable->GetEvaluationCount(), 2);
	return true;
}

#endif
