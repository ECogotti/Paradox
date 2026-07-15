// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/GridCenterGate.h"
#include "AI/GridGoalContention.h"
#include "AI/GridPathDrive.h"
#include "AI/BTTask_MoveToGridCell.h"
#include "AI/GridMoveToCellTask.h"
#include "AI/GridWorldAIController.h"
#include "AI/GridWorldPathFollowingComponent.h"
#include "AI/StateTreeMoveToGridCellTask.h"
#include "AIController.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/PackageName.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationBoundsVolume.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridNavigationQueryFilter.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tasks/AITask_MoveTo.h"
#include "Tasks/StateTreeMoveToTask.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UE::GridWorld::Serialization
{
	void SerializeSnapshot(FArchive& Ar, FGridWorldSnapshot& Snapshot, int32 Version);
	bool CanConsumeVersion(int32 Version);
	bool CanPublishVersion(int32 Version);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridNavigationIntegrationTest, "GridWorld.Navigation.StandardQueries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridNavigationIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldNavigationTest")));
	if (!TestNotNull(TEXT("Transient query world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	if (!TestNotNull(TEXT("Grid navigation data"), NavData))
	{
		World->DestroyWorld(false);
		return false;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = FGuid::NewGuid();
	Snapshot->Revisions.Topology = 1;
	for (int32 X = 0; X < 3; ++X)
	{
		FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
		Cell.Id.Coord = FGridCellCoord(X, 0, 0);
		if (X > 0)
		{
			Cell.Neighbors.Add(X - 1);
			Snapshot->Cells[X - 1].Neighbors.Add(X);
		}
	}
	FString PublishError;
	TestTrue(TEXT("Manual topology publishes"), NavData->PublishSnapshot(Snapshot, &PublishError));

	FNavLocation ProjectedStart;
	FNavLocation ProjectedGoal;
	TestTrue(TEXT("Standard projection finds start"), NavData->ProjectPoint(FVector(5.0, 5.0, 10.0), ProjectedStart, FVector(50.0, 50.0, 200.0)));
	TestTrue(TEXT("Standard projection finds goal"), NavData->ProjectPoint(FVector(245.0, 5.0, -10.0), ProjectedGoal, FVector(50.0, 50.0, 200.0)));
	TestTrue(TEXT("Projected node ref is valid"), NavData->IsNodeRefValid(ProjectedStart.NodeRef));

	FPathFindingQuery Query(NavData, *NavData, ProjectedStart.Location, ProjectedGoal.Location, NavData->GetDefaultQueryFilter());
	const FPathFindingResult PathResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	TestTrue(TEXT("Standard find path succeeds"), PathResult.IsSuccessful());
	const FGridNavigationPath* GridPath = PathResult.Path.IsValid() ? PathResult.Path->CastPath<FGridNavigationPath>() : nullptr;
	TestNotNull(TEXT("Path uses Grid path type"), GridPath);
	if (GridPath != nullptr)
	{
		TestEqual(TEXT("Path retains ordered cells"), GridPath->CellPath.Num(), 3);
		TestEqual(TEXT("Path points are usable by standard path following"), GridPath->GetPathPoints().Num(), 3);
		TestFalse(TEXT("Connected path is not partial"), GridPath->IsPartial());
	}

	FVector RayHit;
	FNavigationRaycastAdditionalResults RayResults;
	TestFalse(TEXT("Raycast through corridor is unobstructed"), AGridNavigationData::NavigationRaycast(
		NavData, ProjectedStart.Location, ProjectedGoal.Location, RayHit, &RayResults, NavData->GetDefaultQueryFilter(), NavData));
	TestTrue(TEXT("Ray end remains in corridor"), RayResults.bIsRayEndInCorridor);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> InvalidSnapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>(*Snapshot);
	const FGridCellData DuplicateCell = InvalidSnapshot->Cells[0];
	InvalidSnapshot->Cells.Add(DuplicateCell);
	AddExpectedError(TEXT("Rejected invalid GridWorld snapshot"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid replacement is rejected"), NavData->PublishSnapshot(InvalidSnapshot, &PublishError));
	TestEqual(TEXT("Last valid topology remains published"), NavData->GetSnapshot()->Cells.Num(), 3);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridNavigationDebugPathTest, "GridWorld.Navigation.DebugPathLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridNavigationDebugPathTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldDebugPathTest")));
	if (!TestNotNull(TEXT("Transient debug world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	AAIController* FirstController = World->SpawnActor<AAIController>();
	AAIController* SecondController = World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("Grid navigation data"), NavData)
		|| !TestNotNull(TEXT("First debug controller"), FirstController)
		|| !TestNotNull(TEXT("Second debug controller"), SecondController))
	{
		World->DestroyWorld(false);
		return false;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = FGuid::NewGuid();
	Snapshot->Revisions.Topology = 1;
	for (int32 X = 0; X < 3; ++X)
	{
		FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
		Cell.Id.Coord = FGridCellCoord(X, 0, 0);
		if (X > 0)
		{
			Cell.Neighbors.Add(X - 1);
			Snapshot->Cells[X - 1].Neighbors.Add(X);
		}
	}
	FString PublishError;
	if (!TestTrue(TEXT("Debug topology publishes"), NavData->PublishSnapshot(Snapshot, &PublishError)))
	{
		World->DestroyWorld(false);
		return false;
	}

	auto FindControllerPath = [NavData](AAIController& Controller)
	{
		FPathFindingQuery Query(&Controller, *NavData, FVector(25.0, 25.0, 0.0), FVector(125.0, 25.0, 0.0), NavData->GetDefaultQueryFilter());
		return AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	};

	FPathFindingResult FirstPathResult = FindControllerPath(*FirstController);
	TestTrue(TEXT("First controller path succeeds"), FirstPathResult.IsSuccessful());
	TArray<TArray<FVector>> DebugPaths;
	TArray<FVector> Reachability;
	NavData->GetDebugQueryData(DebugPaths, Reachability);
	TestEqual(TEXT("First active path is published"), DebugPaths.Num(), 1);

	FGridNavigationPath* FirstGridPath = FirstPathResult.Path.IsValid() ? FirstPathResult.Path->CastPath<FGridNavigationPath>() : nullptr;
	if (TestNotNull(TEXT("Debug path uses Grid path"), FirstGridPath))
	{
		FirstGridPath->GetPathPoints()[1].Location += FVector(0.0, 10.0, 0.0);
		FirstGridPath->DoneUpdating(ENavPathUpdateType::NavigationChanged);
		NavData->GetDebugQueryData(DebugPaths, Reachability);
		if (TestTrue(TEXT("Runtime path update retains a drawable path"), DebugPaths.Num() == 1 && DebugPaths[0].Num() > 1))
		{
			TestEqual(TEXT("Runtime path update refreshes debug points"), DebugPaths[0][1], FirstGridPath->GetPathPoints()[1].Location);
		}
	}

	const FPathFindingResult SecondPathResult = FindControllerPath(*SecondController);
	TestTrue(TEXT("Second controller path succeeds"), SecondPathResult.IsSuccessful());
	NavData->GetDebugQueryData(DebugPaths, Reachability);
	TestEqual(TEXT("Multiple active AI paths are retained"), DebugPaths.Num(), 2);

	if (UPathFollowingComponent* FirstPathFollowing = FirstController->GetPathFollowingComponent())
	{
		FirstPathFollowing->OnRequestFinished.Broadcast(
			FAIRequestID::CurrentRequest,
			FPathFollowingResult(EPathFollowingResult::Success, FPathFollowingResultFlags::Success));
		World->GetTimerManager().Tick(1.0f / 60.0f);
	}
	NavData->GetDebugQueryData(DebugPaths, Reachability);
	TestEqual(TEXT("Completing one move clears only its path"), DebugPaths.Num(), 1);

	if (FGridNavigationPath* SecondGridPath = SecondPathResult.Path.IsValid() ? SecondPathResult.Path->CastPath<FGridNavigationPath>() : nullptr)
	{
		SecondGridPath->Invalidate();
	}
	NavData->GetDebugQueryData(DebugPaths, Reachability);
	TestEqual(TEXT("Invalidation clears the stale path line"), DebugPaths.Num(), 0);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridGeneratedDataPersistenceTest, "GridWorld.Navigation.GeneratedDataPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridGeneratedDataPersistenceTest::RunTest(const FString& Parameters)
{
	const FString PackageName = FString::Printf(TEXT("/Temp/GridWorldGeneratedDataPersistence_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* WorldPackage = CreatePackage(*PackageName);
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(*FPackageName::GetLongPackageAssetName(PackageName)), WorldPackage);
	if (!TestNotNull(TEXT("Editor persistence world package"), WorldPackage)
		|| !TestNotNull(TEXT("Editor persistence world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	if (!TestNotNull(TEXT("Persistent Grid navigation data"), NavData))
	{
		World->DestroyWorld(false);
		return false;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = FGuid::NewGuid();
	Snapshot->Revisions.Topology = 1;
	FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
	Cell.Id.GridId = Snapshot->GridId;
	Cell.Id.Coord = FGridCellCoord(0, 0, 0);

	WorldPackage->ClearDirtyFlag();
	FString PublishError;
	TestTrue(TEXT("Generated topology publishes in an editor level"), NavData->PublishSnapshot(Snapshot, &PublishError));
	TestTrue(TEXT("Publishing generated topology marks its level package dirty"), WorldPackage->IsDirty());

	WorldPackage->ClearDirtyFlag();
	NavData->ClearGridWorld();
	TestTrue(TEXT("Clearing generated topology also marks its level package dirty"), WorldPackage->IsDirty());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridNavigationSlopeWarningTest, "GridWorld.Navigation.SlopeWarning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridNavigationSlopeWarningTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldSlopeWarningTest")));
	if (!TestNotNull(TEXT("Transient slope warning world"), World))
	{
		return false;
	}

	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	AAIController* Controller = World->SpawnActor<AAIController>();
	ACharacter* Character = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Slope warning navigation data"), NavData)
		|| !TestNotNull(TEXT("Slope warning controller"), Controller)
		|| !TestNotNull(TEXT("Slope warning Character"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	Controller->Possess(Character);
	UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
	CharacterMovement->SetWalkableFloorAngle(45.0f);

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = FGuid::NewGuid();
	Snapshot->Revisions.Topology = 1;
	FGridRegionData& Region = Snapshot->Regions.Add(Snapshot->GridId);
	Region.GridId = Snapshot->GridId;
	const FVector3f SixtyDegreeNormal(FVector(-FMath::Sin(FMath::DegreesToRadians(60.0)), 0.0, 0.5));
	for (int32 CellIndex = 0; CellIndex < 2; ++CellIndex)
	{
		FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
		Cell.Id.GridId = Snapshot->GridId;
		Cell.Id.Coord = FGridCellCoord(CellIndex, 0, CellIndex);
		Cell.WorldCenter = FVector(25.0 + 50.0 * CellIndex, 25.0, 50.0 * FMath::Tan(FMath::DegreesToRadians(60.0)) * CellIndex);
		Cell.FloorNormal = SixtyDegreeNormal;
		Cell.bHasAuthoredWorldCenter = true;
	}
	Snapshot->Cells[0].Neighbors.Add(1);
	Snapshot->Cells[1].Neighbors.Add(0);
	FString PublishError;
	if (!TestTrue(TEXT("Slope warning topology publishes"), NavData->PublishSnapshot(Snapshot, &PublishError)))
	{
		World->DestroyWorld(false);
		return false;
	}

	AddExpectedError(TEXT("received a GridWorld path with"), EAutomationExpectedErrorFlags::Contains, 1);
	FPathFindingQuery Query(Controller, *NavData, Snapshot->Cells[0].WorldCenter, Snapshot->Cells[1].WorldCenter, NavData->GetDefaultQueryFilter());
	const FPathFindingResult FirstResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	const FPathFindingResult SecondResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	TestTrue(TEXT("Slope path succeeds"), FirstResult.IsSuccessful() && SecondResult.IsSuccessful());
	const FGridNavigationPath* GridPath = FirstResult.Path.IsValid() ? FirstResult.Path->CastPath<FGridNavigationPath>() : nullptr;
	if (TestNotNull(TEXT("Slope path uses Grid path type"), GridPath))
	{
		TestTrue(TEXT("Path records the steepest crossed slope"), FMath::IsNearlyEqual(GridPath->MaximumFloorSlopeDegrees, 60.0f, 0.1f));
	}
	TestTrue(TEXT("GridWorld never changes Character Walkable Floor Angle"), FMath::IsNearlyEqual(CharacterMovement->GetWalkableFloorAngle(), 45.0f));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridMoveToCellApiTest, "GridWorld.Navigation.MoveToCellApi", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridMoveToCellApiTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Grid AI task extends native MoveTo"), UGridMoveToCellTask::StaticClass()->IsChildOf(UAITask_MoveTo::StaticClass()));
	TestTrue(TEXT("Grid BT task extends native MoveTo"), UBTTask_MoveToGridCell::StaticClass()->IsChildOf(UBTTask_MoveTo::StaticClass()));
	TestTrue(TEXT("Grid StateTree task extends native MoveTo"), FStateTreeMoveToGridCellTask::StaticStruct()->GetSuperStruct() == FStateTreeMoveToTask::StaticStruct());

	const UFunction* AsyncFactory = UGridMoveToCellTask::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UGridMoveToCellTask, MoveToGridCell));
	TestNotNull(TEXT("Blueprint async Move To Grid Cell factory is reflected"), AsyncFactory);
#if WITH_METADATA
	if (AsyncFactory != nullptr)
	{
		TestTrue(TEXT("Blueprint factory is internal-use async API"), AsyncFactory->HasMetaData(TEXT("BlueprintInternalUseOnly")));
		TestNotNull(TEXT("Blueprint factory exposes goal contention policy"), FindFProperty<FProperty>(AsyncFactory, TEXT("GoalContentionPolicy")));
		TestNotNull(TEXT("Blueprint factory exposes alternative search radius"), FindFProperty<FProperty>(AsyncFactory, TEXT("MaxAlternativeSearchRadius")));
		TestNotNull(TEXT("Blueprint factory exposes goal availability timeout"), FindFProperty<FProperty>(AsyncFactory, TEXT("GoalAvailabilityTimeout")));
		TestNotNull(TEXT("Blueprint factory exposes wait warning interval"), FindFProperty<FProperty>(AsyncFactory, TEXT("GoalWaitWarningInterval")));
	}
	const FProperty* BTGoalContentionProperty = FindFProperty<FProperty>(
		UBTTask_MoveToGridCell::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UBTTask_MoveToGridCell, GoalContentionPolicy));
	if (TestNotNull(TEXT("BT Move To Grid Cell exposes goal contention"), BTGoalContentionProperty))
	{
		TestTrue(TEXT("BT goal contention is designer-editable"), BTGoalContentionProperty->HasAnyPropertyFlags(CPF_Edit));
	}
	TestNotNull(
		TEXT("StateTree Move To Grid Cell exposes goal contention"),
		FindFProperty<FProperty>(
			FStateTreeMoveToGridCellTask::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(FStateTreeMoveToGridCellTask, GoalContentionPolicy)));
	const FProperty* DriveModeProperty = FindFProperty<FProperty>(AGridNavigationBoundsVolume::StaticClass(), GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, PathDriveMode));
	if (TestNotNull(TEXT("Path Drive Mode is reflected"), DriveModeProperty))
	{
		TestTrue(TEXT("Path Drive Mode is designer-editable"), DriveModeProperty->HasAnyPropertyFlags(CPF_Edit));
		TestEqual(
			TEXT("Path Drive Mode is shown only for precise styles"),
			DriveModeProperty->GetMetaData(TEXT("EditCondition")),
			FString(TEXT("PathFollowingStyle != EGridPathFollowingStyle::Standard")));
	}
	const FProperty* FinalApproachProperty = FindFProperty<FProperty>(AGridNavigationBoundsVolume::StaticClass(), GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, bUseAcceleratedFinalApproach));
	if (TestNotNull(TEXT("Accelerated Final Approach is reflected"), FinalApproachProperty))
	{
		TestTrue(TEXT("Accelerated Final Approach is designer-editable"), FinalApproachProperty->HasAnyPropertyFlags(CPF_Edit));
		TestTrue(
			TEXT("Accelerated Final Approach is restricted to Direct Velocity"),
			FinalApproachProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("PathDriveMode == EGridPathDriveMode::DirectVelocity")));
	}
	const FProperty* AutoGeometryRebuildProperty = FindFProperty<FProperty>(
		AGridNavigationBoundsVolume::StaticClass(),
		GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, bAutoRebuildOnGeometryChanges));
	if (TestNotNull(TEXT("Auto Rebuild On Geometry Changes is reflected"), AutoGeometryRebuildProperty))
	{
		TestTrue(TEXT("Auto geometry rebuild is designer-editable"), AutoGeometryRebuildProperty->HasAnyPropertyFlags(CPF_Edit));
	}
	const FProperty* OptimizationModeProperty = FindFProperty<FProperty>(
		UGridNavigationQueryFilter::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UGridNavigationQueryFilter, PathOptimizationMode));
	TestNotNull(TEXT("Path Optimization Mode is reflected on the query filter"), OptimizationModeProperty);
	const FProperty* TurnPenaltyProperty = FindFProperty<FProperty>(
		UGridNavigationQueryFilter::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UGridNavigationQueryFilter, BalancedTurnPenalty));
	if (TestNotNull(TEXT("Balanced Turn Penalty is reflected"), TurnPenaltyProperty))
	{
		TestEqual(
			TEXT("Balanced Turn Penalty is shown only for Balanced mode"),
			TurnPenaltyProperty->GetMetaData(TEXT("EditCondition")),
			FString(TEXT("PathOptimizationMode == EGridPathOptimizationMode::Balanced")));
	}
	const FProperty* MaxSearchStatesProperty = FindFProperty<FProperty>(
		UGridNavigationQueryFilter::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UGridNavigationQueryFilter, MaxSearchStates));
	if (TestNotNull(TEXT("Max Search States is reflected"), MaxSearchStatesProperty))
	{
		TestEqual(
			TEXT("Max Search States is shown only for directional optimization"),
			MaxSearchStatesProperty->GetMetaData(TEXT("EditCondition")),
			FString(TEXT("PathOptimizationMode != EGridPathOptimizationMode::ShortestPath")));
	}
	const FProperty* DynamicAgentPolicyProperty = FindFProperty<FProperty>(
		UGridNavigationQueryFilter::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UGridNavigationQueryFilter, DynamicAgentPolicy));
	TestNotNull(TEXT("Dynamic Agent Policy is reflected on the query filter"), DynamicAgentPolicyProperty);
	const FProperty* RepathDelayProperty = FindFProperty<FProperty>(
		UGridNavigationQueryFilter::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UGridNavigationQueryFilter, DynamicAgentRepathDelay));
	if (TestNotNull(TEXT("Dynamic Agent Repath Delay is reflected"), RepathDelayProperty))
	{
		TestEqual(
			TEXT("Repath delay is shown for repathing dynamic-agent policies"),
			RepathDelayProperty->GetMetaData(TEXT("EditCondition")),
			FString(TEXT("DynamicAgentPolicy == EGridDynamicAgentPolicy::YieldThenRepath || DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor")));
	}
	const FProperty* ReservedLookAheadProperty = FindFProperty<FProperty>(
		UGridNavigationQueryFilter::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UGridNavigationQueryFilter, ReservedLookAheadCells));
	if (TestNotNull(TEXT("Reserved Look Ahead Cells is reflected"), ReservedLookAheadProperty))
	{
		TestEqual(
			TEXT("Reserved look-ahead is visible only for Reserved Corridor"),
			ReservedLookAheadProperty->GetMetaData(TEXT("EditCondition")),
			FString(TEXT("DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor")));
	}
