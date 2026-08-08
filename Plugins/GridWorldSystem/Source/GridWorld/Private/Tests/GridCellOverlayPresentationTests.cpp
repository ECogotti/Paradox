// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Navigation/GridNavigationData.h"
#include "Presentation/GridCellOverlayPresentationSubsystem.h"
#include "Presentation/GridCellVisualStyle.h"
#include "Presentation/GridPathPresentationSubsystem.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "UObject/GarbageCollection.h"

namespace UE::GridWorld::CellOverlayPresentationTests
{
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeSnapshot(
		const FGuid& GridId,
		const int64 TopologyRevision,
		TConstArrayView<FGridCellCoord> Coords)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot =
			MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = GridId;
		Snapshot->Revisions.Topology = TopologyRevision;
		Snapshot->Revisions.Traversal = 1;
		Snapshot->Revisions.Occupancy = 1;
		FGridRegionData& Region = Snapshot->Regions.Add(GridId);
		Region.GridId = GridId;
		Region.GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
		for (const FGridCellCoord& Coord : Coords)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = GridId;
			Cell.Id.Coord = Coord;
			Cell.WorldCenter = Region.GridTransform.CellToWorld(Coord);
			Cell.bWalkable = true;
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
	FGridCellOverlayPresentationSessionTest,
	"GridWorld.Presentation.CellOverlay.SessionOwnershipPriorityAndCoexistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridCellOverlayPresentationSessionTest::RunTest(const FString& Parameters)
{
	using namespace UE::GridWorld::CellOverlayPresentationTests;
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(
			GetTransientPackage(),
			UWorld::StaticClass(),
			TEXT("GridCellOverlayPresentationTest")));
	if (!TestNotNull(TEXT("Transient overlay world"), World))
	{
		return false;
	}

	AGridNavigationData* NavigationData = World->SpawnActor<AGridNavigationData>();
	UGridCellOverlayPresentationSubsystem* Overlays =
		World->GetSubsystem<UGridCellOverlayPresentationSubsystem>();
	UGridPathPresentationSubsystem* Paths =
		World->GetSubsystem<UGridPathPresentationSubsystem>();
	UGridRuntimeVisualizationSubsystem* Visualization =
		World->GetSubsystem<UGridRuntimeVisualizationSubsystem>();
	if (!TestNotNull(TEXT("Navigation data"), NavigationData)
		|| !TestNotNull(TEXT("Overlay subsystem"), Overlays)
		|| !TestNotNull(TEXT("Path subsystem"), Paths)
		|| !TestNotNull(TEXT("Visualization subsystem"), Visualization))
	{
		World->DestroyWorld(false);
		return false;
	}

	const FGuid GridId = FGuid::NewGuid();
	const TArray<FGridCellCoord> Coords{
		FGridCellCoord(0, 0, 0),
		FGridCellCoord(1, 0, 0),
		FGridCellCoord(2, 0, 0)};
	FString PublishError;
	TestTrue(
		TEXT("Overlay topology publishes"),
		NavigationData->PublishSnapshot(MakeSnapshot(GridId, 1, Coords), &PublishError));
	const FGridWorldSnapshotPtr Snapshot = NavigationData->GetSnapshot();
	const FGridRevisionSet RevisionsBefore = NavigationData->GetPublishedRevisions();
	const FGridCellId First = Snapshot->Cells[0].Id;
	const FGridCellId Second = Snapshot->Cells[1].Id;

	UObject* PrimaryOwner = NewObject<UGridCellVisualStyle>(World);
	FGridCellOverlayPresentationRequest PrimaryRequest;
	PrimaryRequest.Owner = PrimaryOwner;
	PrimaryRequest.Entries = {
		{First, EGridCellOverlayVisualState::Primary},
		{Second, EGridCellOverlayVisualState::Primary}};
	FGridCellOverlayPresentationHandle PrimaryHandle;
	TestTrue(TEXT("Owner-scoped Primary session creates"),
		Overlays->CreateCellOverlayPresentation(PrimaryRequest, PrimaryHandle));
	TestTrue(TEXT("Primary handle validates"),
		Overlays->IsCellOverlayPresentationValid(PrimaryHandle));

	FGridCellVisualState State;
	TestTrue(TEXT("Primary overlay state is readable"),
		Visualization->GetCellVisualState(Second, State));
	TestEqual(TEXT("Primary overlay resolves"),
		State.OverlayState, EGridCellOverlayVisualState::Primary);

	FGridPathPresentationRequest PathRequest;
	PathRequest.Cells = {First, Second};
	PathRequest.Purpose = EGridPathPresentationPurpose::Preview;
	FGridPathPresentationHandle PathHandle;
	TestTrue(TEXT("Path session creates beside overlay"),
		Paths->CreatePathPresentation(PathRequest, PathHandle));
	Visualization->GetCellVisualState(Second, State);
	TestEqual(TEXT("Overlay survives path contribution"),
		State.OverlayState, EGridCellOverlayVisualState::Primary);
	TestEqual(TEXT("Path survives overlay contribution"),
		State.PathState, EGridCellPathVisualState::Preview);

