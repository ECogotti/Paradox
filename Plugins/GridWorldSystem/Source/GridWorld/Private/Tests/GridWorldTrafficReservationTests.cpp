// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Navigation/GridAStar.h"
#include "Navigation/GridTrafficReservation.h"
#include "Navigation/GridTrafficReservationManager.h"
#include "Navigation/GridWorldSnapshot.h"
#include "UObject/Package.h"

namespace UE::GridWorld::Traffic::Tests
{
	/** Builds a stable cell/location pair for concise traffic tests. */
	FGridTrafficCellLocation MakeLocation(const FGuid& GridId, int32 X, int32 Y, int32 Layer, const FVector& Center)
	{
		FGridTrafficCellLocation Result;
		Result.CellId.GridId = GridId;
		Result.CellId.Coord = FGridCellCoord(X, Y, Layer);
		Result.WorldCenter = Center;
		return Result;
	}

	/** Builds a complete corridor request using the production 42 x 192 cm agent defaults. */
	FGridTrafficCorridorRequest MakeRequest(
		const FGuid& OwnerId,
		UObject& Source,
		APawn& Pawn,
		const FGridTrafficCellLocation& CurrentCell,
		TConstArrayView<FGridTrafficCellLocation> FutureCells)
	{
		FGridTrafficCorridorRequest Request;
		Request.OwnerId = OwnerId;
		Request.Source = &Source;
		Request.Pawn = &Pawn;
		Request.CurrentCell = CurrentCell;
		Request.DesiredFutureCells = FutureCells;
		Request.AgentRadius = 42.0f;
		Request.AgentHeight = 192.0f;
		Request.AdditionalSeparation = 5.0f;
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridTrafficGeometryTest,
	"GridWorld.Traffic.GeometryConflicts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridTrafficGeometryTest::RunTest(const FString& Parameters)
{
	const FGuid GridId = FGuid::NewGuid();
	const FGuid OwnerId = FGuid::NewGuid();
	FGridTrafficReservationSnapshot CellSnapshot;
	FGridTrafficReservedCell& ReservedCell = CellSnapshot.Cells.AddDefaulted_GetRef();
	ReservedCell.OwnerId = OwnerId;
	ReservedCell.CellId = UE::GridWorld::Traffic::Tests::MakeLocation(GridId, 0, 0, 0, FVector::ZeroVector).CellId;
	ReservedCell.WorldCenter = FVector::ZeroVector;
	ReservedCell.AgentRadius = 42.0f;
	ReservedCell.AgentHeight = 192.0f;
	ReservedCell.AdditionalSeparation = 5.0f;

	TestTrue(
		TEXT("Adjacent 50 cm cells conflict for two 42 cm capsules plus separation"),
		CellSnapshot.ConflictsWithCell(FVector(50.0, 0.0, 0.0), 42.0f, 192.0f, 5.0f, FGuid()));
	TestFalse(
		TEXT("Cells beyond the combined capsule clearance do not conflict"),
		CellSnapshot.ConflictsWithCell(FVector(100.0, 0.0, 0.0), 42.0f, 192.0f, 5.0f, FGuid()));
	TestFalse(
		TEXT("Vertically separated layers do not create false traffic conflicts"),
		CellSnapshot.ConflictsWithCell(FVector(0.0, 0.0, 250.0), 42.0f, 192.0f, 5.0f, FGuid()));
	TestFalse(
		TEXT("An owner may overlap its own reservation"),
		CellSnapshot.ConflictsWithCell(FVector::ZeroVector, 42.0f, 192.0f, 5.0f, OwnerId));

	FGridTrafficReservationSnapshot SegmentSnapshot;
	FGridTrafficReservedSegment& ReservedSegment = SegmentSnapshot.Segments.AddDefaulted_GetRef();
	ReservedSegment.OwnerId = OwnerId;
	ReservedSegment.Start = FVector(-50.0, 0.0, 0.0);
	ReservedSegment.End = FVector(50.0, 0.0, 0.0);
	ReservedSegment.AgentRadius = 10.0f;
	ReservedSegment.AgentHeight = 100.0f;
	TestTrue(
		TEXT("Crossing transitions reserve their swept envelopes"),
		SegmentSnapshot.ConflictsWithSegment(
			FVector(0.0, -50.0, 0.0),
			FVector(0.0, 50.0, 0.0),
			10.0f,
			100.0f,
			0.0f,
			FGuid()));
	TestTrue(
		TEXT("Opposite traversal of the same transition conflicts"),
		SegmentSnapshot.ConflictsWithSegment(
			ReservedSegment.End,
			ReservedSegment.Start,
			10.0f,
			100.0f,
			0.0f,
			FGuid()));
	TestFalse(
		TEXT("A crossing on a non-overlapping vertical layer is allowed"),
		SegmentSnapshot.ConflictsWithSegment(
			FVector(0.0, -50.0, 150.0),
			FVector(0.0, 50.0, 150.0),
			10.0f,
			100.0f,
			0.0f,
			FGuid()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridTrafficAStarTest,
	"GridWorld.Traffic.AStarAvoidance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridTrafficAStarTest::RunTest(const FString& Parameters)
{
	FGridWorldSnapshot Snapshot;
	Snapshot.GridId = FGuid::NewGuid();
	Snapshot.Revisions.Topology = 1;
	const FGridCellCoord Coords[] = {
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(2, 0, 0),
		FGridCellCoord(0, 1, 0),
		FGridCellCoord(1, 1, 0),
		FGridCellCoord(2, 1, 0)};
	for (const FGridCellCoord& Coord : Coords)
	{
		FGridCellData& Cell = Snapshot.Cells.AddDefaulted_GetRef();
		Cell.Id.GridId = Snapshot.GridId;
		Cell.Id.Coord = Coord;
		Cell.WorldCenter = FVector(Coord.X * 50.0, Coord.Y * 50.0, 0.0);
		Cell.bHasAuthoredWorldCenter = true;
	}
	auto AddEdge = [&Snapshot](int32 From, int32 To)
	{
		Snapshot.Cells[From].Neighbors.Add(To);
		Snapshot.Cells[To].Neighbors.Add(From);
	};
	AddEdge(0, 1);
	AddEdge(1, 2);
	AddEdge(0, 3);
	AddEdge(3, 4);
	AddEdge(4, 5);
	AddEdge(5, 2);
	FString FinalizeError;
	if (!TestTrue(TEXT("Traffic A* topology finalizes"), Snapshot.Finalize(&FinalizeError)))
	{
		AddError(FinalizeError);
		return false;
	}

	TSharedRef<FGridTrafficReservationSnapshot, ESPMode::ThreadSafe> Traffic = MakeShared<FGridTrafficReservationSnapshot, ESPMode::ThreadSafe>();
	FGridTrafficReservedCell& BlockedCell = Traffic->Cells.AddDefaulted_GetRef();
	BlockedCell.OwnerId = FGuid::NewGuid();
	BlockedCell.CellId = Snapshot.Cells[1].Id;
	BlockedCell.WorldCenter = Snapshot.Cells[1].WorldCenter;
	BlockedCell.AgentRadius = 1.0f;
	BlockedCell.AgentHeight = 100.0f;

	FGridAStarQuery Query;
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 2;
	Query.MovementMode = EGridMovementMode::FourDirections;
	Query.DynamicAgentPolicy = EGridDynamicAgentPolicy::ReservedCorridor;
	Query.TrafficReservations = Traffic;
	Query.TrafficAgentRadius = 1.0f;
	Query.TrafficAgentHeight = 100.0f;
	Query.TrafficAdditionalSeparation = 0.0f;
	const FGridAStarResult Result = FGridAStar().FindPath(Snapshot, Query);
	TestTrue(TEXT("Reserved Corridor A* finds a safe detour"), Result.IsSuccessful());
	TestEqual(TEXT("Safe detour keeps complete start/goal path"), Result.CellIndices.Num(), 5);
	TestFalse(TEXT("Safe detour excludes another owner's protected cell"), Result.CellIndices.Contains(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridTrafficRegistryTest,
	"GridWorld.Traffic.RegistryLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridTrafficRegistryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldTrafficRegistryTest")));
	if (!TestNotNull(TEXT("Transient traffic world"), World))
	{
		return false;
	}

	APawn* FirstPawn = World->SpawnActor<APawn>();
	APawn* SecondPawn = World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("First traffic Pawn"), FirstPawn)
		|| !TestNotNull(TEXT("Second traffic Pawn"), SecondPawn))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FGuid GridId = FGuid::NewGuid();
	const FGuid FirstOwner = FGuid::NewGuid();
	const FGuid SecondOwner = FGuid::NewGuid();
	using UE::GridWorld::Traffic::Tests::MakeLocation;
	const FGridTrafficCellLocation Cell0 = MakeLocation(GridId, 0, 0, 0, FVector(0.0, 0.0, 0.0));
	const FGridTrafficCellLocation Cell1 = MakeLocation(GridId, 1, 0, 0, FVector(50.0, 0.0, 0.0));
	const FGridTrafficCellLocation Cell2 = MakeLocation(GridId, 2, 0, 0, FVector(100.0, 0.0, 0.0));
	const FGridTrafficCellLocation Cell3 = MakeLocation(GridId, 3, 0, 0, FVector(150.0, 0.0, 0.0));
	const FGridTrafficCellLocation Cell5 = MakeLocation(GridId, 5, 0, 0, FVector(250.0, 0.0, 0.0));

	FGridTrafficReservationManager Manager;
	FGridTrafficCorridorResult FirstResult;
	const TArray<FGridTrafficCellLocation> FirstFuture = {Cell1, Cell2};
	TestTrue(
		TEXT("First corridor request is valid"),
		Manager.UpdateCorridor(
			UE::GridWorld::Traffic::Tests::MakeRequest(FirstOwner, *FirstPawn, *FirstPawn, Cell0, FirstFuture),
			FirstResult));
	TestEqual(TEXT("First corridor is granted"), FirstResult.Status, EGridTrafficReservationStatus::Granted);

	FGridTrafficCorridorResult SecondResult;
	const TArray<FGridTrafficCellLocation> SecondFuture = {Cell3, Cell2};
	TestTrue(
		TEXT("Conflicting corridor request remains observable"),
		Manager.UpdateCorridor(
			UE::GridWorld::Traffic::Tests::MakeRequest(SecondOwner, *SecondPawn, *SecondPawn, Cell5, SecondFuture),
			SecondResult));
	TestEqual(TEXT("Second corridor waits instead of stealing a granted prefix"), SecondResult.Status, EGridTrafficReservationStatus::Waiting);
	TestEqual(TEXT("Conflict identifies the granted owner"), SecondResult.BlockingOwnerId, FirstOwner);

	FGridTrafficReservationSnapshotPtr Snapshot = Manager.GetSnapshot();
	if (TestTrue(TEXT("Traffic snapshot is published"), Snapshot.IsValid()))
	{
		TestEqual(TEXT("Both granted and waiting owners have debug records"), Snapshot->DebugEntries.Num(), 2);
		const FGridTrafficReservationDebugData* WaitingDebug = Snapshot->DebugEntries.FindByPredicate(
			[SecondOwner](const FGridTrafficReservationDebugData& Entry)
			{
				return Entry.OwnerId == SecondOwner;
			});
		if (TestNotNull(TEXT("Waiting owner is visible to the debug renderer"), WaitingDebug))
		{
			TestTrue(TEXT("Waiting state is explicit"), WaitingDebug->bWaiting);
			TestEqual(TEXT("Requested cells remain visible while waiting"), WaitingDebug->WaitingFutureCells.Num(), 2);
		}
	}

	TestTrue(TEXT("Releasing a completed corridor changes the registry"), Manager.ReleaseCorridor(FirstOwner, FirstPawn, true));
	TestTrue(
		TEXT("Waiting owner can retry immediately after the reservation changes"),
		Manager.UpdateCorridor(
			UE::GridWorld::Traffic::Tests::MakeRequest(SecondOwner, *SecondPawn, *SecondPawn, Cell5, SecondFuture),
			SecondResult));
	TestEqual(TEXT("Retry acquires the released future corridor"), SecondResult.Status, EGridTrafficReservationStatus::Granted);

	// Advancing from cell 5 to cell 3 must remove the old logical cell in the same publication.
	const TArray<FGridTrafficCellLocation> AdvancedFuture = {Cell2};
	TestTrue(
		TEXT("Rolling the reservation after a gate is supported"),
		Manager.UpdateCorridor(
			UE::GridWorld::Traffic::Tests::MakeRequest(SecondOwner, *SecondPawn, *SecondPawn, Cell3, AdvancedFuture),
			SecondResult));
	Snapshot = Manager.GetSnapshot();
	if (TestTrue(TEXT("Advanced snapshot is published"), Snapshot.IsValid()))
	{
		TestFalse(
			TEXT("The cell behind the crossed gate is released immediately"),
			Snapshot->Cells.ContainsByPredicate([SecondOwner, Cell5](const FGridTrafficReservedCell& Cell)
			{
				return Cell.OwnerId == SecondOwner && Cell.CellId == Cell5.CellId;
			}));
	}

	TestTrue(TEXT("Registry reset releases all runtime-only traffic state"), Manager.Reset());
	Snapshot = Manager.GetSnapshot();
	if (TestTrue(TEXT("Empty snapshot remains available after reset"), Snapshot.IsValid()))
	{
		TestTrue(TEXT("Reset removes protected cells"), Snapshot->Cells.IsEmpty());
		TestTrue(TEXT("Reset removes protected transitions"), Snapshot->Segments.IsEmpty());
		TestTrue(TEXT("Reset removes traffic debug records"), Snapshot->DebugEntries.IsEmpty());
	}

	FGridTrafficGoalClaimRequest FirstClaim;
	FirstClaim.OwnerId = FirstOwner;
	FirstClaim.Claimant = FirstPawn;
	FirstClaim.Pawn = FirstPawn;
	FirstClaim.GoalCell = Cell1;
	FirstClaim.AgentRadius = 42.0f;
	FirstClaim.AgentHeight = 192.0f;
	FirstClaim.AdditionalSeparation = 5.0f;
	bool bClaimStateChanged = false;
	TestTrue(TEXT("A separated goal can be claimed atomically"), Manager.TryClaimGoal(FirstClaim, bClaimStateChanged));
	TestTrue(TEXT("First goal claim publishes a state change"), bClaimStateChanged);

	FGridTrafficGoalClaimRequest AdjacentClaim = FirstClaim;
	AdjacentClaim.OwnerId = SecondOwner;
	AdjacentClaim.Claimant = SecondPawn;
	AdjacentClaim.Pawn = SecondPawn;
	AdjacentClaim.GoalCell = Cell2;
	TestFalse(
		TEXT("An adjacent 50 cm goal is rejected for two 42 cm agents"),
		Manager.TryClaimGoal(AdjacentClaim, bClaimStateChanged));
	TestTrue(TEXT("Reached goal converts to Pawn-lifetime parking"), Manager.CommitParking(FirstClaim));
	Snapshot = Manager.GetSnapshot();
	if (TestTrue(TEXT("Parking snapshot is published"), Snapshot.IsValid()))
	{
		TestTrue(
			TEXT("Parking remains protected after the temporary task claim is consumed"),
			Snapshot->Cells.ContainsByPredicate([FirstOwner, Cell1](const FGridTrafficReservedCell& Cell)
			{
				return Cell.OwnerId == FirstOwner && Cell.CellId == Cell1.CellId && Cell.bGoalOrParking;
			}));
	}
	TestTrue(TEXT("Removing an owner releases its parking protection"), Manager.RemoveOwner(FirstOwner));

	World->DestroyWorld(false);
	return true;
}

#endif
