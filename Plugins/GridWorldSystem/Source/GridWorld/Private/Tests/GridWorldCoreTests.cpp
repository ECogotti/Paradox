// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Debug/GridCellSurfaceProjection.h"
#include "Navigation/GridAStar.h"
#include "Navigation/GridAStarMath.h"
#include "Navigation/GridDirtyAreaPolicy.h"
#include "Navigation/GridNavigationQueryFilter.h"
#include "Navigation/GridWalkingSurface.h"
#include "Navigation/GridWorldSnapshot.h"

namespace UE::GridWorld::Tests
{
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeSquareSnapshot(int32 Width, int32 Height)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = FGuid::NewGuid();
		Snapshot->Revisions.Topology = 1;
		FGridRegionData& Region = Snapshot->Regions.Add(Snapshot->GridId);
		Region.GridId = Snapshot->GridId;
		Region.MovementMode = EGridMovementMode::EightDirections;
		Region.bAllowCornerCutting = true;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
				Cell.Id.Coord = FGridCellCoord(X, Y, 0);
			}
		}
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				FGridCellData& Cell = Snapshot->Cells[Y * Width + X];
				for (int32 DeltaY = -1; DeltaY <= 1; ++DeltaY)
				{
					for (int32 DeltaX = -1; DeltaX <= 1; ++DeltaX)
					{
						const int32 NeighborX = X + DeltaX;
						const int32 NeighborY = Y + DeltaY;
						if ((DeltaX != 0 || DeltaY != 0)
							&& NeighborX >= 0 && NeighborX < Width
							&& NeighborY >= 0 && NeighborY < Height)
						{
							Cell.Neighbors.Add(NeighborY * Width + NeighborX);
						}
					}
				}
			}
		}
		FString Error;
		check(Snapshot->Finalize(&Error));
		return Snapshot;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeTurnChoiceSnapshot()
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = FGuid::NewGuid();
		Snapshot->Revisions.Topology = 1;
		FGridRegionData& Region = Snapshot->Regions.Add(Snapshot->GridId);
		Region.GridId = Snapshot->GridId;
		Region.MovementMode = EGridMovementMode::FourDirections;

		const FGridCellCoord Coords[] = {
			FGridCellCoord(0, 0, 0),
			FGridCellCoord(1, 0, 1),
			FGridCellCoord(1, 1, -1),
			FGridCellCoord(2, 1, 2),
			FGridCellCoord(2, 2, 0),
			FGridCellCoord(3, 2, 1),
			FGridCellCoord(4, 2, 3),
			FGridCellCoord(0, -1, -1),
			FGridCellCoord(0, -2, -2),
			FGridCellCoord(1, -2, 0),
			FGridCellCoord(2, -2, 1),
			FGridCellCoord(3, -2, 2),
			FGridCellCoord(4, -2, 3),
			FGridCellCoord(4, -1, 2),
			FGridCellCoord(4, 0, 1),
			FGridCellCoord(4, 1, 2)};
		for (const FGridCellCoord& Coord : Coords)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = Snapshot->GridId;
			Cell.Id.Coord = Coord;
		}

		const int32 ShortRoute[] = {0, 1, 2, 3, 4, 5, 6};
		const int32 FewTurnRoute[] = {0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 6};
		auto AddDirectedRoute = [&Snapshot](TConstArrayView<int32> Route)
		{
			for (int32 RouteIndex = 1; RouteIndex < Route.Num(); ++RouteIndex)
			{
				Snapshot->Cells[Route[RouteIndex - 1]].Neighbors.Add(Route[RouteIndex]);
			}
		};
		AddDirectedRoute(ShortRoute);
		AddDirectedRoute(FewTurnRoute);
		FString Error;
		check(Snapshot->Finalize(&Error));
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridTransformTest, "GridWorld.Core.Transforms", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridTransformTest::RunTest(const FString& Parameters)
{
	FGridTransform Transform;
	TestEqual(TEXT("Default cell size"), Transform.CellSize, FVector(100.0, 100.0, 50.0));
	Transform.CellSize = FVector(50.0, 50.0, 200.0);
	TestEqual(TEXT("Positive coordinate center"), Transform.CellToWorld(FGridCellCoord(1, 2, 3)), FVector(75.0, 125.0, 600.0));
	TestEqual(TEXT("Negative conversion"), Transform.WorldToCell(FVector(-0.1, -50.1, -101.0)), FGridCellCoord(-1, -2, -1));

	Transform.Origin = FVector(100.0, -50.0, 20.0);
	Transform.Rotation = FRotator(0.0, 90.0, 0.0);
	const FGridCellCoord Coord(-2, 3, 1);
	TestEqual(TEXT("Yaw transform round trip"), Transform.WorldToCell(Transform.CellToWorld(Coord)), Coord);

	Transform.Rotation = FRotator(23.0, -71.0, 14.0);
	TestEqual(TEXT("Full rotation transform round trip"), Transform.WorldToCell(Transform.CellToWorld(Coord)), Coord);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridNodeRefTest, "GridWorld.Core.NodeRefAndChunks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridNodeRefTest::RunTest(const FString& Parameters)
{
	uint32 Generation = 0;
	uint32 Index = 0;
	const NavNodeRef NodeRef = FGridWorldSnapshot::EncodeNodeRef(17, 42);
	TestTrue(TEXT("Node ref decodes"), FGridWorldSnapshot::DecodeNodeRef(NodeRef, Generation, Index));
	TestEqual(TEXT("Generation"), Generation, uint32(17));
	TestEqual(TEXT("Dense index"), Index, uint32(42));
	TestEqual(TEXT("Zero is invalid"), FGridWorldSnapshot::EncodeNodeRef(0, 42), INVALID_NAVNODEREF);

	const FGridChunkCoord NegativeChunk = FGridWorldSnapshot::CellToChunk(FGridCellCoord(-1, -17, 2));
	TestEqual(TEXT("Negative chunk X"), NegativeChunk.X, -1);
	TestEqual(TEXT("Negative chunk Y"), NegativeChunk.Y, -2);
	TestEqual(TEXT("Chunk keeps layer"), NegativeChunk.Layer, 2);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = UE::GridWorld::Tests::MakeSquareSnapshot(2, 1);
	const NavNodeRef CurrentRef = Snapshot->MakeNodeRef(1);
	TestEqual(TEXT("Current node ref resolves"), Snapshot->ResolveNodeRef(CurrentRef), 1);
	Snapshot->Revisions.Topology = 2;
	TestEqual(TEXT("Previous generation is stale"), Snapshot->ResolveNodeRef(CurrentRef), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridAStarTest, "GridWorld.Core.AStar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridAStarTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = UE::GridWorld::Tests::MakeSquareSnapshot(3, 3);
	FGridAStar AStar;
	FGridAStarQuery Query;
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 8;
	Query.MovementMode = EGridMovementMode::EightDirections;
	const FGridAStarResult DiagonalResult = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Eight direction status"), DiagonalResult.Status, EGridQueryStatus::Success);
	TestEqual(TEXT("Eight direction cell count"), DiagonalResult.CellIndices.Num(), 3);
	TestEqual(TEXT("Eight direction fixed cost"), DiagonalResult.TotalCost, int64(2828));

	Query.MovementMode = EGridMovementMode::FourDirections;
	const FGridAStarResult OrthogonalResult = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Four direction cell count"), OrthogonalResult.CellIndices.Num(), 5);
	TestEqual(TEXT("Four direction fixed cost"), OrthogonalResult.TotalCost, int64(4000));

	Snapshot->Cells[1].bWalkable = false;
	Snapshot->Cells[3].bWalkable = false;
	Query.MovementMode = EGridMovementMode::EightDirections;
	Query.bAllowCornerCutting = false;
	const FGridAStarResult NoCornerCutting = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Blocked corner is unreachable"), NoCornerCutting.Status, EGridQueryStatus::Unreachable);
	Query.bAllowCornerCutting = true;
	const FGridAStarResult CornerCutting = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Explicit corner cutting succeeds"), CornerCutting.Status, EGridQueryStatus::Success);

	Snapshot->Cells[1].bWalkable = true;
	Snapshot->Cells[3].bWalkable = true;
	Snapshot->Regions[Snapshot->GridId].MovementMode = EGridMovementMode::FourDirections;
	const FGridAStarResult RegionFourDirections = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Region four direction policy rejects diagonal adjacency"), RegionFourDirections.CellIndices.Num(), 5);

	Snapshot->Regions[Snapshot->GridId].MovementMode = EGridMovementMode::EightDirections;
	Snapshot->Regions[Snapshot->GridId].bAllowCornerCutting = false;
	Snapshot->Cells[1].bWalkable = false;
	Snapshot->Cells[3].bWalkable = false;
	const FGridAStarResult RegionRejectsCornerCutting = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Region corner policy overrides permissive query"), RegionRejectsCornerCutting.Status, EGridQueryStatus::Unreachable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridAStarPoliciesTest, "GridWorld.Core.AStarPolicies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridAStarPoliciesTest::RunTest(const FString& Parameters)
{
	FGridAStar AStar;
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> TwoCells = UE::GridWorld::Tests::MakeSquareSnapshot(2, 1);
	FGridAStarQuery Query;
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 1;

	TwoCells->Cells[1].bOccupied = true;
	TwoCells->Cells[1].bOccupancyBlocks = true;
	Query.OccupancyPolicy = EGridOccupancyPolicy::Ignore;
	TestEqual(TEXT("Occupancy ignored by default"), AStar.FindPath(*TwoCells, Query).Status, EGridQueryStatus::Success);
	Query.OccupancyPolicy = EGridOccupancyPolicy::Block;
	TestEqual(TEXT("Blocking occupancy is honored"), AStar.FindPath(*TwoCells, Query).Status, EGridQueryStatus::Unreachable);
	Query.OccupancyPolicy = EGridOccupancyPolicy::Ignore;
	Query.TraversalChannel = 1;
	TwoCells->Cells[1].TraversalChannels = 1u << 0;
	TestEqual(TEXT("Traversal channel is honored"), AStar.FindPath(*TwoCells, Query).Status, EGridQueryStatus::Unreachable);
	Query.TraversalChannel = 0;

	TwoCells->Cells[1].bOccupancyBlocks = false;
	TwoCells->Cells[1].OccupancyCost = 750;
	Query.OccupancyPolicy = EGridOccupancyPolicy::AddCost;
	TestEqual(TEXT("Occupancy cost is fixed-point"), AStar.FindPath(*TwoCells, Query).TotalCost, int64(1750));

	TwoCells->Cells[0].Neighbors.Reset();
	TwoCells->Cells[1].Neighbors.Reset();
	FGridLinkData& Link = TwoCells->Links.AddDefaulted_GetRef();
	Link.LinkId = FGuid::NewGuid();
	Link.FromCellIndex = 0;
	Link.ToCellIndex = 1;
	Link.TraversalCost = 500;
	Query.OccupancyPolicy = EGridOccupancyPolicy::Ignore;
	Query.bAllowLinks = true;
	TestEqual(TEXT("Explicit link connects cells"), AStar.FindPath(*TwoCells, Query).TotalCost, int64(500));
	Query.bAllowLinks = false;
	TestEqual(TEXT("Filter can reject links"), AStar.FindPath(*TwoCells, Query).Status, EGridQueryStatus::Unreachable);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> DiagonalLinkGrid = UE::GridWorld::Tests::MakeSquareSnapshot(2, 2);
	DiagonalLinkGrid->Regions[DiagonalLinkGrid->GridId].MovementMode = EGridMovementMode::FourDirections;
	for (FGridCellData& Cell : DiagonalLinkGrid->Cells)
	{
		Cell.Neighbors.Reset();
	}
	DiagonalLinkGrid->Cells[3].Id.Coord.Layer = 1;
	DiagonalLinkGrid->Cells[0].Neighbors.Add(3);
	Query = FGridAStarQuery();
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 3;
	Query.MovementMode = EGridMovementMode::FourDirections;
	TestEqual(TEXT("Four directions rejects diagonal adjacency across layers"), AStar.FindPath(*DiagonalLinkGrid, Query).Status, EGridQueryStatus::Unreachable);
	DiagonalLinkGrid->Cells[0].Neighbors.Reset();

	FGridLinkData& DiagonalLink = DiagonalLinkGrid->Links.AddDefaulted_GetRef();
	DiagonalLink.LinkId = FGuid::NewGuid();
	DiagonalLink.FromCellIndex = 0;
	DiagonalLink.ToCellIndex = 3;
	TestEqual(TEXT("Explicit links remain independent from movement mode"), AStar.FindPath(*DiagonalLinkGrid, Query).Status, EGridQueryStatus::Success);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> PartialGrid = UE::GridWorld::Tests::MakeSquareSnapshot(3, 3);
	PartialGrid->Cells[3].bWalkable = false;
	PartialGrid->Cells[4].bWalkable = false;
	PartialGrid->Cells[5].bWalkable = false;
	Query = FGridAStarQuery();
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 8;
	Query.bAllowPartialPath = true;
	const FGridAStarResult Partial = AStar.FindPath(*PartialGrid, Query);
	TestEqual(TEXT("Disconnected goal returns partial"), Partial.Status, EGridQueryStatus::Partial);
	TestTrue(TEXT("Partial path makes progress"), Partial.CellIndices.Num() > 1);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> AgentGrid = UE::GridWorld::Tests::MakeSquareSnapshot(3, 2);
	const FGuid OtherAgentId = FGuid::NewGuid();
	AgentGrid->Cells[1].OccupancyOwners.Add(OtherAgentId);
	Query = FGridAStarQuery();
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 2;
	Query.MovementMode = EGridMovementMode::FourDirections;
	Query.DynamicAgentPolicy = EGridDynamicAgentPolicy::YieldThenRepath;
	const FGridAStarResult AgentDetour = AStar.FindPath(*AgentGrid, Query);
	TestEqual(TEXT("Dynamic agent policy finds a route around an occupied intermediate cell"), AgentDetour.Status, EGridQueryStatus::Success);
	TestEqual(TEXT("Dynamic agent detour uses the four-step alternate corridor"), AgentDetour.CellIndices.Num(), 5);
	TestFalse(TEXT("Dynamic agent detour excludes the occupied cell"), AgentDetour.CellIndices.Contains(1));

	Query.IgnoredOccupancyOwnerId = OtherAgentId;
	const FGridAStarResult OwnOccupancyIgnored = AStar.FindPath(*AgentGrid, Query);
	TestEqual(TEXT("A query ignores its own occupancy identity"), OwnOccupancyIgnored.CellIndices.Num(), 3);
	Query.IgnoredOccupancyOwnerId.Invalidate();
	AgentGrid->Cells[1].ReservationOwners.Add(OtherAgentId);
	const FGridAStarResult ReservationIsNotAgent = AStar.FindPath(*AgentGrid, Query);
	TestEqual(TEXT("A reservation is not treated as a moving agent"), ReservationIsNotAgent.CellIndices.Num(), 3);

	AgentGrid->Cells[1].OccupancyOwners.Reset();
	AgentGrid->Cells[1].ReservationOwners.Reset();
	AgentGrid->Cells[2].OccupancyOwners.Add(OtherAgentId);
	const FGridAStarResult OccupiedGoalPreserved = AStar.FindPath(*AgentGrid, Query);
	TestEqual(TEXT("Goal occupancy remains the endpoint contention policy's responsibility"), OccupiedGoalPreserved.CellIndices.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPathOptimizationTest, "GridWorld.Core.PathOptimization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridPathOptimizationTest::RunTest(const FString& Parameters)
{
	FGridAStar AStar;
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = UE::GridWorld::Tests::MakeTurnChoiceSnapshot();
	FGridAStarQuery Query;
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 6;
	Query.MovementMode = EGridMovementMode::FourDirections;

	const FGridAStarResult Shortest = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Shortest Path preserves the existing six-step route"), Shortest.TotalCost, int64(6000));
	TestEqual(TEXT("Shortest Path reports its logical turns"), Shortest.TurnCount, 4);
	TestEqual(TEXT("Shortest Path retains its existing cell count"), Shortest.CellIndices.Num(), 7);

	Query.PathOptimizationMode = EGridPathOptimizationMode::FewestTurns;
	const FGridAStarResult FewestTurns = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Fewest Turns selects the two-turn route"), FewestTurns.TurnCount, 2);
	TestEqual(TEXT("Fewest Turns may select a longer route"), FewestTurns.TotalCost, int64(10000));
	TestEqual(TEXT("Fewest Turns route contains all ten steps"), FewestTurns.CellIndices.Num(), 11);

	Query.PathOptimizationMode = EGridPathOptimizationMode::Balanced;
	Query.BalancedTurnPenaltyCost = 1500;
	const FGridAStarResult DistanceBiased = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Lower Balanced penalty selects the shorter route"), DistanceBiased.TotalCost, int64(6000));
	TestEqual(TEXT("Lower Balanced penalty retains four turns"), DistanceBiased.TurnCount, 4);

	Query.BalancedTurnPenaltyCost = 2000;
	const FGridAStarResult BalancedTie = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Balanced tie prefers fewer turns"), BalancedTie.TurnCount, 2);
	TestEqual(TEXT("Balanced reports traversal cost without synthetic penalty"), BalancedTie.TotalCost, int64(10000));
	const FGridAStarResult BalancedRepeat = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Balanced path is deterministic"), BalancedRepeat.CellIndices, BalancedTie.CellIndices);

	Snapshot->Cells[9].bOccupied = true;
	Snapshot->Cells[9].OccupancyCost = 6000;
	Query.OccupancyPolicy = EGridOccupancyPolicy::AddCost;
	const FGridAStarResult OccupancyBiased = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Balanced includes occupancy in its real traversal cost"), OccupancyBiased.TotalCost, int64(6000));
	TestEqual(TEXT("Occupancy cost can make Balanced prefer the short route"), OccupancyBiased.TurnCount, 4);
	Query.PathOptimizationMode = EGridPathOptimizationMode::FewestTurns;
	const FGridAStarResult FewestTurnsWithCost = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Fewest Turns remains turn-first despite occupancy cost"), FewestTurnsWithCost.TurnCount, 2);
	TestEqual(TEXT("Fewest Turns still reports the real occupancy cost"), FewestTurnsWithCost.TotalCost, int64(16000));

	Snapshot->Cells[9].bOccupancyBlocks = true;
	Query.OccupancyPolicy = EGridOccupancyPolicy::Block;
	const FGridAStarResult BlockedFewTurnRoute = AStar.FindPath(*Snapshot, Query);
	TestEqual(TEXT("Blocking occupancy removes the fewer-turn route"), BlockedFewTurnRoute.TurnCount, 4);
	TestEqual(TEXT("Blocked Fewest Turns falls back to the valid route"), BlockedFewTurnRoute.TotalCost, int64(6000));

	FGridNavigationQueryFilterImpl FirstFilter;
	FGridNavigationQueryFilterImpl SecondFilter;
	TestTrue(TEXT("Default Grid filters compare equal"), FirstFilter.IsEqual(&SecondFilter));
	SecondFilter.SetPathOptimizationMode(EGridPathOptimizationMode::ShortestPath);
	TestFalse(TEXT("Optimization mode participates in filter equality"), FirstFilter.IsEqual(&SecondFilter));
	FirstFilter.SetPathOptimizationMode(EGridPathOptimizationMode::ShortestPath);
	SecondFilter.SetBalancedTurnPenalty(3.0f);
	TestFalse(TEXT("Balanced penalty participates in filter equality"), FirstFilter.IsEqual(&SecondFilter));
	FirstFilter.SetBalancedTurnPenalty(3.0f);
	SecondFilter.SetMaxSearchStates(32768);
	TestFalse(TEXT("Search-state budget participates in filter equality"), FirstFilter.IsEqual(&SecondFilter));
	FirstFilter.SetMaxSearchStates(32768);
	SecondFilter.SetDynamicAgentPolicy(EGridDynamicAgentPolicy::YieldThenRepath);
	TestFalse(TEXT("Dynamic agent policy participates in filter equality"), FirstFilter.IsEqual(&SecondFilter));
	FirstFilter.SetDynamicAgentPolicy(EGridDynamicAgentPolicy::YieldThenRepath);
	SecondFilter.SetMinimumAgentLookAheadCells(4);
	TestFalse(TEXT("Agent look-ahead participates in filter equality"), FirstFilter.IsEqual(&SecondFilter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridWalkingSurfaceTest, "GridWorld.Core.WalkingSurfaces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridWalkingSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace UE::GridWorld::WalkingSurface;
	constexpr double HalfHeight = 96.0;
	constexpr double Radius = 42.0;
	TestTrue(TEXT("Flat capsule placement is unchanged"), FMath::IsNearlyEqual(
		CalculateUprightCapsuleCenterHeight(HalfHeight, Radius, 1.0),
		97.0,
		UE_DOUBLE_KINDA_SMALL_NUMBER));
	TestTrue(TEXT("45 degree capsule clears the tangent plane"), FMath::IsNearlyEqual(
		CalculateUprightCapsuleCenterHeight(HalfHeight, Radius, FMath::Cos(FMath::DegreesToRadians(45.0))),
		HalfHeight - Radius + Radius / FMath::Cos(FMath::DegreesToRadians(45.0)) + 1.0,
		UE_DOUBLE_KINDA_SMALL_NUMBER));
	TestTrue(TEXT("60 degree capsule clears the tangent plane"), FMath::IsNearlyEqual(
		CalculateUprightCapsuleCenterHeight(HalfHeight, Radius, 0.5),
		139.0,
		UE_DOUBLE_KINDA_SMALL_NUMBER));

	const FVector3f SixtyDegreeNormal(FVector(-FMath::Sin(FMath::DegreesToRadians(60.0)), 0.0, 0.5));
	const FVector RampStart = FVector::ZeroVector;
	const FVector RampEnd(50.0, 0.0, 50.0 * FMath::Tan(FMath::DegreesToRadians(60.0)));
	TestTrue(TEXT("60 degree ramp rise is continuous rather than a step"), FMath::IsNearlyZero(
		CalculateResidualHeightDelta(RampStart, SixtyDegreeNormal, RampEnd, SixtyDegreeNormal),
		0.01));
	TestTrue(TEXT("60 degree ramp may rise farther than MaxStepHeight per cell"), IsWalkingTransitionAllowed(
		RampStart,
		SixtyDegreeNormal,
		RampEnd,
		SixtyDegreeNormal,
		45.0,
		45.0));

	const FVector3f FlatNormal(FVector::UpVector);
	TestTrue(TEXT("Flat step at limit is accepted"), IsWalkingTransitionAllowed(
		FVector::ZeroVector, FlatNormal, FVector(50.0, 0.0, 45.0), FlatNormal, 45.0, 45.0));
	TestFalse(TEXT("Flat ledge above step limit is rejected"), IsWalkingTransitionAllowed(
		FVector::ZeroVector, FlatNormal, FVector(50.0, 0.0, 46.0), FlatNormal, 45.0, 45.0));
	TestTrue(TEXT("Drop at limit is accepted"), IsWalkingTransitionAllowed(
		FVector::ZeroVector, FlatNormal, FVector(50.0, 0.0, -30.0), FlatNormal, 45.0, 30.0));
	TestFalse(TEXT("Drop beyond limit is rejected"), IsWalkingTransitionAllowed(
		FVector::ZeroVector, FlatNormal, FVector(50.0, 0.0, -31.0), FlatNormal, 45.0, 30.0));

	const FVector3f DiagonalRampNormal(FVector(-1.0, -1.0, 1.0).GetSafeNormal());
	TestTrue(TEXT("Diagonal ramp uses both horizontal gradients"), IsWalkingTransitionAllowed(
		FVector::ZeroVector,
		DiagonalRampNormal,
		FVector(50.0, 50.0, 100.0),
		DiagonalRampNormal,
		45.0,
		45.0));
	TestTrue(TEXT("Slope warning is required only above Character support"), RequiresWalkableFloorWarning(60.0, 45.0));
	TestFalse(TEXT("Matching Character slope support needs no warning"), RequiresWalkableFloorWarning(60.0, 60.0));

	TestEqual(
		TEXT("Four-direction heuristic ignores layer quantization"),
		UE::GridWorld::AStarMath::CalculateHeuristic(
			FGridCellCoord(0, 0, -12),
			FGridCellCoord(3, 2, 44),
			EGridMovementMode::FourDirections,
			1000,
			1414),
		int64(5000));
	TestEqual(
		TEXT("Eight-direction heuristic ignores layer quantization"),
		UE::GridWorld::AStarMath::CalculateHeuristic(
			FGridCellCoord(0, 0, -12),
			FGridCellCoord(3, 2, 44),
			EGridMovementMode::EightDirections,
			1000,
			1414),
		int64(3828));

	FGridWorldSnapshot LayeredCornerSnapshot;
	LayeredCornerSnapshot.GridId = FGuid::NewGuid();
	LayeredCornerSnapshot.Revisions.Topology = 1;
	FGridRegionData& LayeredRegion = LayeredCornerSnapshot.Regions.Add(LayeredCornerSnapshot.GridId);
	LayeredRegion.GridId = LayeredCornerSnapshot.GridId;
	LayeredRegion.MovementMode = EGridMovementMode::EightDirections;
	LayeredRegion.bAllowCornerCutting = false;
	const FGridCellCoord LayeredCoords[] = {
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 1),
		FGridCellCoord(0, 1, 2),
		FGridCellCoord(1, 1, 3)};
	for (const FGridCellCoord& Coord : LayeredCoords)
	{
		FGridCellData& Cell = LayeredCornerSnapshot.Cells.AddDefaulted_GetRef();
		Cell.Id.GridId = LayeredCornerSnapshot.GridId;
		Cell.Id.Coord = Coord;
	}
	LayeredCornerSnapshot.Cells[0].Neighbors = {1, 2, 3};
	FGridAStarQuery LayeredQuery;
	LayeredQuery.StartCellIndex = 0;
	LayeredQuery.GoalCellIndex = 3;
	LayeredQuery.MovementMode = EGridMovementMode::EightDirections;
	LayeredQuery.bAllowCornerCutting = false;
	FGridAStar LayeredAStar;
	TestEqual(
		TEXT("No-corner-cutting accepts published orthogonal neighbors on different layers"),
		LayeredAStar.FindPath(LayeredCornerSnapshot, LayeredQuery).Status,
		EGridQueryStatus::Success);
	LayeredCornerSnapshot.Cells[1].bWalkable = false;
	TestEqual(
		TEXT("Blocked layered side still rejects diagonal traversal"),
		LayeredAStar.FindPath(LayeredCornerSnapshot, LayeredQuery).Status,
		EGridQueryStatus::Unreachable);

	FGridRegionData ProjectionRegion;
	ProjectionRegion.GridId = FGuid::NewGuid();
	FGridCellData ProjectionCell;
	ProjectionCell.WorldCenter = FVector(25.0, 25.0, 0.0);
	ProjectionCell.FloorNormal = SixtyDegreeNormal;
	FVector ProjectedVertices[4];
	if (TestTrue(TEXT("Projected debug quad is constructed"), UE::GridWorld::Debug::BuildProjectedCellQuad(
		ProjectionRegion,
		ProjectionCell,
		ProjectedVertices)))
	{
		const FVector ProjectionNormal = FVector(SixtyDegreeNormal).GetSafeNormal();
		for (int32 VertexIndex = 0; VertexIndex < UE_ARRAY_COUNT(ProjectedVertices); ++VertexIndex)
		{
			const FVector PlaneVertex = ProjectedVertices[VertexIndex] - ProjectionNormal * 2.0;
			TestTrue(
				*FString::Printf(TEXT("Projected vertex %d lies on tangent plane"), VertexIndex),
				FMath::IsNearlyZero(FVector::DotProduct(ProjectionNormal, PlaneVertex - ProjectionCell.WorldCenter), 0.01));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPathOptimizationEdgeCasesTest, "GridWorld.Core.PathOptimizationEdgeCases", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridPathOptimizationEdgeCasesTest::RunTest(const FString& Parameters)
{
	auto InitializeSnapshot = [](FGridWorldSnapshot& Snapshot, EGridMovementMode MovementMode)
	{
		Snapshot.GridId = FGuid::NewGuid();
		Snapshot.Revisions.Topology = 1;
		FGridRegionData& Region = Snapshot.Regions.Add(Snapshot.GridId);
		Region.GridId = Snapshot.GridId;
		Region.MovementMode = MovementMode;
		Region.bAllowCornerCutting = true;
	};
	auto AddCell = [](FGridWorldSnapshot& Snapshot, const FGridCellCoord& Coord)
	{
		FGridCellData& Cell = Snapshot.Cells.AddDefaulted_GetRef();
		Cell.Id.GridId = Snapshot.GridId;
		Cell.Id.Coord = Coord;
	};

	FGridAStar AStar;
	FGridWorldSnapshot DiagonalSnapshot;
	InitializeSnapshot(DiagonalSnapshot, EGridMovementMode::EightDirections);
	AddCell(DiagonalSnapshot, FGridCellCoord(0, 0, -4));
	AddCell(DiagonalSnapshot, FGridCellCoord(1, 1, 7));
	AddCell(DiagonalSnapshot, FGridCellCoord(2, 1, -2));
	DiagonalSnapshot.Cells[0].Neighbors.Add(1);
	DiagonalSnapshot.Cells[1].Neighbors.Add(2);
	FGridAStarQuery Query;
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 2;
	Query.MovementMode = EGridMovementMode::EightDirections;
	Query.bAllowCornerCutting = true;
	Query.PathOptimizationMode = EGridPathOptimizationMode::FewestTurns;
	const FGridAStarResult DiagonalResult = AStar.FindPath(DiagonalSnapshot, Query);
	TestEqual(TEXT("Layered diagonal path succeeds"), DiagonalResult.Status, EGridQueryStatus::Success);
	TestEqual(TEXT("Diagonal to orthogonal counts as one turn"), DiagonalResult.TurnCount, 1);
	TestEqual(TEXT("Layer changes do not add turns"), DiagonalResult.TotalCost, int64(2414));

	FGridWorldSnapshot LinkSnapshot;
	InitializeSnapshot(LinkSnapshot, EGridMovementMode::FourDirections);
	AddCell(LinkSnapshot, FGridCellCoord(0, 0, 0));
	AddCell(LinkSnapshot, FGridCellCoord(1, 0, 0));
	AddCell(LinkSnapshot, FGridCellCoord(10, 10, 0));
	AddCell(LinkSnapshot, FGridCellCoord(10, 11, 0));
	AddCell(LinkSnapshot, FGridCellCoord(11, 11, 0));
	LinkSnapshot.Cells[0].Neighbors.Add(1);
	LinkSnapshot.Cells[2].Neighbors.Add(3);
	LinkSnapshot.Cells[3].Neighbors.Add(4);
	FGridLinkData& Link = LinkSnapshot.Links.AddDefaulted_GetRef();
	Link.LinkId = FGuid::NewGuid();
	Link.FromCellIndex = 1;
	Link.ToCellIndex = 2;
	Link.TraversalCost = 1000;
	Query = FGridAStarQuery();
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 4;
	Query.MovementMode = EGridMovementMode::FourDirections;
	Query.PathOptimizationMode = EGridPathOptimizationMode::FewestTurns;
	const FGridAStarResult LinkResult = AStar.FindPath(LinkSnapshot, Query);
	TestEqual(TEXT("Explicit link resets incoming direction"), LinkResult.TurnCount, 1);
	TestEqual(TEXT("Link reset preserves real traversal cost"), LinkResult.TotalCost, int64(4000));

	FGridWorldSnapshot PartialSnapshot;
	InitializeSnapshot(PartialSnapshot, EGridMovementMode::FourDirections);
	const FGridCellCoord PartialCoords[] = {
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(0, 1, 0),
		FGridCellCoord(1, 1, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(2, 0, 0),
		FGridCellCoord(3, 0, 0),
		FGridCellCoord(0, 2, 0),
		FGridCellCoord(0, 3, 0),
		FGridCellCoord(4, 0, 0)};
	for (const FGridCellCoord& Coord : PartialCoords)
	{
		AddCell(PartialSnapshot, Coord);
	}
	const int32 TurningRoute[] = {0, 1, 2, 3, 4, 5};
	const int32 StraightRoute[] = {0, 6, 7};
	for (int32 PathIndex = 1; PathIndex < UE_ARRAY_COUNT(TurningRoute); ++PathIndex)
	{
		PartialSnapshot.Cells[TurningRoute[PathIndex - 1]].Neighbors.Add(TurningRoute[PathIndex]);
	}
	for (int32 PathIndex = 1; PathIndex < UE_ARRAY_COUNT(StraightRoute); ++PathIndex)
	{
		PartialSnapshot.Cells[StraightRoute[PathIndex - 1]].Neighbors.Add(StraightRoute[PathIndex]);
	}
	Query = FGridAStarQuery();
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 8;
	Query.MovementMode = EGridMovementMode::FourDirections;
	Query.PathOptimizationMode = EGridPathOptimizationMode::FewestTurns;
	Query.bAllowPartialPath = true;
	const FGridAStarResult PartialResult = AStar.FindPath(PartialSnapshot, Query);
	TestEqual(TEXT("Directional search returns a partial path"), PartialResult.Status, EGridQueryStatus::Partial);
	TestEqual(TEXT("Partial selection prioritizes geometric progress"), PartialResult.CellIndices.Last(), 5);
	TestEqual(TEXT("Closest partial endpoint may contain more turns"), PartialResult.TurnCount, 3);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> LimitedSnapshot = UE::GridWorld::Tests::MakeTurnChoiceSnapshot();
	Query = FGridAStarQuery();
	Query.StartCellIndex = 0;
	Query.GoalCellIndex = 6;
	Query.MovementMode = EGridMovementMode::FourDirections;
	Query.PathOptimizationMode = EGridPathOptimizationMode::Balanced;
	Query.bAllowPartialPath = true;
	Query.MaxVisitedNodes = 2;
	const FGridAStarResult LimitedResult = AStar.FindPath(*LimitedSnapshot, Query);
	TestEqual(TEXT("Directional visited-state limit is enforced"), LimitedResult.VisitedNodes, 2);
	TestEqual(TEXT("Visited-state limit can return partial progress"), LimitedResult.Status, EGridQueryStatus::Partial);
	TestTrue(TEXT("Directional result identifies a truncated search"), LimitedResult.bReachedSearchLimit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridDirtyAreaPolicyTest, "GridWorld.Core.DirtyAreaPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridDirtyAreaPolicyTest::RunTest(const FString& Parameters)
{
	using UE::GridWorld::Private::ShouldRebuildRegionForDirtyArea;

	const FBox Bounds(FVector::ZeroVector, FVector(100.0));
	const FNavigationDirtyArea GeometryArea(Bounds, ENavigationDirtyFlag::All);
	const FNavigationDirtyArea BoundsArea(Bounds, ENavigationDirtyFlag::All | ENavigationDirtyFlag::NavigationBounds);
	TestFalse(
		TEXT("Automatic geometry is skipped when the region option is disabled"),
		ShouldRebuildRegionForDirtyArea(GeometryArea, true, false));
	TestTrue(
		TEXT("Automatic geometry rebuilds when the region option is enabled"),
		ShouldRebuildRegionForDirtyArea(GeometryArea, true, true));
	TestTrue(
		TEXT("Explicit dirty rebuilds bypass the region option"),
		ShouldRebuildRegionForDirtyArea(GeometryArea, false, false));
	TestTrue(
		TEXT("Navigation bounds changes always rebuild"),
		ShouldRebuildRegionForDirtyArea(BoundsArea, true, false));
	return true;
}

#endif
