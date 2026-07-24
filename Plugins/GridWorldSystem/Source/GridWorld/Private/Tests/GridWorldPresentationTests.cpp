// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interaction/GridCellPointerComponent.h"
#include "Materials/Material.h"
#include "Navigation/GridNavigationData.h"
#include "PhysicsEngine/BodySetup.h"
#include "Presentation/GridCellVisualStyle.h"
#include "Presentation/GridPathPresentationSubsystem.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "Subsystems/GridWorldSubsystem.h"

namespace UE::GridWorld::PresentationTests
{
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeSnapshot(
		int64 TopologyRevision,
		TConstArrayView<FGridCellCoord> CellCoords,
		int32 BlockedCellIndex = INDEX_NONE)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = FGuid::NewGuid();
		Snapshot->GridTransform.Origin = FVector::ZeroVector;
		Snapshot->GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
		Snapshot->Revisions.Topology = TopologyRevision;
		Snapshot->Revisions.Traversal = 1;
		Snapshot->Revisions.Occupancy = 1;
		for (int32 CellIndex = 0; CellIndex < CellCoords.Num(); ++CellIndex)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = Snapshot->GridId;
			Cell.Id.Coord = CellCoords[CellIndex];
			Cell.bWalkable = CellIndex != BlockedCellIndex;
			Cell.bAuthoredBlocked = CellIndex == BlockedCellIndex;
		}
		return Snapshot;
	}

	bool SameRevisions(const FGridRevisionSet& Left, const FGridRevisionSet& Right)
	{
		return Left.Topology == Right.Topology
			&& Left.Traversal == Right.Traversal
			&& Left.Occupancy == Right.Occupancy;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridRuntimeVisualizationStateTest,
	"GridWorld.Presentation.MaterialDataAndLayering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridRuntimeVisualizationStateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Interaction custom-data channel"), FGridCellMaterialDataLayout::InteractionState, 0);
	TestEqual(TEXT("Path custom-data channel"), FGridCellMaterialDataLayout::PathState, 1);
	TestEqual(TEXT("Navigation flags custom-data channel"), FGridCellMaterialDataLayout::NavigationFlags, 2);
	TestEqual(TEXT("Emphasis custom-data channel"), FGridCellMaterialDataLayout::Emphasis, 3);
	TestEqual(TEXT("Resolved red custom-data channel"), FGridCellMaterialDataLayout::ResolvedRed, 4);
	TestEqual(TEXT("Resolved alpha custom-data channel"), FGridCellMaterialDataLayout::ResolvedAlpha, 7);
	TestEqual(TEXT("Path progress custom-data channel"), FGridCellMaterialDataLayout::PathProgress, 8);
	TestEqual(TEXT("Custom value custom-data channel"), FGridCellMaterialDataLayout::CustomStyleValue, 9);
	TestEqual(TEXT("Custom-data float count"), FGridCellMaterialDataLayout::NumFloats, 10);

	FGridCellVisualState State;
	State.InteractionState = EGridCellInteractionVisualState::Selected;
	State.PathState = EGridCellPathVisualState::Destination;
	State.NavigationFlags = static_cast<int32>(EGridCellNavigationVisualFlags::Blocked | EGridCellNavigationVisualFlags::Occupied);
	State.Emphasis = 0.75f;
	State.PathProgress = 0.5f;
	State.CustomStyleValue = 9.0f;
	const FLinearColor ResolvedColor(0.1f, 0.2f, 0.3f, 0.4f);
	float Data[FGridCellMaterialDataLayout::NumFloats];
	FGridCellMaterialDataLayout::Write(State, ResolvedColor, Data);
	TestEqual(TEXT("Interaction is serialized"), Data[0], 2.0f);
	TestEqual(TEXT("Path is serialized independently"), Data[1], static_cast<float>(EGridCellPathVisualState::Destination));
	TestEqual(TEXT("Navigation flags are serialized independently"), Data[2], static_cast<float>(State.NavigationFlags));
	TestEqual(TEXT("Resolved alpha is serialized"), Data[7], 0.4f);
	TestEqual(TEXT("Path progress is serialized"), Data[8], 0.5f);
	TestEqual(TEXT("Custom value is serialized"), Data[9], 9.0f);

	const UGridCellVisualStyle* NativeStyle = GetDefault<UGridCellVisualStyle>();
	TestNotNull(TEXT("Native style defaults exist"), NativeStyle);
	if (NativeStyle != nullptr)
	{
		TestEqual(TEXT("Selection has visual priority over path/navigation"), NativeStyle->ResolveColor(State), NativeStyle->SelectedColor);
		State.InteractionState = EGridCellInteractionVisualState::Hovered;
		TestEqual(TEXT("Hover has priority when not selected"), NativeStyle->ResolveColor(State), NativeStyle->HoveredColor);
		State.InteractionState = EGridCellInteractionVisualState::Unselected;
		TestEqual(TEXT("Path layer remains independently resolvable"), NativeStyle->ResolveColor(State), NativeStyle->DestinationColor);
	}

	UGridCellVisualStyle* DefaultStyle = LoadObject<UGridCellVisualStyle>(
		nullptr,
		TEXT("/GridWorldSystem/Presentation/DA_GridRuntimeCellStyle_Default.DA_GridRuntimeCellStyle_Default"));
	TestNotNull(TEXT("Plugin default style loads"), DefaultStyle);
	if (DefaultStyle != nullptr)
	{
		FString ValidationError;
		TestTrue(TEXT("Plugin default style validates"), DefaultStyle->Validate(ValidationError));
		TestNotNull(TEXT("Style cell mesh loads"), DefaultStyle->CellMesh.Get());
		TestNotNull(TEXT("Plugin material loads through the style"), DefaultStyle->CellMaterial.Get());
		const UMaterial* Material = Cast<UMaterial>(DefaultStyle->CellMaterial);
		if (TestNotNull(TEXT("Default material is a base material"), Material))
		{
			TestEqual(TEXT("Default material is translucent"), Material->GetBlendMode(), BLEND_Translucent);
			TestTrue(TEXT("Default material is two-sided"), Material->IsTwoSided());
			TestFalse(TEXT("Default material remains depth-tested"), Material->bDisableDepthTest);
			TestTrue(TEXT("Default material is unlit"), Material->GetShadingModels().HasShadingModel(MSM_Unlit));
			TestTrue(
				TEXT("Default material supports HISM rendering"),
				Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
		}
	}
	const UStaticMesh* RuntimePlane = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/GridWorldSystem/Presentation/SM_GridRuntimeCell.SM_GridRuntimeCell"));
	if (TestNotNull(TEXT("Plugin runtime plane loads independently from the selected style"), RuntimePlane)
		&& RuntimePlane->GetBodySetup() != nullptr)
	{
		TestEqual(
			TEXT("Plugin runtime plane has no simple collision"),
			RuntimePlane->GetBodySetup()->AggGeom.GetElementCount(),
			0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridRuntimeVisualizationLifecycleTest,
	"GridWorld.Presentation.LifecycleMappingAndRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridRuntimeVisualizationLifecycleTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("UWorld::DestroyActor: World has no context"), EAutomationExpectedErrorFlags::Contains, 2);
#if WITH_EDITOR
	UWorld* DedicatedWorld = UWorld::CreateWorld(
		EWorldType::PIE,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldDedicatedPresentationTest")),
		nullptr,
		false,
		ERHIFeatureLevel::Num,
		nullptr,
		true);
	if (TestNotNull(TEXT("Uninitialized dedicated-like PIE world"), DedicatedWorld))
	{
		DedicatedWorld->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
		DedicatedWorld->InitWorld(UWorld::InitializationValues()
			.CreatePhysicsScene(false)
			.ShouldSimulatePhysics(false)
			.EnableTraceCollision(false)
			.CreateNavigation(false)
			.CreateAISystem(false));
		TestNull(
			TEXT("Runtime visualization subsystem is not created for a dedicated server world"),
			DedicatedWorld->GetSubsystem<UGridRuntimeVisualizationSubsystem>());
		TestNull(
			TEXT("Path presentation subsystem is not created for a dedicated server world"),
			DedicatedWorld->GetSubsystem<UGridPathPresentationSubsystem>());
		DedicatedWorld->DestroyWorld(false);
	}
#endif
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldPresentationTest")));
	if (!TestNotNull(TEXT("Transient presentation world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	UGridRuntimeVisualizationSubsystem* Visualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>();
	if (!TestNotNull(TEXT("Grid navigation data"), NavData)
		|| !TestNotNull(TEXT("Runtime visualization subsystem"), Visualization))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestFalse(TEXT("Visualization subsystem is not created for Editor worlds"), Visualization->DoesSupportWorldType(EWorldType::Editor));
	TestFalse(TEXT("Visualization is disabled by default"), Visualization->IsVisualizationEnabled());

	const TArray<FGridCellCoord> InitialCoords{
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(16, 0, 0),
		FGridCellCoord(17, 0, 0)};
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> InitialSnapshot =
		UE::GridWorld::PresentationTests::MakeSnapshot(1, InitialCoords);
	FString PublishError;
	if (!TestTrue(TEXT("Presentation topology publishes"), NavData->PublishSnapshot(InitialSnapshot, &PublishError)))
	{
		AddError(PublishError);
		World->DestroyWorld(false);
		return false;
	}
	const FGridWorldSnapshotPtr AuthoritativeSnapshot = NavData->GetSnapshot();
	const FGridRevisionSet InitialRevisions = NavData->GetPublishedRevisions();

	if (!TestTrue(TEXT("Default visualization enables"), Visualization->EnableVisualization()))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("Every published cell is mapped"), Visualization->GetVisualizedCellCount(), InitialCoords.Num());
	TestEqual(TEXT("One HISM is created per 16x16 chunk"), Visualization->ChunkRenderers.Num(), 2);
	TestTrue(TEXT("Visualization does not replace the snapshot"), NavData->GetSnapshot() == AuthoritativeSnapshot);
	TestTrue(TEXT("Visualization does not alter navigation revisions"), UE::GridWorld::PresentationTests::SameRevisions(NavData->GetPublishedRevisions(), InitialRevisions));

	for (const TPair<FGridChunkCoord, UGridRuntimeVisualizationSubsystem::FGridChunkRenderer>& Pair : Visualization->ChunkRenderers)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = Pair.Value.Component.Get();
		if (TestNotNull(TEXT("Chunk HISM exists"), Component))
		{
			TestEqual(TEXT("Chunk HISM collision is disabled"), Component->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
			TestFalse(TEXT("Chunk HISM never affects navigation"), Component->CanEverAffectNavigation());
			TestEqual(TEXT("Chunk HISM uses ten custom floats"), Component->NumCustomDataFloats, FGridCellMaterialDataLayout::NumFloats);
			TestEqual(
				TEXT("Chunk HISM uses the style material override"),
				Component->GetMaterial(0),
				Visualization->ActiveStyle->CellMaterial.Get());
		}
	}

	const FGridCellId FirstCell = NavData->GetSnapshot()->Cells[0].Id;
	const FGridCellId SecondCell = NavData->GetSnapshot()->Cells[1].Id;
	UGridRuntimeVisualizationSubsystem::FGridCellVisualHandle FirstHandle;
	UGridRuntimeVisualizationSubsystem::FGridCellVisualHandle SecondHandle;
	TestTrue(TEXT("First stable cell has an internal handle"), Visualization->GetVisualHandleForTesting(FirstCell, FirstHandle));
	TestTrue(TEXT("Second stable cell has an internal handle"), Visualization->GetVisualHandleForTesting(SecondCell, SecondHandle));
	TestEqual(TEXT("Sorted cells share their chunk renderer"), FirstHandle.RendererId, SecondHandle.RendererId);
	TestEqual(TEXT("Deterministic first instance index"), FirstHandle.InstanceIndex, 0);
	TestEqual(TEXT("Deterministic second instance index"), SecondHandle.InstanceIndex, 1);

	UHierarchicalInstancedStaticMeshComponent* FirstRenderer = nullptr;
	for (const TPair<FGridChunkCoord, UGridRuntimeVisualizationSubsystem::FGridChunkRenderer>& Pair : Visualization->ChunkRenderers)
	{
		if (Pair.Value.RendererId == FirstHandle.RendererId)
		{
			FirstRenderer = Pair.Value.Component.Get();
			break;
		}
	}
	if (TestNotNull(TEXT("Mapped renderer exists"), FirstRenderer))
	{
		TArray<float> FirstCellDataBefore;
		TArray<float> SecondCellDataBefore;
		for (int32 Offset = 0; Offset < FGridCellMaterialDataLayout::NumFloats; ++Offset)
		{
			FirstCellDataBefore.Add(FirstRenderer->PerInstanceSMCustomData[FirstHandle.InstanceIndex * FGridCellMaterialDataLayout::NumFloats + Offset]);
			SecondCellDataBefore.Add(FirstRenderer->PerInstanceSMCustomData[SecondHandle.InstanceIndex * FGridCellMaterialDataLayout::NumFloats + Offset]);
		}
		TestTrue(TEXT("Hover update succeeds"), Visualization->SetCellHovered(SecondCell, true));
		for (int32 Offset = 0; Offset < FGridCellMaterialDataLayout::NumFloats; ++Offset)
		{
			TestEqual(
				FString::Printf(TEXT("Hover update leaves first cell channel %d unchanged"), Offset),
				FirstRenderer->PerInstanceSMCustomData[FirstHandle.InstanceIndex * FGridCellMaterialDataLayout::NumFloats + Offset],
				FirstCellDataBefore[Offset]);
		}
		TestNotEqual(
			TEXT("Hover update changes only the target interaction slice"),
			FirstRenderer->PerInstanceSMCustomData[SecondHandle.InstanceIndex * FGridCellMaterialDataLayout::NumFloats + FGridCellMaterialDataLayout::InteractionState],
			SecondCellDataBefore[FGridCellMaterialDataLayout::InteractionState]);
	}

	TestTrue(TEXT("Selection update succeeds"), Visualization->SetCellSelected(SecondCell, true));
	TestTrue(TEXT("Path layer can coexist internally"), Visualization->SetCellPathStateInternal(SecondCell, EGridCellPathVisualState::Preview));
	Visualization->ClearHoveredCells();
	FGridCellVisualState ResolvedState;
	TestTrue(TEXT("Selected state remains readable"), Visualization->GetCellVisualState(SecondCell, ResolvedState));
	TestEqual(TEXT("Clearing hover preserves selection"), ResolvedState.InteractionState, EGridCellInteractionVisualState::Selected);
	TestEqual(TEXT("Path state remains separate from selection"), ResolvedState.PathState, EGridCellPathVisualState::Preview);

	Visualization->SetVisualizationVisible(false);
	TestFalse(TEXT("Allocated visualization can be hidden"), Visualization->IsVisualizationVisible());
	TestTrue(TEXT("Hidden visualization keeps mappings allocated"), Visualization->IsCellVisualized(FirstCell));
	Visualization->SetVisualizationVisible(true);
	TestTrue(TEXT("Allocated visualization can be shown"), Visualization->IsVisualizationVisible());

	const int64 GenerationBeforeExplicitRebuild = Visualization->GetRendererGeneration();
	TestTrue(TEXT("Explicit renderer rebuild succeeds"), Visualization->RebuildVisualization());
	UGridRuntimeVisualizationSubsystem::FGridCellVisualHandle RebuiltFirstHandle;
	TestTrue(TEXT("Rebuilt cell remains mapped"), Visualization->GetVisualHandleForTesting(FirstCell, RebuiltFirstHandle));
	TestEqual(TEXT("Rebuild preserves deterministic instance index"), RebuiltFirstHandle.InstanceIndex, FirstHandle.InstanceIndex);
	TestTrue(TEXT("Rebuild invalidates the old handle generation"), RebuiltFirstHandle.RendererGeneration > FirstHandle.RendererGeneration);
	TestTrue(TEXT("Renderer generation advances"), Visualization->GetRendererGeneration() > GenerationBeforeExplicitRebuild);

	Visualization->DisableVisualization();
	TestFalse(TEXT("Disable releases renderer state"), Visualization->IsVisualizationEnabled());
	TestEqual(TEXT("Disable releases cell mappings"), Visualization->GetVisualizedCellCount(), 0);
	TestTrue(TEXT("Disable retains semantic selection"), Visualization->GetCellVisualState(SecondCell, ResolvedState));
	TestEqual(TEXT("Retained semantic state is still selected"), ResolvedState.InteractionState, EGridCellInteractionVisualState::Selected);
	TestTrue(TEXT("Re-enable rebuilds released resources"), Visualization->EnableVisualization());

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> OverlayLikeSnapshot =
		MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>(*NavData->GetSnapshot());
	OverlayLikeSnapshot->Revisions.Traversal += 1;
	OverlayLikeSnapshot->Revisions.Occupancy += 1;
	OverlayLikeSnapshot->Cells[0].TraversalCost = 2000;
	OverlayLikeSnapshot->Cells[0].bOccupied = true;
	OverlayLikeSnapshot->Cells[0].ReservationOwners.Add(FGuid::NewGuid());
	const int64 GenerationBeforeOverlay = Visualization->GetRendererGeneration();
	TestTrue(TEXT("Overlay-like publication succeeds"), NavData->PublishSnapshot(OverlayLikeSnapshot, &PublishError));
	TestEqual(TEXT("Traversal/occupancy refresh does not rebuild mappings"), Visualization->GetRendererGeneration(), GenerationBeforeOverlay);
	TestTrue(TEXT("Overlay state refreshes through OnGridWorldChanged"), Visualization->GetCellVisualState(FirstCell, ResolvedState));
	const EGridCellNavigationVisualFlags RefreshedFlags = static_cast<EGridCellNavigationVisualFlags>(ResolvedState.NavigationFlags);
	TestTrue(TEXT("High-cost flag refreshes"), EnumHasAnyFlags(RefreshedFlags, EGridCellNavigationVisualFlags::HighCost));
	TestTrue(TEXT("Occupied flag refreshes"), EnumHasAnyFlags(RefreshedFlags, EGridCellNavigationVisualFlags::Occupied));
	TestTrue(TEXT("Reserved flag refreshes"), EnumHasAnyFlags(RefreshedFlags, EGridCellNavigationVisualFlags::Reserved));

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> TopologySnapshot =
		MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>(*NavData->GetSnapshot());
	TopologySnapshot->Revisions.Topology += 1;
	FGridCellData& AddedCell = TopologySnapshot->Cells.AddDefaulted_GetRef();
	AddedCell.Id.GridId = TopologySnapshot->GridId;
	AddedCell.Id.Coord = FGridCellCoord(32, 0, 0);
	const int64 GenerationBeforeTopology = Visualization->GetRendererGeneration();
	TestTrue(TEXT("Topology replacement publishes"), NavData->PublishSnapshot(TopologySnapshot, &PublishError));
	TestTrue(TEXT("Topology notification rebuilds presentation"), Visualization->GetRendererGeneration() > GenerationBeforeTopology);
	TestEqual(TEXT("Topology rebuild maps the new cell"), Visualization->GetVisualizedCellCount(), 5);

	NavData->ClearGridWorld();
	TestEqual(TEXT("Clear notification removes every mapped cell"), Visualization->GetVisualizedCellCount(), 0);
	TestFalse(TEXT("Clear prunes stale semantic state"), Visualization->GetCellVisualState(SecondCell, ResolvedState));
	Visualization->DisableVisualization();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridCellPointerTest,
	"GridWorld.Presentation.PointerProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridCellPointerTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("UWorld::DestroyActor: World has no context"), EAutomationExpectedErrorFlags::Contains, 1);
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldPointerTest")));
	if (!TestNotNull(TEXT("Transient pointer world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	const TArray<FGridCellCoord> Coords{FGridCellCoord(0, 0, 0), FGridCellCoord(2, 0, 0)};
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot =
		UE::GridWorld::PresentationTests::MakeSnapshot(1, Coords, 1);
	FString PublishError;
	if (!TestNotNull(TEXT("Pointer navigation data"), NavData)
		|| !TestTrue(TEXT("Pointer topology publishes"), NavData->PublishSnapshot(Snapshot, &PublishError)))
	{
		World->DestroyWorld(false);
		return false;
	}
	UGridRuntimeVisualizationSubsystem* Visualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>();
	TestNotNull(TEXT("Pointer visualization subsystem"), Visualization);
	if (Visualization != nullptr)
	{
		TestTrue(TEXT("Pointer test visualization enables"), Visualization->EnableVisualization());
	}

	AActor* PointerOwner = World->SpawnActor<AActor>();
	UGridCellPointerComponent* Pointer = NewObject<UGridCellPointerComponent>(PointerOwner);
	PointerOwner->AddInstanceComponent(Pointer);
	Pointer->ProjectionExtent = FVector(40.0, 40.0, 100.0);
	Pointer->RegisterComponent();

	FHitResult FirstHit;
	FirstHit.bBlockingHit = true;
	FirstHit.ImpactPoint = Snapshot->GridTransform.CellToWorld(Coords[0]);
	FGridCellPointerResult Result = Pointer->UpdateFromHitResult(FirstHit);
	TestEqual(TEXT("Hit projection succeeds"), Result.Status, EGridCellPointerStatus::Success);
	TestEqual(TEXT("Hit resolves the stable first cell"), Result.CellId, NavData->GetSnapshot()->Cells[0].Id);
	const FGridCellId StableHoveredCell = Pointer->GetHoveredCell();
	Pointer->UpdateFromHitResult(FirstHit);
	TestEqual(TEXT("Repeated hit keeps the same deduplicated target"), Pointer->GetHoveredCell(), StableHoveredCell);
	if (Visualization != nullptr)
	{
		FGridCellVisualState VisualState;
		TestTrue(TEXT("Automatic hover is readable"), Visualization->GetCellVisualState(StableHoveredCell, VisualState));
		TestEqual(TEXT("Pointer applies hover only"), VisualState.InteractionState, EGridCellInteractionVisualState::Hovered);
		Pointer->bApplyHoverToVisualization = false;
		Pointer->UpdateFromHitResult(FirstHit);
		TestTrue(TEXT("Hover state remains readable after disabling mirroring"), Visualization->GetCellVisualState(StableHoveredCell, VisualState));
		TestEqual(TEXT("Disabling mirroring clears the contribution without changing the target"), VisualState.InteractionState, EGridCellInteractionVisualState::Unselected);
		Pointer->bApplyHoverToVisualization = true;
		Pointer->UpdateFromHitResult(FirstHit);
		TestTrue(TEXT("Re-enabled hover mirroring is readable"), Visualization->GetCellVisualState(StableHoveredCell, VisualState));
		TestEqual(TEXT("Re-enabling mirroring reapplies the same target"), VisualState.InteractionState, EGridCellInteractionVisualState::Hovered);
		Visualization->SetCellSelected(StableHoveredCell, true);
	}

	FHitResult BlockedHit;
	BlockedHit.bBlockingHit = true;
	BlockedHit.ImpactPoint = Snapshot->GridTransform.CellToWorld(Coords[1]);
	Result = Pointer->UpdateFromHitResult(BlockedHit);
	TestEqual(TEXT("Navigable-only policy rejects blocked cells"), Result.Status, EGridCellPointerStatus::NoCell);
	Pointer->ProjectionPolicy = EGridCellPointerPolicy::ExistingCells;
	Result = Pointer->UpdateFromHitResult(BlockedHit);
	TestEqual(TEXT("Existing-cells policy accepts blocked cells"), Result.Status, EGridCellPointerStatus::Success);
	TestFalse(TEXT("Existing-cells result reports non-walkable state"), Result.bWalkable);

	Pointer->ProjectionPolicy = EGridCellPointerPolicy::NavigableOnly;
	AActor* SurfaceActor = World->SpawnActor<AActor>();
	UBoxComponent* Surface = NewObject<UBoxComponent>(SurfaceActor);
	SurfaceActor->AddInstanceComponent(Surface);
	SurfaceActor->SetRootComponent(Surface);
	Surface->SetBoxExtent(FVector(40.0, 40.0, 10.0));
	Surface->SetWorldLocation(Snapshot->GridTransform.CellToWorld(Coords[0]) - FVector(0.0, 0.0, 10.0));
	Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Surface->SetCollisionObjectType(ECC_WorldStatic);
	Surface->SetCollisionResponseToAllChannels(ECR_Ignore);
	Surface->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Surface->RegisterComponent();
	const FVector RayStart = Snapshot->GridTransform.CellToWorld(Coords[0]) + FVector(0.0, 0.0, 100.0);
	Result = Pointer->UpdateFromWorldRay(RayStart, FVector::DownVector, 500.0);
	TestEqual(TEXT("World ray resolves a cell"), Result.Status, EGridCellPointerStatus::Success);
	TestTrue(TEXT("World ray hits gameplay geometry, not the collisionless HISM"), Result.HitResult.GetComponent() == Surface);

	FHitResult Miss;
	Pointer->UpdateFromHitResult(Miss);
	TestFalse(TEXT("Configured miss clears pointer hover"), Pointer->HasHoveredCell());
	if (Visualization != nullptr)
	{
		FGridCellVisualState VisualState;
		TestTrue(TEXT("Selected cell remains present after pointer clear"), Visualization->GetCellVisualState(StableHoveredCell, VisualState));
		TestEqual(TEXT("Pointer clear never cancels selection"), VisualState.InteractionState, EGridCellInteractionVisualState::Selected);
		Visualization->DisableVisualization();
	}
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