	UObject* SecondaryOwner = NewObject<UGridCellVisualStyle>(World);
	FGridCellOverlayPresentationRequest SecondaryRequest;
	SecondaryRequest.Owner = SecondaryOwner;
	SecondaryRequest.Entries = {
		{Second, EGridCellOverlayVisualState::Secondary}};
	FGridCellOverlayPresentationHandle SecondaryHandle;
	TestTrue(TEXT("Equal-priority Secondary session creates"),
		Overlays->CreateCellOverlayPresentation(SecondaryRequest, SecondaryHandle));
	Visualization->GetCellVisualState(Second, State);
	TestEqual(TEXT("Secondary wins equal-priority semantic overlap"),
		State.OverlayState, EGridCellOverlayVisualState::Secondary);

	UObject* HighPriorityOwner = NewObject<UGridCellVisualStyle>(World);
	FGridCellOverlayPresentationRequest HighPriorityRequest;
	HighPriorityRequest.Owner = HighPriorityOwner;
	HighPriorityRequest.Priority = 10;
	HighPriorityRequest.Entries = {
		{Second, EGridCellOverlayVisualState::Primary}};
	FGridCellOverlayPresentationHandle HighPriorityHandle;
	TestTrue(TEXT("High-priority Primary session creates"),
		Overlays->CreateCellOverlayPresentation(HighPriorityRequest, HighPriorityHandle));
	Visualization->GetCellVisualState(Second, State);
	TestEqual(TEXT("Priority wins before semantic rank"),
		State.OverlayState, EGridCellOverlayVisualState::Primary);

	TestTrue(TEXT("High-priority session releases"),
		Overlays->ReleaseCellOverlayPresentation(HighPriorityHandle));
	Visualization->GetCellVisualState(Second, State);
	TestEqual(TEXT("Release reveals next owner contribution"),
		State.OverlayState, EGridCellOverlayVisualState::Secondary);
	TestTrue(TEXT("Secondary session clears without release"),
		Overlays->ClearCellOverlayPresentation(SecondaryHandle));
	Visualization->GetCellVisualState(Second, State);
	TestEqual(TEXT("Clear reveals Primary owner contribution"),
		State.OverlayState, EGridCellOverlayVisualState::Primary);

	FGridCellOverlayPresentationSnapshot SessionSnapshot;
	TestTrue(TEXT("Session remains inspectable"),
		Overlays->GetCellOverlayPresentation(PrimaryHandle, SessionSnapshot));
	TestEqual(TEXT("Session preserves both entries"), SessionSnapshot.Entries.Num(), 2);
	TestTrue(TEXT("Presentation does not replace authoritative snapshot"),
		NavigationData->GetSnapshot() == Snapshot);
	TestTrue(TEXT("Presentation does not mutate navigation revisions"),
		SameRevisions(NavigationData->GetPublishedRevisions(), RevisionsBefore));

	Paths->ReleasePathPresentation(PathHandle);
	Overlays->ReleaseCellOverlayPresentation(SecondaryHandle);
	Overlays->ReleaseCellOverlayPresentation(PrimaryHandle);

	UObject* ExpiringOwner = NewObject<UGridCellVisualStyle>(World);
	FGridCellOverlayPresentationRequest ExpiringRequest;
	ExpiringRequest.Owner = ExpiringOwner;
	ExpiringRequest.Entries = {{First, EGridCellOverlayVisualState::Primary}};
	FGridCellOverlayPresentationHandle ExpiringHandle;
	TestTrue(TEXT("Owner-lifetime overlay session creates"),
		Overlays->CreateCellOverlayPresentation(ExpiringRequest, ExpiringHandle));
	ExpiringRequest.Owner = nullptr;
	ExpiringOwner = nullptr;
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Owner collection releases its overlay session"),
		Overlays->IsCellOverlayPresentationValid(ExpiringHandle));
	TestEqual(TEXT("Owner collection leaves no active overlay sessions"),
		Overlays->GetActiveSessionCount(), 0);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGridCellOverlayVisualStyleTest,
	"GridWorld.Presentation.CellOverlay.VisualStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridCellOverlayVisualStyleTest::RunTest(const FString& Parameters)
{
	const UGridCellVisualStyle* Style = GetDefault<UGridCellVisualStyle>();
	FGridCellVisualState State;
	State.PathState = EGridCellPathVisualState::Preview;
	State.OverlayState = EGridCellOverlayVisualState::Primary;
	TestEqual(
		TEXT("Primary overlay color wins while path state remains retained"),
		Style->ResolveColor(State),
		Style->PrimaryOverlayColor);
	State.OverlayState = EGridCellOverlayVisualState::Secondary;
	TestEqual(
		TEXT("Secondary overlay has its independent style color"),
		Style->ResolveColor(State),
		Style->SecondaryOverlayColor);
	State.InteractionState = EGridCellInteractionVisualState::Hovered;
	TestEqual(
		TEXT("Existing direct hover remains the highest presentation layer"),
		Style->ResolveColor(State),
		Style->HoveredColor);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
