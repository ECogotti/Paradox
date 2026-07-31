#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/PerceptionKnowledgeHearingRangeRendererComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Data/PerceptionKnowledgeProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "PerceptionKnowledgeTags.h"
#include "Settings/PerceptionKnowledgeDeveloperSettings.h"
#include "Subsystems/PerceptionKnowledgeWorldSubsystem.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

struct FPerceptionKnowledgeTestAccessor
{
	static void SetEntityId(
		UPerceptionKnowledgeSourceComponent& Source,
		const FPerceptionKnowledgeEntityId EntityId)
	{
		Source.EntityId = EntityId;
	}

	static void SetSenseRegistration(
		UPerceptionKnowledgeSourceComponent& Source,
		const bool bSight,
		const bool bHearing)
	{
		Source.bRegisterForSight = bSight;
		Source.bRegisterForHearing = bHearing;
	}

	static void SetSemanticRegistered(
		UPerceptionKnowledgeSourceComponent& Source,
		const bool bRegistered)
	{
		Source.bSemanticRegistered = bRegistered;
	}

	static void SetRelationship(
		UPerceptionKnowledgeListenerComponent& Listener,
		UPerceptionKnowledgeSourceComponent& Source,
		const FGameplayTag SenseTag,
		const bool bPerceived)
	{
		Listener.SetPerceptionRelationship(&Source, SenseTag, bPerceived);
	}

	static void Refresh(
		UPerceptionKnowledgeListenerComponent& Listener,
		UPerceptionKnowledgeSourceComponent& Source,
		const FGameplayTag SenseTag,
		const bool bAcquisition)
	{
		Listener.RefreshSourceStates(
			&Source,
			SenseTag,
			1.0f,
			Source.GetOwner()->GetActorLocation(),
			bAcquisition);
	}

	static void ReceiveEvent(
		UPerceptionKnowledgeListenerComponent& Listener,
		const FPerceptionKnowledgeEventObservation& Event)
	{
		Listener.ReceiveEventObservation(Event);
	}

	static void ExpireRecentEvents(UPerceptionKnowledgeListenerComponent& Listener)
	{
		for (UPerceptionKnowledgeListenerComponent::FRecentEventEntry& Entry : Listener.RecentEvents)
		{
			Entry.ExpirationWorldTime = -1.0;
		}
		Listener.CleanupRecentEvents();
	}

	static void CleanupRecentEvents(UPerceptionKnowledgeListenerComponent& Listener)
	{
		Listener.CleanupRecentEvents();
	}

	static void SetKnownStateTimestamp(
		UPerceptionKnowledgeListenerComponent& Listener,
		const FPerceptionKnowledgeEntityId EntityId,
		const FGameplayTag StateTag,
		const double WorldTimestamp)
	{
		FPerceptionKnowledgeStateKey Key;
		Key.EntityId = EntityId;
		Key.StateTag = StateTag;
		if (FPerceptionKnowledgeKnownState* State = Listener.KnownStates.Find(Key))
		{
			State->LastObservedWorldTime = WorldTimestamp;
		}
	}

	static int32 GetPendingNoiseCount(const UPerceptionKnowledgeWorldSubsystem& Subsystem)
	{
		return Subsystem.GetRuntimeStats().PendingSemanticNoises;
	}

	static void CleanupNoises(UPerceptionKnowledgeWorldSubsystem& Subsystem)
	{
		Subsystem.CleanupSemanticNoises();
	}
};