#endif

	const AGridNavigationBoundsVolume* DefaultBounds = GetDefault<AGridNavigationBoundsVolume>();
	TestEqual(TEXT("Bounds default horizontal cell size"), DefaultBounds->HorizontalCellSize, FVector2D(100.0, 100.0));
	TestEqual(TEXT("Bounds default layer height"), DefaultBounds->LayerHeight, 50.0);
	TestEqual(TEXT("Bounds default to four directions"), DefaultBounds->MovementMode, EGridMovementMode::FourDirections);
	TestEqual(TEXT("Bounds default to native path following"), DefaultBounds->PathFollowingStyle, EGridPathFollowingStyle::Standard);
	TestEqual(TEXT("Bounds default to direct-velocity precise-path drive"), DefaultBounds->PathDriveMode, EGridPathDriveMode::DirectVelocity);
	TestFalse(TEXT("Accelerated final approach defaults off"), DefaultBounds->bUseAcceleratedFinalApproach);
	TestTrue(TEXT("Bounds auto-rebuild navigation geometry by default"), DefaultBounds->bAutoRebuildOnGeometryChanges);
	TestEqual(TEXT("Default cell-center tolerance"), DefaultBounds->CellCenterTolerance, 2.0f);
	TestEqual(TEXT("Default stop-speed tolerance"), DefaultBounds->StopSpeedTolerance, 5.0f);
	const UGridNavigationQueryFilter* DefaultGridFilter = GetDefault<UGridNavigationQueryFilter>();
	TestEqual(TEXT("Grid query filter defaults to four directions"), DefaultGridFilter->MovementMode, EGridMovementMode::FourDirections);
	TestEqual(TEXT("Grid query filter defaults to Balanced"), DefaultGridFilter->PathOptimizationMode, EGridPathOptimizationMode::Balanced);
	TestEqual(TEXT("Balanced penalty defaults to two equivalent cells"), DefaultGridFilter->BalancedTurnPenalty, 2.0f);
	TestEqual(TEXT("Directional search defaults to 65536 states"), DefaultGridFilter->MaxSearchStates, 65536);
	TestEqual(TEXT("Dynamic agent avoidance defaults to opt-in"), DefaultGridFilter->DynamicAgentPolicy, EGridDynamicAgentPolicy::Ignore);
	TestEqual(TEXT("Dynamic agent look-ahead defaults to three cells"), DefaultGridFilter->MinimumAgentLookAheadCells, 3);
	TestEqual(TEXT("Reserved corridor defaults to one designer cell"), DefaultGridFilter->ReservedLookAheadCells, 1);
	TestEqual(TEXT("Dynamic agent separation defaults to five centimeters"), DefaultGridFilter->AdditionalAgentSeparation, 5.0f);
	TestEqual(TEXT("Stationary agent threshold defaults to five centimeters per second"), DefaultGridFilter->StationaryAgentSpeedThreshold, 5.0f);
	TestEqual(TEXT("Dynamic agent repath delay defaults to 0.1 seconds"), DefaultGridFilter->DynamicAgentRepathDelay, 0.1f);
	TestTrue(TEXT("GridWorld controllers auto-register Pawn occupancy by default"), GetDefault<AGridWorldAIController>()->bAutoRegisterPawnOccupancy);
	const UBTTask_MoveToGridCell* DefaultGridMoveTask = GetDefault<UBTTask_MoveToGridCell>();
	TestEqual(TEXT("Goal contention defaults to opt-in"), DefaultGridMoveTask->GoalContentionPolicy, EGridGoalContentionPolicy::Ignore);
	TestEqual(TEXT("Alternative search defaults to three graph cells"), DefaultGridMoveTask->MaxAlternativeSearchRadius, 3);
	TestEqual(TEXT("Additional goal separation defaults to five centimeters"), DefaultGridMoveTask->AdditionalGoalSeparation, 5.0f);
	TestTrue(TEXT("Contention-enabled tasks auto-register Pawn occupancy by default"), DefaultGridMoveTask->bAutoRegisterPawnOccupancy);
	TestEqual(TEXT("Goal availability timeout defaults to five seconds"), DefaultGridMoveTask->GoalAvailabilityTimeout, 5.0f);
	TestEqual(TEXT("Goal wait warning interval defaults to one second"), DefaultGridMoveTask->GoalWaitWarningInterval, 1.0f);

	const UBlueprint* SampleController = LoadObject<UBlueprint>(nullptr, TEXT("/GridWorldSystem/AI/BP_GridAiController.BP_GridAiController"));
	if (TestNotNull(TEXT("Sample GridWorld controller Blueprint loads"), SampleController))
	{
		TestTrue(
			TEXT("Sample controller uses the GridWorld native controller"),
			SampleController->ParentClass.Get() == AGridWorldAIController::StaticClass());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridGoalContentionRepeatedRedirectTest, "GridWorld.Navigation.GoalContentionRepeatedRedirect", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridGoalContentionRepeatedRedirectTest::RunTest(const FString& Parameters)
{
	FGridWorldSnapshot Snapshot;
	Snapshot.GridId = FGuid::NewGuid();
	for (int32 X = 0; X < 7; ++X)
	{
		FGridCellData& Cell = Snapshot.Cells.AddDefaulted_GetRef();
		Cell.Id.GridId = Snapshot.GridId;
		Cell.Id.Coord = FGridCellCoord(X, 0, 0);
		Cell.WorldCenter = FVector(X * 50.0, 0.0, 0.0);
		if (X > 0)
		{
			Cell.Neighbors.Add(X - 1);
			Snapshot.Cells[X - 1].Neighbors.Add(X);
		}
	}

	const int32 DesiredIndex = 3;
	Snapshot.Cells[DesiredIndex].OccupancyOwners.Add(FGuid::NewGuid());
	TSet<FGridCellId> RejectedCells;
	auto SelectFirstSeparatedCandidate = [&Snapshot, DesiredIndex, &RejectedCells]()
	{
		TArray<UE::GridWorld::Private::FGridGoalCandidate> Candidates;
		UE::GridWorld::Private::GatherGridGoalCandidates(
			Snapshot,
			DesiredIndex,
			false,
			3,
			RejectedCells,
			Candidates);
		for (const UE::GridWorld::Private::FGridGoalCandidate& Candidate : Candidates)
		{
			if (UE::GridWorld::Private::HasGridGoalOccupancySeparation(
				Snapshot,
				Snapshot.Cells[Candidate.CellIndex],
				FGuid(),
				42.0f,
				192.0f,
				5.0f))
			{
				return Candidate.CellIndex;
			}
		}
		return static_cast<int32>(INDEX_NONE);
	};

	const int32 FirstFallbackIndex = SelectFirstSeparatedCandidate();
	if (TestTrue(TEXT("First occupied goal selects a separated fallback"), Snapshot.Cells.IsValidIndex(FirstFallbackIndex)))
	{
		TestTrue(
			TEXT("A 42 cm agent skips immediately adjacent 50 cm cells"),
			FVector::Dist2D(Snapshot.Cells[DesiredIndex].WorldCenter, Snapshot.Cells[FirstFallbackIndex].WorldCenter) >= 89.0);
		RejectedCells.Add(Snapshot.Cells[FirstFallbackIndex].Id);
		Snapshot.Cells[FirstFallbackIndex].OccupancyOwners.Add(FGuid::NewGuid());
	}

	const int32 SecondFallbackIndex = SelectFirstSeparatedCandidate();
	TestTrue(TEXT("A newly occupied fallback selects another destination"), Snapshot.Cells.IsValidIndex(SecondFallbackIndex));
	TestNotEqual(TEXT("Repeated redirect never reuses the rejected fallback"), SecondFallbackIndex, FirstFallbackIndex);
	TestNotEqual(TEXT("Redirect does not return to the original contested goal"), SecondFallbackIndex, DesiredIndex);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridDynamicAgentFallbackTest, "GridWorld.Navigation.DynamicAgentFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridDynamicAgentFallbackTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldDynamicAgentFallbackTest")));
	if (!TestNotNull(TEXT("Transient dynamic-agent world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	if (!TestNotNull(TEXT("Dynamic-agent navigation data"), NavData))
	{
		World->DestroyWorld(false);
		return false;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = FGuid::NewGuid();
	Snapshot->Revisions.Topology = 1;
	Snapshot->Revisions.Occupancy = 1;
	FGridRegionData& Region = Snapshot->Regions.Add(Snapshot->GridId);
	Region.GridId = Snapshot->GridId;
	Region.MovementMode = EGridMovementMode::FourDirections;
	Region.GridTransform.CellSize = FVector(50.0, 50.0, 200.0);
	for (int32 X = 0; X < 3; ++X)
	{
		FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
		Cell.Id.GridId = Snapshot->GridId;
		Cell.Id.Coord = FGridCellCoord(X, 0, 0);
		Cell.WorldCenter = Region.GridTransform.CellToWorld(Cell.Id.Coord);
		if (X > 0)
		{
			Cell.Neighbors.Add(X - 1);
			Snapshot->Cells[X - 1].Neighbors.Add(X);
		}
	}
	Snapshot->Cells[1].OccupancyOwners.Add(FGuid::NewGuid());
	FString PublishError;
	if (!TestTrue(TEXT("Occupied one-cell corridor publishes"), NavData->PublishSnapshot(Snapshot, &PublishError)))
	{
		World->DestroyWorld(false);
		return false;
	}

	FSharedNavQueryFilter Filter = NavData->GetDefaultQueryFilter()->GetCopy();
	FGridNavigationQueryFilterImpl* GridFilter = static_cast<FGridNavigationQueryFilterImpl*>(Filter->GetImplementation());
	GridFilter->SetMovementMode(EGridMovementMode::FourDirections);
	GridFilter->SetDynamicAgentPolicy(EGridDynamicAgentPolicy::YieldThenRepath);
	GridFilter->SetMinimumAgentLookAheadCells(3);
	GridFilter->SetAdditionalAgentSeparation(5.0f);
	GridFilter->SetStationaryAgentSpeedThreshold(5.0f);
	GridFilter->SetDynamicAgentRepathDelay(0.35f);
	FPathFindingQuery Query(
		NavData,
		*NavData,
		Snapshot->Cells[0].WorldCenter,
		Snapshot->Cells[2].WorldCenter,
		Filter);
	const FPathFindingResult Result = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	const FGridNavigationPath* GridPath = Result.Path.IsValid() ? Result.Path->CastPath<FGridNavigationPath>() : nullptr;
	if (TestNotNull(TEXT("A corridor without a detour remains a valid waiting path"), GridPath))
	{
		TestTrue(TEXT("The path records its dynamic-agent fallback"), GridPath->bUsedDynamicAgentFallback);
		TestEqual(TEXT("The requested Yield Then Repath policy survives fallback"), GridPath->DynamicAgentPolicy, EGridDynamicAgentPolicy::YieldThenRepath);
		TestEqual(TEXT("The fallback retains the complete corridor"), GridPath->CellPath.Num(), 3);
		TestEqual(TEXT("Look-ahead settings are copied to the path"), GridPath->MinimumAgentLookAheadCells, 3);
		TestEqual(TEXT("Repath delay is copied to the path"), GridPath->DynamicAgentRepathDelay, 0.35f);
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPathOptimizationIntegrationTest, "GridWorld.Navigation.PathOptimizationFilter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridPathOptimizationIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldPathOptimizationTest")));
	if (!TestNotNull(TEXT("Transient path optimization world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	if (!TestNotNull(TEXT("Path optimization navigation data"), NavData))
	{
		World->DestroyWorld(false);
		return false;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = FGuid::NewGuid();
	Snapshot->Revisions.Topology = 1;
	FGridRegionData& Region = Snapshot->Regions.Add(Snapshot->GridId);
	Region.GridId = Snapshot->GridId;
	Region.MovementMode = EGridMovementMode::FourDirections;
	Region.GridTransform.CellSize = FVector(50.0, 50.0, 200.0);
	const FGridCellCoord Coords[] = {
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(1, 1, 0),
		FGridCellCoord(2, 1, 0),
		FGridCellCoord(2, 2, 0),
		FGridCellCoord(3, 2, 0),
		FGridCellCoord(4, 2, 0),
		FGridCellCoord(0, -1, 0),
		FGridCellCoord(0, -2, 0),
		FGridCellCoord(1, -2, 0),
		FGridCellCoord(2, -2, 0),
		FGridCellCoord(3, -2, 0),
		FGridCellCoord(4, -2, 0),
		FGridCellCoord(4, -1, 0),
		FGridCellCoord(4, 0, 0),
		FGridCellCoord(4, 1, 0)};
	for (const FGridCellCoord& Coord : Coords)
	{
		FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
		Cell.Id.GridId = Snapshot->GridId;
		Cell.Id.Coord = Coord;
	}
	const int32 ShortRoute[] = {0, 1, 2, 3, 4, 5, 6};
	const int32 FewTurnRoute[] = {0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 6};
	auto AddRoute = [&Snapshot](TConstArrayView<int32> Route)
	{
		for (int32 RouteIndex = 1; RouteIndex < Route.Num(); ++RouteIndex)
		{
			Snapshot->Cells[Route[RouteIndex - 1]].Neighbors.Add(Route[RouteIndex]);
		}
	};
	AddRoute(ShortRoute);
	AddRoute(FewTurnRoute);
	FString PublishError;
	if (!TestTrue(TEXT("Path optimization topology publishes"), NavData->PublishSnapshot(Snapshot, &PublishError)))
	{
		World->DestroyWorld(false);
		return false;
	}

	auto FindWithMode = [NavData](EGridPathOptimizationMode Mode, float TurnPenalty)
	{
		FSharedNavQueryFilter Filter = NavData->GetDefaultQueryFilter()->GetCopy();
		FGridNavigationQueryFilterImpl* GridFilter = static_cast<FGridNavigationQueryFilterImpl*>(Filter->GetImplementation());
		GridFilter->SetMovementMode(EGridMovementMode::FourDirections);
		GridFilter->SetPathOptimizationMode(Mode);
		GridFilter->SetBalancedTurnPenalty(TurnPenalty);
		FPathFindingQuery Query(
			NavData,
			*NavData,
			FVector(25.0, 25.0, 0.0),
			FVector(225.0, 125.0, 0.0),
			Filter);
		return AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	};

	const FPathFindingResult FewestResult = FindWithMode(EGridPathOptimizationMode::FewestTurns, 2.0f);
	const FGridNavigationPath* FewestPath = FewestResult.Path.IsValid() ? FewestResult.Path->CastPath<FGridNavigationPath>() : nullptr;
	if (TestNotNull(TEXT("Fewest Turns filter returns a Grid path"), FewestPath))
	{
		TestEqual(TEXT("Grid path retains Fewest Turns mode"), FewestPath->OptimizationMode, EGridPathOptimizationMode::FewestTurns);
		TestEqual(TEXT("Grid path retains the selected turn count"), FewestPath->TurnCount, 2);
		TestEqual(TEXT("Fewest Turns filter selects the long corridor"), FewestPath->CellPath.Num(), 11);
		TestEqual(TEXT("Reported path cost excludes optimization penalty"), FewestPath->GetCost(), FVector::FReal(10.0));
	}

	const FPathFindingResult BalancedResult = FindWithMode(EGridPathOptimizationMode::Balanced, 1.5f);
	const FGridNavigationPath* BalancedPath = BalancedResult.Path.IsValid() ? BalancedResult.Path->CastPath<FGridNavigationPath>() : nullptr;
	if (TestNotNull(TEXT("Balanced filter returns a Grid path"), BalancedPath))
	{
		TestEqual(TEXT("Grid path retains Balanced mode"), BalancedPath->OptimizationMode, EGridPathOptimizationMode::Balanced);
		TestEqual(TEXT("Balanced filter selects the short corridor"), BalancedPath->TurnCount, 4);
		TestEqual(TEXT("Balanced short corridor keeps real cost"), BalancedPath->GetCost(), FVector::FReal(6.0));
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPathSearchBudgetIntegrationTest, "GridWorld.Navigation.PathSearchBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridPathSearchBudgetIntegrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldPathSearchBudgetTest")));
	if (!TestNotNull(TEXT("Transient search-budget world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	AAIController* Controller = World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("Search-budget navigation data"), NavData)
		|| !TestNotNull(TEXT("Search-budget controller"), Controller))
	{
		World->DestroyWorld(false);
		return false;
	}

	constexpr int32 SideLength = 40;
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = FGuid::NewGuid();
	Snapshot->Revisions.Topology = 1;
	FGridRegionData& Region = Snapshot->Regions.Add(Snapshot->GridId);
	Region.GridId = Snapshot->GridId;
	Region.MovementMode = EGridMovementMode::FourDirections;
	Snapshot->Cells.Reserve(SideLength * SideLength);
	for (int32 Y = 0; Y < SideLength; ++Y)
	{
		for (int32 X = 0; X < SideLength; ++X)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = Snapshot->GridId;
			Cell.Id.Coord = FGridCellCoord(X, Y, 0);
			Cell.WorldCenter = FVector((X + 0.5) * 50.0, (Y + 0.5) * 50.0, 0.0);
		}
	}
	for (int32 Y = 0; Y < SideLength; ++Y)
	{
		for (int32 X = 0; X < SideLength; ++X)
		{
			FGridCellData& Cell = Snapshot->Cells[Y * SideLength + X];
			if (X + 1 < SideLength)
			{
				Cell.Neighbors.Add(Y * SideLength + X + 1);
			}
			if (Y + 1 < SideLength)
			{
				Cell.Neighbors.Add((Y + 1) * SideLength + X);
			}
			if (X > 0)
			{
				Cell.Neighbors.Add(Y * SideLength + X - 1);
			}
			if (Y > 0)
			{
				Cell.Neighbors.Add((Y - 1) * SideLength + X);
			}
		}
	}
	FString PublishError;
	if (!TestTrue(TEXT("Search-budget topology publishes"), NavData->PublishSnapshot(Snapshot, &PublishError)))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FVector Start = Snapshot->Cells[0].WorldCenter;
	const FVector Goal = Snapshot->Cells.Last().WorldCenter;
	UGridNavigationQueryFilter* FullBudgetFilterObject = NewObject<UGridNavigationQueryFilter>(GetTransientPackage());
	FullBudgetFilterObject->MovementMode = EGridMovementMode::FourDirections;
	FullBudgetFilterObject->PathOptimizationMode = EGridPathOptimizationMode::FewestTurns;
	const FSharedConstNavQueryFilter FullBudgetFilter = FullBudgetFilterObject->GetQueryFilter(*NavData, Controller);
	TestEqual(TEXT("Grid filter propagates its 65536-state default"), FullBudgetFilter->GetMaxSearchNodes(), uint32(65536));
	FPathFindingQuery FullBudgetQuery(Controller, *NavData, Start, Goal, FullBudgetFilter);
	const FPathFindingResult FullBudgetResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), FullBudgetQuery);
	const FGridNavigationPath* FullBudgetPath = FullBudgetResult.Path.IsValid()
		? FullBudgetResult.Path->CastPath<FGridNavigationPath>()
		: nullptr;
	if (TestNotNull(TEXT("Full directional budget returns a Grid path"), FullBudgetPath))
	{
		TestFalse(TEXT("Full directional budget reaches the goal without a partial path"), FullBudgetPath->IsPartial());
		TestFalse(TEXT("Full directional budget does not hit its limit"), FullBudgetPath->DidSearchReachedLimit());
		TestTrue(TEXT("Regression topology requires more than the legacy 2048 states"), FullBudgetPath->VisitedNodes > 2048);
		TestEqual(TEXT("Fewest Turns reaches the opposite corner with one turn"), FullBudgetPath->TurnCount, 1);
	}

	UGridNavigationQueryFilter* LimitedFilterObject = NewObject<UGridNavigationQueryFilter>(GetTransientPackage());
	LimitedFilterObject->MovementMode = EGridMovementMode::FourDirections;
	LimitedFilterObject->PathOptimizationMode = EGridPathOptimizationMode::FewestTurns;
	LimitedFilterObject->MaxSearchStates = 2048;
	const FSharedConstNavQueryFilter LimitedFilter = LimitedFilterObject->GetQueryFilter(*NavData, Controller);
	TestEqual(TEXT("Custom low search-state budget is propagated"), LimitedFilter->GetMaxSearchNodes(), uint32(2048));
	AddExpectedError(TEXT("after reaching the Max Search States limit"), EAutomationExpectedErrorFlags::Contains, 1);
	FPathFindingQuery LimitedQuery(Controller, *NavData, Start, Goal, LimitedFilter);
	const FPathFindingResult LimitedResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), LimitedQuery);
	const FGridNavigationPath* LimitedPath = LimitedResult.Path.IsValid()
		? LimitedResult.Path->CastPath<FGridNavigationPath>()
		: nullptr;
	if (TestNotNull(TEXT("Limited directional budget returns partial progress"), LimitedPath))
	{
		TestTrue(TEXT("Limited directional budget marks the path partial"), LimitedPath->IsPartial());
		TestTrue(TEXT("Limited directional budget records the reached limit"), LimitedPath->DidSearchReachedLimit());
		TestEqual(TEXT("Limited directional search visits exactly its budget"), LimitedPath->VisitedNodes, 2048);
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridPrecisePathMetadataTest, "GridWorld.Navigation.PrecisePathMetadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridPrecisePathMetadataTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldPrecisePathTest")));
	if (!TestNotNull(TEXT("Transient precise-path world"), World))
	{
		return false;
	}

	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	AGridWorldAIController* GridController = World->SpawnActor<AGridWorldAIController>();
	if (!TestNotNull(TEXT("Grid navigation data"), NavData)
		|| !TestNotNull(TEXT("GridWorld controller"), GridController))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(
		TEXT("GridWorld controller installs precise path following"),
		GridController->GetPathFollowingComponent()->IsA<UGridWorldPathFollowingComponent>());

	auto MakeTurnSnapshot = [](
		EGridPathFollowingStyle Style,
		EGridPathDriveMode DriveMode,
		bool bUseAcceleratedFinalApproach,
		int64 TopologyRevision)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = FGuid::NewGuid();
		Snapshot->Revisions.Topology = TopologyRevision;
		FGridRegionData& Region = Snapshot->Regions.Add(Snapshot->GridId);
		Region.GridId = Snapshot->GridId;
		Region.GridTransform.CellSize = FVector(50.0, 50.0, 200.0);
		Region.PathFollowingStyle = Style;
		Region.PathDriveMode = DriveMode;
		Region.bUseAcceleratedFinalApproach = bUseAcceleratedFinalApproach;
		Region.CellCenterTolerance = 2.0f;
		Region.StopSpeedTolerance = 5.0f;

		const FGridCellCoord Coordinates[] = {
			FGridCellCoord(0, 0, 0),
			FGridCellCoord(1, 0, 0),
			FGridCellCoord(2, 0, 0),
			FGridCellCoord(2, 1, 0)};
		for (const FGridCellCoord& Coordinate : Coordinates)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = Snapshot->GridId;
			Cell.Id.Coord = Coordinate;
			Cell.WorldCenter = Region.GridTransform.CellToWorld(Coordinate);
		}
		for (int32 CellIndex = 1; CellIndex < Snapshot->Cells.Num(); ++CellIndex)
		{
			Snapshot->Cells[CellIndex - 1].Neighbors.Add(CellIndex);
			Snapshot->Cells[CellIndex].Neighbors.Add(CellIndex - 1);
		}
		return Snapshot;
	};

	auto FindTurnPath = [NavData, GridController](const FVector& StartLocation)
	{
		FPathFindingQuery Query(
			GridController,
			*NavData,
			StartLocation,
			FVector(125.0, 75.0, 0.0),
			NavData->GetDefaultQueryFilter());
		return AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	};

	FString PublishError;
	TestTrue(TEXT("Center-constrained topology publishes"), NavData->PublishSnapshot(MakeTurnSnapshot(
		EGridPathFollowingStyle::CenterConstrained,
		EGridPathDriveMode::Accelerated,
		false,
		1), &PublishError));
	const FVector ActualStart(10.0, 15.0, 0.0);
	const FPathFindingResult CenterConstrainedResult = FindTurnPath(ActualStart);
	const FGridNavigationPath* CenterConstrainedPath = CenterConstrainedResult.Path.IsValid()
		? CenterConstrainedResult.Path->CastPath<FGridNavigationPath>()
		: nullptr;
	if (TestNotNull(TEXT("Center-constrained path is generated"), CenterConstrainedPath))
	{
		TestEqual(TEXT("Precise path includes actual start plus every cell center"), CenterConstrainedPath->GetPathPoints().Num(), 5);
		TestEqual(TEXT("Precise path keeps the actual start"), CenterConstrainedPath->GetPathPoints()[0].Location, ActualStart);
		TestFalse(TEXT("Initial centering remains continuous"), CenterConstrainedPath->GetFollowingData(1)->bRequiresStop);
		TestFalse(TEXT("Collinear middle center remains continuous"), CenterConstrainedPath->GetFollowingData(2)->bRequiresStop);
		TestFalse(TEXT("Turn center remains continuous"), CenterConstrainedPath->GetFollowingData(3)->bRequiresStop);
		TestTrue(TEXT("Final center is a stop"), CenterConstrainedPath->GetFollowingData(4)->bRequiresStop);
		TestTrue(TEXT("Initial center is a one-way gate"), CenterConstrainedPath->GetFollowingData(1)->bRequiresCenterGate);
		TestFalse(TEXT("Center-constrained skips a collinear gate"), CenterConstrainedPath->GetFollowingData(2)->bRequiresCenterGate);
		TestTrue(TEXT("Turn center is a one-way gate"), CenterConstrainedPath->GetFollowingData(3)->bRequiresCenterGate);
		TestFalse(TEXT("Final stop is not an intermediate gate"), CenterConstrainedPath->GetFollowingData(4)->bRequiresCenterGate);
		TestEqual(TEXT("Center-constrained first target is initial center"), CenterConstrainedPath->GetNextFollowingTargetIndex(0), 1);
		TestEqual(TEXT("Center-constrained skips the collinear center"), CenterConstrainedPath->GetNextFollowingTargetIndex(1), 3);
		const FGridPathPointFollowingData* FinalFollowingData = CenterConstrainedPath->GetFollowingData(4);
		TestTrue(TEXT("Accelerated drive metadata reaches the path"), FinalFollowingData != nullptr && FinalFollowingData->DriveMode == EGridPathDriveMode::Accelerated);
		TArray<FGridCenterGateDebugData> CenterConstrainedGates;
		CenterConstrainedPath->GetCenterGateDebugData(CenterConstrainedGates);
		TestEqual(TEXT("Center-constrained debug exposes initial and turn gate planes"), CenterConstrainedGates.Num(), 2);
		if (!CenterConstrainedGates.IsEmpty())
		{
			TestEqual(TEXT("Default 50 cm cell gate has a 12.5 cm half width"), CenterConstrainedGates[0].HalfWidth, 12.5f);
		}
		TArray<TArray<FVector>> DebugPaths;
		TArray<FVector> DebugReachability;
		TArray<FVector> DebugStopPoints;
		TArray<FGridCenterGateDebugData> DebugGates;
		NavData->GetDebugQueryData(DebugPaths, DebugReachability, DebugStopPoints, DebugGates);
		TestEqual(TEXT("Active path debug publishes center-constrained gate planes"), DebugGates.Num(), 2);
	}

	TestTrue(TEXT("Cell-by-cell topology publishes"), NavData->PublishSnapshot(MakeTurnSnapshot(
		EGridPathFollowingStyle::CellByCell,
		EGridPathDriveMode::DirectVelocity,
		true,
		2), &PublishError));
	const FPathFindingResult CellByCellResult = FindTurnPath(ActualStart);
	const FGridNavigationPath* CellByCellPath = CellByCellResult.Path.IsValid()
		? CellByCellResult.Path->CastPath<FGridNavigationPath>()
		: nullptr;
	if (TestNotNull(TEXT("Cell-by-cell path is generated"), CellByCellPath))
	{
		for (int32 PointIndex = 1; PointIndex < CellByCellPath->GetPathPoints().Num() - 1; ++PointIndex)
		{
			const FGridPathPointFollowingData* FollowingData = CellByCellPath->GetFollowingData(PointIndex);
			TestTrue(*FString::Printf(TEXT("Cell-by-cell point %d has following data"), PointIndex), FollowingData != nullptr);
			if (FollowingData != nullptr)
			{
				TestFalse(*FString::Printf(TEXT("Cell-by-cell point %d remains continuous"), PointIndex), FollowingData->bRequiresStop);
				TestTrue(*FString::Printf(TEXT("Cell-by-cell point %d is a one-way gate"), PointIndex), FollowingData->bRequiresCenterGate);
			}
		}
		const FGridPathPointFollowingData* FinalFollowingData = CellByCellPath->GetFollowingData(CellByCellPath->GetPathPoints().Num() - 1);
		TestTrue(TEXT("Cell-by-cell final center requires a stop"), FinalFollowingData != nullptr && FinalFollowingData->bRequiresStop);
		TestTrue(TEXT("Direct Velocity metadata reaches the path"), FinalFollowingData != nullptr && FinalFollowingData->DriveMode == EGridPathDriveMode::DirectVelocity);
		TestTrue(TEXT("Accelerated final metadata reaches the path"), FinalFollowingData != nullptr && FinalFollowingData->bUseAcceleratedFinalApproach);
		FGridPathDriveDebugData PathDriveDebugData;
		TestTrue(TEXT("Precise path exposes drive debug data"), CellByCellPath->GetDriveDebugData(PathDriveDebugData));
		TestEqual(TEXT("Drive debug identifies Direct Velocity"), PathDriveDebugData.DriveMode, EGridPathDriveMode::DirectVelocity);
		TestTrue(TEXT("Drive debug identifies accelerated final approach"), PathDriveDebugData.bUseAcceleratedFinalApproach);
		TArray<FGridCenterGateDebugData> CellByCellGates;
		CellByCellPath->GetCenterGateDebugData(CellByCellGates);
		TestEqual(TEXT("Cell-by-cell debug exposes every intermediate gate plane"), CellByCellGates.Num(), 3);
		TArray<TArray<FVector>> DebugPaths;
		TArray<FVector> DebugReachability;
		TArray<FVector> DebugStopPoints;
		TArray<FGridCenterGateDebugData> DebugGates;
		TArray<FGridPathDriveDebugData> DebugDriveData;
		NavData->GetDebugQueryData(DebugPaths, DebugReachability, DebugStopPoints, DebugGates, DebugDriveData);
		TestEqual(TEXT("Runtime debug replaces the previous gate plan atomically"), DebugGates.Num(), 3);
		TestEqual(TEXT("Runtime debug exposes one drive label per active path"), DebugDriveData.Num(), 1);
		if (!DebugDriveData.IsEmpty())
		{
			TestEqual(TEXT("Runtime debug label uses Direct Velocity"), DebugDriveData[0].DriveMode, EGridPathDriveMode::DirectVelocity);
		}
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridCenterGateTest, "GridWorld.Navigation.CenterGateTraversal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridCenterGateTest::RunTest(const FString& Parameters)
{
	using UE::GridWorld::Private::EGridCenterGateTraversalResult;
	using UE::GridWorld::Private::FGridCenterGate;

	FGridCenterGate Gate;
	Gate.Forward = FVector2D(1.0, 0.0);
	Gate.HalfWidth = 12.5f;
	Gate.CenterTolerance = 2.0f;
	Gate.HeightTolerance = 20.0f;

	TestTrue(
		TEXT("Approaching from behind remains pending"),
		Gate.Evaluate(FVector(-10.0, 0.0, 0.0), FVector(-5.0, 0.0, 0.0), true) == EGridCenterGateTraversalResult::Pending);
	TestTrue(
		TEXT("Forward crossing inside the gate passes"),
		Gate.Evaluate(FVector(-5.0, 10.0, 0.0), FVector(5.0, 10.0, 0.0), true) == EGridCenterGateTraversalResult::Passed);
	TestTrue(
		TEXT("Forward crossing outside the gate misses"),
		Gate.Evaluate(FVector(-5.0, 20.0, 0.0), FVector(5.0, 20.0, 0.0), true) == EGridCenterGateTraversalResult::Missed);
	TestTrue(
		TEXT("An already-expired gate never asks the Pawn to reverse"),
		Gate.Evaluate(FVector(5.0, 20.0, 0.0), FVector(4.0, 20.0, 0.0), true) == EGridCenterGateTraversalResult::Missed);
	TestTrue(
		TEXT("Entering the strict center tolerance passes immediately"),
		Gate.Evaluate(FVector(-5.0, 0.0, 0.0), FVector(-1.0, 1.0, 0.0), true) == EGridCenterGateTraversalResult::Passed);

	Gate.Forward = FVector2D(1.0, 1.0).GetSafeNormal();
	TestTrue(
		TEXT("Diagonal gate crossing uses the same one-way rule"),
		Gate.Evaluate(FVector(-5.0, -5.0, 0.0), FVector(5.0, 5.0, 0.0), true) == EGridCenterGateTraversalResult::Passed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridDirectVelocityTest, "GridWorld.Navigation.DirectVelocity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridDirectVelocityTest::RunTest(const FString& Parameters)
{
	using UE::GridWorld::Private::CalculateDirectMoveVelocity;

	const FVector IntermediateVelocity = CalculateDirectMoveVelocity(
		FVector::ZeroVector,
		FVector(50.0, 0.0, 0.0),
		1.0f / 60.0f,
		600.0f,
		false);
	TestEqual(TEXT("Intermediate segment requests constant max speed"), IntermediateVelocity, FVector(600.0, 0.0, 0.0));

	const FVector TurnVelocity = CalculateDirectMoveVelocity(
		FVector(50.0, 0.0, 0.0),
		FVector(50.0, 50.0, 0.0),
		1.0f / 60.0f,
		600.0f,
		false);
	TestEqual(TEXT("A 90-degree turn changes velocity direction immediately"), TurnVelocity, FVector(0.0, 600.0, 0.0));

	const float DeltaTime = 1.0f / 60.0f;
	const FVector FinalVelocity = CalculateDirectMoveVelocity(
		FVector(0.0, 0.0, 0.0),
		FVector(5.0, 0.0, 0.0),
		DeltaTime,
		600.0f,
		true);
	TestEqual(TEXT("Final segment clamps speed to the remaining distance"), FinalVelocity, FVector(300.0, 0.0, 0.0));
	TestEqual(TEXT("Final velocity reaches the center without overshoot"), FinalVelocity * DeltaTime, FVector(5.0, 0.0, 0.0));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldDirectDriveLifecycleTest")));
	if (!TestNotNull(TEXT("Transient direct-drive world"), World))
	{
		return false;
	}
	AGridNavigationData* NavData = World->SpawnActor<AGridNavigationData>();
	AGridWorldAIController* GridController = World->SpawnActor<AGridWorldAIController>();
	ACharacter* Character = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Direct-drive navigation data"), NavData)
		|| !TestNotNull(TEXT("Direct-drive controller"), GridController)
		|| !TestNotNull(TEXT("Direct-drive character"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	GridController->Possess(Character);
	UGridNavigationOccupancyComponent* AutomaticOccupancy = UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*Character);
	if (TestNotNull(TEXT("GridWorld controller registers Pawn occupancy on possession"), AutomaticOccupancy))
	{
		TestFalse(TEXT("Automatic Pawn occupancy does not block ordinary occupancy queries"), AutomaticOccupancy->bBlocksWhenConsidered);
		TestEqual(TEXT("Automatic Pawn occupancy adds no traversal cost"), AutomaticOccupancy->AdditionalCost, 0);
	}
	UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
	UPathFollowingComponent* PathFollowing = GridController->GetPathFollowingComponent();
	if (!TestNotNull(TEXT("Character movement component"), CharacterMovement)
		|| !TestNotNull(TEXT("Grid path following component"), PathFollowing))
	{
		World->DestroyWorld(false);
		return false;
	}

	FNavMovementProperties* MovementProperties = CharacterMovement->GetNavMovementProperties();
	if (!TestNotNull(TEXT("Character nav movement properties"), MovementProperties))
	{
		World->DestroyWorld(false);
		return false;
	}
	MovementProperties->bUseAccelerationForPaths = true;
	CharacterMovement->bRequestedMoveUseAcceleration = true;

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = FGuid::NewGuid();
	Snapshot->Revisions.Topology = 1;
	FGridRegionData& Region = Snapshot->Regions.Add(Snapshot->GridId);
	Region.GridId = Snapshot->GridId;
	Region.PathFollowingStyle = EGridPathFollowingStyle::CellByCell;
	Region.PathDriveMode = EGridPathDriveMode::DirectVelocity;
	for (int32 X = 0; X < 2; ++X)
	{
		FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
		Cell.Id.GridId = Snapshot->GridId;
		Cell.Id.Coord = FGridCellCoord(X, 0, 0);
		Cell.WorldCenter = Region.GridTransform.CellToWorld(Cell.Id.Coord);
	}
	Snapshot->Cells[0].Neighbors.Add(1);
	Snapshot->Cells[1].Neighbors.Add(0);
	FString PublishError;
	TestTrue(TEXT("Direct-drive topology publishes"), NavData->PublishSnapshot(Snapshot, &PublishError));

	FPathFindingQuery Query(
		GridController,
		*NavData,
		FVector::ZeroVector,
		FVector(75.0, 25.0, 0.0),
		NavData->GetDefaultQueryFilter());
	FPathFindingResult PathResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	if (TestTrue(TEXT("Direct-drive path succeeds"), PathResult.IsSuccessful()))
	{
		const FAIRequestID RequestId = PathFollowing->RequestMove(FAIMoveRequest(FVector(75.0, 25.0, 0.0)), PathResult.Path);
		TestTrue(TEXT("Direct-drive request starts"), RequestId.IsValid());
		TestFalse(TEXT("Direct drive disables path acceleration while active"), MovementProperties->bUseAccelerationForPaths);
		TestFalse(TEXT("Direct drive disables Character requested-move acceleration while active"), CharacterMovement->bRequestedMoveUseAcceleration);
		PathFollowing->AbortMove(*GridController, FPathFollowingResultFlags::OwnerFinished, RequestId);
		TestTrue(TEXT("Path acceleration is restored after abort"), MovementProperties->bUseAccelerationForPaths);
		TestTrue(TEXT("Character requested-move acceleration is restored after abort"), CharacterMovement->bRequestedMoveUseAcceleration);
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> StandardSnapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>(*Snapshot);
	StandardSnapshot->Revisions.Topology = 2;
	StandardSnapshot->Regions[StandardSnapshot->GridId].PathFollowingStyle = EGridPathFollowingStyle::Standard;
	TestTrue(TEXT("Standard-drive topology publishes"), NavData->PublishSnapshot(StandardSnapshot, &PublishError));
	MovementProperties->bUseAccelerationForPaths = false;
	CharacterMovement->bRequestedMoveUseAcceleration = true;
	PathResult = AGridNavigationData::FindPath(NavData->GetNavAgentProperties(), Query);
	if (TestTrue(TEXT("Standard path succeeds"), PathResult.IsSuccessful()))
	{
		const FAIRequestID RequestId = PathFollowing->RequestMove(FAIMoveRequest(FVector(75.0, 25.0, 0.0)), PathResult.Path);
		TestTrue(TEXT("Standard request starts"), RequestId.IsValid());
		TestFalse(TEXT("Standard path preserves disabled path acceleration"), MovementProperties->bUseAccelerationForPaths);
		TestTrue(TEXT("Standard path preserves Character requested-move acceleration"), CharacterMovement->bRequestedMoveUseAcceleration);
		PathFollowing->AbortMove(*GridController, FPathFollowingResultFlags::OwnerFinished, RequestId);
	}

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridSerializationMigrationTest, "GridWorld.Navigation.SerializationV5", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridSerializationMigrationTest::RunTest(const FString& Parameters)
{
	FGridWorldSnapshot SourceSnapshot;
	SourceSnapshot.GridId = FGuid::NewGuid();
	SourceSnapshot.Revisions.Topology = 7;
	FGridRegionData& SourceRegion = SourceSnapshot.Regions.Add(SourceSnapshot.GridId);
	SourceRegion.GridId = SourceSnapshot.GridId;
	SourceRegion.PathFollowingStyle = EGridPathFollowingStyle::CellByCell;
	SourceRegion.PathDriveMode = EGridPathDriveMode::DirectVelocity;
	SourceRegion.bUseAcceleratedFinalApproach = true;
	FGridCellData& SourceCell = SourceSnapshot.Cells.AddDefaulted_GetRef();
	SourceCell.Id.GridId = SourceSnapshot.GridId;
	SourceCell.Id.Coord = FGridCellCoord(2, -1, 3);
	SourceCell.WorldCenter = FVector(125.0, -25.0, 75.0);
	SourceCell.FloorNormal = FVector3f(FVector(-0.5, 0.0, FMath::Sqrt(0.75)).GetSafeNormal());
	SourceCell.bHasAuthoredWorldCenter = true;

	TArray<uint8> Version5Bytes;
	{
		FMemoryWriter Writer(Version5Bytes);
		UE::GridWorld::Serialization::SerializeSnapshot(Writer, SourceSnapshot, 5);
	}
	FGridWorldSnapshot ReloadedSnapshot;
	{
		FMemoryReader Reader(Version5Bytes);
		UE::GridWorld::Serialization::SerializeSnapshot(Reader, ReloadedSnapshot, 5);
	}
	const FGridRegionData* ReloadedRegion = ReloadedSnapshot.FindRegion(SourceSnapshot.GridId);
	if (TestNotNull(TEXT("Version 5 region loads"), ReloadedRegion))
	{
		TestEqual(TEXT("Version 5 preserves precise path style"), ReloadedRegion->PathFollowingStyle, EGridPathFollowingStyle::CellByCell);
		TestEqual(TEXT("Version 5 preserves Direct Velocity"), ReloadedRegion->PathDriveMode, EGridPathDriveMode::DirectVelocity);
		TestTrue(TEXT("Version 5 preserves accelerated final approach"), ReloadedRegion->bUseAcceleratedFinalApproach);
	}
	if (TestEqual(TEXT("Version 5 preserves cells"), ReloadedSnapshot.Cells.Num(), 1))
	{
		TestTrue(
			TEXT("Version 5 preserves floor normal"),
			FVector(ReloadedSnapshot.Cells[0].FloorNormal).Equals(FVector(SourceCell.FloorNormal), UE_KINDA_SMALL_NUMBER));
	}

	for (int32 LegacyVersion = 2; LegacyVersion <= 4; ++LegacyVersion)
	{
		TArray<uint8> LegacyBytes;
		{
			FMemoryWriter Writer(LegacyBytes);
			UE::GridWorld::Serialization::SerializeSnapshot(Writer, SourceSnapshot, LegacyVersion);
		}
		FGridWorldSnapshot ConsumedLegacySnapshot;
		FMemoryReader Reader(LegacyBytes);
		UE::GridWorld::Serialization::SerializeSnapshot(Reader, ConsumedLegacySnapshot, LegacyVersion);
		TestFalse(*FString::Printf(TEXT("Version %d payload is consumed without archive errors"), LegacyVersion), Reader.IsError());
		TestEqual(
			*FString::Printf(TEXT("Version %d payload is consumed completely"), LegacyVersion),
			Reader.Tell(),
			static_cast<int64>(LegacyBytes.Num()));
		TestTrue(*FString::Printf(TEXT("Version %d is recognized for safe consumption"), LegacyVersion), UE::GridWorld::Serialization::CanConsumeVersion(LegacyVersion));
		TestFalse(*FString::Printf(TEXT("Version %d cannot be published without rebuild"), LegacyVersion), UE::GridWorld::Serialization::CanPublishVersion(LegacyVersion));
	}
	TestTrue(TEXT("Version 5 may be published"), UE::GridWorld::Serialization::CanPublishVersion(5));
	TestFalse(TEXT("Unknown future version cannot be consumed"), UE::GridWorld::Serialization::CanConsumeVersion(6));
	return true;
}

#endif
