// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/GridNavigationLinkComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Navigation/GridOverlayComposer.h"
#include "Navigation/GridWorldSnapshot.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridOverlayCompositionTest, "GridWorld.Overlay.Composition", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridOverlayCompositionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GridWorldOverlayTest")));
	if (!TestNotNull(TEXT("Transient test world"), World))
	{
		return false;
	}
	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Overlay owner"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Base = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Base->GridId = FGuid::NewGuid();
	Base->Revisions.Topology = 1;
	Base->Revisions.Traversal = 1;
	Base->Revisions.Occupancy = 1;
	for (int32 X = 0; X < 2; ++X)
	{
		FGridCellData& Cell = Base->Cells.AddDefaulted_GetRef();
		Cell.Id.Coord = FGridCellCoord(X, 0, 0);
	}
	Base->Cells[0].Neighbors.Add(1);
	Base->Cells[1].Neighbors.Add(0);
	FString FinalizeError;
	TestTrue(TEXT("Base snapshot finalizes"), Base->Finalize(&FinalizeError));

	UGridNavigationModifierComponent* CostModifier = NewObject<UGridNavigationModifierComponent>(Owner);
	Owner->AddInstanceComponent(CostModifier);
	CostModifier->SetRelativeLocation(Base->Cells[0].WorldCenter);
	CostModifier->BoxExtent = FVector(20.0, 20.0, 20.0);
	CostModifier->bBlockCells = false;
	CostModifier->AdditiveCost = 100;
	CostModifier->CostMultiplier = 2.0;
	CostModifier->RegisterComponent();
	CostModifier->Activate(true);

	UGridNavigationModifierComponent* BlockingModifier = NewObject<UGridNavigationModifierComponent>(Owner);
	Owner->AddInstanceComponent(BlockingModifier);
	BlockingModifier->SetRelativeLocation(Base->Cells[0].WorldCenter);
	BlockingModifier->BoxExtent = FVector(20.0, 20.0, 20.0);
	BlockingModifier->bBlockCells = true;
	BlockingModifier->RegisterComponent();
	BlockingModifier->Activate(true);

	UGridNavigationOccupancyComponent* Occupant = NewObject<UGridNavigationOccupancyComponent>(Owner);
	Owner->AddInstanceComponent(Occupant);
	Occupant->SetRelativeLocation(Base->Cells[1].WorldCenter);
	Occupant->BoxExtent = FVector(20.0, 20.0, 20.0);
	Occupant->bIsReservation = true;
	Occupant->AdditionalCost = 750;
	Occupant->RegisterComponent();
	Occupant->Activate(true);

	UGridNavigationOccupancyComponent* AgentOccupant = NewObject<UGridNavigationOccupancyComponent>(Owner);
	Owner->AddInstanceComponent(AgentOccupant);
	AgentOccupant->SetRelativeLocation(Base->Cells[0].WorldCenter);
	AgentOccupant->BoxExtent = FVector(20.0, 20.0, 20.0);
	AgentOccupant->bBlocksWhenConsidered = false;
	AgentOccupant->AdditionalCost = 0;
	AgentOccupant->RegisterComponent();
	AgentOccupant->Activate(true);

	UGridNavigationLinkComponent* LinkComponent = NewObject<UGridNavigationLinkComponent>(Owner);
	Owner->AddInstanceComponent(LinkComponent);
	LinkComponent->StartOffset = Base->Cells[0].WorldCenter;
	LinkComponent->EndOffset = Base->Cells[1].WorldCenter;
	LinkComponent->TraversalCost = 500;
	LinkComponent->RegisterComponent();
	LinkComponent->Activate(true);

	FGridChangeSet FirstChangeSet;
	const TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> TraversalOverlay = FGridOverlayComposer::Compose(*World, *Base, &Base.Get(), false, FirstChangeSet);
	TestFalse(TEXT("Blocker prevails"), TraversalOverlay->Cells[0].bWalkable);
	TestEqual(TEXT("Additive cost precedes multiplier"), TraversalOverlay->Cells[0].TraversalCost, 2200);
	TestTrue(TEXT("Occupancy composed separately"), TraversalOverlay->Cells[1].bOccupied);
	TestTrue(TEXT("Ordinary occupancy owner is retained"), TraversalOverlay->Cells[1].OccupancyOwners.Contains(Occupant->OccupantId));
	TestEqual(TEXT("Reservation owner recorded"), TraversalOverlay->Cells[1].ReservationOwners.Num(), 1);
	TestEqual(TEXT("Explicit link composed"), TraversalOverlay->Links.Num(), 1);
	TestTrue(TEXT("Traversal changes are classified separately"), FirstChangeSet.ChangedTraversalCells.Contains(Base->Cells[0].Id));
	TestTrue(TEXT("Reservation blocking changes are classified separately"), FirstChangeSet.ChangedBlockingOccupancyCells.Contains(Base->Cells[1].Id));
	TestTrue(TEXT("Occupancy cost changes are classified separately"), FirstChangeSet.ChangedOccupancyCostCells.Contains(Base->Cells[1].Id));
	TestTrue(TEXT("Agent identity changes are classified separately"), FirstChangeSet.ChangedOccupancyOwnerCells.Contains(Base->Cells[0].Id));

	FGridChangeSet OccupancyChangeSet;
	const TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> OccupancyOverlay = FGridOverlayComposer::Compose(*World, *Base, &TraversalOverlay.Get(), true, OccupancyChangeSet);
	TestFalse(TEXT("Occupancy-only update preserves traversal block"), OccupancyOverlay->Cells[0].bWalkable);
	TestEqual(TEXT("Occupancy-only update preserves traversal cost"), OccupancyOverlay->Cells[0].TraversalCost, 2200);
	TestEqual(TEXT("Occupancy revision increments"), OccupancyOverlay->Revisions.Occupancy, TraversalOverlay->Revisions.Occupancy + 1);
	TestEqual(TEXT("Traversal revision remains stable"), OccupancyOverlay->Revisions.Traversal, TraversalOverlay->Revisions.Traversal);

	AgentOccupant->SetWorldLocation(Base->Cells[1].WorldCenter);
	FGridChangeSet AgentMoveChangeSet;
	const TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> AgentMoveOverlay =
		FGridOverlayComposer::Compose(*World, *Base, &OccupancyOverlay.Get(), true, AgentMoveChangeSet);
	TestTrue(TEXT("Moving a non-blocking agent changes the owner overlay"), AgentMoveChangeSet.ChangedOccupancyOwnerCells.Num() > 0);
	TestTrue(TEXT("Moving a non-blocking agent changes the aggregate cells"), AgentMoveChangeSet.ChangedCells.Num() > 0);
	TestTrue(TEXT("Moving a non-blocking agent does not change traversal"), AgentMoveChangeSet.ChangedTraversalCells.IsEmpty());
	TestTrue(TEXT("Moving a non-blocking agent does not change blocking occupancy"), AgentMoveChangeSet.ChangedBlockingOccupancyCells.IsEmpty());
	TestTrue(TEXT("Moving a zero-cost agent does not change occupancy cost"), AgentMoveChangeSet.ChangedOccupancyCostCells.IsEmpty());
	TestEqual(TEXT("Agent move still increments only occupancy"), AgentMoveOverlay->Revisions.Traversal, OccupancyOverlay->Revisions.Traversal);

	World->DestroyWorld(false);
	return true;
}

#endif
