#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/WorldStateParticipantComponent.h"
#include "Engine/World.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Serialization/WorldStatePropertySerializer.h"
#include "Spawning/WorldStateSpawnStrategy.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Tests/WorldStateTestTypes.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"

namespace UE::WorldState::Tests
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

	/** Spawns a deterministically named fixture Actor. */
	AWorldStateTestActor* SpawnTestActor(UWorld& World, FName Name)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<AWorldStateTestActor>(AWorldStateTestActor::StaticClass(), FTransform::Identity, Parameters);
	}

	/** Authors a property selection using the same source class and canonical signature as the editor picker. */
	void SelectProperty(
		UWorldStateParticipantComponent& Participant,
		UObject& Source,
		const FWorldStateCaptureSourceId& SourceId,
		FName PropertyName,
		EWorldStateReferenceRequirement ReferenceRequirement = EWorldStateReferenceRequirement::Optional)
	{
		FProperty* Property = FindFProperty<FProperty>(Source.GetClass(), PropertyName);
		if (!Property)
		{
			return;
		}
		FWorldStatePropertySelection& Selection = Participant.CapturedProperties.AddDefaulted_GetRef();
		Selection.CaptureSourceId = SourceId;
		Selection.PropertyName = PropertyName;
		Selection.ExpectedSourceClass = FSoftClassPath(Source.GetClass());
		Selection.ExpectedTypeSignature = FWorldStatePropertySerializer::BuildTypeSignature(Property);
		Selection.ReferenceRequirement = ReferenceRequirement;
	}

	/** Convenience selector for the fixture's authored data Component. */
	void SelectDataProperty(
		AWorldStateTestActor& Actor,
		FName PropertyName,
		EWorldStateReferenceRequirement ReferenceRequirement = EWorldStateReferenceRequirement::Optional)
	{
		SelectProperty(
			*Actor.Participant,
			*Actor.DataComponent,
			FWorldStateCaptureSourceId::Component(Actor.DataComponent->GetFName()),
			PropertyName,
			ReferenceRequirement);
	}

	/** Captures the required transactional complete baseline. */
	FWorldStateCaptureResult CaptureBaseline(UWorldStateSubsystem& Subsystem)
	{
		FWorldStateCaptureRequest Request;
		Request.Label = TEXT("AutomationBaseline");
		Request.Scope.Kind = EWorldStateRestoreScopeKind::CompleteSnapshot;
		return Subsystem.CaptureBaseline(Request);
	}

	/** Starts the transient world and explicitly dispatches BeginPlay for engine-version-stable tests. */
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

	/** Binds every participant delegate to the lifecycle observer fixture. */
	void BindParticipantObserver(UWorldStateParticipantComponent& Participant, UWorldStateTestObserver& Observer)
	{
		Participant.OnWorldStatePreCapture.AddDynamic(&Observer, &UWorldStateTestObserver::HandlePreCapture);
		Participant.OnWorldStateCaptured.AddDynamic(&Observer, &UWorldStateTestObserver::HandleCaptured);
		Participant.OnWorldStatePreRestore.AddDynamic(&Observer, &UWorldStateTestObserver::HandlePreRestore);
		Participant.OnWorldStatePropertiesRestored.AddDynamic(&Observer, &UWorldStateTestObserver::HandlePropertiesRestored);
		Participant.OnWorldStateRestored.AddDynamic(&Observer, &UWorldStateTestObserver::HandleRestored);
		Participant.OnWorldStateRestoreFailed.AddDynamic(&Observer, &UWorldStateTestObserver::HandleRestoreFailed);
	}

	/** External strategy fixture proving a valid custom strategy may intentionally change Actor path. */
	class FWorldStateDifferentPathSpawnStrategy final : public IWorldStateSpawnStrategy
	{
	public:
		virtual bool CanSpawn(const UWorld& World, const FWorldStateSpawnDescriptor& Descriptor, FString& OutError) const override
		{
			if (!Descriptor.ActorClass.Get())
			{
				OutError = TEXT("The captured test Actor class is not loaded.");
				return false;
			}
			return true;
		}

		virtual AActor* Spawn(UWorld& World, const FWorldStateSpawnDescriptor& Descriptor, FString& OutError) const override
		{
			if (!CanSpawn(World, Descriptor, OutError))
			{
				return nullptr;
			}
			FActorSpawnParameters Parameters;
			Parameters.Name = FName(*(Descriptor.ActorName.ToString() + TEXT("_DifferentPath")));
			Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
			Parameters.OverrideLevel = World.PersistentLevel;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World.SpawnActor<AActor>(Descriptor.ActorClass.Get(), Descriptor.Transform, Parameters);
		}
	};
}

