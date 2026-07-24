// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Presentation/GridCellVisualStyle.h"

namespace UE::GridWorld::PredictionInjectionTests
{
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeLinearSnapshot(
		const FGuid& GridId,
		int64 TopologyRevision,
		int64 TraversalRevision = 1)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot =
			MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = GridId;
		Snapshot->GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
		Snapshot->Revisions.Topology = TopologyRevision;
		Snapshot->Revisions.Traversal = TraversalRevision;
		Snapshot->Revisions.Occupancy = 1;
		FGridRegionData& Region = Snapshot->Regions.Add(GridId);
		Region.GridId = GridId;
		Region.GridTransform = Snapshot->GridTransform;
		for (int32 CellX = 0; CellX < 4; ++CellX)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = GridId;
			Cell.Id.Coord = FGridCellCoord(CellX, 0, 0);
			Cell.WorldCenter = Snapshot->GridTransform.CellToWorld(Cell.Id.Coord);
			if (CellX > 0)
			{
				Cell.Neighbors.Add(CellX - 1);
				Snapshot->Cells[CellX - 1].Neighbors.Add(CellX);
			}
		}
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridExactPathInjectionTest,
	"GridWorld.Presentation.Path.Prediction.ExactInjectionValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridExactPathInjectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridExactPathInjectionTest")));
	if (!TestNotNull(TEXT("Transient injection-test world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	if (!TestNotNull(TEXT("Grid navigation authority"), NavData))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FGuid GridId = FGuid::NewGuid();
	FString PublishError;
	TestTrue(
		TEXT("Linear topology publishes"),
		NavData->PublishSnapshot(
			UE::GridWorld::PredictionInjectionTests::MakeLinearSnapshot(GridId, 1),
			&PublishError));
	const FGridWorldSnapshotPtr Snapshot = NavData->GetSnapshot();
	TArray<FGridCellId> Cells;
	for (const FGridCellData& Cell : Snapshot->Cells)
	{
		Cells.Add(Cell.Id);
	}

	UGridCellVisualStyle* Querier = NewObject<UGridCellVisualStyle>(World);
	FNavAgentProperties AgentProperties;
	AgentProperties.AgentRadius = 34.0f;
	AgentProperties.AgentHeight = 144.0f;
	const FGridRevisionSet RevisionsBefore = NavData->GetPublishedRevisions();
	FGridInjectedPath InjectedPath;
	const FGridInjectedPathValidationResult Created = NavData->CreateExactInjectedPath(
		Querier,
		AgentProperties,
		nullptr,
		Snapshot->Cells[0].WorldCenter,
		Cells,
		Cells.Last(),
		true,
		false,
		EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal,
		FGuid::NewGuid(),
		InjectedPath);
	TestTrue(TEXT("Authority stamps a valid exact path"), Created.bIsValid);
	if (!Created.bIsValid)
	{
		AddError(FString::Printf(TEXT("Exact path creation failed: %s"), *Created.DiagnosticMessage));
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("Injected payload has opaque identity"), InjectedPath.PathInstanceId.IsValid());
	TestEqual(TEXT("Injected payload preserves ordered cells"), InjectedPath.Cells, Cells);
	TestEqual(
		TEXT("Unadjusted injected payload preserves the requested goal"),
		InjectedPath.RequestedGoalCell,
		InjectedPath.OriginalGoalCell);
	TestEqual(TEXT("Injection does not change topology revision"), NavData->GetPublishedRevisions().Topology, RevisionsBefore.Topology);
	TestEqual(TEXT("Injection does not change traversal revision"), NavData->GetPublishedRevisions().Traversal, RevisionsBefore.Traversal);

	const FGridInjectedPathValidationResult Validated = NavData->ValidateInjectedPath(
		InjectedPath,
		Querier,
		AgentProperties,
		Snapshot->Cells[0].WorldCenter);
	TestTrue(TEXT("Unchanged payload revalidates"), Validated.bIsValid);
	const FPathFindingResult Materialized = NavData->MaterializeInjectedPath(
		InjectedPath,
		Querier,
		AgentProperties,
		Snapshot->Cells[0].WorldCenter);
	TestTrue(TEXT("Validated payload materializes as a normal path"), Materialized.IsSuccessful());
	const FGridNavigationPath* GridPath = Materialized.Path.IsValid()
		? Materialized.Path->CastPath<FGridNavigationPath>()
		: nullptr;
	TestNotNull(TEXT("Materialized path uses FGridNavigationPath"), GridPath);
	if (GridPath != nullptr)
	{
		TestEqual(TEXT("Materialized path retains exact cells"), GridPath->CellPath, Cells);
		TestEqual(TEXT("Materialized path records injected origin"), GridPath->Origin, EGridNavigationPathOrigin::Injected);
		TestEqual(TEXT("Materialized path retains injected identity"), GridPath->PathInstanceId, InjectedPath.PathInstanceId);
		TestEqual(TEXT("Materialized path correlates to preview"), GridPath->SourcePreviewId, InjectedPath.SourcePreviewId);
	}

	FGridInjectedPath DisconnectedPath = InjectedPath;
	Swap(DisconnectedPath.Cells[1], DisconnectedPath.Cells[2]);
	const FGridInjectedPathValidationResult Disconnected = NavData->ValidateInjectedPath(
		DisconnectedPath,
		Querier,
		AgentProperties,
		Snapshot->Cells[0].WorldCenter);
	TestFalse(TEXT("Disconnected exact transitions are rejected"), Disconnected.bIsValid);
	TestEqual(
		TEXT("Disconnected transition exposes a structured failure"),
		Disconnected.FailureReason,
		EGridInjectedPathFailureReason::DisconnectedCells);

	TArray<FGridCellId> AdjustedCells = Cells;
	AdjustedCells.Pop(EAllowShrinking::No);
	FGridInjectedPath AdjustedPath;
	const FGridInjectedPathValidationResult AdjustedCreated = NavData->CreateExactInjectedPath(
		Querier,
		AgentProperties,
		nullptr,
		Snapshot->Cells[0].WorldCenter,
		AdjustedCells,
		AdjustedCells.Last(),
		false,
		false,
		EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal,
		FGuid::NewGuid(),
		AdjustedPath);
	TestTrue(TEXT("A shortened occupied-goal prefix is a valid exact path"), AdjustedCreated.bIsValid);
	if (AdjustedCreated.bIsValid)
	{
		AdjustedPath.RequestedGoalCell = Cells.Last();
		TestEqual(TEXT("Adjusted payload keeps its effective end"), AdjustedPath.OriginalGoalCell, AdjustedCells.Last());
		TestEqual(TEXT("Adjusted payload separately retains gameplay intent"), AdjustedPath.RequestedGoalCell, Cells.Last());
		TestTrue(
			TEXT("Requested-goal metadata does not invalidate the authoritative prefix"),
			NavData->ValidateInjectedPath(
				AdjustedPath,
				Querier,
				AgentProperties,
				Snapshot->Cells[0].WorldCenter).bIsValid);
	}

	TestTrue(
		TEXT("Topology replacement publishes"),
		NavData->PublishSnapshot(
			UE::GridWorld::PredictionInjectionTests::MakeLinearSnapshot(GridId, 2),
			&PublishError));
	const FGridInjectedPathValidationResult Stale = NavData->ValidateInjectedPath(
		InjectedPath,
		Querier,
		AgentProperties,
		NavData->GetSnapshot()->Cells[0].WorldCenter);
	TestFalse(TEXT("Topology change stales an injected path"), Stale.bIsValid);
	TestEqual(TEXT("Topology staleness is explicit"), Stale.FailureReason, EGridInjectedPathFailureReason::StaleTopology);

	World->DestroyWorld(false);
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
