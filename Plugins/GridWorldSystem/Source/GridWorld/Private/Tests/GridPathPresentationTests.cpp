// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/GridWorldAIController.h"
#include "AI/GridWorldPathFollowingComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "Presentation/GridCellVisualStyle.h"
#include "Presentation/GridPathLineVisualStyle.h"
#include "Presentation/GridPathLineVisualizationSubsystem.h"
#include "Presentation/GridPathPresentationSubsystem.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "UObject/GarbageCollection.h"

namespace UE::GridWorld::PathPresentationTests
{
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakePathSnapshot(
		const FGuid& GridId,
		int64 TopologyRevision,
		TConstArrayView<FGridCellCoord> Coords)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = GridId;
		Snapshot->Revisions.Topology = TopologyRevision;
		Snapshot->Revisions.Traversal = 1;
		Snapshot->Revisions.Occupancy = 1;
		FGridRegionData& Region = Snapshot->Regions.Add(GridId);
		Region.GridId = GridId;
		Region.GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
		Region.PathFollowingStyle = EGridPathFollowingStyle::CellByCell;
		for (const FGridCellCoord& Coord : Coords)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = GridId;
			Cell.Id.Coord = Coord;
			Cell.WorldCenter = Region.GridTransform.CellToWorld(Coord);
		}
		for (int32 CellIndex = 1; CellIndex < Snapshot->Cells.Num(); ++CellIndex)
		{
			Snapshot->Cells[CellIndex - 1].Neighbors.Add(CellIndex);
			Snapshot->Cells[CellIndex].Neighbors.Add(CellIndex - 1);
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
	FGridPathPresentationSessionTest,
	"GridWorld.Presentation.Path.SessionLifecycleAndOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridPathPresentationSessionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridPathPresentationSessionTest")));
	if (!TestNotNull(TEXT("Transient path-presentation world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>();
	UGridRuntimeVisualizationSubsystem* Visualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>();
	if (!TestNotNull(TEXT("Navigation data"), NavData)
		|| !TestNotNull(TEXT("Path presentation subsystem"), Presentation)
		|| !TestNotNull(TEXT("Runtime visualization subsystem"), Visualization))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FGuid GridId = FGuid::NewGuid();
	const TArray<FGridCellCoord> Coords{
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(2, 0, 0),
		FGridCellCoord(2, 1, 0),
		FGridCellCoord(2, 2, 0)};
	FString PublishError;
	TestTrue(
		TEXT("Path-presentation topology publishes"),
		NavData->PublishSnapshot(UE::GridWorld::PathPresentationTests::MakePathSnapshot(GridId, 1, Coords), &PublishError));
	const FGridWorldSnapshotPtr AuthoritativeSnapshot = NavData->GetSnapshot();
	const FGridRevisionSet RevisionsBeforePresentation = NavData->GetPublishedRevisions();
	TArray<FGridCellId> Cells;
	for (const FGridCellData& Cell : AuthoritativeSnapshot->Cells)
	{
		Cells.Add(Cell.Id);
	}

	FGridPathPresentationRequest PreviewRequest;
	PreviewRequest.Cells = Cells;
	PreviewRequest.SourceRevisions = AuthoritativeSnapshot->Revisions;
	PreviewRequest.Purpose = EGridPathPresentationPurpose::Preview;
	PreviewRequest.Priority = 0;
	FGridPathPresentationHandle PreviewHandle;
	TestTrue(TEXT("Preview session creates"), Presentation->CreatePathPresentation(PreviewRequest, PreviewHandle));
	TestTrue(TEXT("Preview handle validates"), Presentation->IsPathPresentationValid(PreviewHandle));
	TestEqual(TEXT("One session is live"), Presentation->GetActiveSessionCount(), 1);

	FGridCellVisualState VisualState;
	TestTrue(TEXT("Preview state is readable while renderer is disabled"), Visualization->GetCellVisualState(Cells[2], VisualState));
	TestEqual(TEXT("Preview contributes through the path layer"), VisualState.PathState, EGridCellPathVisualState::Preview);
	TestEqual(TEXT("Path progress is normalized"), VisualState.PathProgress, 0.5f);

	FGridPathPresentationRequest ActiveRequest;
	ActiveRequest.Cells = Cells;
	ActiveRequest.Purpose = EGridPathPresentationPurpose::Active;
	ActiveRequest.ProgressMode = EGridPathProgressPresentationMode::TraversedAndRemaining;
	ActiveRequest.CurrentCellIndex = 2;
	FGridPathPresentationHandle ActiveHandle;
	TestTrue(TEXT("Active session creates"), Presentation->CreatePathPresentation(ActiveRequest, ActiveHandle));
	TestTrue(TEXT("Renderer enables with live path sessions"), Visualization->EnableVisualization());
	const int64 RendererGenerationBeforeSessionUpdates = Visualization->GetRendererGeneration();
	TestTrue(TEXT("Traversed state resolves"), Visualization->GetCellVisualState(Cells[0], VisualState));
	TestEqual(TEXT("Active purpose wins equal-priority overlap"), VisualState.PathState, EGridCellPathVisualState::ActiveTraversed);
	TestTrue(TEXT("Current state resolves"), Visualization->GetCellVisualState(Cells[2], VisualState));
	TestEqual(TEXT("Current cell is distinct"), VisualState.PathState, EGridCellPathVisualState::ActiveCurrent);
	TestTrue(TEXT("Destination state resolves"), Visualization->GetCellVisualState(Cells.Last(), VisualState));
	TestEqual(TEXT("Active destination is distinct"), VisualState.PathState, EGridCellPathVisualState::Destination);

	TestTrue(TEXT("Preview priority changes"), Presentation->SetPathPresentationPriority(PreviewHandle, 10));
	TestTrue(TEXT("Overlap remains readable"), Visualization->GetCellVisualState(Cells[2], VisualState));
	TestEqual(TEXT("Explicit priority wins before purpose"), VisualState.PathState, EGridCellPathVisualState::Preview);
	TestTrue(TEXT("Preview priority restores"), Presentation->SetPathPresentationPriority(PreviewHandle, -1));
	TestTrue(TEXT("Active session hides without release"), Presentation->SetPathPresentationVisible(ActiveHandle, false));
	TestTrue(TEXT("Hidden overlap remains readable"), Visualization->GetCellVisualState(Cells[2], VisualState));
	TestEqual(TEXT("Hidden active session reveals preview"), VisualState.PathState, EGridCellPathVisualState::Preview);
	TestTrue(TEXT("Active session shows without rebuild"), Presentation->SetPathPresentationVisible(ActiveHandle, true));
	TestTrue(TEXT("Progress update succeeds"), Presentation->UpdatePathPresentationProgress(ActiveHandle, 3));
	TestEqual(
		TEXT("Progress and visibility updates do not rebuild HISM mappings"),
		Visualization->GetRendererGeneration(),
		RendererGenerationBeforeSessionUpdates);
	TestTrue(TEXT("New current state resolves"), Visualization->GetCellVisualState(Cells[3], VisualState));
	TestEqual(TEXT("Progress update moves current"), VisualState.PathState, EGridCellPathVisualState::ActiveCurrent);

	TestTrue(TEXT("Selection coexists"), Visualization->SetCellSelected(Cells[3], true));
	TestTrue(TEXT("Layered state remains readable"), Visualization->GetCellVisualState(Cells[3], VisualState));
	TestEqual(TEXT("Selection is independent"), VisualState.InteractionState, EGridCellInteractionVisualState::Selected);
	TestEqual(TEXT("Path state survives selection"), VisualState.PathState, EGridCellPathVisualState::ActiveCurrent);

	const TArray<FGridCellId> Replacement{Cells[2], Cells[3], Cells[4]};
	TestTrue(TEXT("Immediate replacement succeeds"), Presentation->UpdatePathPresentation(ActiveHandle, Replacement, 0));
	TestTrue(TEXT("Obsolete cell remains queryable"), Visualization->GetCellVisualState(Cells[0], VisualState));
	TestEqual(TEXT("Immediate replacement clears obsolete active contribution"), VisualState.PathState, EGridCellPathVisualState::Preview);
	TestTrue(TEXT("Preview releases independently"), Presentation->ReleasePathPresentation(PreviewHandle));
	TestFalse(TEXT("Released preview handle is stale"), Presentation->IsPathPresentationValid(PreviewHandle));
	TestFalse(TEXT("Repeated release fails safely"), Presentation->ReleasePathPresentation(PreviewHandle));
	TestTrue(TEXT("Obsolete cell remains a published cell"), Visualization->GetCellVisualState(Cells[0], VisualState));
	TestEqual(TEXT("Releasing preview removes only its contribution"), VisualState.PathState, EGridCellPathVisualState::None);

	FGridPathPresentationRequest PreserveRequest = ActiveRequest;
	PreserveRequest.ReplacementPolicy = EGridPathReplacementPolicy::PreserveTraversed;
	PreserveRequest.CurrentCellIndex = 3;
	PreserveRequest.Priority = 20;
	FGridPathPresentationHandle PreserveHandle;
	TestTrue(TEXT("Preserve-traversed session creates"), Presentation->CreatePathPresentation(PreserveRequest, PreserveHandle));
	TestTrue(TEXT("Preserve-traversed replacement succeeds"), Presentation->UpdatePathPresentation(PreserveHandle, Replacement, 0));
	TestTrue(TEXT("Preserved history remains readable"), Visualization->GetCellVisualState(Cells[0], VisualState));
	TestEqual(TEXT("Only already traversed obsolete cells are preserved"), VisualState.PathState, EGridCellPathVisualState::ActiveTraversed);
	TestTrue(TEXT("Preserve session releases independently"), Presentation->ReleasePathPresentation(PreserveHandle));
	TestTrue(TEXT("Released preserved history cell remains published"), Visualization->GetCellVisualState(Cells[0], VisualState));
	TestEqual(TEXT("Releasing preserve session clears its history"), VisualState.PathState, EGridCellPathVisualState::None);

	FGridPathQueryResult QueryResult;
	QueryResult.Status = EGridQueryStatus::Partial;
	QueryResult.Cells = Cells;
	QueryResult.Revisions = AuthoritativeSnapshot->Revisions;
	FGridPathPresentationRequest QueryTemplate;
	QueryTemplate.bVisible = false;
	FGridPathPresentationHandle QueryHandle;
	TestTrue(
		TEXT("Successful or partial Blueprint query results create sessions"),
		Presentation->CreatePathPresentationFromQueryResult(QueryResult, QueryTemplate, QueryHandle));
	FGridPathPresentationSnapshot QuerySnapshot;
	TestTrue(TEXT("Query-result session is inspectable"), Presentation->GetPathPresentation(QueryHandle, QuerySnapshot));
	TestEqual(TEXT("Query-result cells are snapshotted"), QuerySnapshot.Cells, Cells);
	TestEqual(TEXT("Query-result revisions are snapshotted"), QuerySnapshot.SourceRevisions.Topology, AuthoritativeSnapshot->Revisions.Topology);
	Presentation->ReleasePathPresentation(QueryHandle);

	TestTrue(TEXT("Clear retains active handle"), Presentation->ClearPathPresentation(ActiveHandle));
	TestTrue(TEXT("Cleared handle remains valid"), Presentation->IsPathPresentationValid(ActiveHandle));
	TestTrue(TEXT("Cleared session can be reused"), Presentation->UpdatePathPresentation(ActiveHandle, Cells, 1));
	TestTrue(TEXT("Invalidation preserves session"), Presentation->MarkPathPresentationInvalid(ActiveHandle));
	TestTrue(TEXT("Invalid state resolves"), Visualization->GetCellVisualState(Cells[1], VisualState));
	TestEqual(TEXT("Invalid outranks active states"), VisualState.PathState, EGridCellPathVisualState::Invalid);

	TestTrue(TEXT("Presentation preserves authoritative snapshot"), NavData->GetSnapshot() == AuthoritativeSnapshot);
	TestTrue(
		TEXT("Presentation preserves navigation revisions"),
		UE::GridWorld::PathPresentationTests::SameRevisions(NavData->GetPublishedRevisions(), RevisionsBeforePresentation));

	UGridCellVisualStyle* Owner = NewObject<UGridCellVisualStyle>(GetTransientPackage());
	FGridPathPresentationRequest OwnerRequest = PreviewRequest;
	OwnerRequest.Lifetime = EGridPathPresentationLifetime::OwnerLifetime;
	OwnerRequest.Owner = Owner;
	FGridPathPresentationHandle OwnerHandle;
	TestTrue(TEXT("Owner-lifetime session creates"), Presentation->CreatePathPresentation(OwnerRequest, OwnerHandle));
	OwnerRequest.Owner = nullptr;
	Owner = nullptr;
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Owner collection releases its session"), Presentation->IsPathPresentationValid(OwnerHandle));

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> ReducedSnapshot =
		UE::GridWorld::PathPresentationTests::MakePathSnapshot(GridId, 2, MakeArrayView(Coords).Left(4));
	TestTrue(TEXT("Reduced topology publishes"), NavData->PublishSnapshot(ReducedSnapshot, &PublishError));
	FGridPathPresentationSnapshot ActiveSnapshot;
	TestTrue(TEXT("Topology-invalidated session remains inspectable"), Presentation->GetPathPresentation(ActiveHandle, ActiveSnapshot));
	TestTrue(TEXT("Missing topology marks the session invalid"), ActiveSnapshot.bInvalid);
	TestTrue(TEXT("Existing invalid cell remains readable"), Visualization->GetCellVisualState(Cells[1], VisualState));
	TestEqual(TEXT("Existing cells display topology invalidation"), VisualState.PathState, EGridCellPathVisualState::Invalid);

	Presentation->ReleasePathPresentation(ActiveHandle);
	Visualization->DisableVisualization();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridPathPresentationModesTest,
	"GridWorld.Presentation.Path.ProgressModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridPathPresentationModesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridPathPresentationModesTest")));
	if (!TestNotNull(TEXT("Transient progress-mode world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	const FGuid GridId = FGuid::NewGuid();
	const TArray<FGridCellCoord> Coords{
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(2, 0, 0),
		FGridCellCoord(2, 1, 0),
		FGridCellCoord(2, 2, 0)};
	FString PublishError;
	TestTrue(
		TEXT("Progress-mode topology publishes"),
		NavData->PublishSnapshot(UE::GridWorld::PathPresentationTests::MakePathSnapshot(GridId, 1, Coords), &PublishError));
	UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>();
	UGridRuntimeVisualizationSubsystem* Visualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>();
	if (!TestNotNull(TEXT("Path presentation subsystem"), Presentation)
		|| !TestNotNull(TEXT("Visualization subsystem"), Visualization))
	{
		World->DestroyWorld(false);
		return false;
	}
	TArray<FGridCellId> Cells;
	for (const FGridCellData& Cell : NavData->GetSnapshot()->Cells)
	{
		Cells.Add(Cell.Id);
	}
	FGridPathPresentationRequest Request;
	Request.Cells = Cells;
	Request.Purpose = EGridPathPresentationPurpose::Active;
	Request.ProgressMode = EGridPathProgressPresentationMode::TraversedAndRemaining;
	Request.CurrentCellIndex = 2;
	FGridPathPresentationHandle Handle;
	TestTrue(TEXT("Mode-test session creates"), Presentation->CreatePathPresentation(Request, Handle));

	FGridCellVisualState State;
	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::RemainingOnly);
	Visualization->GetCellVisualState(Cells[2], State);
	TestEqual(TEXT("Remaining Only hides current"), State.PathState, EGridCellPathVisualState::None);
	Visualization->GetCellVisualState(Cells[3], State);
	TestEqual(TEXT("Remaining Only keeps future"), State.PathState, EGridCellPathVisualState::ActiveRemaining);

	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::CurrentAndRemaining);
	Visualization->GetCellVisualState(Cells[2], State);
	TestEqual(TEXT("Current and Remaining shows current"), State.PathState, EGridCellPathVisualState::ActiveCurrent);
	Visualization->GetCellVisualState(Cells[1], State);
	TestEqual(TEXT("Current and Remaining hides traversed"), State.PathState, EGridCellPathVisualState::None);

	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::DestinationOnly);
	Visualization->GetCellVisualState(Cells[3], State);
	TestEqual(TEXT("Destination Only hides ordinary remaining cells"), State.PathState, EGridCellPathVisualState::None);
	Visualization->GetCellVisualState(Cells[4], State);
	TestEqual(TEXT("Destination Only shows destination"), State.PathState, EGridCellPathVisualState::Destination);

	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::EndpointsAndTurns);
	Visualization->GetCellVisualState(Cells[0], State);
	TestNotEqual(TEXT("Endpoints and Turns shows start"), State.PathState, EGridCellPathVisualState::None);
	Visualization->GetCellVisualState(Cells[1], State);
	TestEqual(TEXT("Endpoints and Turns hides straight middle"), State.PathState, EGridCellPathVisualState::None);
	Visualization->GetCellVisualState(Cells[2], State);
	TestEqual(TEXT("Endpoints and Turns shows turn"), State.PathState, EGridCellPathVisualState::ActiveCurrent);
	Visualization->GetCellVisualState(Cells[3], State);
	TestEqual(TEXT("Endpoints and Turns hides second straight middle"), State.PathState, EGridCellPathVisualState::None);

	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::AllCells);
	Visualization->GetCellVisualState(Cells[0], State);
	TestEqual(TEXT("All Cells uses a uniform active state"), State.PathState, EGridCellPathVisualState::ActiveRemaining);
	Presentation->ReleasePathPresentation(Handle);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridPathLineRendererTest,
	"GridWorld.Presentation.Path.LineRenderer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridPathLineRendererTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridPathLineRendererTest")));
	if (!TestNotNull(TEXT("Transient path-line world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>();
	UGridPathLineVisualizationSubsystem* LineVisualization = World->GetSubsystem<UGridPathLineVisualizationSubsystem>();
	UGridRuntimeVisualizationSubsystem* CellVisualization = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>();
	if (!TestNotNull(TEXT("Navigation data"), NavData)
		|| !TestNotNull(TEXT("Path presentation subsystem"), Presentation)
		|| !TestNotNull(TEXT("Path-line visualization subsystem"), LineVisualization)
		|| !TestNotNull(TEXT("Cell visualization subsystem"), CellVisualization))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestFalse(TEXT("Line renderer is disabled by default"), LineVisualization->IsLineVisualizationEnabled());

	const FGuid GridId = FGuid::NewGuid();
	const TArray<FGridCellCoord> Coords{
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(2, 0, 0),
		FGridCellCoord(2, 1, 0),
		FGridCellCoord(2, 2, 0)};
	FString PublishError;
	TestTrue(
		TEXT("Path-line topology publishes"),
		NavData->PublishSnapshot(UE::GridWorld::PathPresentationTests::MakePathSnapshot(GridId, 1, Coords), &PublishError));
	const FGridWorldSnapshotPtr AuthoritativeSnapshot = NavData->GetSnapshot();
	const FGridRevisionSet RevisionsBeforePresentation = NavData->GetPublishedRevisions();
	TArray<FGridCellId> Cells;
	for (const FGridCellData& Cell : AuthoritativeSnapshot->Cells)
	{
		Cells.Add(Cell.Id);
	}

	FGridPathPresentationRequest Request;
	Request.Cells = Cells;
	Request.Purpose = EGridPathPresentationPurpose::Active;
	Request.ProgressMode = EGridPathProgressPresentationMode::TraversedAndRemaining;
	Request.CurrentCellIndex = 2;
	FGridPathPresentationHandle Handle;
	TestTrue(TEXT("Default cell-only session creates"), Presentation->CreatePathPresentation(Request, Handle));
	TestTrue(TEXT("Default line style loads and enables"), LineVisualization->EnableLineVisualization());
	const UGridPathLineVisualStyle* DefaultStyle = LineVisualization->GetActiveStyle();
	if (TestNotNull(TEXT("Default line style asset is active"), DefaultStyle))
	{
		TestNotNull(TEXT("Default segment mesh is assigned"), DefaultStyle->SegmentMesh.Get());
		TestNotNull(TEXT("Default marker mesh is assigned"), DefaultStyle->MarkerMesh.Get());
		TestNotNull(TEXT("Default line material is assigned"), DefaultStyle->SegmentMaterial.Get());
	}
	TestEqual(TEXT("Cell-only session creates no line segments"), LineVisualization->GetSegmentInstanceCount(), 0);
	FGridCellVisualState CellState;
	TestTrue(TEXT("Cell-only state is readable"), CellVisualization->GetCellVisualState(Cells[2], CellState));
	TestEqual(TEXT("Cell-only session contributes current state"), CellState.PathState, EGridCellPathVisualState::ActiveCurrent);

	TestTrue(TEXT("Session switches to line-only"), Presentation->SetPathPresentationRenderers(Handle, false, true));
	TestTrue(TEXT("Line-only cell remains readable"), CellVisualization->GetCellVisualState(Cells[2], CellState));
	TestEqual(TEXT("Line-only session contributes no cell overlay"), CellState.PathState, EGridCellPathVisualState::None);
	TestEqual(TEXT("Strict polyline creates one segment per transition"), LineVisualization->GetSegmentInstanceCount(), Cells.Num() - 1);
	TestEqual(TEXT("Strict polyline creates one marker per displayed point"), LineVisualization->GetMarkerInstanceCount(), Cells.Num());
	if (TestNotNull(TEXT("Segment HISM exists"), LineVisualization->SegmentComponent.Get()))
	{
		TestEqual(
			TEXT("Line HISM collision is disabled"),
			LineVisualization->SegmentComponent->GetCollisionEnabled(),
			ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Line HISM never affects navigation"), LineVisualization->SegmentComponent->CanEverAffectNavigation());
		TestEqual(
			TEXT("Line HISM uses the documented custom-data layout"),
			LineVisualization->SegmentComponent->NumCustomDataFloats,
			FGridPathLineMaterialDataLayout::NumFloats);
	}

	UGridPathLineVisualStyle* CustomStyle = DefaultStyle != nullptr
		? DuplicateObject<UGridPathLineVisualStyle>(DefaultStyle, GetTransientPackage())
		: nullptr;
	if (TestNotNull(TEXT("Custom line style duplicates"), CustomStyle))
	{
		CustomStyle->SegmentMesh = DefaultStyle->MarkerMesh;
		TestTrue(TEXT("Editable segment mesh style enables"), LineVisualization->EnableLineVisualization(CustomStyle));
		TestTrue(TEXT("Custom line style becomes active"), LineVisualization->GetActiveStyle() == CustomStyle);
		TestEqual(TEXT("Custom mesh preserves semantic segment count"), LineVisualization->GetSegmentInstanceCount(), Cells.Num() - 1);
	}
	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::DestinationOnly);
	TestEqual(TEXT("Destination-only line has no segment"), LineVisualization->GetSegmentInstanceCount(), 0);
	TestEqual(TEXT("Destination-only line retains its marker"), LineVisualization->GetMarkerInstanceCount(), 1);
	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::EndpointsAndTurns);
	TestEqual(TEXT("Endpoints-and-turns joins three significant points"), LineVisualization->GetSegmentInstanceCount(), 2);
	TestEqual(TEXT("Endpoints-and-turns marks significant points"), LineVisualization->GetMarkerInstanceCount(), 3);
	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::RemainingOnly);
	TestEqual(TEXT("Remaining line starts at the current logical point"), LineVisualization->GetSegmentInstanceCount(), 2);
	TestTrue(TEXT("Line progress advances"), Presentation->UpdatePathPresentationProgress(Handle, 3));
	TestEqual(TEXT("Progress removes traversed line geometry"), LineVisualization->GetSegmentInstanceCount(), 1);
	Presentation->SetPathPresentationMode(Handle, EGridPathProgressPresentationMode::TraversedAndRemaining);
	Presentation->UpdatePathPresentationProgress(Handle, 2);

	TestTrue(TEXT("Session enables both renderers"), Presentation->SetPathPresentationRenderers(Handle, true, true));
	CellVisualization->GetCellVisualState(Cells[2], CellState);
	TestEqual(TEXT("Both mode retains the cell overlay"), CellState.PathState, EGridCellPathVisualState::ActiveCurrent);
	TestEqual(TEXT("Both mode retains the line"), LineVisualization->GetSegmentInstanceCount(), Cells.Num() - 1);

	TestTrue(TEXT("Session disables both renderers"), Presentation->SetPathPresentationRenderers(Handle, false, false));
	CellVisualization->GetCellVisualState(Cells[2], CellState);
	TestEqual(TEXT("Neither mode clears the cell overlay"), CellState.PathState, EGridCellPathVisualState::None);
	TestEqual(TEXT("Neither mode clears line segments"), LineVisualization->GetSegmentInstanceCount(), 0);
	FGridPathPresentationSnapshot SessionSnapshot;
	TestTrue(TEXT("Renderer selection remains inspectable"), Presentation->GetPathPresentation(Handle, SessionSnapshot));
	TestFalse(TEXT("Snapshot reports cell renderer disabled"), SessionSnapshot.bRenderCellOverlay);
	TestFalse(TEXT("Snapshot reports line renderer disabled"), SessionSnapshot.bRenderLine);

	Presentation->SetPathPresentationRenderers(Handle, false, true);
	const int64 GenerationBeforeVisibility = LineVisualization->GetRendererGeneration();
	const int32 SegmentsBeforeVisibility = LineVisualization->GetSegmentInstanceCount();
	LineVisualization->SetLineVisualizationVisible(false);
	TestFalse(TEXT("Line resources can be hidden"), LineVisualization->IsLineVisualizationVisible());
	TestEqual(TEXT("Hiding preserves line instances"), LineVisualization->GetSegmentInstanceCount(), SegmentsBeforeVisibility);
	TestEqual(TEXT("Hiding does not rebuild line mappings"), LineVisualization->GetRendererGeneration(), GenerationBeforeVisibility);
	LineVisualization->SetLineVisualizationVisible(true);

	LineVisualization->DisableLineVisualization();
	TestFalse(TEXT("Line renderer disables independently"), LineVisualization->IsLineVisualizationEnabled());
	TestEqual(TEXT("Disabling releases line instances"), LineVisualization->GetSegmentInstanceCount(), 0);
	TestTrue(TEXT("Disabling line rendering preserves the path session"), Presentation->IsPathPresentationValid(Handle));
	TestTrue(TEXT("Line renderer re-enables from current session state"), LineVisualization->EnableLineVisualization(CustomStyle));
	TestEqual(TEXT("Re-enable rebuilds the current line"), LineVisualization->GetSegmentInstanceCount(), Cells.Num() - 1);

	TestTrue(TEXT("Line presentation preserves authoritative snapshot"), NavData->GetSnapshot() == AuthoritativeSnapshot);
	TestTrue(
		TEXT("Line presentation preserves navigation revisions"),
		UE::GridWorld::PathPresentationTests::SameRevisions(NavData->GetPublishedRevisions(), RevisionsBeforePresentation));
	Presentation->ReleasePathPresentation(Handle);
	TestEqual(TEXT("Releasing the session clears line geometry"), LineVisualization->GetSegmentInstanceCount(), 0);
	LineVisualization->DisableLineVisualization();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridPathFollowerPresentationTest,
	"GridWorld.Presentation.Path.PathFollowerIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridPathFollowerPresentationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridPathFollowerPresentationTest")));
	if (!TestNotNull(TEXT("Transient follower-presentation world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	AGridWorldAIController* Controller = World->SpawnActor<AGridWorldAIController>();
	ACharacter* Character = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Navigation data"), NavData)
		|| !TestNotNull(TEXT("GridWorld controller"), Controller)
		|| !TestNotNull(TEXT("Character"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	Controller->Possess(Character);
	UGridWorldPathFollowingComponent* Follower = Cast<UGridWorldPathFollowingComponent>(Controller->GetPathFollowingComponent());
	UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>();
	if (!TestNotNull(TEXT("GridWorld path follower"), Follower)
		|| !TestNotNull(TEXT("Path presentation subsystem"), Presentation))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestFalse(TEXT("Automatic active presentation is opt-in"), Follower->bPresentActivePath);
	TestTrue(TEXT("Follower presentation defaults to cell overlay"), Follower->bPresentActivePathAsCellOverlay);
	TestFalse(TEXT("Follower line presentation is opt-in"), Follower->bPresentActivePathAsLine);

	const FGuid GridId = FGuid::NewGuid();
	const TArray<FGridCellCoord> Coords{
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(2, 0, 0)};
	FString PublishError;
	TestTrue(
		TEXT("Follower topology publishes"),
		NavData->PublishSnapshot(UE::GridWorld::PathPresentationTests::MakePathSnapshot(GridId, 1, Coords), &PublishError));
	FPathFindingQuery Query(
		Controller,
		*NavData,
		NavData->GetSnapshot()->Cells[0].WorldCenter,
		NavData->GetSnapshot()->Cells.Last().WorldCenter,
		NavData->GetDefaultQueryFilter());
	FPathFindingResult PathResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	if (TestTrue(TEXT("Follower path succeeds"), PathResult.IsSuccessful()))
	{
		const FAIRequestID RequestId = Follower->RequestMove(
			FAIMoveRequest(NavData->GetSnapshot()->Cells.Last().WorldCenter),
			PathResult.Path);
		TestTrue(TEXT("Opt-out move request starts"), RequestId.IsValid());
		TestEqual(TEXT("Opt-out follower creates no session"), Presentation->GetActiveSessionCount(), 0);
		Follower->AbortMove(*Controller, FPathFollowingResultFlags::OwnerFinished, RequestId);
	}

	Follower->SetActivePathPresentationRenderers(false, true);
	Follower->SetActivePathPresentationEnabled(true);
	PathResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	if (TestTrue(TEXT("Opt-in follower path succeeds"), PathResult.IsSuccessful()))
	{
		const FAIRequestID RequestId = Follower->RequestMove(
			FAIMoveRequest(NavData->GetSnapshot()->Cells.Last().WorldCenter),
			PathResult.Path);
		TestTrue(TEXT("Opt-in move request starts"), RequestId.IsValid());
		TestEqual(TEXT("Opt-in follower creates one active session"), Presentation->GetActiveSessionCount(), 1);
		const FGridPathPresentationHandle OriginalHandle = Follower->ActivePathPresentationHandle;
		TestTrue(TEXT("Follower session handle validates"), Presentation->IsPathPresentationValid(OriginalHandle));
		FGridPathPresentationSnapshot FollowerSnapshot;
		TestTrue(TEXT("Follower renderer choice is inspectable"), Presentation->GetPathPresentation(OriginalHandle, FollowerSnapshot));
		TestFalse(TEXT("Follower can disable active cell presentation"), FollowerSnapshot.bRenderCellOverlay);
		TestTrue(TEXT("Follower can enable active line presentation"), FollowerSnapshot.bRenderLine);
		Follower->SetActivePathPresentationRenderers(true, true);

		PathResult.Path->DoneUpdating(ENavPathUpdateType::NavigationChanged);
		TestEqual(TEXT("Recalculation reuses the same session handle"), Follower->ActivePathPresentationHandle, OriginalHandle);
		TestEqual(TEXT("Recalculation does not create another session"), Presentation->GetActiveSessionCount(), 1);
		PathResult.Path->Invalidate();
		FGridCellVisualState State;
		TestTrue(
			TEXT("Invalidated follower cell remains readable"),
			World->GetSubsystem<UGridRuntimeVisualizationSubsystem>()->GetCellVisualState(
				NavData->GetSnapshot()->Cells[0].Id,
				State));
		TestEqual(TEXT("Native invalidation marks the active session"), State.PathState, EGridCellPathVisualState::Invalid);
		Follower->AbortMove(*Controller, FPathFollowingResultFlags::OwnerFinished, RequestId);
		TestEqual(TEXT("Abort releases the active session"), Presentation->GetActiveSessionCount(), 0);
		TestFalse(TEXT("Released follower handle is stale"), Presentation->IsPathPresentationValid(OriginalHandle));
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
