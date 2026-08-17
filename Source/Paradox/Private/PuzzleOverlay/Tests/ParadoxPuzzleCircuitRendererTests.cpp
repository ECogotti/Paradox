#if WITH_DEV_AUTOMATION_TESTS

#include "Conditions/PuzzleInputStateCondition.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Interaction/ParadoxSelectionComponent.h"
#include "Inventory/ParadoxPuzzleItemSlotActor.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Paradox.h"
#include "Tests/ParadoxSelectionTestTypes.h"
#include "PuzzleOverlay/ParadoxPuzzleCircuitRendererComponent.h"
#include "Puzzles/ParadoxVerticalBarrier.h"
#include "Puzzles/PressurePlate.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimeLoop/ParadoxChronoSpawn.h"
#include "Types/WorldStateTypes.h"
#include "UObject/UObjectIterator.h"

namespace UE::Paradox::PuzzleOverlay::Tests
{
	struct FScopedOverlayWorld
	{
		explicit FScopedOverlayWorld(const TCHAR* Name)
		{
			Context = GEngine ? &GEngine->CreateNewWorldContext(EWorldType::Game) : nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedOverlayWorld()
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

		void StartPlay() const
		{
			if (!World || World->HasBegunPlay())
			{
				return;
			}
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			for (TActorIterator<AActor> Iterator(World); Iterator; ++Iterator)
			{
				if (!Iterator->HasActorBegunPlay())
				{
					Iterator->DispatchBeginPlay();
				}
			}
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	template <typename ActorType>
	ActorType* Spawn(UWorld& World, const FName Name, const FVector& Location = FVector::ZeroVector)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<ActorType>(ActorType::StaticClass(), FTransform(Location), Parameters);
	}

	template <typename ComponentType>
	ComponentType* AddComponent(AActor& Owner, const FName Name)
	{
		ComponentType* Component = NewObject<ComponentType>(&Owner, ComponentType::StaticClass(), Name);
		Owner.AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	FHitResult MakeHit(AParadoxSelectionTestActor& Actor)
	{
		return FHitResult(
			&Actor,
			Actor.StaticMesh,
			Actor.GetActorLocation(),
			FVector::UpVector);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleCircuitRendererDefaultsTest,
	"Paradox.PuzzleOverlay.Renderer.DefaultsAndStencilContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleCircuitRendererDefaultsTest::RunTest(const FString& Parameters)
{
	const UParadoxPuzzleCircuitRendererComponent* Defaults =
		GetDefault<UParadoxPuzzleCircuitRendererComponent>();
	if (!TestNotNull(TEXT("Renderer CDO exists"), Defaults))
	{
		return false;
	}
	TestFalse(TEXT("Renderer has no polling Tick"), Defaults->PrimaryComponentTick.bCanEverTick);
	TestTrue(TEXT("Wire custom depth is enabled by default"), Defaults->bRenderWiresInCustomDepth);
	TestEqual(TEXT("Input stencil defaults to reserved range start"), Defaults->InputStencilValue, 210);
	TestEqual(TEXT("Output stencil defaults to reserved range start"), Defaults->OutputStencilValue, 220);
	TestTrue(TEXT("Input stencil remains below Hover"), Defaults->InputStencilValue < 230);
	TestTrue(TEXT("Output stencil remains below Hover"), Defaults->OutputStencilValue < 230);
	TestEqual(TEXT("Inactive custom data default"), Defaults->InactiveSignalStrength, 0.15f);
	TestEqual(TEXT("Active custom data default"), Defaults->ActiveSignalStrength, 1.0f);
	TestEqual(TEXT("Multi-thread routing is the component default"),
		Defaults->ExecutionMode,
		EParadoxPuzzleRoutingExecutionMode::MultiThreaded);
	TestEqual(TEXT("Endpoint clearance default"), Defaults->RoutingSettings.EndpointClearance, 25.0);
	TestEqual(TEXT("Port spacing default"), Defaults->RoutingSettings.PortSpacing, 16.0);
	TestEqual(TEXT("Port edge inset default"), Defaults->RoutingSettings.PortEdgeInset, 8.0);
	TestEqual(TEXT("Distributed Repulsive is the renderer default"),
		Defaults->RoutingSettings.Algorithm,
		EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive);
	TestEqual(TEXT("Grid routing has no subdivision by default"),
		Defaults->RoutingSettings.GridCellSubdivision,
		EParadoxPuzzleRoutingSubdivision::None);
	TestEqual(TEXT("No subdivision uses one routing cell per GridWorld cell"),
		GetParadoxPuzzleRoutingSubdivisionFactor(EParadoxPuzzleRoutingSubdivision::None),
		1);
	TestEqual(TEXT("2 x 2 subdivision creates four routing cells"),
		GetParadoxPuzzleRoutingSubdivisionFactor(EParadoxPuzzleRoutingSubdivision::TwoByTwo),
		2);
	TestEqual(TEXT("4 x 4 subdivision creates sixteen routing cells"),
		GetParadoxPuzzleRoutingSubdivisionFactor(EParadoxPuzzleRoutingSubdivision::FourByFour),
		4);
	TestEqual(TEXT("8 x 8 subdivision creates sixty-four routing cells"),
		GetParadoxPuzzleRoutingSubdivisionFactor(EParadoxPuzzleRoutingSubdivision::EightByEight),
		8);
	TestNotNull(TEXT("Grid subdivision is designer-facing"), FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, GridCellSubdivision)));
	TestNull(TEXT("Derived Pitch X is not exposed as tuning"), FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, PitchX)));
	TestNull(TEXT("Derived Pitch Y is not exposed as tuning"), FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, PitchY)));
	TestEqual(TEXT("Ordered candidate budget default"), Defaults->RoutingSettings.MaxOrderedBundleCandidatesPerLink, 128);
	TestEqual(TEXT("Bundle optimization pass default"), Defaults->RoutingSettings.MaxBundleOptimizationPasses, 4);
	TestEqual(TEXT("Metro ordering pass default"), Defaults->RoutingSettings.MaxMetroOrderingPasses, 8);
	TestEqual(TEXT("Bend penalty default"), Defaults->RoutingSettings.BendPenalty, 100.0);
	TestEqual(TEXT("Bundle reuse bonus default"), Defaults->RoutingSettings.BundleReuseBonus, 35.0);
	TestEqual(TEXT("Engine cube fallback is configured"),
		Defaults->WireMesh.ToSoftObjectPath(),
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));

	const AParadoxPlayerController* Controller = GetDefault<AParadoxPlayerController>();
	TestNotNull(TEXT("Controller owns the renderer as a native default subobject"),
		Controller ? Controller->GetPuzzleCircuitRendererComponent() : nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleCircuitRendererActorOptInTest,
	"Paradox.PuzzleOverlay.Renderer.NativeActorOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleCircuitRendererActorOptInTest::RunTest(const FString& Parameters)
{
	const APressurePlate* PressurePlate = GetDefault<APressurePlate>();
	const AParadoxVerticalBarrier* Barrier = GetDefault<AParadoxVerticalBarrier>();
	const AParadoxChronoSpawn* ChronoSpawn = GetDefault<AParadoxChronoSpawn>();
	TestTrue(TEXT("Pressure Plate opts in"), PressurePlate && PressurePlate->SelectableComponent
		&& PressurePlate->SelectableComponent->bShowPuzzleConnectionsWhenSelected);
	TestTrue(TEXT("Vertical Barrier opts in"), Barrier && Barrier->SelectableComponent
		&& Barrier->SelectableComponent->bShowPuzzleConnectionsWhenSelected);
	TestFalse(TEXT("Chrono Spawn remains selection-only"), ChronoSpawn
		&& ChronoSpawn->GetSelectableComponent()
		&& ChronoSpawn->GetSelectableComponent()->bShowPuzzleConnectionsWhenSelected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleCircuitRendererWireTargetValidationTest,
	"Paradox.PuzzleOverlay.Renderer.WireTargetValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleCircuitRendererWireTargetValidationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::PuzzleOverlay::Tests;
	FScopedOverlayWorld Scope(TEXT("ParadoxPuzzleOverlayValidationWorld"));
	AParadoxSelectionTestActor* Actor = Scope.World
		? Spawn<AParadoxSelectionTestActor>(*Scope.World, TEXT("OverlayValidationActor"))
		: nullptr;
	if (!TestNotNull(TEXT("Validation Actor exists"), Actor))
	{
		return false;
	}
	Actor->Selectable->bShowPuzzleConnectionsWhenSelected = true;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UBoxComponent* Box = AddComponent<UBoxComponent>(
			*Actor,
			*FString::Printf(TEXT("WireTarget_%d"), Index));
		Box->ComponentTags.Add(TEXT("WireTarget"));
		Box->SetBoxExtent(FVector(50.0));
	}
#if WITH_EDITOR
	FDataValidationContext ValidationContext;
	TestEqual(TEXT("Multiple WireTarget boxes produce a warning and a deterministic runtime choice"),
		static_cast<UObject*>(Actor->Selectable)->IsDataValid(ValidationContext),
		EDataValidationResult::Valid);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleCircuitRendererBlueprintWireTargetValidationTest,
	"Paradox.PuzzleOverlay.Renderer.BlueprintWireTargetValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleCircuitRendererBlueprintWireTargetValidationTest::RunTest(
	const FString& Parameters)
{
	UClass* KeyCardSlotClass = LoadClass<AParadoxPuzzleItemSlotActor>(
		nullptr,
		TEXT("/Game/Environment/SpaceShip/Blueprints/BP_KeyCardSlot.BP_KeyCardSlot_C"));
	const AParadoxPuzzleItemSlotActor* KeyCardSlot = KeyCardSlotClass
		? Cast<AParadoxPuzzleItemSlotActor>(KeyCardSlotClass->GetDefaultObject())
		: nullptr;
	if (!TestNotNull(TEXT("BP_KeyCardSlot class resolves"), KeyCardSlot)
		|| !TestNotNull(
			TEXT("BP_KeyCardSlot owns Selectable component"),
			KeyCardSlot ? KeyCardSlot->GetSelectableComponent() : nullptr))
	{
		return false;
	}

	FDataValidationContext ValidationContext;
	static_cast<UObject*>(KeyCardSlot->GetSelectableComponent())->IsDataValid(ValidationContext);
	bool bFoundPointFallbackWarning = false;
	bool bFoundMultipleWireTargetsWarning = false;
	for (const FDataValidationContext::FIssue& Issue : ValidationContext.GetIssues())
	{
		bFoundPointFallbackWarning |= Issue.Message.ToString().Contains(
			TEXT("point fallback"),
			ESearchCase::IgnoreCase);
		bFoundMultipleWireTargetsWarning |= Issue.Message.ToString().Contains(
			TEXT("Multiple UBoxComponent instances"),
			ESearchCase::IgnoreCase);
	}
	TestFalse(
		TEXT("Blueprint-authored WireTarget prevents the point-fallback warning"),
		bFoundPointFallbackWarning);
	TestFalse(
		TEXT("Blueprint CDO with one authored WireTarget is not ambiguous"),
		bFoundMultipleWireTargetsWarning);

	using namespace UE::Paradox::PuzzleOverlay::Tests;
	FScopedOverlayWorld Scope(TEXT("ParadoxPuzzleOverlayBlueprintValidationWorld"));
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxPuzzleItemSlotActor* PlacedKeyCardSlot = Scope.World
		? Scope.World->SpawnActor<AParadoxPuzzleItemSlotActor>(
			KeyCardSlotClass,
			FTransform::Identity,
			SpawnParameters)
		: nullptr;
	if (!TestNotNull(TEXT("Placed BP_KeyCardSlot instance exists"), PlacedKeyCardSlot)
		|| !TestNotNull(
			TEXT("Placed BP_KeyCardSlot owns Selectable component"),
			PlacedKeyCardSlot ? PlacedKeyCardSlot->GetSelectableComponent() : nullptr))
	{
		return false;
	}

	FDataValidationContext PlacedValidationContext;
	static_cast<UObject*>(PlacedKeyCardSlot->GetSelectableComponent())->IsDataValid(
		PlacedValidationContext);
	bFoundMultipleWireTargetsWarning = false;
	for (const FDataValidationContext::FIssue& Issue : PlacedValidationContext.GetIssues())
	{
		bFoundMultipleWireTargetsWarning |= Issue.Message.ToString().Contains(
			TEXT("Multiple UBoxComponent instances"),
			ESearchCase::IgnoreCase);
	}
	TestFalse(
		TEXT("Placed Blueprint with one WireTarget does not count its SCS template twice"),
		bFoundMultipleWireTargetsWarning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleCircuitRendererLifecycleTest,
	"Paradox.PuzzleOverlay.Renderer.SelectionStateTopologyAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleCircuitRendererLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::PuzzleOverlay::Tests;
	FScopedOverlayWorld Scope(TEXT("ParadoxPuzzleOverlayLifecycleWorld"));
	if (!TestNotNull(TEXT("Overlay test world exists"), Scope.World))
	{
		return false;
	}

	AParadoxPuzzleOverlayTestController* PlayerController =
		Spawn<AParadoxPuzzleOverlayTestController>(*Scope.World, TEXT("OverlayPlayerController"));
	AParadoxSelectionTestActor* Source = Spawn<AParadoxSelectionTestActor>(
		*Scope.World,
		TEXT("OverlaySource"),
		FVector(-300.0, 0.0, 0.0));
	AParadoxSelectionTestActor* Target = Spawn<AParadoxSelectionTestActor>(
		*Scope.World,
		TEXT("OverlayTarget"),
		FVector::ZeroVector);
	AParadoxSelectionTestActor* GateSource = Spawn<AParadoxSelectionTestActor>(
		*Scope.World,
		TEXT("OverlayGateSource"),
		FVector(-300.0, 300.0, 0.0));
	APuzzleController* PuzzleController =
		Spawn<APuzzleController>(*Scope.World, TEXT("OverlayPuzzleController"));
	if (!TestNotNull(TEXT("Player Controller exists"), PlayerController)
		|| !TestNotNull(TEXT("Source exists"), Source)
		|| !TestNotNull(TEXT("Target exists"), Target)
		|| !TestNotNull(TEXT("Gate source exists"), GateSource)
		|| !TestNotNull(TEXT("Puzzle Controller exists"), PuzzleController))
	{
		return false;
	}

	UPuzzleEmitterComponent* Emitter =
		AddComponent<UPuzzleEmitterComponent>(*Source, TEXT("OverlayEmitter"));
	UPuzzleReceiverComponent* Receiver =
		AddComponent<UPuzzleReceiverComponent>(*Target, TEXT("OverlayReceiver"));
	UPuzzleEmitterComponent* GateEmitter =
		AddComponent<UPuzzleEmitterComponent>(*GateSource, TEXT("OverlayGateEmitter"));
	for (AParadoxSelectionTestActor* Endpoint : {Source, Target})
	{
		UBoxComponent* WireTarget = AddComponent<UBoxComponent>(
			*Endpoint,
			*FString::Printf(TEXT("%s_WireTarget"), *Endpoint->GetName()));
		WireTarget->ComponentTags.Add(TEXT("WireTarget"));
		WireTarget->SetBoxExtent(FVector(50.0, 60.0, 20.0));
		WireTarget->AttachToComponent(
			Endpoint->GetRootComponent(),
			FAttachmentTransformRules::KeepRelativeTransform);
		WireTarget->SetRelativeLocation(FVector::ZeroVector);
		if (Endpoint == Target)
		{
			WireTarget->SetRelativeRotation(FRotator(0.0, 45.0, 0.0));
		}
	}
	GateSource->StaticMesh->SetStaticMesh(
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Target->Selectable->bShowPuzzleConnectionsWhenSelected = true;
	GateSource->Selectable->bShowPuzzleConnectionsWhenSelected = true;

	FPuzzleInputBinding& Input = PuzzleController->InputBindings.AddDefaulted_GetRef();
	Input.InputId = TEXT("Main");
	Input.EmitterActor = Source;
	Input.bSpecifyEmitterComponent = true;
	Input.EmitterComponentName = Emitter->GetFName();
	Input.SignalTag = ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag();
	FPuzzleEmitterGateBinding& GateBinding = Input.EmitterGates.AddDefaulted_GetRef();
	GateBinding.InputId = TEXT("Gate");
	GateBinding.EmitterActor = GateSource;
	GateBinding.bSpecifyEmitterComponent = true;
	GateBinding.EmitterComponentName = GateEmitter->GetFName();
	GateBinding.SignalTag = ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag();
	UPuzzleInputStateCondition* GateCondition = NewObject<UPuzzleInputStateCondition>(PuzzleController);
	GateCondition->InputId = GateBinding.InputId;
	Input.GateConditions.Add(GateCondition);
	UPuzzleInputStateCondition* RootCondition = NewObject<UPuzzleInputStateCondition>(PuzzleController);
	RootCondition->InputId = Input.InputId;
	PuzzleController->RootCondition = RootCondition;
	FPuzzleReceiverBinding& ReceiverBinding = PuzzleController->ReceiverBindings.AddDefaulted_GetRef();
	ReceiverBinding.ReceiverActor = Target;
	ReceiverBinding.bSpecifyReceiverComponent = true;
	ReceiverBinding.ReceiverComponentName = Receiver->GetFName();

	Scope.StartPlay();
	UParadoxSelectionComponent* Selection = PlayerController->GetSelectionComponent();
	UParadoxPuzzleCircuitRendererComponent* Renderer =
		PlayerController->GetPuzzleCircuitRendererComponent();
	if (!TestNotNull(TEXT("Selection Component exists"), Selection)
		|| !TestNotNull(TEXT("Renderer Component exists"), Renderer))
	{
		return false;
	}
	// The broad lifecycle regression intentionally remains immediate and deterministic;
	// asynchronous cancellation and delegate semantics are covered by the dedicated test.
	Renderer->ExecutionMode = EParadoxPuzzleRoutingExecutionMode::Standard;

	TestTrue(TEXT("Selecting the Receiver Actor succeeds"),
		Selection->HandleSelectionPointerHit(MakeHit(*Target), true));
	TArray<FParadoxPuzzleWireRoute> Routes = Renderer->GetRenderedRoutes();
	if (!TestEqual(TEXT("Incoming primary creates one Input route"), Routes.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Incoming route direction is Input"),
		Routes[0].Direction,
		EParadoxPuzzleWireDirection::Input);
	TestTrue(TEXT("Source port uses the tagged endpoint box"), Routes[0].SourcePort.Bounds.bFromWireTarget);
	TestTrue(TEXT("Target port uses the tagged endpoint box"), Routes[0].TargetPort.Bounds.bFromWireTarget);
	TestEqual(TEXT("Tagged source reports custom Wire Box provenance"),
		Routes[0].SourcePort.Bounds.Source,
		EParadoxPuzzleWireBoxSource::CustomWireTarget);
	TestTrue(TEXT("Rotated WireTarget is projected into the routing-frame AABB"),
		Routes[0].TargetPort.Bounds.GetExtent().X > 70.0
		&& Routes[0].TargetPort.Bounds.GetExtent().Y > 70.0);
	TestEqual(TEXT("Input enters the target from its west face"),
		Routes[0].TargetPort.Side,
		EParadoxPuzzlePortSide::West);
	TestFalse(TEXT("Unpublished signal starts inactive"), Routes[0].bActive);
	int32 VisibleWireComponentCount = 0;
	for (TObjectIterator<UInstancedStaticMeshComponent> Iterator; Iterator; ++Iterator)
	{
		UInstancedStaticMeshComponent* Component = *Iterator;
		if (Component->GetWorld() != Scope.World
			|| !Component->GetName().StartsWith(TEXT("Puzzle Input Wire Instances")))
		{
			continue;
		}
		++VisibleWireComponentCount;
		TestTrue(TEXT("Wire instances are owned by a visible presentation Actor"),
			IsValid(Component->GetOwner()) && !Component->GetOwner()->IsHidden());
		TestTrue(TEXT("Input wire geometry was submitted to the ISM"),
			Component->GetInstanceCount() > 0);
		TestEqual(TEXT("Renderer submits exactly the router-owned final segments"),
			Component->GetInstanceCount(),
			Routes[0].Segments.Num());
	}
	TestEqual(TEXT("Exactly one visible Input ISM is active in the test world"),
		VisibleWireComponentCount,
		1);
	const int64 InitialGeneration = Renderer->GetRoutingGeneration();

	TestTrue(TEXT("Gate emitter authorizes the primary input"),
		GateEmitter->SetSignalState(
			ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag(),
			true,
			nullptr));
	TestTrue(TEXT("Emitter publishes the primary signal"),
		Emitter->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag(), true, nullptr));
	Routes = Renderer->GetRenderedRoutes();
	TestEqual(TEXT("A state-only update does not reroute"),
		Renderer->GetRoutingGeneration(),
		InitialGeneration);
	TestTrue(TEXT("The existing route receives the active state"),
		Routes.Num() == 1 && Routes[0].bActive);
	TestTrue(TEXT("The existing route receives the new signal validity"),
		Routes.Num() == 1 && Routes[0].bSignalValid);
	TestTrue(TEXT("The existing route receives the new Controller result"),
		Routes.Num() == 1
		&& Routes[0].bControllerResultValid
		&& Routes[0].bControllerResultActive);

	Source->SetActorLocation(FVector(-290.0, 0.0, 0.0));
	Renderer->ProcessPendingEndpointReroutes();
	TestTrue(TEXT("Exact endpoint-bounds movement reroutes its face group"),
		Renderer->GetRoutingGeneration() > InitialGeneration);
	const int64 BoundsMovementGeneration = Renderer->GetRoutingGeneration();
	UBoxComponent* TargetWireTarget = Target->FindComponentByTag<UBoxComponent>(TEXT("WireTarget"));
	if (TestNotNull(TEXT("Target WireTarget remains available"), TargetWireTarget))
	{
		TargetWireTarget->SetRelativeLocation(FVector(5.0, 0.0, 0.0));
		Renderer->ProcessPendingEndpointReroutes();
		TestTrue(TEXT("WireTarget component transform triggers a reroute"),
			Renderer->GetRoutingGeneration() > BoundsMovementGeneration);
	}
	const int64 WireTargetMovementGeneration = Renderer->GetRoutingGeneration();
	Source->SetActorLocation(FVector(-150.0, 0.0, 0.0));
	Renderer->ProcessPendingEndpointReroutes();
	TestTrue(TEXT("Crossing a lattice coordinate reroutes affected links"),
		Renderer->GetRoutingGeneration() > WireTargetMovementGeneration);

	const int64 CoalescedMovementGeneration = Renderer->GetRoutingGeneration();
	Source->SetActorLocation(FVector(-140.0, 0.0, 0.0));
	if (TargetWireTarget)
	{
		TargetWireTarget->SetRelativeLocation(FVector(10.0, 0.0, 0.0));
	}
	TestEqual(TEXT("Endpoint updates are deferred until the coalesced next-tick batch"),
		Renderer->GetRoutingGeneration(), CoalescedMovementGeneration);
	Renderer->ProcessPendingEndpointReroutes();
	TestEqual(TEXT("Multiple endpoint updates in one frame produce one routing generation"),
		Renderer->GetRoutingGeneration(), CoalescedMovementGeneration + 1);

	PuzzleController->ShutdownPuzzleController();
	TestEqual(TEXT("Topology removal clears the selected subgraph"),
		Renderer->GetRenderedRoutes().Num(),
		0);
	TestEqual(TEXT("Topology removal does not clear selection"),
		Selection->GetSelectedActor(),
		static_cast<AActor*>(Target));

	TestTrue(TEXT("Controller can rebuild its graph after shutdown"),
		PuzzleController->InitializePuzzleController());
	TestEqual(TEXT("Topology re-registration restores the route"),
		Renderer->GetRenderedRoutes().Num(),
		1);
	TestTrue(TEXT("Selecting the gate-source Actor succeeds"),
		Selection->HandleSelectionPointerHit(MakeHit(*GateSource), true));
	Routes = Renderer->GetRenderedRoutes();
	if (!TestEqual(TEXT("Outgoing gate influence creates one route"), Routes.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Outgoing gate route uses Output presentation"),
		Routes[0].Direction,
		EParadoxPuzzleWireDirection::Output);
	TestEqual(TEXT("Outgoing gate route retains GateInfluence identity"),
		Routes[0].LinkKind,
		EPuzzleGraphLinkKind::GateInfluence);
	TestTrue(TEXT("Direct visual mesh supplies fallback endpoint bounds"),
		Routes[0].SourcePort.Bounds.bValid
		&& !Routes[0].SourcePort.Bounds.bFromWireTarget
		&& !Routes[0].SourcePort.Bounds.bPointFallback);
	TestEqual(TEXT("Automatic source reports visible-mesh provenance"),
		Routes[0].SourcePort.Bounds.Source,
		EParadoxPuzzleWireBoxSource::VisibleMeshes);
	UWorldStateSubsystem* WorldState = Scope.World->GetSubsystem<UWorldStateSubsystem>();
	if (!TestNotNull(TEXT("World State subsystem exists"), WorldState))
	{
		return false;
	}
	FWorldStateRestoreLifecycleContext RestoreContext;
	RestoreContext.Stage = EWorldStateRestoreStage::Preflight;
	WorldState->OnRestoreStartedNative().Broadcast(RestoreContext);
	TestNull(TEXT("WorldState reset clears the selection"), Selection->GetSelectedActor());
	TestNull(TEXT("WorldState reset clears the displayed Actor"), Renderer->GetDisplayedActor());
	TestEqual(TEXT("WorldState reset clears all routes"), Renderer->GetRenderedRoutes().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleCircuitRendererAsyncLifecycleTest,
	"Paradox.PuzzleOverlay.Renderer.AsyncCancellationAndDelegates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleCircuitRendererAsyncLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::PuzzleOverlay::Tests;
	FScopedOverlayWorld Scope(TEXT("ParadoxPuzzleOverlayAsyncLifecycleWorld"));
	AParadoxPuzzleOverlayTestController* PlayerController = Spawn<AParadoxPuzzleOverlayTestController>(
		*Scope.World,
		TEXT("AsyncOverlayPlayerController"));
	AParadoxSelectionTestActor* Target = Spawn<AParadoxSelectionTestActor>(
		*Scope.World,
		TEXT("AsyncOverlayTarget"));
	if (!TestNotNull(TEXT("Player Controller exists"), PlayerController)
		|| !TestNotNull(TEXT("Target exists"), Target))
	{
		return false;
	}
	Target->Selectable->bShowPuzzleConnectionsWhenSelected = true;
	Scope.StartPlay();

	UParadoxSelectionComponent* Selection = PlayerController->GetSelectionComponent();
	UParadoxPuzzleCircuitRendererComponent* Renderer =
		PlayerController->GetPuzzleCircuitRendererComponent();
	if (!TestNotNull(TEXT("Selection exists"), Selection)
		|| !TestNotNull(TEXT("Renderer exists"), Renderer))
	{
		return false;
	}
	Renderer->ExecutionMode = EParadoxPuzzleRoutingExecutionMode::MultiThreaded;

	FEvent* WorkerEntered = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* AllowWorkerToFinish = FPlatformProcess::GetSynchEventFromPool(true);
	Renderer->BeforeWorkerSolveTestHook = [WorkerEntered, AllowWorkerToFinish]()
	{
		WorkerEntered->Trigger();
		AllowWorkerToFinish->Wait();
	};

	int32 StartedCount = 0;
	int32 FinishedCount = 0;
	bool bStartedOnGameThread = false;
	bool bFinishedOnGameThread = false;
	EParadoxPuzzleWireCalculationCompletionStatus CompletionStatus =
		EParadoxPuzzleWireCalculationCompletionStatus::Failed;
	FGuid StartedRequestId;
	Renderer->OnWireCalculationStartedNative.AddLambda(
		[&StartedCount, &bStartedOnGameThread, &StartedRequestId](
			const FParadoxPuzzleWireCalculationContext& Context)
		{
			++StartedCount;
			bStartedOnGameThread = IsInGameThread();
			StartedRequestId = Context.RequestId;
		});
	Renderer->OnWireCalculationFinishedNative.AddLambda(
		[&FinishedCount, &bFinishedOnGameThread, &CompletionStatus](
			const FParadoxPuzzleWireCalculationResult& Result)
		{
			++FinishedCount;
			bFinishedOnGameThread = IsInGameThread();
			CompletionStatus = Result.Status;
		});

	TestTrue(TEXT("Selection starts asynchronous routing"),
		Selection->HandleSelectionPointerHit(MakeHit(*Target), true));
	TestTrue(TEXT("Started delegate fires once"), StartedCount == 1);
	TestTrue(TEXT("Started delegate runs on Game Thread"), bStartedOnGameThread);
	TestTrue(TEXT("Started request has a stable identity"), StartedRequestId.IsValid());
	TestTrue(TEXT("Worker reaches deterministic barrier"), WorkerEntered->Wait(5000));
	TestTrue(TEXT("Renderer reports active calculation"), Renderer->IsWireCalculationInProgress());

	Selection->DeselectCurrentActor();
	TestNull(TEXT("Deselection clears displayed Actor immediately"), Renderer->GetDisplayedActor());
	TestEqual(TEXT("Deselection clears rendered routes immediately"), Renderer->GetRenderedRoutes().Num(), 0);
	AllowWorkerToFinish->Trigger();

	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (Renderer->IsWireCalculationInProgress() && FPlatformTime::Seconds() < Deadline)
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.001f);
	}
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

	TestFalse(TEXT("Cancelled worker completes"), Renderer->IsWireCalculationInProgress());
	TestEqual(TEXT("Finished delegate balances Started"), FinishedCount, 1);
	TestTrue(TEXT("Finished delegate runs on Game Thread"), bFinishedOnGameThread);
	TestEqual(TEXT("Deselection reports cancellation"),
		CompletionStatus,
		EParadoxPuzzleWireCalculationCompletionStatus::Cancelled);
	TestEqual(TEXT("Late result never repopulates routes"), Renderer->GetRenderedRoutes().Num(), 0);

	Renderer->BeforeWorkerSolveTestHook = {};
	FPlatformProcess::ReturnSynchEventToPool(WorkerEntered);
	FPlatformProcess::ReturnSynchEventToPool(AllowWorkerToFinish);
	return true;
}

#endif
