#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Interaction/ParadoxSelectionTypes.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Navigation/GridNavigationData.h"
#include "Paradox.h"
#include "ParadoxInteractionTestTypes.h"
#include "ParadoxSelectionTestTypes.h"
#include "Presentation/GridCellOverlayPresentationSubsystem.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "Puzzles/ParadoxVerticalBarrier.h"
#include "Puzzles/PressurePlate.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimeLoop/ParadoxChronoSpawn.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectSubsystem.h"
#include "Types/WorldStateTypes.h"

namespace UE::Paradox::Selection::Tests
{
	struct FScopedSelectionWorld
	{
		explicit FScopedSelectionWorld(const TCHAR* Name)
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
		}

		~FScopedSelectionWorld()
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

		void StartPlay()
		{
			if (World && !World->HasBegunPlay())
			{
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
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	template <typename T>
	T* Spawn(UWorld& World, const FName Name, const FVector& Location = FVector::ZeroVector)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<T>(T::StaticClass(), FTransform(Location), Parameters);
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
	FParadoxSelectionPresentationTest,
	"Paradox.Selection.PresentationAndExactRestoration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxSelectionPresentationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Selection::Tests;
	FScopedSelectionWorld TestWorld(TEXT("ParadoxSelectionPresentationWorld"));
	if (!TestNotNull(TEXT("Selection test world exists"), TestWorld.World))
	{
		return false;
	}

	AParadoxSelectionTestController* Controller = Spawn<AParadoxSelectionTestController>(
		*TestWorld.World,
		TEXT("SelectionController"));
	AParadoxSelectionTestActor* First = Spawn<AParadoxSelectionTestActor>(
		*TestWorld.World,
		TEXT("FirstSelectable"));
	AParadoxSelectionTestActor* Second = Spawn<AParadoxSelectionTestActor>(
		*TestWorld.World,
		TEXT("SecondSelectable"),
		FVector(200.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Selection controller exists"), Controller)
		|| !TestNotNull(TEXT("First selectable exists"), First)
		|| !TestNotNull(TEXT("Second selectable exists"), Second))
	{
		return false;
	}

	First->StaticMesh->SetRenderCustomDepth(false);
	First->StaticMesh->SetCustomDepthStencilValue(17);
	First->StaticMesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_1);
	First->SkeletalMesh->SetRenderCustomDepth(true);
	First->SkeletalMesh->SetCustomDepthStencilValue(33);
	First->SkeletalMesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_4);
	First->NonMeshPrimitive->SetRenderCustomDepth(false);
	First->NonMeshPrimitive->SetCustomDepthStencilValue(99);
	TestWorld.StartPlay();

	const FHitResult FirstHit = MakeHit(*First);
	const FHitResult SecondHit = MakeHit(*Second);
	Controller->Selection->UpdateHoverFromHitResult(FirstHit, true);
	TestEqual(TEXT("Hover Actor is authoritative"), Controller->Selection->GetHoveredActor(), static_cast<AActor*>(First));
	TestEqual(TEXT("Hover applies stencil 230 to Static Mesh"), First->StaticMesh->CustomDepthStencilValue, UE::Paradox::Selection::HoverStencilValue);
	TestEqual(TEXT("Hover applies stencil 230 to Skeletal Mesh"), First->SkeletalMesh->CustomDepthStencilValue, UE::Paradox::Selection::HoverStencilValue);
	TestFalse(TEXT("Non-mesh primitives are not outlined"), First->NonMeshPrimitive->bRenderCustomDepth);
	TestEqual(TEXT("Non-mesh stencil remains untouched"), First->NonMeshPrimitive->CustomDepthStencilValue, 99);

	TestTrue(TEXT("RMB selectable hit selects the Actor"), Controller->Selection->HandleSelectionPointerHit(FirstHit, true));
	TestEqual(TEXT("Selection applies stencil 240"), First->StaticMesh->CustomDepthStencilValue, UE::Paradox::Selection::SelectedStencilValue);
	Controller->Selection->UpdateHoverFromHitResult(FHitResult(), false);
	TestEqual(TEXT("Mouse exit preserves selected stencil"), First->StaticMesh->CustomDepthStencilValue, UE::Paradox::Selection::SelectedStencilValue);

	TestFalse(TEXT("Empty-world RMB has no selectable hit"), Controller->Selection->HandleSelectionPointerHit(FHitResult(), false));
	TestNull(TEXT("Empty-world RMB clears selection"), Controller->Selection->GetSelectedActor());
	Controller->Selection->UpdateHoverFromHitResult(FirstHit, true);
	TestTrue(TEXT("RMB can select the Actor again"), Controller->Selection->HandleSelectionPointerHit(FirstHit, true));
	Controller->Selection->UpdateHoverFromHitResult(SecondHit, true);
	TestTrue(TEXT("RMB on B replaces A"), Controller->Selection->HandleSelectionPointerHit(SecondHit, true));
	TestEqual(TEXT("Selecting B replaces A"), Controller->Selection->GetSelectedActor(), static_cast<AActor*>(Second));
	TestFalse(TEXT("Replacing selection restores A custom depth"), First->StaticMesh->bRenderCustomDepth);
	TestEqual(TEXT("Replacing selection restores A stencil"), First->StaticMesh->CustomDepthStencilValue, 17);
	TestEqual(TEXT("Replacing selection restores A write mask"), First->StaticMesh->CustomDepthStencilWriteMask, ERendererStencilMask::ERSM_1);
	TestTrue(TEXT("Pre-enabled skeletal custom depth restores exactly"), First->SkeletalMesh->bRenderCustomDepth);
	TestEqual(TEXT("Skeletal stencil restores exactly"), First->SkeletalMesh->CustomDepthStencilValue, 33);
	TestEqual(TEXT("Skeletal write mask restores exactly"), First->SkeletalMesh->CustomDepthStencilWriteMask, ERendererStencilMask::ERSM_4);

	TestTrue(TEXT("RMB on selected Actor toggles it off"), Controller->Selection->HandleSelectionPointerHit(SecondHit, true));
	TestNull(TEXT("Toggle leaves no selection"), Controller->Selection->GetSelectedActor());
	TestEqual(TEXT("Deselected hovered Actor falls back to hover"), Second->StaticMesh->CustomDepthStencilValue, UE::Paradox::Selection::HoverStencilValue);
	Controller->Selection->DeselectCurrentActor();
	Controller->Selection->ResetSelectionState();
	Controller->Selection->ResetSelectionState();
	TestNull(TEXT("Repeated reset clears hover"), Controller->Selection->GetHoveredActor());
	TestNull(TEXT("Repeated reset clears selection"), Controller->Selection->GetSelectedActor());

	Controller->Selection->UpdateHoverFromHitResult(FirstHit, true);
	UStaticMeshComponent* DynamicMesh = NewObject<UStaticMeshComponent>(First, TEXT("DynamicSelectionMesh"));
	First->AddInstanceComponent(DynamicMesh);
	DynamicMesh->SetupAttachment(First->Root);
	DynamicMesh->SetRenderCustomDepth(false);
	DynamicMesh->SetCustomDepthStencilValue(61);
	DynamicMesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_8);
	DynamicMesh->RegisterComponent();
	TestTrue(TEXT("Selection transition reconciles a dynamic mesh"), Controller->Selection->HandleSelectionPointerHit(FirstHit, true));
	TestEqual(TEXT("Dynamic mesh receives selected stencil"), DynamicMesh->CustomDepthStencilValue, UE::Paradox::Selection::SelectedStencilValue);
	First->SkeletalMesh->DestroyComponent();
	Controller->Selection->ResetSelectionState();
	TestFalse(TEXT("Surviving Static Mesh restores after another highlighted mesh is destroyed"), First->StaticMesh->bRenderCustomDepth);
	TestFalse(TEXT("Dynamic mesh original Custom Depth restores"), DynamicMesh->bRenderCustomDepth);
	TestEqual(TEXT("Dynamic mesh original stencil restores"), DynamicMesh->CustomDepthStencilValue, 61);
	TestEqual(TEXT("Dynamic mesh original write mask restores"), DynamicMesh->CustomDepthStencilWriteMask, ERendererStencilMask::ERSM_8);