namespace UE::PerceptionKnowledge::Tests
{
	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			Context = GEngine
				? &GEngine->CreateNewWorldContext(EWorldType::Game)
				: nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
			if (World)
			{
				// UWorld::CreateWorld(Game) deliberately omits AI by default; native
				// Sight/Hearing tests require the same AI system that a game world owns.
				World->CreateAISystem();
			}
		}

		~FScopedTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(true);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}

		void StartPlay() const
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		void Tick(const float DeltaSeconds, const int32 Count = 1) const
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				World->Tick(LEVELTICK_All, DeltaSeconds);
			}
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	UPerceptionKnowledgeProfile* MakeProfile(
		UObject& Outer,
		const float RecentEventLifetime = 1.0f)
	{
		UPerceptionKnowledgeProfile* Profile = NewObject<UPerceptionKnowledgeProfile>(&Outer);
		Profile->SightRadius = 1500.0f;
		Profile->LoseSightRadius = 1700.0f;
		Profile->PeripheralVisionHalfAngle = 70.0f;
		Profile->SightMaxAge = 0.5f;
		Profile->HearingRange = 1500.0f;
		Profile->HearingMaxAge = 0.5f;
		Profile->RecentEventLifetime = RecentEventLifetime;
		Profile->MaxRecentEvents = 8;
		Profile->RepeatedObservationPolicy =
			EPerceptionKnowledgeRepeatedObservationPolicy::AcquisitionsAndChanges;
		return Profile;
	}

	AActor* SpawnActorWithBounds(UWorld& World, const TCHAR* Name, const FVector& Location)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = FName(Name);
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World.SpawnActor<AActor>(
			AActor::StaticClass(),
			Location,
			FRotator::ZeroRotator,
			Parameters);
		if (!Actor)
		{
			return nullptr;
		}
		UBoxComponent* Bounds = NewObject<UBoxComponent>(Actor, TEXT("TestBounds"));
		Actor->AddInstanceComponent(Bounds);
		Actor->SetRootComponent(Bounds);
		Bounds->SetBoxExtent(FVector(30.0, 30.0, 60.0));
		Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bounds->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	UPerceptionKnowledgeSourceComponent* AddSource(
		AActor& Owner,
		const FPerceptionKnowledgeEntityId ExplicitId = FPerceptionKnowledgeEntityId(),
		const bool bRegister = true)
	{
		UPerceptionKnowledgeSourceComponent* Source =
			NewObject<UPerceptionKnowledgeSourceComponent>(
				&Owner,
				TEXT("PerceptionKnowledgeSource"));
		Owner.AddInstanceComponent(Source);
		if (ExplicitId.IsValid())
		{
			FPerceptionKnowledgeTestAccessor::SetEntityId(*Source, ExplicitId);
		}
		if (bRegister)
		{
			Source->RegisterComponent();
		}
		return Source;
	}

	UPerceptionKnowledgeListenerComponent* AddListener(
		AActor& Owner,
		UPerceptionKnowledgeProfile& Profile)
	{
		UPerceptionKnowledgeListenerComponent* Listener =
			NewObject<UPerceptionKnowledgeListenerComponent>(
				&Owner,
				TEXT("PerceptionKnowledgeListener"));
		Owner.AddInstanceComponent(Listener);
		Listener->SetListenerProfile(&Profile);
		Listener->RegisterComponent();
		return Listener;
	}

	ACharacter* SpawnCharacter(
		UWorld& World,
		const TCHAR* Name,
		const FVector& Location,
		const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = FName(Name);
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<ACharacter>(
			ACharacter::StaticClass(),
			Location,
			Rotation,
			Parameters);
	}

	FGameplayTag SightTag()
	{
		return PerceptionKnowledgeTags::Sense_Sight.GetTag();
	}

	FGameplayTag HearingTag()
	{
		return PerceptionKnowledgeTags::Sense_Hearing.GetTag();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPerceptionKnowledgeValueAndIdentityTest,
	"PerceptionKnowledge.Core.ValueIdentityAndRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerceptionKnowledgeValueAndIdentityTest::RunTest(const FString& Parameters)
{
	using namespace UE::PerceptionKnowledge::Tests;

	bool BoolValue = true;
	int64 IntegerValue = 0;
	double FloatValue = 0.0;
	FName NameValue;
	FGameplayTag GameplayTagValue;
	FPerceptionKnowledgeEntityId EntityValue;
	FVector VectorValue;
	const FPerceptionKnowledgeEntityId TestEntity = FPerceptionKnowledgeEntityId::NewId();

	TestTrue(TEXT("Bool value is valid"), FPerceptionKnowledgeValue::MakeBool(false).IsValid());
	TestTrue(TEXT("Bool false remains a known typed value"),
		FPerceptionKnowledgeValue::MakeBool(false).GetBool(BoolValue) && !BoolValue);
	TestTrue(TEXT("Integer round trips"),
		FPerceptionKnowledgeValue::MakeInteger(42).GetInteger(IntegerValue) && IntegerValue == 42);
	TestTrue(TEXT("Float round trips"),
		FPerceptionKnowledgeValue::MakeFloat(3.5).GetFloat(FloatValue)
			&& FMath::IsNearlyEqual(FloatValue, 3.5));
	TestTrue(TEXT("Name round trips"),
		FPerceptionKnowledgeValue::MakeName(TEXT("Ready")).GetName(NameValue)
			&& NameValue == TEXT("Ready"));
	TestTrue(TEXT("Gameplay Tag round trips"),
		FPerceptionKnowledgeValue::MakeGameplayTag(SightTag()).GetGameplayTag(GameplayTagValue)
			&& GameplayTagValue == SightTag());
	TestTrue(TEXT("Entity ID round trips"),
		FPerceptionKnowledgeValue::MakeEntityId(TestEntity).GetEntityId(EntityValue)
			&& EntityValue == TestEntity);
	TestTrue(TEXT("Vector round trips"),
		FPerceptionKnowledgeValue::MakeVector(FVector(1.0, 2.0, 3.0)).GetVector(VectorValue)
			&& VectorValue == FVector(1.0, 2.0, 3.0));
	TestTrue(TEXT("Different semantic types never compare equal"),
		FPerceptionKnowledgeValue::MakeInteger(1) != FPerceptionKnowledgeValue::MakeFloat(1.0));

	const UPerceptionKnowledgeSourceComponent* SourceCDO =
		GetDefault<UPerceptionKnowledgeSourceComponent>();
	TestFalse(TEXT("Source CDO has no identity"), SourceCDO->GetEntityId().IsValid());

	FScopedTestWorld Scope(TEXT("PerceptionKnowledgeIdentityWorld"));
	if (!TestNotNull(TEXT("Transient identity world"), Scope.World))
	{
		return false;
	}
	AActor* FirstActor = SpawnActorWithBounds(*Scope.World, TEXT("AuthoredSource"), FVector::ZeroVector);
	UPerceptionKnowledgeSourceComponent* FirstSource =
		FirstActor ? AddSource(*FirstActor) : nullptr;
	if (!TestNotNull(TEXT("First Source"), FirstSource))
	{
		return false;
	}
	const FPerceptionKnowledgeEntityId StableId = FirstSource->GetEntityId();
	TestTrue(TEXT("Runtime Source receives a valid identity"), StableId.IsValid());
	TestTrue(TEXT("Source registers semantically"), FirstSource->IsSemanticallyRegistered());
	TestEqual(
		TEXT("Registry contains first Source"),
		Scope.World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()->GetRuntimeStats().RegisteredSources,
		1);

	FirstSource->UnregisterComponent();
	TestEqual(
		TEXT("Unregister removes Source"),
		Scope.World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()->GetRuntimeStats().RegisteredSources,
		0);
	TestEqual(TEXT("Unregister preserves persistent identity"), FirstSource->GetEntityId(), StableId);
	FirstSource->RegisterComponent();
	TestEqual(TEXT("Re-registration preserves persistent identity"), FirstSource->GetEntityId(), StableId);

	AActor* LifecycleActor = SpawnActorWithBounds(
		*Scope.World,
		TEXT("IdentityLifecycleSource"),
		FVector(-100.0, 0.0, 0.0));
	UPerceptionKnowledgeSourceComponent* LifecycleSource =
		LifecycleActor ? AddSource(*LifecycleActor, FPerceptionKnowledgeEntityId(), false) : nullptr;
	if (TestNotNull(TEXT("Identity lifecycle Source"), LifecycleSource))
	{
		LifecycleSource->OnComponentCreated();
		const FPerceptionKnowledgeEntityId AuthoredId = LifecycleSource->GetEntityId();
		LifecycleSource->PostLoad();
		TestEqual(TEXT("PostLoad preserves authored identity"), LifecycleSource->GetEntityId(), AuthoredId);
		LifecycleSource->PostDuplicate(EDuplicateMode::PIE);
		TestEqual(TEXT("PIE duplication preserves authored identity"), LifecycleSource->GetEntityId(), AuthoredId);
		LifecycleSource->PostDuplicate(EDuplicateMode::Normal);
		TestNotEqual(TEXT("Ordinary duplication regenerates identity"), LifecycleSource->GetEntityId(), AuthoredId);
	}

	TestEqual(
		TEXT("Disabling unregisters Source"),
		FirstSource->SetSourceEnabled(false).Status,
		EPerceptionKnowledgeOperationStatus::Success);
	TestFalse(TEXT("Disabled Source is not semantically registered"), FirstSource->IsSemanticallyRegistered());
	TestTrue(TEXT("Re-enabling registers Source"), FirstSource->SetSourceEnabled(true).IsSuccess());
	TestTrue(TEXT("Re-enabled Source is registered"), FirstSource->IsSemanticallyRegistered());

	AActor* DuplicateActor = SpawnActorWithBounds(
		*Scope.World,
		TEXT("DuplicateSource"),
		FVector(100.0, 0.0, 0.0));
	UPerceptionKnowledgeSourceComponent* DuplicateSource =
		DuplicateActor ? AddSource(*DuplicateActor, StableId, false) : nullptr;
	if (TestNotNull(TEXT("Duplicate Source"), DuplicateSource))
	{
		AddExpectedError(TEXT("Rejected duplicate EntityId"), EAutomationExpectedErrorFlags::Contains, 1);
		DuplicateSource->RegisterComponent();
		TestEqual(
			TEXT("Duplicate registration is explicit"),
			DuplicateSource->GetLastRegistrationResult().Status,
			EPerceptionKnowledgeOperationStatus::DuplicateEntityId);
		TestEqual(
			TEXT("Duplicate never replaces first Source"),
			Scope.World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()->FindSource(StableId),
			FirstSource);
	}

	AActor* AssignmentActor = SpawnActorWithBounds(
		*Scope.World,
		TEXT("AssignedIdentitySource"),
		FVector(150.0, 0.0, 0.0));
	UPerceptionKnowledgeSourceComponent* AssignmentSource =
		AssignmentActor ? AddSource(*AssignmentActor) : nullptr;
	if (TestNotNull(TEXT("Identity assignment Source"), AssignmentSource))
	{
		const FPerceptionKnowledgeEntityId OriginalAssignmentId =
			AssignmentSource->GetEntityId();
		const FPerceptionKnowledgeEntityId RequestedAssignmentId =
			FPerceptionKnowledgeEntityId::NewId();
		TestEqual(
			TEXT("Registered Source rejects identity mutation"),
			AssignmentSource->AssignEntityId(RequestedAssignmentId).Status,
			EPerceptionKnowledgeOperationStatus::InvalidArgument);
		TestEqual(
			TEXT("Registered Source also rejects a no-op identity assignment"),
			AssignmentSource->AssignEntityId(OriginalAssignmentId).Status,
			EPerceptionKnowledgeOperationStatus::InvalidArgument);
		TestEqual(
			TEXT("Rejected registered mutation preserves identity"),
			AssignmentSource->GetEntityId(),
			OriginalAssignmentId);
		TestTrue(
			TEXT("Assignment Source disables cleanly"),
			AssignmentSource->SetSourceEnabled(false).IsSuccess());
		TestEqual(
			TEXT("Invalid requested identity is rejected"),
			AssignmentSource
				->AssignEntityId(FPerceptionKnowledgeEntityId())
				.Status,
			EPerceptionKnowledgeOperationStatus::InvalidEntityId);
		TestEqual(
			TEXT("Live identity collision is rejected"),
			AssignmentSource->AssignEntityId(StableId).Status,
			EPerceptionKnowledgeOperationStatus::DuplicateEntityId);
		TestEqual(
			TEXT("Collision preserves the previous identity"),
			AssignmentSource->GetEntityId(),
			OriginalAssignmentId);
		TestTrue(
			TEXT("Disabled Source accepts a unique identity"),
			AssignmentSource
				->AssignEntityId(RequestedAssignmentId)
				.IsSuccess());
		TestEqual(
			TEXT("Assigned identity becomes authoritative"),
			AssignmentSource->GetEntityId(),
			RequestedAssignmentId);
		TestTrue(
			TEXT("Assigned Source re-enables cleanly"),
			AssignmentSource->SetSourceEnabled(true).IsSuccess());
		TestEqual(
			TEXT("Registry resolves the assigned identity"),
			Scope.World
				->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()
				->FindSource(RequestedAssignmentId),
			AssignmentSource);
		TestNull(
			TEXT("Registry no longer resolves the replaced identity"),
			Scope.World
				->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()
				->FindSource(OriginalAssignmentId));
	}

	AActor* UnconfiguredActor = SpawnActorWithBounds(
		*Scope.World,
		TEXT("UnconfiguredSource"),
		FVector(200.0, 0.0, 0.0));
	UPerceptionKnowledgeSourceComponent* UnconfiguredSource =
		UnconfiguredActor ? AddSource(*UnconfiguredActor, FPerceptionKnowledgeEntityId(), false) : nullptr;
	if (TestNotNull(TEXT("Unconfigured Source"), UnconfiguredSource))
	{
		FPerceptionKnowledgeTestAccessor::SetSenseRegistration(
			*UnconfiguredSource,
			false,
			false);
		UnconfiguredSource->RegisterComponent();
		TestEqual(
			TEXT("Source without senses is rejected"),
			UnconfiguredSource->GetLastRegistrationResult().Status,
			EPerceptionKnowledgeOperationStatus::InvalidArgument);
	}

	FirstActor->Destroy();
	TestNull(
		TEXT("Source destruction unregisters immediately"),
		Scope.World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()->FindSource(StableId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPerceptionKnowledgeStoreTest,
	"PerceptionKnowledge.Knowledge.StateEventRevisionSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerceptionKnowledgeStoreTest::RunTest(const FString& Parameters)
{
	using namespace UE::PerceptionKnowledge::Tests;
	FScopedTestWorld Scope(TEXT("PerceptionKnowledgeStoreWorld"));
	if (!TestNotNull(TEXT("Transient store world"), Scope.World))
	{
		return false;
	}

	AActor* ListenerOwner =
		SpawnActorWithBounds(*Scope.World, TEXT("ListenerOwner"), FVector::ZeroVector);
	AActor* SourceOwner =
		SpawnActorWithBounds(*Scope.World, TEXT("StateSource"), FVector(300.0, 0.0, 0.0));
	UPerceptionKnowledgeProfile* Profile =
		ListenerOwner ? MakeProfile(*ListenerOwner) : nullptr;
	UPerceptionKnowledgeListenerComponent* Listener =
		ListenerOwner && Profile ? AddListener(*ListenerOwner, *Profile) : nullptr;
	UPerceptionKnowledgeSourceComponent* Source =
		SourceOwner ? AddSource(*SourceOwner) : nullptr;
	if (!TestNotNull(TEXT("Store Listener"), Listener)
		|| !TestNotNull(TEXT("Store Source"), Source))
	{
		return false;
	}

	int32 ObservationCount = 0;
	int32 KnownChangeCount = 0;
	Listener->OnObservationProducedNative().AddLambda(
		[&ObservationCount](const FPerceptionKnowledgeObservation&)
		{
			++ObservationCount;
		});
	Listener->OnKnownStateChangedNative().AddLambda(
		[&KnownChangeCount](
			const FPerceptionKnowledgeKnownState&,
			const FPerceptionKnowledgeKnownState&)
		{
			++KnownChangeCount;
		});

	TestTrue(
		TEXT("Initial false state is stored on Source"),
		Source->SetObservableState(
			SightTag(),
			FPerceptionKnowledgeValue::MakeBool(false)).IsSuccess());
	FPerceptionKnowledgeTestAccessor::SetRelationship(
		*Listener,
		*Source,
		SightTag(),
		true);
	FPerceptionKnowledgeTestAccessor::Refresh(
		*Listener,
		*Source,
		SightTag(),
		true);

	FPerceptionKnowledgeKnownState Known;
	TestTrue(
		TEXT("Visible acquisition learns state"),
		Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known));
	bool bPowered = true;
	TestTrue(
		TEXT("Known false is distinct and retained"),
		Known.Status == EPerceptionKnowledgeFactStatus::Known
			&& Known.Value.GetBool(bPowered)
			&& !bPowered);
	TestEqual(TEXT("First learning sets fact revision"), Known.FactRevision, int64(1));
	TestEqual(TEXT("First learning sets global revision"), Known.KnowledgeRevision, int64(1));
	TestEqual(TEXT("First learning broadcasts state change"), KnownChangeCount, 1);

	FPerceptionKnowledgeTestAccessor::Refresh(
		*Listener,
		*Source,
		SightTag(),
		false);
	Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known);
	TestEqual(TEXT("Repeated accepted observation advances global revision"), Listener->GetKnowledgeRevision(), int64(2));
	TestEqual(TEXT("Repeated semantic value does not advance fact revision"), Known.FactRevision, int64(1));
	TestEqual(TEXT("Anti-spam suppresses duplicate public observation"), ObservationCount, 1);

	TestTrue(
		TEXT("Visible Source change propagates without reacquisition"),
		Source->SetObservableState(
			SightTag(),
			FPerceptionKnowledgeValue::MakeBool(true)).IsSuccess());
	Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known);
	TestTrue(
		TEXT("Knowledge updates to true while relationship remains active"),
		Known.Value.GetBool(bPowered) && bPowered);
	TestEqual(TEXT("Semantic change advances fact revision"), Known.FactRevision, int64(2));
	TestEqual(TEXT("Semantic change advances global revision"), Listener->GetKnowledgeRevision(), int64(3));
	TestEqual(TEXT("Semantic change broadcasts"), KnownChangeCount, 2);

	TestEqual(
		TEXT("Incompatible type is rejected"),
		Source->SetObservableState(
			SightTag(),
			FPerceptionKnowledgeValue::MakeInteger(1)).Status,
		EPerceptionKnowledgeOperationStatus::TypeMismatch);
	TestTrue(
		TEXT("Explicit Unknown propagates"),
		Source->SetObservableStateUnknown(SightTag()).IsSuccess());
	Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known);
	TestEqual(
		TEXT("Unknown is an explicit status, not Bool false"),
		Known.Status,
		EPerceptionKnowledgeFactStatus::Unknown);

	const int64 RevisionBeforeEvent = Listener->GetKnowledgeRevision();
	FPerceptionKnowledgeEventRequest EventRequest;
	EventRequest.EventTag = HearingTag();
	EventRequest.SenseTag = SightTag();
	EventRequest.Strength = 0.75f;
	TestTrue(TEXT("Sight-routed transient event emits"), Source->EmitObservableEvent(EventRequest).IsSuccess());
	TestEqual(TEXT("Transient event enters recent memory"), Listener->GetRecentEvents().Num(), 1);
	TestEqual(TEXT("Transient event never changes state revision"), Listener->GetKnowledgeRevision(), RevisionBeforeEvent);
	Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known);
	TestEqual(TEXT("Transient event never overwrites state"), Known.Status, EPerceptionKnowledgeFactStatus::Unknown);

	EventRequest.SenseTag = HearingTag();
	TestEqual(
		TEXT("Direct Hearing event is rejected in favor of semantic noise"),
		Source->EmitObservableEvent(EventRequest).Status,
		EPerceptionKnowledgeOperationStatus::UnsupportedSense);

	FPerceptionKnowledgeSnapshotFilter Filter;
	Filter.EntityIds.Add(Source->GetEntityId());
	Filter.StateTags.AddTag(SightTag());
	Filter.SenseTags.AddTag(SightTag());
	const FPerceptionKnowledgeSnapshot Snapshot = Listener->BuildKnowledgeSnapshot(Filter);
	TestEqual(TEXT("Filtered snapshot contains one copied state"), Snapshot.States.Num(), 1);
	TestEqual(TEXT("Snapshot captures global revision"), Snapshot.KnowledgeRevision, Listener->GetKnowledgeRevision());
	Filter.SenseTags.Reset();
	Filter.SenseTags.AddTag(HearingTag());
	TestEqual(TEXT("Snapshot rejects a non-matching Sense filter"), Listener->BuildKnowledgeSnapshot(Filter).States.Num(), 0);
	Filter.SenseTags.Reset();
	Filter.SenseTags.AddTag(SightTag());
	FPerceptionKnowledgeTestAccessor::SetKnownStateTimestamp(
		*Listener,
		Source->GetEntityId(),
		SightTag(),
		-10.0);
	Filter.MaxAgeSeconds = 1.0;
	TestEqual(TEXT("Snapshot rejects state older than Max Age"), Listener->BuildKnowledgeSnapshot(Filter).States.Num(), 0);

	TestTrue(TEXT("Remove invalidates current observers"), Source->RemoveObservableState(SightTag()).IsSuccess());
	Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known);
	TestEqual(TEXT("Removed Source exposure remains invalidated in listener knowledge"), Known.Status, EPerceptionKnowledgeFactStatus::Invalidated);

	for (int32 EventIndex = 0; EventIndex < 10; ++EventIndex)
	{
		EventRequest.SenseTag = SightTag();
		Source->EmitObservableEvent(EventRequest);
	}
	TestEqual(TEXT("Recent Event Memory enforces bounded capacity"), Listener->GetRecentEvents().Num(), Profile->MaxRecentEvents);
	FPerceptionKnowledgeTestAccessor::ExpireRecentEvents(*Listener);
	TestEqual(TEXT("Expired recent event is batch-cleaned"), Listener->GetRecentEvents().Num(), 0);

	const FPerceptionKnowledgeEntityId DestroyedId = Source->GetEntityId();
	SourceOwner->Destroy();
	TestNull(TEXT("Destroyed Source no longer resolves"), Scope.World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()->FindSource(DestroyedId));
	TestEqual(TEXT("Disconnected snapshot remains valid after Source destruction"), Snapshot.States.Num(), 1);
	TestEqual(TEXT("Snapshot retains copied Entity ID"), Snapshot.States[0].Key.EntityId, DestroyedId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPerceptionKnowledgeControllerTest,
	"PerceptionKnowledge.Listener.ControllerBodiesAndPossession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerceptionKnowledgeControllerTest::RunTest(const FString& Parameters)
{
	using namespace UE::PerceptionKnowledge::Tests;
	FScopedTestWorld Scope(TEXT("PerceptionKnowledgeControllerWorld"));
	if (!TestNotNull(TEXT("Transient controller world"), Scope.World))
	{
		return false;
	}

	APlayerController* PlayerController = Scope.World->SpawnActor<APlayerController>();
	ACharacter* PlayerPawn = SpawnCharacter(
		*Scope.World,
		TEXT("PlayerPawn"),
		FVector(100.0, 50.0, 20.0),
		FRotator(0.0, 135.0, 0.0));
	if (!TestNotNull(TEXT("Player Controller"), PlayerController)
		|| !TestNotNull(TEXT("Player Pawn"), PlayerPawn))
	{
		return false;
	}
	PlayerController->Possess(PlayerPawn);
	UPerceptionKnowledgeProfile* PlayerProfile = MakeProfile(*PlayerController);
	UPerceptionKnowledgeListenerComponent* PlayerListener =
		AddListener(*PlayerController, *PlayerProfile);
	TestTrue(TEXT("Player Controller listener Body Actor is its Pawn"), PlayerListener->GetResolvedBodyActor() == PlayerPawn);

	FVector ViewLocation;
	FVector ViewDirection;
	TestTrue(TEXT("Player Controller listener resolves a valid viewpoint"), PlayerListener->GetListenerViewpoint(ViewLocation, ViewDirection));
	TestTrue(
		TEXT("Player Controller direction follows Pawn eyes, not a top-down camera"),
		ViewDirection.Equals(PlayerPawn->GetActorForwardVector(), 0.01));
	TestFalse(TEXT("Possessed listener is not suspended"), PlayerListener->IsObservationSuspended());

	PlayerController->UnPossess();
	TestNull(TEXT("Unpossessed Player Controller has no Body Actor"), PlayerListener->GetResolvedBodyActor());
	TestTrue(TEXT("Unpossessed Player Controller suspends observations"), PlayerListener->IsObservationSuspended());
	TestEqual(TEXT("Unpossess does not erase Knowledge Store"), PlayerListener->GetKnowledgeRevision(), int64(0));

	ACharacter* ReplacementPawn = SpawnCharacter(
		*Scope.World,
		TEXT("ReplacementPawn"),
		FVector(10.0, 0.0, 0.0),
		FRotator(0.0, 20.0, 0.0));
	PlayerController->Possess(ReplacementPawn);
	TestTrue(TEXT("Possession updates Body Actor"), PlayerListener->GetResolvedBodyActor() == ReplacementPawn);
	TestFalse(TEXT("New possession resumes observations"), PlayerListener->IsObservationSuspended());

	AAIController* AIController = Scope.World->SpawnActor<AAIController>();
	ACharacter* AIPawn = SpawnCharacter(
		*Scope.World,
		TEXT("AIPawn"),
		FVector::ZeroVector,
		FRotator::ZeroRotator);
	AIController->Possess(AIPawn);
	UPerceptionKnowledgeProfile* AIProfile = MakeProfile(*AIController);
	UPerceptionKnowledgeListenerComponent* AIListener =
		AddListener(*AIController, *AIProfile);
	TestTrue(TEXT("AI Controller listener Body Actor is its Pawn"), AIListener->GetResolvedBodyActor() == AIPawn);
	TestTrue(TEXT("AI Controller listener registers"), AIListener->GetLastRegistrationResult().IsSuccess());

	APlayerController* PawnlessController = Scope.World->SpawnActor<APlayerController>();
	UPerceptionKnowledgeProfile* PawnlessProfile = MakeProfile(*PawnlessController);
	UPerceptionKnowledgeListenerComponent* PawnlessListener =
		AddListener(*PawnlessController, *PawnlessProfile);
	TestTrue(TEXT("Pawnless Player Controller is explicitly suspended"), PawnlessListener->IsObservationSuspended());
	TestFalse(TEXT("Pawnless Player Controller has no viewpoint"), PawnlessListener->GetListenerViewpoint(ViewLocation, ViewDirection));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPerceptionKnowledgeControllerHearingRendererTest,
	"PerceptionKnowledge.Listener.ControllerHearingRendererUsesBodyPrimitive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerceptionKnowledgeControllerHearingRendererTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::PerceptionKnowledge::Tests;
	FScopedTestWorld Scope(TEXT("PerceptionKnowledgeHearingRendererWorld"));
	if (!TestNotNull(TEXT("Transient renderer world"), Scope.World))
	{
		return false;
	}

	AAIController* Controller =
		Scope.World->SpawnActor<AAIController>();
	ACharacter* Pawn = SpawnCharacter(
		*Scope.World,
		TEXT("RendererPawn"),
		FVector::ZeroVector);
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestNotNull(TEXT("Renderer Controller"), Controller)
		|| !TestNotNull(TEXT("Renderer Pawn"), Pawn)
		|| !TestNotNull(TEXT("Engine test Sphere"), SphereMesh))
	{
		return false;
	}
	Controller->Possess(Pawn);

	UPerceptionKnowledgeProfile* Profile =
		MakeProfile(*Controller);
	UPerceptionKnowledgeListenerComponent* Listener =
		AddListener(*Controller, *Profile);
	UPerceptionKnowledgeHearingRangeRendererComponent* Renderer =
		NewObject<UPerceptionKnowledgeHearingRangeRendererComponent>(
			Controller,
			TEXT("HearingRangeRenderer"));
	Controller->AddInstanceComponent(Renderer);
	Renderer->SetStaticMesh(SphereMesh);
	Renderer->SetListener(Listener);
	Renderer->SetGameplayVisible(true);
	Renderer->RegisterComponent();

	UStaticMeshComponent* BodyPrimitive =
		Renderer->GetActiveRenderComponent();
	if (!TestNotNull(
		TEXT("Controller-owned renderer creates a Body-owned primitive"),
		BodyPrimitive))
	{
		return false;
	}
	TestTrue(
		TEXT("Render primitive is not owned by the hidden Controller"),
		BodyPrimitive != Renderer);
	TestTrue(
		TEXT("Render primitive is owned by the possessed Pawn"),
		BodyPrimitive->GetOwner() == Pawn);
	TestTrue(
		TEXT("Render primitive is anchored to the Pawn root"),
		BodyPrimitive->GetAttachParent() == Pawn->GetRootComponent());
	TestTrue(
		TEXT("Gameplay visibility displays the hearing range"),
		Renderer->IsHearingRangeVisible());
	TestEqual(
		TEXT("Renderer reads the effective profile range"),
		Renderer->GetRenderedHearingRange(),
		Profile->HearingRange);
	TestTrue(
		TEXT("Authored sphere radius is scaled to the Hearing Range"),
		FMath::IsNearlyEqual(
			BodyPrimitive->GetComponentScale().X * 50.0f,
			Profile->HearingRange,
			0.1f));
	TestEqual(
		TEXT("Render primitive has no collision"),
		BodyPrimitive->GetCollisionEnabled(),
		ECollisionEnabled::NoCollision);
	TestFalse(
		TEXT("Render primitive generates no overlaps"),
		BodyPrimitive->GetGenerateOverlapEvents());
	TestFalse(
		TEXT("Render primitive cannot affect navigation"),
		BodyPrimitive->CanEverAffectNavigation());

	Controller->UnPossess();
	TestNull(
		TEXT("Unpossess removes the obsolete Body primitive"),
		Renderer->GetActiveRenderComponent());
	TestFalse(
		TEXT("Unpossess hides the hearing range"),
		Renderer->IsHearingRangeVisible());

	ACharacter* ReplacementPawn = SpawnCharacter(
		*Scope.World,
		TEXT("ReplacementRendererPawn"),
		FVector(100.0, 0.0, 0.0));
	Controller->Possess(ReplacementPawn);
	BodyPrimitive = Renderer->GetActiveRenderComponent();
	TestNotNull(
		TEXT("Possession creates a replacement Body primitive"),
		BodyPrimitive);
	if (BodyPrimitive)
	{
		TestTrue(
			TEXT("Replacement primitive follows the new Pawn"),
			BodyPrimitive->GetOwner() == ReplacementPawn);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPerceptionKnowledgeRuntimeProfilePropagationTest,
	"PerceptionKnowledge.Listener.RuntimeProfilePropagatesToNativeSensesAndRenderer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerceptionKnowledgeRuntimeProfilePropagationTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::PerceptionKnowledge::Tests;
	FScopedTestWorld Scope(TEXT("PerceptionKnowledgeRuntimeProfileWorld"));
	if (!TestNotNull(TEXT("Transient runtime-profile world"), Scope.World))
	{
		return false;
	}

	AAIController* Controller = Scope.World->SpawnActor<AAIController>();
	ACharacter* Pawn = SpawnCharacter(
		*Scope.World,
		TEXT("RuntimeProfilePawn"),
		FVector::ZeroVector);
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestNotNull(TEXT("Runtime-profile Controller"), Controller)
		|| !TestNotNull(TEXT("Runtime-profile Pawn"), Pawn)
		|| !TestNotNull(TEXT("Runtime-profile Sphere"), SphereMesh))
	{
		return false;
	}
	Controller->Possess(Pawn);

	UPerceptionKnowledgeProfile* Profile = MakeProfile(*Controller);
	UPerceptionKnowledgeListenerComponent* Listener =
		AddListener(*Controller, *Profile);
	UPerceptionKnowledgeHearingRangeRendererComponent* Renderer =
		NewObject<UPerceptionKnowledgeHearingRangeRendererComponent>(
			Controller,
			TEXT("RuntimeProfileHearingRenderer"));
	Controller->AddInstanceComponent(Renderer);
	Renderer->SetStaticMesh(SphereMesh);
	Renderer->SetListener(Listener);
	Renderer->SetGameplayVisible(true);
	Renderer->RegisterComponent();

	UAISenseConfig_Sight* NativeSight =
		Listener->GetSenseConfig<UAISenseConfig_Sight>();
	UAISenseConfig_Hearing* NativeHearing =
		Listener->GetSenseConfig<UAISenseConfig_Hearing>();
	UStaticMeshComponent* BodyPrimitive =
		Renderer->GetActiveRenderComponent();
	if (!TestNotNull(TEXT("Native Sight config"), NativeSight)
		|| !TestNotNull(TEXT("Native Hearing config"), NativeHearing)
		|| !TestNotNull(TEXT("Body-owned Hearing primitive"), BodyPrimitive))
	{
		return false;
	}

	int32 ListenerConfigurationChangeCount = 0;
	const FDelegateHandle ConfigurationHandle =
		Listener->OnListenerConfigurationChangedNative().AddLambda(
			[&ListenerConfigurationChangeCount]()
			{
				++ListenerConfigurationChangeCount;
			});

	FString Error;
	TestTrue(
		TEXT("Runtime Sight ranges validate and publish"),
		Profile->SetSightRanges(625.0f, 775.0f, Error));
	TestTrue(
		TEXT("Runtime Hearing range validates and publishes"),
		Profile->SetHearingRange(925.0f, Error));
	TestEqual(
		TEXT("Each runtime profile mutation notifies the listener"),
		ListenerConfigurationChangeCount,
		2);
	TestEqual(
		TEXT("Native Sight Radius updates immediately"),
		NativeSight->SightRadius,
		625.0f);
	TestEqual(
		TEXT("Native Lose Sight Radius updates immediately"),
		NativeSight->LoseSightRadius,
		775.0f);
	TestEqual(
		TEXT("Native Hearing Range updates immediately"),
		NativeHearing->HearingRange,
		925.0f);
	TestEqual(
		TEXT("Listener exposes the applied Sight Radius"),
		Listener->GetEffectiveSightRadius(),
		625.0f);
	TestEqual(
		TEXT("Listener exposes the applied Hearing Range"),
		Listener->GetEffectiveHearingRange(),
		925.0f);
	TestEqual(
		TEXT("Hearing renderer receives the applied range"),
		Renderer->GetRenderedHearingRange(),
		925.0f);
	TestTrue(
		TEXT("Hearing renderer rescales after the runtime profile mutation"),
		FMath::IsNearlyEqual(
			BodyPrimitive->GetComponentScale().X * 50.0f,
			925.0f,
			0.1f));

	const float PreviousSightRadius = Profile->SightRadius;
	TestFalse(
		TEXT("Invalid coupled Sight ranges are rejected"),
		Profile->SetSightRanges(1000.0f, 500.0f, Error));
	TestEqual(
		TEXT("Rejected Sight ranges do not partially mutate the Profile"),
		Profile->SightRadius,
		PreviousSightRadius);

	Profile->SightRadius = 1000.0f;
	Profile->LoseSightRadius = 500.0f;
	AddExpectedError(
		TEXT("is suspended because Profile="),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(
		TEXT("Invalid direct C++ bulk changes are reported"),
		Profile->NotifyRuntimeConfigurationChanged(Error));
	TestTrue(
		TEXT("Invalid published Profile suspends the listener"),
		Listener->IsObservationSuspended());
	TestEqual(
		TEXT("Invalid published Profile disables applied Sight"),
		Listener->GetEffectiveSightRadius(),
		0.0f);
	TestEqual(
		TEXT("Invalid published Profile disables applied Hearing"),
		Listener->GetEffectiveHearingRange(),
		0.0f);
	TestFalse(
		TEXT("Invalid published Profile hides the Hearing renderer"),
		Renderer->IsHearingRangeVisible());

	Profile->SightRadius = 625.0f;
	Profile->LoseSightRadius = 775.0f;
	TestTrue(
		TEXT("Corrected C++ bulk changes resume propagation"),
		Profile->NotifyRuntimeConfigurationChanged(Error));
	TestFalse(
		TEXT("Corrected Profile resumes the listener"),
		Listener->IsObservationSuspended());
	TestTrue(
		TEXT("Corrected Profile redisplays the Hearing renderer"),
		Renderer->IsHearingRangeVisible());

#if WITH_EDITOR
	Profile->HearingRange = 1125.0f;
	FProperty* HearingRangeProperty = FindFProperty<FProperty>(
		UPerceptionKnowledgeProfile::StaticClass(),
		GET_MEMBER_NAME_CHECKED(
			UPerceptionKnowledgeProfile,
			HearingRange));
	if (TestNotNull(
		TEXT("Reflected Hearing Range property"),
		HearingRangeProperty))
	{
		FPropertyChangedEvent PropertyChangedEvent(
			HearingRangeProperty,
			EPropertyChangeType::ValueSet);
		Profile->PostEditChangeProperty(PropertyChangedEvent);
		TestEqual(
			TEXT("Editor Data Asset edits update native Hearing"),
			NativeHearing->HearingRange,
			1125.0f);
		TestEqual(
			TEXT("Editor Data Asset edits update the Hearing renderer"),
			Renderer->GetRenderedHearingRange(),
			1125.0f);
	}
#endif

	Listener->OnListenerConfigurationChangedNative().Remove(
		ConfigurationHandle);
	Listener->SetListenerProfile(nullptr);
	TestEqual(
		TEXT("Clearing the Profile clears the applied Sight range"),
		Listener->GetEffectiveSightRadius(),
		0.0f);
	TestEqual(
		TEXT("Clearing the Profile clears the applied Hearing range"),
		Listener->GetEffectiveHearingRange(),
		0.0f);
	TestFalse(
		TEXT("Clearing the Profile hides the Hearing renderer"),
		Renderer->IsHearingRangeVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPerceptionKnowledgeNativeSightTest,
	"PerceptionKnowledge.Perception.NativeSightRetentionAndVisibleRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerceptionKnowledgeNativeSightTest::RunTest(const FString& Parameters)
{
	using namespace UE::PerceptionKnowledge::Tests;
	FScopedTestWorld Scope(TEXT("PerceptionKnowledgeSightWorld"));
	if (!TestNotNull(TEXT("Transient Sight world"), Scope.World))
	{
		return false;
	}

	AAIController* Controller = Scope.World->SpawnActor<AAIController>();
	ACharacter* Pawn = SpawnCharacter(
		*Scope.World,
		TEXT("SightPawn"),
		FVector::ZeroVector,
		FRotator::ZeroRotator);
	Controller->Possess(Pawn);
	UPerceptionKnowledgeProfile* Profile = MakeProfile(*Controller);
	UPerceptionKnowledgeListenerComponent* Listener = AddListener(*Controller, *Profile);
	AActor* SourceActor = SpawnActorWithBounds(
		*Scope.World,
		TEXT("VisibleSource"),
		FVector(400.0, 0.0, 40.0));
	UPerceptionKnowledgeSourceComponent* Source = AddSource(*SourceActor);
	Source->SetObservableState(SightTag(), FPerceptionKnowledgeValue::MakeBool(false));

	Scope.StartPlay();
	Scope.Tick(0.1f, 8);
	TestTrue(
		TEXT("Native AI Sight acquires registered Source"),
		Listener->IsEntityCurrentlyPerceived(Source->GetEntityId(), SightTag()));
	FPerceptionKnowledgeKnownState Known;
	TestTrue(
		TEXT("Native Sight acquisition produces Source state"),
		Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known));

	const int64 BeforeVisibleChange = Listener->GetKnowledgeRevision();
	Source->SetObservableState(SightTag(), FPerceptionKnowledgeValue::MakeBool(true));
	Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known);
	bool bPowered = false;
	TestTrue(
		TEXT("State updates while continuously visible without reacquisition"),
		Known.Value.GetBool(bPowered) && bPowered
			&& Listener->GetKnowledgeRevision() > BeforeVisibleChange);

	SourceActor->SetActorLocation(FVector(5000.0, 0.0, 0.0));
	Scope.Tick(0.1f, 10);
	TestFalse(
		TEXT("Native Sight loss clears current relationship"),
		Listener->IsEntityCurrentlyPerceived(Source->GetEntityId(), SightTag()));
	TestTrue(
		TEXT("Sight loss retains current knowledge"),
		Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known));

	FString RuntimeProfileError;
	TestTrue(
		TEXT("Sight range can expand at runtime"),
		Profile->SetSightRanges(5500.0f, 6000.0f, RuntimeProfileError));
	Scope.Tick(0.1f, 10);
	TestTrue(
		TEXT("Native Sight reacquires a Source inside the expanded runtime range"),
		Listener->IsEntityCurrentlyPerceived(Source->GetEntityId(), SightTag()));

	TestTrue(
		TEXT("Explicit forgetting removes retained state"),
		Listener->ForgetEntity(Source->GetEntityId()).IsSuccess());
	TestFalse(
		TEXT("Forgotten state no longer queries"),
		Listener->GetKnownState(Source->GetEntityId(), SightTag(), Known));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPerceptionKnowledgeNativeHearingTest,
	"PerceptionKnowledge.Perception.NativeHearingCorrelationRangeAndExpiration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerceptionKnowledgeNativeHearingTest::RunTest(const FString& Parameters)
{
	using namespace UE::PerceptionKnowledge::Tests;
	FScopedTestWorld Scope(TEXT("PerceptionKnowledgeHearingWorld"));
	if (!TestNotNull(TEXT("Transient Hearing world"), Scope.World))
	{
		return false;
	}

	UPerceptionKnowledgeDeveloperSettings* Settings =
		GetMutableDefault<UPerceptionKnowledgeDeveloperSettings>();
	const float OriginalCorrelationLifetime = Settings->SemanticNoiseCorrelationLifetime;
	const float OriginalCleanupInterval = Settings->SemanticNoiseCleanupInterval;
	Settings->SemanticNoiseCorrelationLifetime = 0.25f;
	Settings->SemanticNoiseCleanupInterval = 0.1f;

	AAIController* NearController = Scope.World->SpawnActor<AAIController>();
	ACharacter* NearPawn = SpawnCharacter(
		*Scope.World,
		TEXT("NearPawn"),
		FVector::ZeroVector);
	NearController->Possess(NearPawn);
	UPerceptionKnowledgeProfile* NearProfile = MakeProfile(*NearController, 0.5f);
	UPerceptionKnowledgeListenerComponent* NearListener =
		AddListener(*NearController, *NearProfile);

	AAIController* FarController = Scope.World->SpawnActor<AAIController>();
	ACharacter* FarPawn = SpawnCharacter(
		*Scope.World,
		TEXT("FarPawn"),
		FVector(5000.0, 0.0, 0.0));
	FarController->Possess(FarPawn);
	UPerceptionKnowledgeProfile* FarProfile = MakeProfile(*FarController, 0.5f);
	UPerceptionKnowledgeListenerComponent* FarListener =
		AddListener(*FarController, *FarProfile);

	AActor* SourceActor = SpawnActorWithBounds(
		*Scope.World,
		TEXT("NoiseSource"),
		FVector(400.0, 0.0, 0.0));
	UPerceptionKnowledgeSourceComponent* Source = AddSource(*SourceActor);
	AActor* InstigatorActor = SpawnActorWithBounds(
		*Scope.World,
		TEXT("NoiseInstigator"),
		FVector(400.0, 0.0, 0.0));
	UPerceptionKnowledgeSourceComponent* InstigatorSource = AddSource(*InstigatorActor);

	Scope.StartPlay();
	FPerceptionKnowledgeNoiseRequest Request;
	Request.EventTag = HearingTag();
	Request.Instigator = InstigatorActor;
	Request.Loudness = 1.0f;
	Request.MaxRange = 1500.0f;
	Request.Strength = 0.8f;
	TestTrue(TEXT("First semantic noise uses native Hearing"), Source->EmitSemanticNoise(Request).IsSuccess());
	TestTrue(TEXT("Closely timed second noise receives a distinct correlation"), Source->EmitSemanticNoise(Request).IsSuccess());
	Scope.Tick(0.05f, 4);

	const TArray<FPerceptionKnowledgeEventObservation> NearEvents = NearListener->GetRecentEvents();
	const TArray<FPerceptionKnowledgeEventObservation> FarEvents = FarListener->GetRecentEvents();
	TestEqual(TEXT("Listener inside range receives both semantic noises"), NearEvents.Num(), 2);
	TestEqual(TEXT("Listener outside range receives no semantic noise"), FarEvents.Num(), 0);
	if (NearEvents.Num() == 2)
	{
		TestNotEqual(TEXT("Closely timed noises remain unambiguous"), NearEvents[0].ObservationId, NearEvents[1].ObservationId);
		TestEqual(TEXT("Correlated event retains Source identity"), NearEvents[0].SourceEntityId, Source->GetEntityId());
		TestEqual(TEXT("Different native Instigator retains its identity"), NearEvents[0].InstigatorEntityId, InstigatorSource->GetEntityId());
		TestTrue(TEXT("Correlated Hearing event retains loudness"), FMath::IsNearlyEqual(NearEvents[0].Loudness, 1.0f));
	}

	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		Scope.World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>();
	TestEqual(
		TEXT("Correlation entries remain available for multiple listeners until TTL"),
		FPerceptionKnowledgeTestAccessor::GetPendingNoiseCount(*Subsystem),
		2);
	Scope.Tick(0.1f, 4);
	FPerceptionKnowledgeTestAccessor::CleanupNoises(*Subsystem);
	TestEqual(
		TEXT("Expired correlation entries are cleaned"),
		FPerceptionKnowledgeTestAccessor::GetPendingNoiseCount(*Subsystem),
		0);
	Scope.Tick(0.1f, 5);
	TestEqual(TEXT("Recent Event Memory expires by timer"), NearListener->GetRecentEvents().Num(), 0);
	FPerceptionKnowledgeTestAccessor::CleanupRecentEvents(*NearListener);
	TestFalse(
		TEXT("Batched cleanup expires the Hearing relationship with recent event memory"),
		NearListener->IsEntityCurrentlyPerceived(Source->GetEntityId(), HearingTag()));

	FString RuntimeProfileError;
	TestTrue(
		TEXT("Far listener Hearing range can expand at runtime"),
		FarProfile->SetHearingRange(6000.0f, RuntimeProfileError));
	Request.MaxRange = 6000.0f;
	TestTrue(
		TEXT("Semantic noise after runtime Hearing expansion uses native Hearing"),
		Source->EmitSemanticNoise(Request).IsSuccess());
	Scope.Tick(0.05f, 4);
	TestEqual(
		TEXT("Far listener receives noise inside its expanded runtime Hearing range"),
		FarListener->GetRecentEvents().Num(),
		1);

	Settings->SemanticNoiseCorrelationLifetime = OriginalCorrelationLifetime;
	Settings->SemanticNoiseCleanupInterval = OriginalCleanupInterval;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPerceptionKnowledgeDebugFrameTest,
	"PerceptionKnowledge.Debug.FrameGatesColorsAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPerceptionKnowledgeDebugFrameTest::RunTest(const FString& Parameters)
{
	using namespace UE::PerceptionKnowledge::Tests;
	FScopedTestWorld Scope(TEXT("PerceptionKnowledgeDebugWorld"));
	if (!TestNotNull(TEXT("Transient debug world"), Scope.World))
	{
		return false;
	}

	AActor* ListenerOwner =
		SpawnActorWithBounds(*Scope.World, TEXT("DebugListener"), FVector::ZeroVector);
	UPerceptionKnowledgeProfile* Profile = MakeProfile(*ListenerOwner);
	UPerceptionKnowledgeListenerComponent* Listener =
		AddListener(*ListenerOwner, *Profile);
	AActor* SourceOwner =
		SpawnActorWithBounds(*Scope.World, TEXT("DebugSource"), FVector(250.0, 0.0, 0.0));
	UPerceptionKnowledgeSourceComponent* Source = AddSource(*SourceOwner);
	if (!TestNotNull(TEXT("Debug Listener"), Listener)
		|| !TestNotNull(TEXT("Debug Source"), Source))
	{
		return false;
	}

	IConsoleVariable* DebugCVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("PerceptionKnowledge.Debug"));
	if (!TestNotNull(TEXT("Global debug CVar"), DebugCVar))
	{
		return false;
	}
	const int32 OriginalDebugValue = DebugCVar->GetInt();

	DebugCVar->Set(0, ECVF_SetByCode);
	Listener->SetDebugEnabled(true);
	FPerceptionKnowledgeDebugFrame Frame = Listener->BuildDebugFrame();
	TestFalse(TEXT("Global off prevents drawing"), Frame.bShouldDraw);
	TestFalse(TEXT("Global off prevents expensive frame construction"), Frame.bExpensiveDataBuilt);

	DebugCVar->Set(1, ECVF_SetByCode);
	Listener->SetDebugEnabled(false);
	Frame = Listener->BuildDebugFrame();
	TestFalse(TEXT("Local off prevents drawing"), Frame.bShouldDraw);
	TestFalse(TEXT("Local off prevents expensive frame construction"), Frame.bExpensiveDataBuilt);

	Listener->SetDebugEnabled(true);
	Frame = Listener->BuildDebugFrame();
	TestTrue(TEXT("Global AND Local enables frame"), Frame.bShouldDraw);
	TestTrue(TEXT("Enabled debug builds value frame"), Frame.bExpensiveDataBuilt);
	TestTrue(TEXT("Listener frame contains viewpoint"), Frame.bHasValidViewpoint);
	TestEqual(TEXT("Valid Listener/viewpoint is white"), Frame.ListenerColor, FColor::White);
	TestEqual(TEXT("Registered Source appears in debug frame"), Frame.Sources.Num(), 1);
	if (Frame.Sources.Num() == 1)
	{
		TestEqual(TEXT("Registered unperceived Source is blue"), Frame.Sources[0].Color, FColor::Blue);
		TestTrue(TEXT("Registered Source exposes non-zero bounds"), !Frame.Sources[0].BoundsExtent.IsNearlyZero());
	}

	FPerceptionKnowledgeTestAccessor::SetRelationship(
		*Listener,
		*Source,
		SightTag(),
		true);
	Frame = Listener->BuildDebugFrame();
	if (Frame.Sources.Num() == 1)
	{
		TestEqual(TEXT("Sight-perceived Source is cyan"), Frame.Sources[0].Color, FColor::Cyan);
	}

	Source->SetObservableState(SightTag(), FPerceptionKnowledgeValue::MakeBool(true));
	FPerceptionKnowledgeTestAccessor::Refresh(
		*Listener,
		*Source,
		SightTag(),
		false);
	FPerceptionKnowledgeTestAccessor::SetRelationship(
		*Listener,
		*Source,
		SightTag(),
		false);
	FPerceptionKnowledgeTestAccessor::SetRelationship(
		*Listener,
		*Source,
		HearingTag(),
		true);
	Frame = Listener->BuildDebugFrame();
	if (Frame.Sources.Num() == 1)
	{
		TestEqual(TEXT("Hearing-perceived Source is yellow"), Frame.Sources[0].Color, FColor::Yellow);
	}
	FPerceptionKnowledgeTestAccessor::SetRelationship(
		*Listener,
		*Source,
		HearingTag(),
		false);
	Frame = Listener->BuildDebugFrame();
	if (Frame.Sources.Num() == 1)
	{
		TestEqual(TEXT("Known but unperceived Source is gray"), Frame.Sources[0].Color, FColor::Silver);
	}

	FPerceptionKnowledgeTestAccessor::SetSemanticRegistered(*Source, false);
	Frame = Listener->BuildDebugFrame();
	if (Frame.Sources.Num() == 1)
	{
		TestEqual(TEXT("Invalid Source configuration is magenta"), Frame.Sources[0].Color, FColor::Magenta);
	}
	FPerceptionKnowledgeTestAccessor::SetSemanticRegistered(*Source, true);

	DebugCVar->Set(OriginalDebugValue, ECVF_SetByCode);
	return true;
}

#endif