/** Covers reflected values, recursive containers, reference filtering, ArrayDim and payload isolation. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStateSerializationTest,
	"WorldState.Runtime.Serialization.ValuesContainersReferencesAndIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStateSerializationTest::RunTest(const FString& Parameters)
{
	using namespace UE::WorldState::Tests;
	UWorldStateTestDataComponent* Data = NewObject<UWorldStateTestDataComponent>();
	TestNotNull(TEXT("Transient serialization source exists"), Data);
	if (!Data)
	{
		return false;
	}

	Data->bBoolValue = true;
	Data->IntegerValue = 314;
	Data->FixedIntegers[0] = 3;
	Data->FixedIntegers[1] = 1;
	Data->FixedIntegers[2] = 4;
	Data->FloatingValue = 9.25;
	Data->EnumValue = EWorldStateTestEnum::Third;
	Data->NameValue = TEXT("CapturedName");
	Data->StringValue = TEXT("CapturedString");
	Data->TextValue = FText::FromString(TEXT("CapturedText"));
	Data->VectorValue = FVector(1.0, 2.0, 3.0);
	Data->RotatorValue = FRotator(10.0, 20.0, 30.0);
	Data->TransformValue = FTransform(FRotator(4.0, 5.0, 6.0), FVector(7.0, 8.0, 9.0), FVector(1.5));
	Data->NativeValue.Nested.Count = 17;
	Data->NativeValue.Nested.Location = FVector(11.0, 12.0, 13.0);
	Data->NativeValue.Nested.Tags = { TEXT("One"), TEXT("Two") };
	Data->NativeValue.Numbers = { 5, 8, 13 };
	Data->NativeValue.Names = { TEXT("Alpha"), TEXT("Beta") };
	Data->NativeValue.Labels.Add(TEXT("Door"), TEXT("Open"));
	FWorldStateTestNestedValue Element;
	Element.Count = 22;
	Element.Tags = { TEXT("Nested") };
	Data->StructArray = { Element };
	Data->IntegerSet = { 2, 4, 8 };
	Data->StructMap.Add(TEXT("Entry"), Element);
	Data->SoftActor = TSoftObjectPtr<AActor>(FSoftObjectPath(TEXT("/Game/WorldStateTests/OptionalActor.OptionalActor")));
	Data->SoftActorClass = AWorldStateTestActor::StaticClass();

	const TArray<FName> SupportedProperties = {
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, bBoolValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, FixedIntegers),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, FloatingValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, EnumValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, NameValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, StringValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, TextValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, VectorValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, RotatorValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, TransformValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, NativeValue),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, StructArray),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerSet),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, StructMap),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, SoftActor),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, SoftActorClass)
	};

	TMap<FName, TArray<uint8>> Payloads;
	for (FName PropertyName : SupportedProperties)
	{
		FProperty* Property = FindFProperty<FProperty>(Data->GetClass(), PropertyName);
		const FWorldStatePropertyValidationResult Validation = FWorldStatePropertySerializer::Validate(Property);
		TestTrue(*FString::Printf(TEXT("%s validates"), *PropertyName.ToString()), Validation.IsValid());
		TestFalse(*FString::Printf(TEXT("%s has canonical type signature"), *PropertyName.ToString()), Validation.TypeSignature.IsEmpty());
		FString Error;
		TestTrue(*FString::Printf(TEXT("%s serializes"), *PropertyName.ToString()), FWorldStatePropertySerializer::Serialize(Property, Data, Payloads.FindOrAdd(PropertyName), Error));
		TestTrue(*FString::Printf(TEXT("%s payload is owned"), *PropertyName.ToString()), Payloads.FindChecked(PropertyName).Num() > 0);
	}

	const FWorldStateTestNativeValue CapturedNative = Data->NativeValue;
	Data->bBoolValue = false;
	Data->IntegerValue = -1;
	Data->FixedIntegers[0] = 0;
	Data->FixedIntegers[1] = 0;
	Data->FixedIntegers[2] = 0;
	Data->FloatingValue = -1.0;
	Data->EnumValue = EWorldStateTestEnum::First;
	Data->NameValue = NAME_None;
	Data->StringValue.Reset();
	Data->TextValue = FText::GetEmpty();
	Data->VectorValue = FVector::ZeroVector;
	Data->RotatorValue = FRotator::ZeroRotator;
	Data->TransformValue = FTransform::Identity;
	Data->NativeValue = FWorldStateTestNativeValue();
	Data->StructArray.Reset();
	Data->IntegerSet.Reset();
	Data->StructMap.Reset();
	Data->SoftActor.Reset();
	Data->SoftActorClass.Reset();

	for (FName PropertyName : SupportedProperties)
	{
		FProperty* Property = FindFProperty<FProperty>(Data->GetClass(), PropertyName);
		FString Error;
		TestTrue(*FString::Printf(TEXT("%s deserializes"), *PropertyName.ToString()), FWorldStatePropertySerializer::Deserialize(Property, Data, Payloads.FindChecked(PropertyName), Error));
	}

	TestTrue(TEXT("bool restored"), Data->bBoolValue);
	TestEqual(TEXT("integer restored"), Data->IntegerValue, 314);
	TestTrue(TEXT("fixed ArrayDim restored"), Data->FixedIntegers[0] == 3 && Data->FixedIntegers[1] == 1 && Data->FixedIntegers[2] == 4);
	TestEqual(TEXT("floating point restored"), Data->FloatingValue, 9.25);
	TestEqual(TEXT("enum restored"), Data->EnumValue, EWorldStateTestEnum::Third);
	TestEqual(TEXT("name restored"), Data->NameValue, FName(TEXT("CapturedName")));
	TestEqual(TEXT("string restored"), Data->StringValue, FString(TEXT("CapturedString")));
	TestTrue(TEXT("text restored"), Data->TextValue.EqualTo(FText::FromString(TEXT("CapturedText"))));
	TestEqual(TEXT("vector restored"), Data->VectorValue, FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("rotator restored"), Data->RotatorValue, FRotator(10.0, 20.0, 30.0));
	TestTrue(TEXT("transform restored"), Data->TransformValue.Equals(FTransform(FRotator(4.0, 5.0, 6.0), FVector(7.0, 8.0, 9.0), FVector(1.5))));
	TestTrue(TEXT("complete native struct restored"), Data->NativeValue == CapturedNative);
	TestTrue(TEXT("array of structs restored"), Data->StructArray.Num() == 1 && Data->StructArray[0].Count == 22);
	TestTrue(TEXT("set restored"), Data->IntegerSet.Contains(2) && Data->IntegerSet.Contains(4) && Data->IntegerSet.Contains(8));
	TestTrue(TEXT("map of structs restored"), Data->StructMap.Contains(TEXT("Entry")) && Data->StructMap.FindChecked(TEXT("Entry")).Count == 22);
	TestEqual(TEXT("soft object path preserved"), Data->SoftActor.ToSoftObjectPath(), FSoftObjectPath(TEXT("/Game/WorldStateTests/OptionalActor.OptionalActor")));
	TestEqual(TEXT("soft class path preserved"), Data->SoftActorClass.ToSoftObjectPath(), FSoftObjectPath(AWorldStateTestActor::StaticClass()));

	const FProperty* HardProperty = FindFProperty<FProperty>(Data->GetClass(), GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, HardObject));
	const FProperty* WeakProperty = FindFProperty<FProperty>(Data->GetClass(), GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, WeakObject));
	const FProperty* NestedHardProperty = FindFProperty<FProperty>(Data->GetClass(), GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, ForbiddenNested));
	const FProperty* NestedHardArrayProperty = FindFProperty<FProperty>(Data->GetClass(), GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, ForbiddenArray));
	TestEqual(TEXT("hard reference rejected"), FWorldStatePropertySerializer::Validate(HardProperty).Status, EWorldStatePropertyValidationStatus::HardObjectReferenceRejected);
	TestEqual(TEXT("weak reference rejected"), FWorldStatePropertySerializer::Validate(WeakProperty).Status, EWorldStatePropertyValidationStatus::WeakObjectReferenceRejected);
	const FWorldStatePropertyValidationResult NestedValidation = FWorldStatePropertySerializer::Validate(NestedHardProperty);
	TestFalse(TEXT("nested hard reference rejected"), NestedValidation.IsValid());
	TestTrue(TEXT("nested rejection identifies member path"), NestedValidation.NestedFailurePath.Contains(TEXT("HardReference")));
	const FWorldStatePropertyValidationResult ContainerValidation = FWorldStatePropertySerializer::Validate(NestedHardArrayProperty);
	TestFalse(TEXT("container with nested hard reference is rejected"), ContainerValidation.IsValid());
	TestTrue(TEXT("container rejection identifies nested member path"), ContainerValidation.NestedFailurePath.Contains(TEXT("HardReference")));

	TArray<FWorldStateDiscoveredSoftReference> References;
	FWorldStatePropertySerializer::CollectSoftReferences(FindFProperty<FProperty>(Data->GetClass(), GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, SoftActor)), Data, References);
	FWorldStatePropertySerializer::CollectSoftReferences(FindFProperty<FProperty>(Data->GetClass(), GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, SoftActorClass)), Data, References);
	TestEqual(TEXT("soft references discovered recursively"), References.Num(), 2);

	TArray<uint8> IndependentPayload = Payloads.FindChecked(GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, StringValue));
	Payloads.FindChecked(GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, StringValue))[0] ^= 0xff;
	TestNotEqual(TEXT("payload copies are independent"), IndependentPayload[0], Payloads.FindChecked(GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, StringValue))[0]);
	return true;
}

/** Covers baseline immutability, transform/value restore and callback observation boundaries. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStateBaselineRestoreTest,
	"WorldState.Runtime.Baseline.RestoreValuesTransformsCallbacksAndIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStateBaselineRestoreTest::RunTest(const FString& Parameters)
{
	using namespace UE::WorldState::Tests;
	FScopedTestWorld Scope(TEXT("WorldStateBaselineRestoreTestWorld"));
	TestNotNull(TEXT("Test world exists"), Scope.World);
	if (!Scope.World)
	{
		return false;
	}
	AWorldStateTestActor* Actor = SpawnTestActor(*Scope.World, TEXT("CapturedActor"));
	TestNotNull(TEXT("Participant actor exists"), Actor);
	if (!Actor)
	{
		return false;
	}

	Actor->OwnerValue = 41;
	Actor->DataComponent->bBoolValue = true;
	Actor->DataComponent->IntegerValue = 72;
	Actor->DataComponent->StringValue = TEXT("Baseline");
	Actor->DataComponent->UnselectedValue = 5;
	Actor->DataComponent->SoftActor = TSoftObjectPtr<AActor>(FSoftObjectPath(TEXT("/Game/WorldStateTests/Optional.Optional")));
	Actor->SetActorTransform(FTransform(FRotator(0.0, 35.0, 0.0), FVector(100.0, 200.0, 300.0)));
	Actor->TestAlternateParent->SetRelativeTransform(FTransform(FRotator(0.0, 15.0, 0.0), FVector(0.0, 5.0, 0.0)));
	Actor->TestPivot->AttachToComponent(Actor->TestAlternateParent, FAttachmentTransformRules::KeepRelativeTransform);
	Actor->TestPivot->SetRelativeTransform(FTransform(FRotator(0.0, 0.0, 65.0), FVector(10.0, 0.0, 0.0)));
	SelectProperty(*Actor->Participant, *Actor, FWorldStateCaptureSourceId::OwnerActor(), GET_MEMBER_NAME_CHECKED(AWorldStateTestActor, OwnerValue));
	SelectDataProperty(*Actor, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, bBoolValue));
	SelectDataProperty(*Actor, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
	SelectDataProperty(*Actor, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, StringValue));
	SelectDataProperty(*Actor, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, SoftActor));
	FWorldStateSceneComponentCaptureSelection& ParentSceneSelection = Actor->Participant->SceneComponentCaptureSelections.AddDefaulted_GetRef();
	ParentSceneSelection.CaptureSourceId = FWorldStateCaptureSourceId::Component(Actor->TestAlternateParent->GetFName());
	FWorldStateSceneComponentCaptureSelection& SceneSelection = Actor->Participant->SceneComponentCaptureSelections.AddDefaulted_GetRef();
	SceneSelection.CaptureSourceId = FWorldStateCaptureSourceId::Component(Actor->TestPivot->GetFName());

	UWorldStateTestObserver* Observer = NewObject<UWorldStateTestObserver>(Scope.World);
	Observer->WatchedActor = Actor;
	Observer->bSetIntegerDuringPreCapture = true;
	Observer->PreCaptureIntegerValue = 73;
	BindParticipantObserver(*Actor->Participant, *Observer);
	StartPlay(*Scope.World);
	UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
	TestNotNull(TEXT("World State subsystem exists"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}
	Observer->Subsystem = Subsystem;
	Observer->bRequestNestedRestore = true;
	TestTrue(TEXT("runtime participant gets an ID"), Actor->Participant->ParticipantId.IsValid());
	TestEqual(TEXT("participant registers once"), Subsystem->GetParticipantStateSummaries().Num(), 1);
	TestTrue(TEXT("registration finalizes"), Subsystem->FinalizeWorldStateRegistration().IsSuccess());

	const FWorldStateCaptureResult Baseline = CaptureBaseline(*Subsystem);
	TestTrue(TEXT("baseline capture succeeds"), Baseline.IsSuccess());
	TestEqual(TEXT("PreCapture happens once"), Observer->PreCaptureCount, 1);
	TestEqual(TEXT("Captured happens once"), Observer->CapturedCount, 1);
	TestEqual(TEXT("PreCapture mutation is serialized into the baseline"), Actor->DataComponent->IntegerValue, 73);
	TestTrue(TEXT("baseline exists"), Subsystem->HasBaseline());
	FWorldStateSnapshotSummary Summary;
	TestTrue(TEXT("baseline summary is queryable"), Subsystem->GetSnapshotSummary(Baseline.SnapshotId, Summary));
	TestTrue(TEXT("summary identifies baseline"), Summary.bBaseline);
	TestTrue(TEXT("summary reports owned payload bytes"), Summary.PayloadBytes > 0);

	const FWorldStateCaptureResult RejectedOverwrite = CaptureBaseline(*Subsystem);
	TestEqual(TEXT("valid baseline cannot be overwritten"), RejectedOverwrite.Status, EWorldStateOperationStatus::RejectedInvalidRequest);

	Actor->OwnerValue = -1;
	Actor->DataComponent->bBoolValue = false;
	Actor->DataComponent->IntegerValue = -1;
	Actor->DataComponent->StringValue = TEXT("Mutated");
	Actor->DataComponent->UnselectedValue = 99;
	Actor->DataComponent->SoftActor.Reset();
	Actor->SetActorLocation(FVector(-100.0, -200.0, -300.0));
	Actor->TestAlternateParent->SetRelativeTransform(FTransform::Identity);
	Actor->TestPivot->AttachToComponent(Actor->TestRoot, FAttachmentTransformRules::KeepRelativeTransform);
	Actor->TestPivot->SetRelativeTransform(FTransform::Identity);

	int32 StartedCount = 0;
	int32 ScopeResolvedCount = 0;
	int32 CompletedCount = 0;
	int32 FailedCount = 0;
	EWorldStateOperationStatus GlobalNestedRestoreStatus = EWorldStateOperationStatus::Success;
	bool bStartedObservedPreMutationState = false;
	bool bScopeResolvedObservedPreMutationState = false;
	bool bCompletedObservedFinalState = false;
	FWorldStateRestoreSessionId StartedSession;
	Subsystem->OnRestoreStartedNative().AddLambda([&](const FWorldStateRestoreLifecycleContext& Context)
	{
		++StartedCount;
		StartedSession = Context.RestoreSessionId;
		bStartedObservedPreMutationState = Actor->DataComponent->IntegerValue == -1 && Actor->GetActorLocation().Equals(FVector(-100.0, -200.0, -300.0));
		GlobalNestedRestoreStatus = Subsystem->RestoreBaseline(FWorldStateRestoreRequest()).Status;
	});
	Subsystem->OnRestoreScopeResolvedNative().AddLambda([&](const FWorldStateRestoreLifecycleContext& Context)
	{
		++ScopeResolvedCount;
		bScopeResolvedObservedPreMutationState = Actor->DataComponent->IntegerValue == -1 && Actor->GetActorLocation().Equals(FVector(-100.0, -200.0, -300.0));
	});
	Subsystem->OnRestoreCompletedNative().AddLambda([&](const FWorldStateRestoreResult& Result)
	{
		++CompletedCount;
		bCompletedObservedFinalState = Actor->DataComponent->IntegerValue == 73 && Observer->RestoredCount == 1;
	});
	Subsystem->OnRestoreFailedNative().AddLambda([&](const FWorldStateRestoreResult& Result) { ++FailedCount; });
	FWorldStateRestoreRequest MissingSnapshotRequest;
	MissingSnapshotRequest.SnapshotId = FWorldStateSnapshotId::NewId();
	const FWorldStateRestoreResult MissingSnapshotRestore = Subsystem->RestoreSnapshot(MissingSnapshotRequest);
	TestEqual(TEXT("missing snapshot is rejected before acceptance"), MissingSnapshotRestore.Status, EWorldStateOperationStatus::RejectedInvalidRequest);
	TestEqual(TEXT("rejected request emits no Started event"), StartedCount, 0);

	const FWorldStateRestoreResult Restore = Subsystem->RestoreBaseline(FWorldStateRestoreRequest());
	TestTrue(TEXT("baseline restore succeeds with optional-reference warning"), Restore.IsSuccess());
	TestEqual(TEXT("accepted restore starts once"), StartedCount, 1);
	TestEqual(TEXT("scope resolves once"), ScopeResolvedCount, 1);
	TestEqual(TEXT("successful restore completes once"), CompletedCount, 1);
	TestEqual(TEXT("successful restore never fails globally"), FailedCount, 0);
	TestTrue(TEXT("same session ID is retained"), StartedSession == Restore.RestoreSessionId);
	TestTrue(TEXT("Started is broadcast before world mutation"), bStartedObservedPreMutationState);
	TestTrue(TEXT("ScopeResolved is broadcast before existence or structural mutation"), bScopeResolvedObservedPreMutationState);
	TestTrue(TEXT("Completed observes final values and participant callbacks"), bCompletedObservedFinalState);
	TestEqual(TEXT("nested global restore is rejected busy"), GlobalNestedRestoreStatus, EWorldStateOperationStatus::RejectedBusy);
	TestEqual(TEXT("nested participant restore is rejected busy"), Observer->NestedRestoreStatus, EWorldStateOperationStatus::RejectedBusy);
	TestEqual(TEXT("participant PreRestore happens once"), Observer->PreRestoreCount, 1);
	TestEqual(TEXT("participant PropertiesRestored happens once"), Observer->PropertiesRestoredCount, 1);
	TestEqual(TEXT("participant Restored happens once"), Observer->RestoredCount, 1);
	TestEqual(TEXT("PreRestore observes the pre-mutation value"), Observer->IntegerSeenAtPreRestore, -1);
	TestTrue(TEXT("PreRestore observes the pre-mutation Actor transform"), Observer->LocationSeenAtPreRestore.Equals(FVector(-100.0, -200.0, -300.0)));
	TestEqual(TEXT("PropertiesRestored observes the applied value"), Observer->IntegerSeenAtPropertiesRestored, 73);
	TestEqual(TEXT("Restored observes the applied value"), Observer->IntegerSeenAtRestored, 73);
	TestTrue(TEXT("Restored observes the applied Actor transform"), Observer->LocationSeenAtRestored.Equals(FVector(100.0, 200.0, 300.0)));
	TestEqual(TEXT("owner property restored"), Actor->OwnerValue, 41);
	TestTrue(TEXT("bool restored"), Actor->DataComponent->bBoolValue);
	TestEqual(TEXT("component integer restored"), Actor->DataComponent->IntegerValue, 73);
	TestEqual(TEXT("component string restored"), Actor->DataComponent->StringValue, FString(TEXT("Baseline")));
	TestEqual(TEXT("unselected property remains mutated"), Actor->DataComponent->UnselectedValue, 99);
	TestTrue(TEXT("Actor transform restored before completion"), Actor->GetActorTransform().Equals(FTransform(FRotator(0.0, 35.0, 0.0), FVector(100.0, 200.0, 300.0)), 0.01));
	TestTrue(TEXT("selected parent Scene Component transform restored"), Actor->TestAlternateParent->GetRelativeTransform().Equals(FTransform(FRotator(0.0, 15.0, 0.0), FVector(0.0, 5.0, 0.0)), 0.01));
	TestTrue(TEXT("non-root relative transform restored"), Actor->TestPivot->GetRelativeTransform().Equals(FTransform(FRotator(0.0, 0.0, 65.0), FVector(10.0, 0.0, 0.0)), 0.01));
	TestTrue(TEXT("changed parent without attachment capture remains unchanged"), Actor->TestPivot->GetAttachParent() == Actor->TestRoot);
	TestTrue(TEXT("changed parent without attachment capture produces a warning"), Restore.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue)
	{
		return Issue.Code == TEXT("SceneParentChanged") && Issue.Severity == EWorldStateIssueSeverity::Warning;
	}));
	TestEqual(TEXT("optional soft path restored"), Actor->DataComponent->SoftActor.ToSoftObjectPath(), FSoftObjectPath(TEXT("/Game/WorldStateTests/Optional.Optional")));
	TestTrue(TEXT("optional unresolved soft path is observable"), Restore.ReferenceResults.ContainsByPredicate([](const FWorldStateReferenceResolutionResult& Item)
	{
		return Item.Status == EWorldStateReferenceResolutionStatus::UnresolvedAllowed;
	}));

	Actor->DataComponent->IntegerValue = -9;
	Actor->Participant->MarkParticipantDirty();
	TArray<FWorldStateParticipantSummary> ParticipantSummaries = Subsystem->GetParticipantStateSummaries();
	const FWorldStateParticipantSummary* DirtySummary = ParticipantSummaries.FindByPredicate([Actor](const FWorldStateParticipantSummary& Item)
	{
		return Item.ParticipantId == Actor->Participant->ParticipantId;
	});
	TestTrue(TEXT("participant dirty state is queryable"), DirtySummary && DirtySummary->bDirty);
	FWorldStateRestoreRequest DirtyRestoreRequest;
	DirtyRestoreRequest.Scope.Kind = EWorldStateRestoreScopeKind::DirtyParticipants;
	const FWorldStateRestoreResult DirtyRestore = Subsystem->RestoreBaseline(DirtyRestoreRequest);
	TestTrue(TEXT("dirty participant scope restores successfully"), DirtyRestore.IsSuccess());
	TestEqual(TEXT("dirty participant value restored"), Actor->DataComponent->IntegerValue, 73);
	ParticipantSummaries = Subsystem->GetParticipantStateSummaries();
	DirtySummary = ParticipantSummaries.FindByPredicate([Actor](const FWorldStateParticipantSummary& Item)
	{
		return Item.ParticipantId == Actor->Participant->ParticipantId;
	});
	TestTrue(TEXT("successful restore clears participant dirty state"), DirtySummary && !DirtySummary->bDirty);

	Actor->DataComponent->IntegerValue = 88;
	FWorldStateCaptureRequest RuntimeRequest;
	RuntimeRequest.Label = TEXT("RuntimeOne");
	const FWorldStateCaptureResult RuntimeSnapshot = Subsystem->CaptureRuntimeSnapshot(RuntimeRequest);
	TestTrue(TEXT("runtime snapshot captures independently"), RuntimeSnapshot.IsSuccess());
	FWorldStateCaptureRequest InvalidRuntimeRequest;
	InvalidRuntimeRequest.Label = TEXT("InvalidRuntimeCapture");
	InvalidRuntimeRequest.Scope.Kind = EWorldStateRestoreScopeKind::ParticipantIds;
	InvalidRuntimeRequest.Scope.ParticipantIds = { FWorldStateParticipantId::NewId() };
	const FWorldStateCaptureResult InvalidRuntimeSnapshot = Subsystem->CaptureRuntimeSnapshot(InvalidRuntimeRequest);
	TestFalse(TEXT("invalid runtime capture is rejected"), InvalidRuntimeSnapshot.IsSuccess());
	TestTrue(TEXT("failed capture does not replace an existing runtime snapshot"), Subsystem->GetSnapshotSummary(RuntimeSnapshot.SnapshotId, Summary));
	Actor->DataComponent->IntegerValue = 101;
	const FWorldStateRestoreResult BaselineAgain = Subsystem->RestoreBaseline(FWorldStateRestoreRequest());
	TestTrue(TEXT("baseline remains restorable after runtime snapshot"), BaselineAgain.IsSuccess());
	TestEqual(TEXT("runtime snapshot does not mutate baseline"), Actor->DataComponent->IntegerValue, 73);
	return true;
}

/** Covers template/duplicate identity rules, registry duplicates and authored validation failures. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStateIdentityValidationTest,
	"WorldState.Runtime.Identity.RegistrationDuplicatesAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStateIdentityValidationTest::RunTest(const FString& Parameters)
{
	using namespace UE::WorldState::Tests;
	const UWorldStateParticipantComponent* Template = GetDefault<UWorldStateParticipantComponent>();
	TestFalse(TEXT("component template has no participant ID"), Template->ParticipantId.IsValid());

	FScopedTestWorld Scope(TEXT("WorldStateIdentityTestWorld"));
	if (!TestNotNull(TEXT("Test world exists"), Scope.World))
	{
		return false;
	}
	AWorldStateTestActor* First = SpawnTestActor(*Scope.World, TEXT("FirstParticipant"));
	AWorldStateTestActor* Second = SpawnTestActor(*Scope.World, TEXT("SecondParticipant"));
	if (!TestNotNull(TEXT("First actor exists"), First) || !TestNotNull(TEXT("Second actor exists"), Second))
	{
		return false;
	}
	TestTrue(TEXT("first instance ID valid before play"), First->Participant->ParticipantId.IsValid());
	TestTrue(TEXT("second instance ID valid before play"), Second->Participant->ParticipantId.IsValid());
	TestNotEqual(TEXT("normal instances receive distinct IDs"), First->Participant->ParticipantId.ToString(), Second->Participant->ParticipantId.ToString());
	UWorldStateParticipantComponent* NormalDuplicate = DuplicateObject<UWorldStateParticipantComponent>(First->Participant, First);
	TestNotNull(TEXT("normal component duplication succeeds"), NormalDuplicate);
	TestTrue(TEXT("normal component duplication regenerates ID"), NormalDuplicate && NormalDuplicate->ParticipantId != First->Participant->ParticipantId);
	UWorldStateParticipantComponent* PieDuplicate = Cast<UWorldStateParticipantComponent>(StaticDuplicateObject(
		First->Participant,
		First,
		TEXT("WorldStateParticipantPIEDuplicate"),
		RF_AllFlags,
		nullptr,
		EDuplicateMode::PIE));
	TestNotNull(TEXT("PIE component duplication succeeds"), PieDuplicate);
	TestTrue(TEXT("PIE component duplication preserves ID"), PieDuplicate && PieDuplicate->ParticipantId == First->Participant->ParticipantId);
	Second->Participant->ParticipantId = First->Participant->ParticipantId;
	SelectDataProperty(*First, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
	First->Participant->CapturedProperties[0].ExpectedSourceClass = FSoftClassPath(AActor::StaticClass());
	const FWorldStatePropertySelection DuplicateSelection = First->Participant->CapturedProperties[0];
	First->Participant->CapturedProperties.Add(DuplicateSelection);
	UWorldStateTestDataComponent* RuntimeInstanceSource = NewObject<UWorldStateTestDataComponent>(First, TEXT("RuntimeInstanceSource"));
	First->AddInstanceComponent(RuntimeInstanceSource);
	RuntimeInstanceSource->RegisterComponent();
	SelectProperty(
		*First->Participant,
		*RuntimeInstanceSource,
		FWorldStateCaptureSourceId::Component(RuntimeInstanceSource->GetFName()),
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
	SelectProperty(*First->Participant, *First, FWorldStateCaptureSourceId::OwnerActor(), GET_MEMBER_NAME_CHECKED(AWorldStateTestActor, OwnerValue));
	First->Participant->CapturedProperties.Last().ExpectedTypeSignature = TEXT("Stale.Type.Signature");
	FWorldStatePropertySelection& Missing = First->Participant->CapturedProperties.AddDefaulted_GetRef();
	Missing.CaptureSourceId = FWorldStateCaptureSourceId::Component(TEXT("MissingComponent"));
	Missing.PropertyName = TEXT("MissingProperty");
	FWorldStateSceneComponentCaptureSelection& RootConflict = First->Participant->SceneComponentCaptureSelections.AddDefaulted_GetRef();
	RootConflict.CaptureSourceId = FWorldStateCaptureSourceId::Component(First->TestRoot->GetFName());
	FWorldStateSceneComponentCaptureSelection& MissingScene = First->Participant->SceneComponentCaptureSelections.AddDefaulted_GetRef();
	MissingScene.CaptureSourceId = FWorldStateCaptureSourceId::Component(TEXT("RemovedSceneComponent"));

	AddExpectedError(TEXT("Duplicate ParticipantId"), EAutomationExpectedErrorFlags::Contains, 1);
	StartPlay(*Scope.World);
	UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
	if (!TestNotNull(TEXT("Subsystem exists"), Subsystem))
	{
		return false;
	}
	TestEqual(TEXT("duplicate participant ID is rejected by registry"), Subsystem->GetParticipantStateSummaries().Num(), 1);
	const FWorldStateOperationResult Validation = First->Participant->ValidateCapturedProperties();
	TestFalse(TEXT("invalid selections are rejected"), Validation.IsSuccess());
	TestTrue(TEXT("duplicate property is structured"), Validation.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Code == TEXT("DuplicateProperty"); }));
	TestTrue(TEXT("missing source is structured"), Validation.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Code == TEXT("MissingCaptureSource"); }));
	TestTrue(TEXT("expected source class mismatch is structured"), Validation.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Code == TEXT("SourceClassMismatch"); }));
	TestTrue(TEXT("incompatible property type signature is structured"), Validation.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Code == TEXT("TypeSignatureMismatch"); }));
	TestTrue(TEXT("runtime instance Component source is rejected without reconstruction contract"), Validation.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Code == TEXT("UnstableRuntimeCaptureSource"); }));
	TestTrue(TEXT("root transform authority conflict is structured"), Validation.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Code == TEXT("CompetingTransformAuthority"); }));
	TestTrue(TEXT("missing selected Scene Component is structured"), Validation.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Code == TEXT("MissingSceneComponent"); }));
	TestFalse(TEXT("invalid registry cannot finalize"), Subsystem->FinalizeWorldStateRegistration().IsSuccess());
	return true;
}

/** Covers phase/dependency ordering, partial-scope expansion and cycle rejection before mutation. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStateDependencyTest,
	"WorldState.Runtime.Restore.DependenciesPartialScopeAndCyclePreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStateDependencyTest::RunTest(const FString& Parameters)
{
	using namespace UE::WorldState::Tests;
	{
		FScopedTestWorld Scope(TEXT("WorldStateDependencyExpansionWorld"));
		AWorldStateTestActor* Prerequisite = Scope.World ? SpawnTestActor(*Scope.World, TEXT("Prerequisite")) : nullptr;
		AWorldStateTestActor* Dependent = Scope.World ? SpawnTestActor(*Scope.World, TEXT("Dependent")) : nullptr;
		if (!Prerequisite || !Dependent)
		{
			AddError(TEXT("Dependency test actors could not be created."));
			return false;
		}
		Prerequisite->DataComponent->IntegerValue = 10;
		Dependent->DataComponent->IntegerValue = 20;
		SelectDataProperty(*Prerequisite, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
		SelectDataProperty(*Dependent, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
		TArray<FWorldStateParticipantId> RestoreCallbackOrder;
		UWorldStateTestObserver* PrerequisiteObserver = NewObject<UWorldStateTestObserver>(Scope.World);
		UWorldStateTestObserver* DependentObserver = NewObject<UWorldStateTestObserver>(Scope.World);
		PrerequisiteObserver->RestoreOrderLog = &RestoreCallbackOrder;
		DependentObserver->RestoreOrderLog = &RestoreCallbackOrder;
		BindParticipantObserver(*Prerequisite->Participant, *PrerequisiteObserver);
		BindParticipantObserver(*Dependent->Participant, *DependentObserver);
		StartPlay(*Scope.World);
		Dependent->Participant->RestoreAfter.Add(Prerequisite->Participant->ParticipantId);
		UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
		TestTrue(TEXT("dependency registration finalizes"), Subsystem && Subsystem->FinalizeWorldStateRegistration().IsSuccess());
		const FWorldStateCaptureResult Baseline = Subsystem ? CaptureBaseline(*Subsystem) : FWorldStateCaptureResult();
		TestTrue(TEXT("dependency baseline captured"), Baseline.IsSuccess());
		Prerequisite->DataComponent->IntegerValue = -10;
		Dependent->DataComponent->IntegerValue = -20;
		FWorldStateRestoreRequest ExactRequest;
		ExactRequest.SnapshotId = Baseline.SnapshotId;
		ExactRequest.Scope.Kind = EWorldStateRestoreScopeKind::ParticipantIds;
		ExactRequest.Scope.ParticipantIds = { Dependent->Participant->ParticipantId };
		ExactRequest.DependencyExpansion = EWorldStateDependencyExpansionPolicy::ExactSelection;
		const FWorldStateRestoreResult ExactRestore = Subsystem->RestoreSnapshot(ExactRequest);
		TestEqual(TEXT("incomplete exact scope fails preflight"), ExactRestore.Status, EWorldStateOperationStatus::PreflightFailed);
		TestFalse(TEXT("incomplete exact scope does not mutate world"), ExactRestore.bMutationBegan);
		TestEqual(TEXT("prerequisite remains mutated after rejected exact scope"), Prerequisite->DataComponent->IntegerValue, -10);
		TestEqual(TEXT("dependent remains mutated after rejected exact scope"), Dependent->DataComponent->IntegerValue, -20);
		FWorldStateRestoreRequest Request;
		Request.SnapshotId = Baseline.SnapshotId;
		Request.Scope.Kind = EWorldStateRestoreScopeKind::ParticipantIds;
		Request.Scope.ParticipantIds = { Dependent->Participant->ParticipantId };
		const FWorldStateRestoreResult Restore = Subsystem->RestoreSnapshot(Request);
		TestTrue(TEXT("partial restore with dependency succeeds"), Restore.IsSuccess());
		TestEqual(TEXT("requested count remains exact"), Restore.RequestedParticipantCount, 1);
		TestEqual(TEXT("dependency expansion restores two"), Restore.RestoredParticipantCount, 2);
		TestEqual(TEXT("prerequisite value restored"), Prerequisite->DataComponent->IntegerValue, 10);
		TestEqual(TEXT("dependent value restored"), Dependent->DataComponent->IntegerValue, 20);
		TestTrue(TEXT("dependency callbacks complete in deterministic prerequisite-first order"),
			RestoreCallbackOrder.Num() == 2 &&
			RestoreCallbackOrder[0] == Prerequisite->Participant->ParticipantId &&
			RestoreCallbackOrder[1] == Dependent->Participant->ParticipantId);
	}

	{
		FScopedTestWorld Scope(TEXT("WorldStateDependencyCycleWorld"));
		AWorldStateTestActor* First = Scope.World ? SpawnTestActor(*Scope.World, TEXT("CycleFirst")) : nullptr;
		AWorldStateTestActor* Second = Scope.World ? SpawnTestActor(*Scope.World, TEXT("CycleSecond")) : nullptr;
		if (!First || !Second)
		{
			AddError(TEXT("Cycle test actors could not be created."));
			return false;
		}
		SelectDataProperty(*First, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
		SelectDataProperty(*Second, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
		StartPlay(*Scope.World);
		First->Participant->RestoreAfter.Add(Second->Participant->ParticipantId);
		Second->Participant->RestoreAfter.Add(First->Participant->ParticipantId);
		UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
		TestTrue(TEXT("cycle participant validation still finalizes"), Subsystem && Subsystem->FinalizeWorldStateRegistration().IsSuccess());
		const FWorldStateCaptureResult Baseline = Subsystem ? CaptureBaseline(*Subsystem) : FWorldStateCaptureResult();
		TestTrue(TEXT("cycle snapshot can be captured"), Baseline.IsSuccess());
		int32 FailedCount = 0;
		Subsystem->OnRestoreFailedNative().AddLambda([&](const FWorldStateRestoreResult& Result) { ++FailedCount; });
		const FWorldStateRestoreResult Restore = Subsystem->RestoreBaseline(FWorldStateRestoreRequest());
		TestEqual(TEXT("dependency cycle fails preflight"), Restore.Status, EWorldStateOperationStatus::PreflightFailed);
		TestEqual(TEXT("failed terminal event occurs once"), FailedCount, 1);
		TestFalse(TEXT("preflight cycle does not mutate world"), Restore.bMutationBegan);
	}
	return true;
}

/** Covers FailFast and ContinueBestEffort behavior using deterministic payload corruption. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStateRestoreFailurePolicyTest,
	"WorldState.Runtime.Restore.PropertyFailurePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStateRestoreFailurePolicyTest::RunTest(const FString& Parameters)
{
	using namespace UE::WorldState::Tests;
	FScopedTestWorld Scope(TEXT("WorldStatePropertyFailurePolicyWorld"));
	AWorldStateTestActor* Corrupted = Scope.World ? SpawnTestActor(*Scope.World, TEXT("CorruptedPayloadParticipant")) : nullptr;
	AWorldStateTestActor* Healthy = Scope.World ? SpawnTestActor(*Scope.World, TEXT("HealthyPayloadParticipant")) : nullptr;
	if (!Corrupted || !Healthy)
	{
		AddError(TEXT("Property failure-policy test Actors could not be created."));
		return false;
	}
	Corrupted->DataComponent->IntegerValue = 111;
	Healthy->DataComponent->IntegerValue = 222;
	SelectDataProperty(*Corrupted, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
	SelectDataProperty(*Healthy, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
	StartPlay(*Scope.World);
	UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
	TestTrue(TEXT("failure-policy registration finalizes"), Subsystem && Subsystem->FinalizeWorldStateRegistration().IsSuccess());
	TestTrue(TEXT("failure-policy baseline captures"), Subsystem && CaptureBaseline(*Subsystem).IsSuccess());
	FWorldStateCaptureRequest RuntimeRequest;
	RuntimeRequest.Label = TEXT("FailurePolicySnapshot");
	const FWorldStateCaptureResult Snapshot = Subsystem ? Subsystem->CaptureRuntimeSnapshot(RuntimeRequest) : FWorldStateCaptureResult();
	TestTrue(TEXT("failure-policy runtime snapshot captures"), Snapshot.IsSuccess());
	TestTrue(TEXT("test seam corrupts only the selected immutable payload copy"), Subsystem && Subsystem->CorruptSnapshotPropertyPayloadForTests(
		Snapshot.SnapshotId,
		Corrupted->Participant->ParticipantId,
		GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue)));
	Corrupted->DataComponent->IntegerValue = -111;
	Healthy->DataComponent->IntegerValue = -222;
	FWorldStateRestoreRequest RestoreRequest;
	RestoreRequest.SnapshotId = Snapshot.SnapshotId;
	RestoreRequest.FailurePolicy = EWorldStateRestoreFailurePolicy::ContinueBestEffort;
	const FWorldStateRestoreResult Restore = Subsystem->RestoreSnapshot(RestoreRequest);
	TestEqual(TEXT("best-effort property error remains an observable terminal failure"), Restore.Status, EWorldStateOperationStatus::RestoreFailed);
	TestEqual(TEXT("best-effort property error identifies the Properties stage"), Restore.FailureStage, EWorldStateRestoreStage::Properties);
	TestTrue(TEXT("best-effort property error records partial world mutation"), Restore.bMutationBegan && Restore.bPartiallyRestored);
	TestEqual(TEXT("archive failure does not write a default value"), Corrupted->DataComponent->IntegerValue, -111);
	TestEqual(TEXT("ContinueBestEffort restores the independent healthy participant"), Healthy->DataComponent->IntegerValue, 222);
	TestEqual(TEXT("only the healthy participant reaches successful completion"), Restore.RestoredParticipantCount, 1);
	TestTrue(TEXT("corrupted property result remains structured"), Restore.ParticipantResults.ContainsByPredicate([Corrupted](const FWorldStateParticipantResult& ParticipantResult)
	{
		return ParticipantResult.ParticipantId == Corrupted->Participant->ParticipantId && ParticipantResult.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue)
		{
			return Issue.Code == TEXT("PropertyRestoreFailed") && Issue.Severity == EWorldStateIssueSeverity::Error;
		});
	}));
	return true;
}

/** Covers runtime respawn/destroy, attachment structure, callbacks, registry mutation and teardown. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStateExistenceAttachmentTest,
	"WorldState.Runtime.Existence.RespawnDestroyAttachmentReferencesAndTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStateExistenceAttachmentTest::RunTest(const FString& Parameters)
{
	using namespace UE::WorldState::Tests;
	TWeakObjectPtr<UWorldStateSubsystem> TeardownSubsystem;
	{
		FScopedTestWorld Scope(TEXT("WorldStateExistenceWorld"));
		AWorldStateTestActor* Parent = Scope.World ? SpawnTestActor(*Scope.World, TEXT("AttachmentParent")) : nullptr;
		AWorldStateTestActor* Managed = Scope.World ? SpawnTestActor(*Scope.World, TEXT("ManagedRuntimeActor")) : nullptr;
		if (!Parent || !Managed)
		{
			AddError(TEXT("Existence test actors could not be created."));
			return false;
		}
		Managed->OwnerValue = 707;
		Managed->DataComponent->IntegerValue = 808;
		Managed->Participant->ExistencePolicy = EWorldStateExistencePolicy::RespawnAndDestroy;
		Managed->Participant->bCaptureAttachment = true;
		Managed->Participant->RestoreAfter.Add(Parent->Participant->ParticipantId);
		Managed->AttachToActor(Parent, FAttachmentTransformRules::KeepRelativeTransform, TEXT("ManagedSocket"));
		Managed->TestPivot->SetRelativeTransform(FTransform(FRotator(0.0, 0.0, 42.0), FVector(14.0, 0.0, 0.0)));
		SelectProperty(*Managed->Participant, *Managed, FWorldStateCaptureSourceId::OwnerActor(), GET_MEMBER_NAME_CHECKED(AWorldStateTestActor, OwnerValue));
		SelectDataProperty(*Managed, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, IntegerValue));
		FWorldStateSceneComponentCaptureSelection& PivotSelection = Managed->Participant->SceneComponentCaptureSelections.AddDefaulted_GetRef();
		PivotSelection.CaptureSourceId = FWorldStateCaptureSourceId::Component(Managed->TestPivot->GetFName());
		Parent->DataComponent->SoftActor = Managed;
		SelectDataProperty(*Parent, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, SoftActor), EWorldStateReferenceRequirement::Required);

		StartPlay(*Scope.World);
		UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
		TeardownSubsystem = Subsystem;
		TestTrue(TEXT("existence registration finalizes"), Subsystem && Subsystem->FinalizeWorldStateRegistration().IsSuccess());
		const FWorldStateCaptureResult Baseline = Subsystem ? CaptureBaseline(*Subsystem) : FWorldStateCaptureResult();
		TestTrue(TEXT("existence baseline captures"), Baseline.IsSuccess());
		const FWorldStateParticipantId ManagedId = Managed->Participant->ParticipantId;
		const FSoftObjectPath ManagedPath(Managed);

		AWorldStateTestActor* Extra = SpawnTestActor(*Scope.World, TEXT("SnapshotAbsentManagedActor"));
		TestNotNull(TEXT("snapshot-absent Actor spawned"), Extra);
		if (Extra)
		{
			if (!Extra->HasActorBegunPlay())
			{
				Extra->DispatchBeginPlay();
			}
			Extra->Participant->ExistencePolicy = EWorldStateExistencePolicy::RespawnAndDestroy;
			Extra->Participant->bCaptureExistence = true;
		}
		Parent->DataComponent->SoftActor.Reset();
		Managed->Destroy();
		Managed = nullptr;
		CollectGarbage(RF_NoFlags);
		TestEqual(TEXT("EndPlay unregisters destroyed participant symmetrically"), Subsystem->GetParticipantStateSummaries().Num(), Extra ? 2 : 1);

		const FWorldStateRestoreResult Restore = Subsystem->RestoreBaseline(FWorldStateRestoreRequest());
		TestTrue(TEXT("runtime Actor respawns and complete restore succeeds"), Restore.IsSuccess());
		AWorldStateTestActor* Recreated = nullptr;
		for (TActorIterator<AWorldStateTestActor> It(Scope.World); It; ++It)
		{
			if (It->Participant && It->Participant->ParticipantId == ManagedId)
			{
				Recreated = *It;
				break;
			}
		}
		TestNotNull(TEXT("missing managed Actor recreated"), Recreated);
		TestTrue(TEXT("recreated participant preserves ID"), Recreated && Recreated->Participant->ParticipantId == ManagedId);
		TestTrue(TEXT("default spawn preserves exact object path"), Recreated && FSoftObjectPath(Recreated) == ManagedPath);
		TestEqual(TEXT("captured value restored after respawn"), Recreated ? Recreated->OwnerValue : 0, 707);
		TestEqual(TEXT("authored Component source exists before its property restore"), Recreated ? Recreated->DataComponent->IntegerValue : 0, 808);
		TestTrue(TEXT("Actor attachment restored"), Recreated && Recreated->GetAttachParentActor() == Parent);
		TestTrue(TEXT("Scene Component relative transform restored after attachment"), Recreated && Recreated->TestPivot->GetRelativeTransform().Equals(FTransform(FRotator(0.0, 0.0, 42.0), FVector(14.0, 0.0, 0.0)), 0.01));
		TestTrue(TEXT("required soft Actor path resolves after managed respawn"), Parent->DataComponent->SoftActor.Get() == Recreated);
		TestTrue(TEXT("snapshot-absent managed Actor is destroyed from copied registry iteration"), !Extra || Extra->IsActorBeingDestroyed());
	}
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("world teardown releases its World State subsystem"), TeardownSubsystem.IsValid());

	{
		FScopedTestWorld Scope(TEXT("WorldStateMissingStrategyWorld"));
		AWorldStateTestActor* Actor = Scope.World ? SpawnTestActor(*Scope.World, TEXT("MissingStrategyActor")) : nullptr;
		if (!Actor)
		{
			AddError(TEXT("Missing-strategy test Actor could not be created."));
			return false;
		}
		Actor->Participant->ExistencePolicy = EWorldStateExistencePolicy::RespawnIfMissing;
		Actor->Participant->SpawnStrategyId = TEXT("WorldState.Tests.MissingStrategy");
		SelectProperty(*Actor->Participant, *Actor, FWorldStateCaptureSourceId::OwnerActor(), GET_MEMBER_NAME_CHECKED(AWorldStateTestActor, OwnerValue));
		StartPlay(*Scope.World);
		UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
		TestTrue(TEXT("missing-strategy registration finalizes"), Subsystem && Subsystem->FinalizeWorldStateRegistration().IsSuccess());
		const FWorldStateCaptureResult Baseline = Subsystem ? CaptureBaseline(*Subsystem) : FWorldStateCaptureResult();
		TestTrue(TEXT("missing-strategy baseline captures"), Baseline.IsSuccess());
		Actor->Destroy();
		Actor = nullptr;
		CollectGarbage(RF_NoFlags);
		int32 StartedCount = 0;
		int32 FailedCount = 0;
		Subsystem->OnRestoreStartedNative().AddLambda([&](const FWorldStateRestoreLifecycleContext&) { ++StartedCount; });
		Subsystem->OnRestoreFailedNative().AddLambda([&](const FWorldStateRestoreResult&) { ++FailedCount; });
		const FWorldStateRestoreResult Restore = Subsystem->RestoreBaseline(FWorldStateRestoreRequest());
		TestEqual(TEXT("missing required spawn strategy fails preflight"), Restore.Status, EWorldStateOperationStatus::PreflightFailed);
		TestFalse(TEXT("missing strategy preflight does not mutate"), Restore.bMutationBegan);
		TestEqual(TEXT("accepted missing-strategy request starts once"), StartedCount, 1);
		TestEqual(TEXT("accepted missing-strategy request fails once"), FailedCount, 1);
	}

	{
		FScopedTestWorld Scope(TEXT("WorldStateDifferentPathStrategyWorld"));
		AWorldStateTestActor* Referrer = Scope.World ? SpawnTestActor(*Scope.World, TEXT("DifferentPathReferrer")) : nullptr;
		AWorldStateTestActor* Managed = Scope.World ? SpawnTestActor(*Scope.World, TEXT("DifferentPathManaged")) : nullptr;
		if (!Referrer || !Managed)
		{
			AddError(TEXT("Different-path strategy test Actors could not be created."));
			return false;
		}
		const FName StrategyId(TEXT("WorldState.Tests.DifferentPath"));
		Managed->OwnerValue = 909;
		Managed->Participant->ExistencePolicy = EWorldStateExistencePolicy::RespawnIfMissing;
		Managed->Participant->SpawnStrategyId = StrategyId;
		SelectProperty(*Managed->Participant, *Managed, FWorldStateCaptureSourceId::OwnerActor(), GET_MEMBER_NAME_CHECKED(AWorldStateTestActor, OwnerValue));
		Referrer->DataComponent->SoftActor = Managed;
		SelectDataProperty(*Referrer, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, SoftActor));
		StartPlay(*Scope.World);
		UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
		TestTrue(TEXT("external different-path strategy registers"), Subsystem && Subsystem->RegisterSpawnStrategy(StrategyId, MakeShared<FWorldStateDifferentPathSpawnStrategy>()));
		TestTrue(TEXT("different-path registration finalizes"), Subsystem && Subsystem->FinalizeWorldStateRegistration().IsSuccess());
		const FWorldStateCaptureResult Baseline = Subsystem ? CaptureBaseline(*Subsystem) : FWorldStateCaptureResult();
		TestTrue(TEXT("different-path baseline captures"), Baseline.IsSuccess());
		const FWorldStateParticipantId ManagedId = Managed->Participant->ParticipantId;
		const FSoftObjectPath CapturedManagedPath(Managed);
		Referrer->DataComponent->SoftActor.Reset();
		Managed->Destroy();
		Managed = nullptr;
		CollectGarbage(RF_NoFlags);

		const FWorldStateRestoreResult Restore = Subsystem->RestoreBaseline(FWorldStateRestoreRequest());
		TestTrue(TEXT("external different-path restore completes with observable warnings"), Restore.IsSuccess());
		AWorldStateTestActor* Recreated = nullptr;
		for (TActorIterator<AWorldStateTestActor> It(Scope.World); It; ++It)
		{
			if (It->Participant && It->Participant->ParticipantId == ManagedId)
			{
				Recreated = *It;
				break;
			}
		}
		TestNotNull(TEXT("external strategy recreated the participant"), Recreated);
		TestTrue(TEXT("external strategy may recreate at a different path"), Recreated && FSoftObjectPath(Recreated) != CapturedManagedPath);
		TestEqual(TEXT("external strategy participant properties still restore"), Recreated ? Recreated->OwnerValue : 0, 909);
		TestEqual(TEXT("captured soft path remains unchanged"), Referrer->DataComponent->SoftActor.ToSoftObjectPath(), CapturedManagedPath);
		TestTrue(TEXT("different recreated path remains observably unresolved"), Restore.ReferenceResults.ContainsByPredicate([](const FWorldStateReferenceResolutionResult& Item)
		{
			return Item.Status == EWorldStateReferenceResolutionStatus::UnresolvedAllowed;
		}));
		TestTrue(TEXT("different recreated path produces a structured warning"), Restore.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue)
		{
			return Issue.Code == TEXT("RespawnPathChanged") && Issue.Severity == EWorldStateIssueSeverity::Warning;
		}));
	}
	return true;
}

/** Covers required unresolved soft paths producing an observable reference-stage failure. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStateRequiredReferenceTest,
	"WorldState.Runtime.Restore.RequiredSoftReferenceFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStateRequiredReferenceTest::RunTest(const FString& Parameters)
{
	using namespace UE::WorldState::Tests;
	FScopedTestWorld Scope(TEXT("WorldStateRequiredReferenceWorld"));
	AWorldStateTestActor* Actor = Scope.World ? SpawnTestActor(*Scope.World, TEXT("RequiredReferenceParticipant")) : nullptr;
	if (!Actor)
	{
		AddError(TEXT("Required-reference test actor could not be created."));
		return false;
	}
	Actor->DataComponent->SoftActor = TSoftObjectPtr<AActor>(FSoftObjectPath(TEXT("/Game/WorldStateTests/RequiredMissing.RequiredMissing")));
	SelectDataProperty(*Actor, GET_MEMBER_NAME_CHECKED(UWorldStateTestDataComponent, SoftActor), EWorldStateReferenceRequirement::Required);
	UWorldStateTestObserver* Observer = NewObject<UWorldStateTestObserver>(Scope.World);
	BindParticipantObserver(*Actor->Participant, *Observer);
	StartPlay(*Scope.World);
	UWorldStateSubsystem* Subsystem = Scope.World->GetSubsystem<UWorldStateSubsystem>();
	TestTrue(TEXT("required-reference registration finalizes"), Subsystem && Subsystem->FinalizeWorldStateRegistration().IsSuccess());
	const FWorldStateCaptureResult Baseline = Subsystem ? CaptureBaseline(*Subsystem) : FWorldStateCaptureResult();
	TestTrue(TEXT("unresolved path can be captured without loading"), Baseline.IsSuccess());
	Actor->DataComponent->SoftActor.Reset();
	int32 CompletedCount = 0;
	int32 FailedCount = 0;
	FWorldStateRestoreSessionId StartedSessionId;
	FWorldStateRestoreSessionId FailedSessionId;
	EWorldStateRestoreStage ObservedFailureStage = EWorldStateRestoreStage::None;
	bool bObservedMutationBegan = false;
	Subsystem->OnRestoreStartedNative().AddLambda([&](const FWorldStateRestoreLifecycleContext& Context) { StartedSessionId = Context.RestoreSessionId; });
	Subsystem->OnRestoreCompletedNative().AddLambda([&](const FWorldStateRestoreResult& Result) { ++CompletedCount; });
	Subsystem->OnRestoreFailedNative().AddLambda([&](const FWorldStateRestoreResult& Result)
	{
		++FailedCount;
		FailedSessionId = Result.RestoreSessionId;
		ObservedFailureStage = Result.FailureStage;
		bObservedMutationBegan = Result.bMutationBegan;
	});
	const FWorldStateRestoreResult Restore = Subsystem->RestoreBaseline(FWorldStateRestoreRequest());
	TestEqual(TEXT("required unresolved reference fails"), Restore.Status, EWorldStateOperationStatus::RestoreFailed);
	TestEqual(TEXT("failure stage identifies references"), Restore.FailureStage, EWorldStateRestoreStage::References);
	TestTrue(TEXT("failure records prior mutation"), Restore.bMutationBegan);
	TestEqual(TEXT("Completed is mutually exclusive"), CompletedCount, 0);
	TestEqual(TEXT("Failed occurs exactly once"), FailedCount, 1);
	TestTrue(TEXT("Failed retains the Started session ID"), FailedSessionId == StartedSessionId && FailedSessionId == Restore.RestoreSessionId);
	TestEqual(TEXT("Failed delegate exposes the terminal stage"), ObservedFailureStage, EWorldStateRestoreStage::References);
	TestTrue(TEXT("Failed delegate exposes whether mutation began"), bObservedMutationBegan);
	TestEqual(TEXT("participant failure callback occurs exactly once"), Observer->FailedCount, 1);
	TestEqual(TEXT("participant failure callback exposes a structured reason"), Observer->LastFailureCode, FName(TEXT("RequiredReferenceUnresolved")));
	TestEqual(TEXT("subsystem enters Failed after mutation"), Subsystem->GetWorldStateSubsystemState(), EWorldStateSubsystemState::Failed);
	TestTrue(TEXT("path value was restored before resolution failure"), Actor->DataComponent->SoftActor.ToSoftObjectPath() == FSoftObjectPath(TEXT("/Game/WorldStateTests/RequiredMissing.RequiredMissing")));
	return true;
}

#endif