	Controller->Selection->UpdateHoverFromHitResult(SecondHit, true);
	Controller->Selection->HandleSelectionPointerHit(SecondHit, true);
	Second->Destroy();
	Controller->Selection->ResetSelectionState();
	TestNull(TEXT("Destroyed selected Actor is no longer exposed"), Controller->Selection->GetSelectedActor());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxSelectionWidgetAndWorldStateTest,
	"Paradox.Selection.WidgetAndWorldStateReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxSelectionWidgetAndWorldStateTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Selection::Tests;
	FScopedSelectionWorld TestWorld(TEXT("ParadoxSelectionWidgetWorld"));
	if (!TestNotNull(TEXT("Widget test world exists"), TestWorld.World))
	{
		return false;
	}

	AParadoxSelectionTestController* Controller = Spawn<AParadoxSelectionTestController>(
		*TestWorld.World,
		TEXT("WidgetSelectionController"));
	AParadoxSelectionTestActor* Actor = Spawn<AParadoxSelectionTestActor>(
		*TestWorld.World,
		TEXT("WidgetSelectable"));
	AParadoxSelectionTestActor* ExternalAnchorActor = Spawn<AParadoxSelectionTestActor>(
		*TestWorld.World,
		TEXT("ExternalWidgetAnchor"),
		FVector(100.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Widget controller exists"), Controller)
		|| !TestNotNull(TEXT("Widget selectable exists"), Actor)
		|| !TestNotNull(TEXT("External widget anchor Actor exists"), ExternalAnchorActor))
	{
		return false;
	}
	Actor->Selectable->SelectionWidgetClass = UParadoxSelectionTestWidget::StaticClass();
	Actor->Selectable->WidgetAnchor.OtherActor = ExternalAnchorActor;
	TestWorld.StartPlay();

	const FHitResult Hit = MakeHit(*Actor);
	AddExpectedError(TEXT("ignored widget anchor"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Widget Actor selection succeeds"), Controller->Selection->HandleSelectionPointerHit(Hit, true));
	UWidgetComponent* WidgetComponent = Actor->Selectable->GetInteractionWidget();
	if (!TestNotNull(TEXT("Widget component is created lazily"), WidgetComponent))
	{
		return false;
	}
	TestTrue(TEXT("Selected widget is visible"), WidgetComponent->IsVisible());
	TestEqual(TEXT("Selected widget collision is query-only"), WidgetComponent->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("Selected widget never generates gameplay overlaps"), WidgetComponent->GetGenerateOverlapEvents());
	TestEqual(TEXT("Widget uses World space"), WidgetComponent->GetWidgetSpace(), EWidgetSpace::World);
	TestTrue(TEXT("Widget is rendered from both sides"), WidgetComponent->GetTwoSided());
	TestEqual(TEXT("External widget anchors fall back to the selected Actor root"), WidgetComponent->GetAttachParent(), Actor->Root.Get());
	TestTrue(TEXT("Camera-facing updates tick only while the widget is visible"), Actor->Selectable->IsComponentTickEnabled());
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
	TestTrue(
		TEXT("Widget forward follows the inverse camera forward vector"),
		WidgetComponent->GetForwardVector().Equals(-CameraRotation.Vector(), KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Widget uses configured draw size"), WidgetComponent->GetDrawSize(), FVector2D(400.0f, 160.0f));
	UParadoxSelectionTestWidget* Widget = Cast<UParadoxSelectionTestWidget>(WidgetComponent->GetUserWidgetObject());
	if (!TestNotNull(TEXT("Configured native widget instance exists"), Widget))
	{
		return false;
	}
	TestEqual(TEXT("Widget context contains selected Actor"), Widget->GetSelectedActor(), static_cast<AActor*>(Actor));
	TestEqual(TEXT("Widget context contains selectable"), Widget->GetSelectableComponent(), Actor->Selectable.Get());
	TestEqual(TEXT("Widget context contains selection authority"), Widget->GetSelectionComponent(), Controller->Selection.Get());
	TestEqual(TEXT("Widget context contains owning controller"), Widget->GetOwningPlayerController(), static_cast<APlayerController*>(Controller));
	TestFalse(TEXT("Empty-world RMB has no selectable widget hit"), Controller->Selection->HandleSelectionPointerHit(FHitResult(), false));
	TestNull(TEXT("Empty-world RMB clears widget selection"), Controller->Selection->GetSelectedActor());
	TestFalse(TEXT("Empty-world RMB hides widget"), WidgetComponent->IsVisible());
	TestEqual(TEXT("Empty-world RMB disables widget collision"), WidgetComponent->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Hidden widget stops camera-facing updates"), Actor->Selectable->IsComponentTickEnabled());
	TestNull(TEXT("Empty-world RMB clears widget Actor context"), Widget->GetSelectedActor());
	TestTrue(TEXT("RMB selection reuses the hidden widget"), Controller->Selection->HandleSelectionPointerHit(Hit, true));
	TestEqual(TEXT("Widget component is reused"), Actor->Selectable->GetInteractionWidget(), WidgetComponent);
	TestTrue(TEXT("Reused widget is visible"), WidgetComponent->IsVisible());

	UWorldStateSubsystem* WorldState = TestWorld.World->GetSubsystem<UWorldStateSubsystem>();
	if (!TestNotNull(TEXT("World State subsystem exists"), WorldState))
	{
		return false;
	}
	FWorldStateRestoreLifecycleContext RestoreContext;
	RestoreContext.Stage = EWorldStateRestoreStage::Preflight;
	WorldState->OnRestoreStartedNative().Broadcast(RestoreContext);
	TestNull(TEXT("Restore Started immediately clears selection"), Controller->Selection->GetSelectedActor());
	TestFalse(TEXT("Restore Started hides widget"), WidgetComponent->IsVisible());
	TestEqual(TEXT("Hidden widget no longer intercepts cursor queries"), WidgetComponent->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestNull(TEXT("Restore Started clears widget Actor context"), Widget->GetSelectedActor());
	Controller->Selection->ResetSelectionState();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxSelectionInteractionCellSessionTest,
	"Paradox.Selection.InteractionCellSessionLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxSelectionInteractionCellSessionTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Selection::Tests;
	FScopedSelectionWorld TestWorld(TEXT("ParadoxSelectionInteractionCellWorld"));
	if (!TestNotNull(TEXT("Interaction-cell test world exists"), TestWorld.World))
	{
		return false;
	}

	AGridNavigationData* NavigationData =
		Spawn<AGridNavigationData>(*TestWorld.World, TEXT("GridNavigationData"));
	AParadoxSelectionTestController* Controller =
		Spawn<AParadoxSelectionTestController>(*TestWorld.World, TEXT("SelectionController"));
	APawn* Requester = Spawn<APawn>(*TestWorld.World, TEXT("Requester"));
	AParadoxSelectionTestActor* Target =
		Spawn<AParadoxSelectionTestActor>(*TestWorld.World, TEXT("InteractionTarget"));
	if (!TestNotNull(TEXT("Navigation data exists"), NavigationData)
		|| !TestNotNull(TEXT("Selection controller exists"), Controller)
		|| !TestNotNull(TEXT("Requester Pawn exists"), Requester)
		|| !TestNotNull(TEXT("Interaction target exists"), Target))
	{
		return false;
	}
	Controller->Possess(Requester);

	const FGuid GridId = FGuid::NewGuid();
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> GridSnapshot =
		MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	GridSnapshot->GridId = GridId;
	GridSnapshot->Revisions.Topology = 1;
	GridSnapshot->Revisions.Traversal = 1;
	GridSnapshot->Revisions.Occupancy = 1;
	FGridRegionData& Region = GridSnapshot->Regions.Add(GridId);
	Region.GridId = GridId;
	Region.GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
	FGridCellData& Cell = GridSnapshot->Cells.AddDefaulted_GetRef();
	Cell.Id.GridId = GridId;
	Cell.Id.Coord = FGridCellCoord(0, 0, 0);
	Cell.WorldCenter = FVector::ZeroVector;
	Cell.bWalkable = true;
	FString PublishError;
	TestTrue(TEXT("Interaction-cell grid publishes"),
		NavigationData->PublishSnapshot(GridSnapshot, &PublishError));

	USmartObjectDefinition* SmartObjectDefinition =
		NewObject<USmartObjectDefinition>(Target, TEXT("SelectionSmartObjectDefinition"));
	FSmartObjectSlotDefinition& Slot = SmartObjectDefinition->DebugAddSlot();
	Slot.ID = FGuid::NewGuid();
	Slot.BehaviorDefinitions.Add(
		NewObject<UParadoxInteractionTestBehaviorDefinition>(SmartObjectDefinition));
	SmartObjectDefinition->SetActivityTags(FGameplayTagContainer(
		ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag()));
	TestTrue(TEXT("Selection Smart Object Definition validates"),
		SmartObjectDefinition->Validate());
	Target->SmartObject->SetDefinition(SmartObjectDefinition);
	Target->Selectable->bShowInteractionCellsWhenSelected = true;
	FParadoxInteractionDefinition& Definition =
		Target->Interaction->InteractionDefinitions.AddDefaulted_GetRef();
	Definition.InteractionTag = ParadoxInteractionTestTags::Primary;
	Definition.SlotActivityRequirements = FGameplayTagQuery::MakeQuery_MatchTag(
		ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag());

	TestWorld.StartPlay();
	USmartObjectSubsystem* SmartObjects =
		USmartObjectSubsystem::GetCurrent(TestWorld.World);
	if (!Target->SmartObject->GetRegisteredHandle().IsValid() && SmartObjects)
	{
		SmartObjects->RegisterSmartObject(Target->SmartObject);
	}
	Target->Interaction->RefreshInteractionSources();
	UGridCellOverlayPresentationSubsystem* Overlays =
		TestWorld.World->GetSubsystem<UGridCellOverlayPresentationSubsystem>();
	UGridRuntimeVisualizationSubsystem* Visualization =
		TestWorld.World->GetSubsystem<UGridRuntimeVisualizationSubsystem>();
	if (!TestNotNull(TEXT("Cell overlay subsystem exists"), Overlays)
		|| !TestNotNull(TEXT("Visualization subsystem exists"), Visualization)
		|| !TestTrue(TEXT("Selection Smart Object registers"),
			Target->SmartObject->GetRegisteredHandle().IsValid()))
	{
		return false;
	}

	const FHitResult Hit = MakeHit(*Target);
	TestTrue(TEXT("RMB selects interaction target"),
		Controller->Selection->HandleSelectionPointerHit(Hit, true));
	TestEqual(TEXT("Selection owns one interaction-cell session"),
		Overlays->GetActiveSessionCount(), 1);
	FGridCellVisualState VisualState;
	TestTrue(TEXT("Selected interaction cell remains readable"),
		Visualization->GetCellVisualState(Cell.Id, VisualState));
	TestEqual(TEXT("Free interaction cell uses Primary overlay"),
		VisualState.OverlayState, EGridCellOverlayVisualState::Primary);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> OccupiedGridSnapshot =
		MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>(*GridSnapshot);
	OccupiedGridSnapshot->Revisions.Occupancy = 2;
	OccupiedGridSnapshot->Cells[0].bOccupied = true;
	OccupiedGridSnapshot->Cells[0].OccupancyOwners.Add(FGuid::NewGuid());
	TestTrue(TEXT("Interaction-cell occupancy change publishes"),
		NavigationData->PublishSnapshot(OccupiedGridSnapshot, &PublishError));
	TestEqual(TEXT("GridWorld refresh preserves one interaction-cell session"),
		Overlays->GetActiveSessionCount(), 1);
	Visualization->GetCellVisualState(Cell.Id, VisualState);
	TestEqual(TEXT("Occupied interaction cell refreshes to Secondary overlay"),
		VisualState.OverlayState, EGridCellOverlayVisualState::Secondary);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> FreeGridSnapshot =
		MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>(*GridSnapshot);
	FreeGridSnapshot->Revisions.Occupancy = 3;
	TestTrue(TEXT("Interaction-cell availability restoration publishes"),
		NavigationData->PublishSnapshot(FreeGridSnapshot, &PublishError));
	Visualization->GetCellVisualState(Cell.Id, VisualState);
	TestEqual(TEXT("Free interaction cell refreshes back to Primary overlay"),
		VisualState.OverlayState, EGridCellOverlayVisualState::Primary);

	Controller->Selection->DeselectCurrentActor();
	TestEqual(TEXT("Deselect releases the interaction-cell session"),
		Overlays->GetActiveSessionCount(), 0);
	Visualization->GetCellVisualState(Cell.Id, VisualState);
	TestEqual(TEXT("Deselect removes only the interaction overlay"),
		VisualState.OverlayState, EGridCellOverlayVisualState::None);

	Controller->Selection->HandleSelectionPointerHit(Hit, true);
	UWorldStateSubsystem* WorldState =
		TestWorld.World->GetSubsystem<UWorldStateSubsystem>();
	FWorldStateRestoreLifecycleContext RestoreContext;
	RestoreContext.Stage = EWorldStateRestoreStage::Preflight;
	WorldState->OnRestoreStartedNative().Broadcast(RestoreContext);
	TestEqual(TEXT("World State reset-start releases the interaction-cell session"),
		Overlays->GetActiveSessionCount(), 0);

	Controller->Selection->HandleSelectionPointerHit(Hit, true);
	Target->Destroy();
	TestNull(TEXT("Destroyed selected Actor clears selection"),
		Controller->Selection->GetSelectedActor());
	TestEqual(TEXT("Destroyed selected Actor releases the interaction-cell session"),
		Overlays->GetActiveSessionCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxSelectionNativeCompositionTest,
	"Paradox.Selection.NativeActorComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxSelectionNativeCompositionTest::RunTest(const FString& Parameters)
{
	const APressurePlate* PressurePlate = GetDefault<APressurePlate>();
	const AParadoxVerticalBarrier* VerticalBarrier = GetDefault<AParadoxVerticalBarrier>();
	const AParadoxChronoSpawn* ChronoSpawn = GetDefault<AParadoxChronoSpawn>();
	TestNotNull(TEXT("Pressure Plate owns native Selectable Component"), PressurePlate ? PressurePlate->SelectableComponent.Get() : nullptr);
	TestNotNull(TEXT("Vertical Barrier owns native Selectable Component"), VerticalBarrier ? VerticalBarrier->SelectableComponent.Get() : nullptr);
	TestNotNull(TEXT("Chrono Spawn owns native Selectable Component"), ChronoSpawn ? ChronoSpawn->GetSelectableComponent() : nullptr);
	TestNotNull(TEXT("Pressure Plate owns native Paradox Interaction Component"), PressurePlate ? PressurePlate->InteractionComponent.Get() : nullptr);
	TestNotNull(TEXT("Vertical Barrier owns native Paradox Interaction Component"), VerticalBarrier ? VerticalBarrier->InteractionComponent.Get() : nullptr);
	TestNotNull(TEXT("Pressure Plate owns native Smart Object Component"), PressurePlate ? PressurePlate->SmartObjectComponent.Get() : nullptr);
	TestNotNull(TEXT("Vertical Barrier owns native Smart Object Component"), VerticalBarrier ? VerticalBarrier->SmartObjectComponent.Get() : nullptr);
	TestNull(TEXT("Chrono Spawn intentionally has no Paradox Interaction Component"),
		ChronoSpawn ? ChronoSpawn->FindComponentByClass<UParadoxInteractionComponent>() : nullptr);
	TestNull(TEXT("Chrono Spawn intentionally has no Smart Object Component"),
		ChronoSpawn ? ChronoSpawn->FindComponentByClass<USmartObjectComponent>() : nullptr);
	return true;
}

#endif
