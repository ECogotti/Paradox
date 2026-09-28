#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayActionInstance.h"
#include "Characters/ParadoxCharacter.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/SceneComponent.h"
#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayActionTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridTrafficReservation.h"
#include "Paradox.h"
#include "ParadoxSelectionTestTypes.h"
#include "Puzzles/ParadoxDumbwaiter.h"
#include "Puzzles/ParadoxTeleportGateInteractionAction.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "SmartObjectComponent.h"
#include "SmartObjectSubsystem.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "Tests/ParadoxTeleportGateTestTypes.h"

struct FParadoxTeleportGateTestAccessor
{
	static bool HasExitClaim(const AParadoxTeleportGate& Gate)
	{
		return Gate.bHasActiveExitClaim;
	}

	static bool HasCommittedPlacement(const AParadoxTeleportGate& Gate)
	{
		return Gate.bHasCommittedPlacement;
	}

	static FGridCellId GetExitCell(const AParadoxTeleportGate& Gate)
	{
		return Gate.ActiveExitCell;
	}

	static bool ResolveSafeTunnelAnchorPlacement(
		AParadoxTeleportGate& Source,
		AParadoxCharacter& Character,
		const AParadoxTeleportGate& Destination,
		FTransform& OutTransform,
		FString& OutDiagnostic)
	{
		return Source.ResolveSafeTunnelAnchorPlacement(
			Character,
			Destination,
			OutTransform,
			OutDiagnostic);
	}
};

struct FParadoxTeleportGateInteractionActionTestAccessor
{
	static void Tick(UParadoxEnterTeleportGateInteractionAction& Action)
	{
		Action.OnActionTick_Implementation(1.0f / 60.0f);
	}

	static void Timeout(UParadoxEnterTeleportGateInteractionAction& Action)
	{
		Action.HandleTunnelMoveTimedOut();
	}

	static float GetTimeoutSeconds(
		const UParadoxEnterTeleportGateInteractionAction& Action)
	{
		return Action.ActiveTunnelMoveTimeoutSeconds;
	}

	static bool IsTimeoutPaused(
		const UParadoxEnterTeleportGateInteractionAction& Action)
	{
		UWorld* World = Action.GetWorld();
		return World
			&& World->GetTimerManager().IsTimerPaused(Action.TunnelMoveTimeoutHandle);
	}
};

namespace UE::Paradox::TeleportGate::Tests
{
	constexpr int32 SourceCellX = 2;
	constexpr int32 SourceInteractionCellX = 1;
	constexpr int32 DestinationInteractionCellX = 6;
	constexpr int32 DestinationExitCellX = 7;

	struct FScopedWorld
	{
		explicit FScopedWorld(const TCHAR* Name)
		{
			Context = GEngine ? &GEngine->CreateNewWorldContext(EWorldType::Game) : nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
				World->SetShouldTick(true);
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedWorld()
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
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (!It->HasActorBegunPlay())
				{
					It->DispatchBeginPlay();
				}
			}
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	template <typename T>
	T* Spawn(UWorld& World, const FName Name, const FVector Location)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<T>(T::StaticClass(), FTransform(Location), Parameters);
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeGridSnapshot(const FGuid& GridId)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot =
			MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = GridId;
		Snapshot->Revisions.Topology = 1;
		Snapshot->Revisions.Traversal = 1;
		Snapshot->Revisions.Occupancy = 1;
		FGridRegionData& Region = Snapshot->Regions.Add(GridId);
		Region.GridId = GridId;
		Region.GridTransform.Origin = FVector(-50.0, -50.0, -25.0);
		Region.GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
		for (int32 X = 0; X <= 8; ++X)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = GridId;
			Cell.Id.Coord = FGridCellCoord(X, 0, 0);
			Cell.WorldCenter = Region.GridTransform.CellToWorld(Cell.Id.Coord);
			Cell.FloorNormal = FVector3f::UpVector;
			Cell.bWalkable = X != SourceCellX && X != DestinationExitCellX;
			if (X > 0)
			{
				Cell.Neighbors.Add(X - 1);
				Snapshot->Cells[X - 1].Neighbors.Add(X);
			}
		}
		return Snapshot;
	}

	struct FFixture
	{
		explicit FFixture(const TCHAR* WorldName, const bool bClone = false)
			: Scope(WorldName)
		{
			if (!Scope.World)
			{
				return;
			}

			NavigationData = Spawn<AGridNavigationData>(
				*Scope.World, TEXT("TeleportGateGrid"), FVector::ZeroVector);
			GridId = FGuid::NewGuid();
			FString PublishDiagnostic;
			if (!NavigationData
				|| !NavigationData->PublishSnapshot(MakeGridSnapshot(GridId), &PublishDiagnostic))
			{
				return;
			}

			Source = Spawn<AParadoxTeleportGateTestActor>(
				*Scope.World,
				TEXT("SourceTeleportGate"),
				FVector(SourceCellX * 100.0, 0.0, 0.0));
			Destination = Spawn<AParadoxTeleportGateTestActor>(
				*Scope.World,
				TEXT("DestinationTeleportGate"),
				FVector(DestinationExitCellX * 100.0, 0.0, 0.0));
			Character = bClone
				? static_cast<AParadoxCharacter*>(Spawn<AParadoxCloneCharacter>(
					*Scope.World,
					TEXT("TeleportGateClone"),
					FVector(SourceInteractionCellX * 100.0, 0.0, 0.0)))
				: static_cast<AParadoxCharacter*>(Spawn<AParadoxPlayerCharacter>(
					*Scope.World,
					TEXT("TeleportGatePlayer"),
					FVector(SourceInteractionCellX * 100.0, 0.0, 0.0)));
			MovementController = Spawn<AParadoxPuzzleOverlayTestController>(
				*Scope.World, TEXT("TeleportGateMovementController"), FVector::ZeroVector);
			PuzzleController = NewObject<APuzzleController>(
				GetTransientPackage(),
				APuzzleController::StaticClass(),
				MakeUniqueObjectName(
					GetTransientPackage(),
					APuzzleController::StaticClass(),
					TEXT("TeleportGatePuzzleController")));
			if (!Source || !Destination || !Character || !MovementController || !PuzzleController)
			{
				return;
			}

			Source->SetFlags(RF_WasLoaded);
			Destination->SetFlags(RF_WasLoaded);
			Source->LinkedEndpoint = Destination;
			Destination->LinkedEndpoint = Source;
			Source->TransferOutDuration = 60.0f;
			Destination->TransferInDuration = 60.0f;
			Source->TransferAnchor->SetRelativeLocation(FVector::ZeroVector);
			Destination->TransferAnchor->SetRelativeLocation(FVector::ZeroVector);

			SmartObjectDefinition = NewObject<USmartObjectDefinition>(
				GetTransientPackage(),
				TEXT("TeleportGateRuntimeSmartObject"));
			FSmartObjectSlotDefinition& Slot = SmartObjectDefinition->DebugAddSlot();
			Slot.ID = FGuid::NewGuid();
			Slot.Offset = FVector3f(-100.0f, 0.0f, 0.0f);
			Slot.BehaviorDefinitions.Add(
				NewObject<UParadoxTeleportGateTestBehaviorDefinition>(SmartObjectDefinition));
			if (!SmartObjectDefinition->Validate())
			{
				return;
			}
			Source->GetSmartObjectComponent()->SetDefinition(SmartObjectDefinition);
			Destination->GetSmartObjectComponent()->SetDefinition(SmartObjectDefinition);

			EnterDefinition = NewObject<UParadoxEnterTeleportGateInteractionActionDefinition>(
				GetTransientPackage(),
				TEXT("TeleportGateRuntimeEnterDefinition"));
			Source->GetInteractionComponent()->InteractionDefinitions.Reset();
			Destination->GetInteractionComponent()->InteractionDefinitions.Reset();
			AddEnterDefinition(*Source);
			AddEnterDefinition(*Destination);

			MovementController->Possess(Character);
			Scope.StartPlay();
			Source->PuzzleReceiver->SetControllerRequest(PuzzleController, true);
			Destination->PuzzleReceiver->SetControllerRequest(PuzzleController, true);
			UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
				*Character,
				Character->GetCapsuleComponent()->GetScaledCapsuleRadius(),
				Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f,
				true);

			USmartObjectSubsystem* SmartObjects = USmartObjectSubsystem::GetCurrent(Scope.World);
			if (!SmartObjects)
			{
				return;
			}
			if (!Source->GetSmartObjectComponent()->GetRegisteredHandle().IsValid()
				&& !SmartObjects->RegisterSmartObject(Source->GetSmartObjectComponent()))
			{
				return;
			}
			if (!Destination->GetSmartObjectComponent()->GetRegisteredHandle().IsValid()
				&& !SmartObjects->RegisterSmartObject(Destination->GetSmartObjectComponent()))
			{
				return;
			}
			Source->GetInteractionComponent()->RefreshInteractionSources();
			Destination->GetInteractionComponent()->RefreshInteractionSources();
			SmartObjects->GetAllSlots(
				Source->GetSmartObjectComponent()->GetRegisteredHandle(),
				SourceSlots);
			bInitialized = SourceSlots.Num() == 1;
		}

		void AddEnterDefinition(AParadoxTeleportGate& Gate)
		{
			FParadoxInteractionDefinition& Entry =
				Gate.GetInteractionComponent()->InteractionDefinitions.AddDefaulted_GetRef();
			Entry.InteractionTag = ParadoxGameplayTags::Interaction_TeleportGate_Enter;
			Entry.GameplayActionDefinition = EnterDefinition;
		}

		FParadoxInteractionRequestResult RequestEnter()
		{
			FParadoxInteractionRequestResult Result =
				Source->GetInteractionComponent()->RequestInteraction(
				Character,
				ParadoxGameplayTags::Interaction_TeleportGate_Enter,
				ParadoxGameplayTags::Origin_Player,
				MovementController);
			ActiveAction = GetAction(Result);
			return Result;
		}

		UParadoxEnterTeleportGateInteractionAction* GetAction(
			const FParadoxInteractionRequestResult& Request) const
		{
			return Request.SubmissionResult.Handle.IsValid() && Character
				? Cast<UParadoxEnterTeleportGateInteractionAction>(
					Character->GetGameplayActionComponent()->GetActionInstance(
						Request.SubmissionResult.Handle))
				: nullptr;
		}

		FGridCellId Cell(const int32 X) const
		{
			FGridCellId Result;
			Result.GridId = GridId;
			Result.Coord = FGridCellCoord(X, 0, 0);
			return Result;
		}

		bool HasParkingAtExit() const
		{
			const UGridNavigationOccupancyComponent* Occupancy = Character
				? UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*Character)
				: nullptr;
			const FGridTrafficReservationSnapshotPtr Snapshot = NavigationData
				? NavigationData->GetTrafficReservationSnapshot()
				: nullptr;
			if (!Occupancy || !Snapshot.IsValid())
			{
				return false;
			}
			for (const FGridTrafficReservedCell& Reserved : Snapshot->Cells)
			{
				if (Reserved.OwnerId == Occupancy->OccupantId
					&& Reserved.CellId == Cell(DestinationInteractionCellX)
					&& Reserved.bGoalOrParking)
				{
					return true;
				}
			}
			return false;
		}

		bool TickActiveAction()
		{
			if (!ActiveAction.IsValid())
			{
				return false;
			}
			FParadoxTeleportGateInteractionActionTestAccessor::Tick(*ActiveAction.Get());
			return true;
		}

		bool TimeoutActiveMove()
		{
			if (!ActiveAction.IsValid())
			{
				return false;
			}
			FParadoxTeleportGateInteractionActionTestAccessor::Timeout(*ActiveAction.Get());
			return true;
		}

		bool CompleteIngress()
		{
			if (!Character || !Source)
			{
				return false;
			}
			Character->SetActorLocation(
				Source->TransferAnchor->GetComponentLocation(),
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
			return TickActiveAction();
		}

		bool CompleteEgress()
		{
			if (!Character)
			{
				return false;
			}
			Character->SetActorLocation(
				FVector(DestinationInteractionCellX * 100.0, 0.0, 0.0),
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
			return TickActiveAction();
		}

		bool IsValid() const
		{
			return bInitialized && Scope.World && NavigationData && Source && Destination
				&& Character && MovementController && PuzzleController && EnterDefinition;
		}

		FScopedWorld Scope;
		FGuid GridId;
		AGridNavigationData* NavigationData = nullptr;
		AParadoxTeleportGateTestActor* Source = nullptr;
		AParadoxTeleportGateTestActor* Destination = nullptr;
		AParadoxCharacter* Character = nullptr;
		AParadoxPuzzleOverlayTestController* MovementController = nullptr;
		APuzzleController* PuzzleController = nullptr;
		USmartObjectDefinition* SmartObjectDefinition = nullptr;
		UParadoxEnterTeleportGateInteractionActionDefinition* EnterDefinition = nullptr;
		TWeakObjectPtr<UParadoxEnterTeleportGateInteractionAction> ActiveAction;
		TArray<FSmartObjectSlotHandle> SourceSlots;
		bool bInitialized = false;
	};

	const TCHAR* ScenarioNames[] = {
		TEXT("01.Architecture.SelectionCatalogAndNoEmitter"),
		TEXT("02.Approach.AdjacentCellNeverGateCell"),
		TEXT("03.Activation.NoOverlapAuthority"),
		TEXT("04.Validation.InactiveReceiver"),
		TEXT("05.Validation.InvalidPair"),
		TEXT("06.Validation.DestinationCellOccupied"),
		TEXT("07.Transaction.AtomicPairLock"),
		TEXT("08.Action.ConflictsRejectedAndMovementStopped"),
		TEXT("09.Commit.CollisionSafeTeleport"),
		TEXT("10.Commit.ParkingOccupancyCoherent"),
		TEXT("11.Action.CompletesOnlyAfterTransferIn"),
		TEXT("12.Semantics.CloneUsesSameEnter"),
		TEXT("13.Replay.NoCoordinateParameters"),
		TEXT("14.Reset.TransferOutReleasesEverything"),
		TEXT("15.Reset.AfterCommitKeepsDestination"),
		TEXT("16.Reset.AfterCompletionIsIdempotent"),
		TEXT("17.Regression.ReceiverDisabledAfterAcquisition"),
		TEXT("18.Regression.StaleCallbacksIgnored"),
		TEXT("19.Regression.ActionCancellationCancelsOperation"),
		TEXT("20.Timeout.IngressForcesCommit"),
		TEXT("21.Action.PauseResumeDirectMovement"),
		TEXT("22.Timeout.EgressForcesSuccessfulRecovery"),
		TEXT("23.Regression.PreflightDefersTunnelPlacementUntilAcquisition"),
		TEXT("24.Runtime.ForcedInputDirectionAndTimeoutBudget")
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FParadoxTeleportGateScenariosTest,
	"Paradox.TeleportGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FParadoxTeleportGateScenariosTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	for (int32 Index = 0;
		Index < UE_ARRAY_COUNT(UE::Paradox::TeleportGate::Tests::ScenarioNames);
		++Index)
	{
		OutBeautifiedNames.Add(UE::Paradox::TeleportGate::Tests::ScenarioNames[Index]);
		OutTestCommands.Add(FString::FromInt(Index));
	}
}

bool FParadoxTeleportGateScenariosTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::TeleportGate::Tests;
	const int32 Scenario = FCString::Atoi(*Parameters);
	FFixture Fixture(
		*FString::Printf(TEXT("ParadoxTeleportGateScenario%d"), Scenario + 1),
		Scenario == 11);
	if (!TestTrue(TEXT("Teleport Gate fixture is valid"), Fixture.IsValid()))
	{
		return false;
	}

	switch (Scenario)
	{
	case 0:
	{
		const AParadoxTeleportGate* Defaults = GetDefault<AParadoxTeleportGate>();
		TestFalse(TEXT("Teleport Gate is concrete"), AParadoxTeleportGate::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
		TestFalse(TEXT("Teleport Gate never ticks"), Defaults->PrimaryActorTick.bCanEverTick);
		TestNotNull(TEXT("Selectable component exists"), Defaults->GetSelectableComponent());
		TestNotNull(TEXT("Smart Object component exists"), Defaults->GetSmartObjectComponent());
		TestNotNull(TEXT("Interaction component exists"), Defaults->GetInteractionComponent());
		TestNotNull(TEXT("Permanent navigation blocker exists"), Defaults->GetPermanentNavigationBlocker());
		TestNotNull(TEXT("Transit navigation blocker exists"), Defaults->GetTransitNavigationBlocker());
		TestTrue(TEXT("Permanent navigation blocker is attached to the Gate root"),
			Defaults->GetPermanentNavigationBlocker()
				&& Defaults->GetPermanentNavigationBlocker()->GetAttachParent() == Defaults->SceneRoot.Get());
		TestTrue(TEXT("Permanent navigation blocker always blocks by default"),
			Defaults->GetPermanentNavigationBlocker()
				&& Defaults->GetPermanentNavigationBlocker()->bAutoActivate
				&& Defaults->GetPermanentNavigationBlocker()->bBlockCells);
		TestTrue(TEXT("Transit navigation blocker is attached to the Gate root"),
			Defaults->GetTransitNavigationBlocker()
				&& Defaults->GetTransitNavigationBlocker()->GetAttachParent() == Defaults->SceneRoot.Get());
		TestTrue(TEXT("Transit navigation blocker permits navigation while idle"),
			Defaults->GetTransitNavigationBlocker()
				&& Defaults->GetTransitNavigationBlocker()->bAutoActivate
				&& !Defaults->GetTransitNavigationBlocker()->bBlockCells);
		TestNotNull(TEXT("Inherited Exit Anchor exists"), Defaults->TransferAnchor.Get());
		TestEqual(TEXT("Transfer-Out completion is explicit"), Defaults->TransferOutCompletionMode, EParadoxTransferPhaseCompletionMode::Explicit);
		TestEqual(TEXT("Transfer-In completion is explicit"), Defaults->TransferInCompletionMode, EParadoxTransferPhaseCompletionMode::Explicit);
		TestNull(TEXT("Teleport Gate owns no Emitter"), Defaults->FindComponentByClass<UPuzzleEmitterComponent>());
		const USmartObjectDefinition* AuthoredSmartObject = Defaults->GetSmartObjectComponent()->GetDefinition();
		TestNotNull(TEXT("Dedicated Gate Smart Object is configured"), AuthoredSmartObject);
		TestTrue(
			TEXT("Dedicated Gate Smart Object has the authored path"),
			AuthoredSmartObject
				&& AuthoredSmartObject->GetPathName().Contains(TEXT("SOD_TeleportGate")));
		TestEqual(TEXT("Gate definition has exactly one external slot"),
			AuthoredSmartObject ? AuthoredSmartObject->GetSlots().Num() : 0,
			1);
		const UGameplayActionDefinition* Asset = LoadObject<UGameplayActionDefinition>(
			nullptr,
			TEXT("/Game/Data/GameplayActions/DA_ParadoxEnterTeleportGate.DA_ParadoxEnterTeleportGate"));
		TestNotNull(TEXT("Native Enter Definition asset exists"), Asset);
		TestTrue(TEXT("Enter asset uses dedicated Definition class"), Asset && Asset->IsA<UParadoxEnterTeleportGateInteractionActionDefinition>());
		break;
	}
	case 1:
	{
		const FParadoxInteractionQueryResult Query =
			Fixture.Source->GetInteractionComponent()->QueryInteractionOptionsByTag(
				Fixture.Character,
				ParadoxGameplayTags::Interaction_TeleportGate_Enter);
		TestEqual(TEXT("One adjacent Enter option is exposed"), Query.Options.Num(), 1);
		const FGridCellQueryResult GateCell =
			Fixture.Scope.World->GetSubsystem<UGridWorldSubsystem>()->ProjectPoint(
				Fixture.Source->GetActorLocation());
		if (Query.Options.Num() == 1)
		{
			AddInfo(FString::Printf(
				TEXT("Resolved interaction cell=(%d,%d,%d), Gate cell=(%d,%d,%d)."),
				Query.Options[0].GridCellId.Coord.X,
				Query.Options[0].GridCellId.Coord.Y,
				Query.Options[0].GridCellId.Coord.Layer,
				GateCell.CellId.Coord.X,
				GateCell.CellId.Coord.Y,
				GateCell.CellId.Coord.Layer));
		}
		TestTrue(TEXT("Interaction option is an external adjacent cell"),
			Query.Options.Num() == 1
				&& Query.Options[0].GridCellId == Fixture.Cell(SourceInteractionCellX)
				&& Query.Options[0].GridCellId != GateCell.CellId);
		break;
	}
	case 2:
	{
		Fixture.Character->SetActorLocation(Fixture.Source->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
		Fixture.Scope.World->Tick(ELevelTick::LEVELTICK_All, 0.05f);
		TestEqual(TEXT("Overlap does not acquire transfer"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestTrue(TEXT("Overlap does not teleport"), Fixture.Character->GetActorLocation().Equals(Fixture.Source->GetActorLocation()));
		break;
	}
	case 3:
	{
		Fixture.Source->PuzzleReceiver->SetControllerRequest(Fixture.PuzzleController, false);
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestFalse(TEXT("Inactive source rejects Enter"), Request.IsAccepted());
		TestEqual(TEXT("Pair remains Idle"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
		break;
	}
	case 4:
	{
		Fixture.Destination->LinkedEndpoint = nullptr;
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestFalse(TEXT("Non-reciprocal pair rejects Enter"), Request.IsAccepted());
		TestEqual(TEXT("Source remains Idle"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		break;
	}
	case 5:
	{
		APawn* Blocker = Spawn<APawn>(
			*Fixture.Scope.World,
			TEXT("TeleportGateExitBlocker"),
			FVector(DestinationInteractionCellX * 100.0, 0.0, 0.0));
		if (TestNotNull(TEXT("Exit blocker spawns"), Blocker))
		{
			USceneComponent* Root = NewObject<USceneComponent>(Blocker, TEXT("BlockerRoot"));
			Blocker->AddInstanceComponent(Root);
			Blocker->SetRootComponent(Root);
			Root->RegisterComponent();
			Blocker->SetActorLocation(FVector(DestinationInteractionCellX * 100.0, 0.0, 0.0));
			UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(*Blocker, 42.0f, 192.0f, true);
			const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
			TestFalse(TEXT("Occupied destination exit rejects Enter"), Request.IsAccepted());
			TestEqual(TEXT("Rejected Character remains at source"), Fixture.Character->GetActorLocation().X, SourceInteractionCellX * 100.0);
		}
		break;
	}
	case 6:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		if (!Request.IsAccepted())
		{
			AddInfo(FString::Printf(
				TEXT("Enter rejected: request=%s query=%s submission=%s diagnostic=%s"),
				*UEnum::GetValueAsString(Request.Status),
				*UEnum::GetValueAsString(Request.QueryStatus),
				*UEnum::GetValueAsString(Request.SubmissionResult.Status),
				*Request.DiagnosticMessage));
		}
		TestTrue(TEXT("Enter acquires transaction"), Request.IsAccepted());
		if (Fixture.Source->GetTransferState() == EParadoxTransferEndpointState::Idle)
		{
			FGameplayActionResult TerminalResult;
			if (Fixture.Character->GetGameplayActionComponent()->GetActionResult(
				Request.SubmissionResult.Handle,
				TerminalResult))
			{
				AddInfo(FString::Printf(
					TEXT("Enter terminated during acquisition: %s"),
					*TerminalResult.DiagnosticMessage));
			}
		}
		TestEqual(TEXT("Source locks as Sending"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Sending);
		TestEqual(TEXT("Destination locks as Receiving"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Receiving);
		TestTrue(TEXT("Exit cell is atomically claimed"), FParadoxTeleportGateTestAccessor::HasExitClaim(*Fixture.Source));
		TestTrue(TEXT("Source transit footprint blocks during ingress"),
			Fixture.Source->GetTransitNavigationBlocker()->bBlockCells);
		TestTrue(TEXT("Destination transit footprint also blocks during ingress"),
			Fixture.Destination->GetTransitNavigationBlocker()->bBlockCells);
		Fixture.Source->ResetTransferEndpoint();
		TestFalse(TEXT("Source transit footprint is navigable after reset"),
			Fixture.Source->GetTransitNavigationBlocker()->bBlockCells);
		TestFalse(TEXT("Destination transit footprint is navigable after reset"),
			Fixture.Destination->GetTransitNavigationBlocker()->bBlockCells);
		break;
	}
	case 7:
	{
		Fixture.Character->GetCharacterMovement()->Velocity = FVector(500.0f, 0.0f, 0.0f);
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Enter starts"), Request.IsAccepted());
		TestTrue(TEXT("Stale movement velocity is stopped"), Fixture.Character->GetVelocity().IsNearlyZero());
		const FParadoxInteractionRequestResult Concurrent = Fixture.RequestEnter();
		TestFalse(TEXT("Conflicting Enter is rejected while locks are owned"), Concurrent.IsAccepted());
		Fixture.Source->ResetTransferEndpoint();
		break;
	}
	case 8:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		UParadoxEnterTeleportGateInteractionAction* Action = Fixture.GetAction(Request);
		TestNotNull(TEXT("Running Enter action exists"), Action);
		TestTrue(TEXT("Ingress enables native action ticking"), Action && Action->IsActionTickEnabled());
		TestTrue(TEXT("Ingress completion commits through TeleportTo"), Fixture.CompleteIngress());
		TestTrue(TEXT("Source transit footprint remains blocked after commit"),
			Fixture.Source->GetTransitNavigationBlocker()->bBlockCells);
		TestTrue(TEXT("Destination transit footprint blocks during egress"),
			Fixture.Destination->GetTransitNavigationBlocker()->bBlockCells);
		const FGridCellQueryResult AnchorCell =
			Fixture.Scope.World->GetSubsystem<UGridWorldSubsystem>()->GetCell(
				Fixture.Cell(DestinationExitCellX));
		TestFalse(TEXT("Destination anchor cell is intentionally non-walkable"), AnchorCell.bWalkable);
		TestTrue(TEXT("Character transform is at collision-safe tunnel anchor"),
			Fixture.Character->GetActorLocation().Equals(Fixture.Destination->TransferAnchor->GetComponentLocation()));
		Fixture.Destination->ResetTransferEndpoint();
		break;
	}
	case 9:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Transfer-out commits"), Request.IsAccepted() && Fixture.CompleteIngress());
		TestTrue(TEXT("Temporary goal claim remains through egress"), FParadoxTeleportGateTestAccessor::HasExitClaim(*Fixture.Source));
		TestTrue(TEXT("Committed placement remains tracked through Transfer-In"), FParadoxTeleportGateTestAccessor::HasCommittedPlacement(*Fixture.Source));
		TestTrue(TEXT("Egress reaches the reserved external slot"), Fixture.CompleteEgress());
		TestFalse(TEXT("Temporary goal claim is released after egress"), FParadoxTeleportGateTestAccessor::HasExitClaim(*Fixture.Source));
		TestTrue(TEXT("GridWorld parking protects the external slot"), Fixture.HasParkingAtExit());
		break;
	}
	case 10:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Enter is accepted"), Request.IsAccepted());
		TestNotNull(TEXT("Enter remains owned while Transfer-Out runs"), Fixture.GetAction(Request));
		TestTrue(TEXT("Transfer-Out completes only after ingress"), Fixture.CompleteIngress());
		TestNotNull(TEXT("Enter remains owned during destination egress"), Fixture.GetAction(Request));
		TestTrue(TEXT("Transfer-In completes only after egress"), Fixture.CompleteEgress());
		TestNull(TEXT("Enter releases only after Transfer-In"), Fixture.GetAction(Request));
		TestFalse(TEXT("Source transit footprint is navigable after completion"),
			Fixture.Source->GetTransitNavigationBlocker()->bBlockCells);
		TestFalse(TEXT("Destination transit footprint is navigable after completion"),
			Fixture.Destination->GetTransitNavigationBlocker()->bBlockCells);
		FGameplayActionResult Result;
		TestTrue(TEXT("Terminal action result exists"), Fixture.Character->GetGameplayActionComponent()->GetActionResult(Request.SubmissionResult.Handle, Result));
		TestEqual(TEXT("Enter succeeds after Transfer-In"), Result.TerminalState, EGameplayActionState::Succeeded);
		break;
	}
	case 11:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Clone uses the same Enter submission"), Request.IsAccepted());
		TestNotNull(TEXT("Clone owns the same native Enter action"), Fixture.GetAction(Request));
		TestTrue(TEXT("Clone transfer commits after ingress"), Fixture.CompleteIngress());
		Fixture.Destination->ResetTransferEndpoint();
		break;
	}
	case 12:
	{
		const UGameplayActionDefinition* Definition = GetDefault<UParadoxEnterTeleportGateInteractionActionDefinition>();
		TestEqual(TEXT("Enter is required in the semantic journal"), Definition->JournalRequirement, EGameplayActionJournalRequirement::Required);
		TestTrue(TEXT("Enter schema contains semantic Target"), Definition->DefaultParameters.FindPropertyDescByName(ParadoxInteractionActionParameters::Target) != nullptr);
		TestTrue(TEXT("Enter schema contains semantic InteractionTag"), Definition->DefaultParameters.FindPropertyDescByName(ParadoxInteractionActionParameters::InteractionTag) != nullptr);
		TestNull(TEXT("Enter schema records no teleport destination coordinate"), Definition->DefaultParameters.FindPropertyDescByName(TEXT("Destination")));
		TestNull(TEXT("Enter schema records no teleport transform"), Definition->DefaultParameters.FindPropertyDescByName(TEXT("TeleportTransform")));
		break;
	}
	case 13:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Transfer begins"), Request.IsAccepted());
		TestTrue(TEXT("Reset cancels Transfer-Out"), Fixture.Source->ResetTransferEndpoint());
		TestEqual(TEXT("Source is Idle after reset"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestEqual(TEXT("Destination is Idle after reset"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestFalse(TEXT("Reset releases destination claim"), FParadoxTeleportGateTestAccessor::HasExitClaim(*Fixture.Source));
		TestEqual(TEXT("Character stays at source before commit"), Fixture.Character->GetActorLocation().X, SourceInteractionCellX * 100.0);
		TestNull(TEXT("Reset terminates Enter action"), Fixture.GetAction(Request));
		break;
	}
	case 14:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Transfer commits"), Request.IsAccepted() && Fixture.CompleteIngress());
		TestTrue(TEXT("Reset cancels Transfer-In"), Fixture.Destination->ResetTransferEndpoint());
		TestEqual(TEXT("Committed Character recovers to destination slot"), Fixture.Character->GetActorLocation().X, DestinationInteractionCellX * 100.0);
		TestTrue(TEXT("Committed parking remains authoritative"), Fixture.HasParkingAtExit());
		TestEqual(TEXT("Pair becomes Idle"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		break;
	}
	case 15:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Transfer commits"), Request.IsAccepted() && Fixture.CompleteIngress());
		TestTrue(TEXT("Transfer completes"), Fixture.CompleteEgress());
		TestFalse(TEXT("Reset after completion is an idempotent no-op"), Fixture.Source->ResetTransferEndpoint());
		TestEqual(TEXT("Character remains at destination slot"), Fixture.Character->GetActorLocation().X, DestinationInteractionCellX * 100.0);
		break;
	}
	case 16:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Transfer begins"), Request.IsAccepted());
		Fixture.Source->PuzzleReceiver->SetControllerRequest(Fixture.PuzzleController, false);
		Fixture.Destination->PuzzleReceiver->SetControllerRequest(Fixture.PuzzleController, false);
		TestNotNull(TEXT("Acquired Enter remains running after Receiver deactivation"), Fixture.GetAction(Request));
		TestTrue(TEXT("Acquired Transfer-Out still commits"), Fixture.CompleteIngress());
		TestTrue(TEXT("Acquired Transfer-In still completes"), Fixture.CompleteEgress());
		break;
	}
	case 17:
	{
		const FParadoxInteractionRequestResult First = Fixture.RequestEnter();
		const FGuid OldOperationId = Fixture.Source->GetCurrentTransferOperationId();
		TestTrue(TEXT("First operation starts"), First.IsAccepted());
		TestTrue(TEXT("First operation resets"), Fixture.Source->ResetTransferEndpoint());
		const FParadoxInteractionRequestResult Second = Fixture.RequestEnter();
		const FGuid NewOperationId = Fixture.Source->GetCurrentTransferOperationId();
		TestTrue(TEXT("Replacement operation starts"), Second.IsAccepted());
		TestTrue(TEXT("Operation IDs are unique"), OldOperationId != NewOperationId);
		TestFalse(TEXT("Stale Transfer-Out callback is ignored"), Fixture.Source->CompleteTransferOutForTest(OldOperationId));
		TestEqual(TEXT("Replacement remains in Transfer-Out"), Fixture.Source->GetTransferPhase(), EParadoxTransferPhase::TransferOut);
		TestTrue(TEXT("Current ingress callback commits"), Fixture.CompleteIngress());
		Fixture.Destination->ResetTransferEndpoint();
		break;
	}
	case 18:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Enter starts"), Request.IsAccepted());
		TestEqual(
			TEXT("Cancelling Enter reaches Gameplay Actions"),
			Fixture.Character->GetGameplayActionComponent()->CancelAction(
				Request.SubmissionResult.Handle,
				GameplayActionTags::Result_Cancelled_ByRequester),
			EGameplayActionOperationResult::Succeeded);
		TestEqual(TEXT("Cancellation releases source"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestEqual(TEXT("Cancellation releases destination"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestFalse(TEXT("Cancellation releases exit claim"), FParadoxTeleportGateTestAccessor::HasExitClaim(*Fixture.Source));
		USmartObjectSubsystem* SmartObjects = USmartObjectSubsystem::GetCurrent(Fixture.Scope.World);
		TestTrue(TEXT("Cancellation releases Smart Object claim"), SmartObjects && SmartObjects->CanBeClaimed(Fixture.SourceSlots[0]));
		break;
	}
	case 19:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Enter starts before ingress timeout"), Request.IsAccepted());
		TestTrue(TEXT("Ingress timeout callback is delivered"), Fixture.TimeoutActiveMove());
		TestEqual(TEXT("Ingress timeout commits the teleport"), Fixture.Source->GetTransferPhase(), EParadoxTransferPhase::TransferIn);
		TestTrue(TEXT("Ingress timeout places Character at destination anchor"),
			Fixture.Character->GetActorLocation().Equals(
				Fixture.Destination->TransferAnchor->GetComponentLocation()));
		TestTrue(TEXT("Destination exit claim remains owned during egress"),
			FParadoxTeleportGateTestAccessor::HasExitClaim(*Fixture.Source));
		Fixture.Destination->ResetTransferEndpoint();
		break;
	}
	case 20:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		UParadoxEnterTeleportGateInteractionAction* Action = Fixture.GetAction(Request);
		TestTrue(TEXT("Enter starts before pause"), Request.IsAccepted());
		TestTrue(TEXT("Direct movement timeout starts active"),
			Action && !FParadoxTeleportGateInteractionActionTestAccessor::IsTimeoutPaused(*Action));
		TestEqual(TEXT("Scheduler pause succeeds"), Fixture.Character->GetGameplayActionComponent()->PauseActions(), EGameplayActionOperationResult::Succeeded);
		TestTrue(TEXT("Direct movement timeout pauses with the action"),
			Action && FParadoxTeleportGateInteractionActionTestAccessor::IsTimeoutPaused(*Action));
		TestEqual(TEXT("Scheduler resume succeeds"), Fixture.Character->GetGameplayActionComponent()->ResumeActions(), EGameplayActionOperationResult::Succeeded);
		TestTrue(TEXT("Direct movement timeout resumes with the action"),
			Action && !FParadoxTeleportGateInteractionActionTestAccessor::IsTimeoutPaused(*Action));
		Fixture.Source->ResetTransferEndpoint();
		break;
	}
	case 21:
	{
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Enter starts before egress timeout"), Request.IsAccepted());
		TestTrue(TEXT("Ingress commits"), Fixture.CompleteIngress());
		TestTrue(TEXT("Egress timeout callback is delivered"), Fixture.TimeoutActiveMove());
		TestEqual(TEXT("Egress timeout completes the pair"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestEqual(TEXT("Egress timeout forces destination slot recovery"), Fixture.Character->GetActorLocation().X, DestinationInteractionCellX * 100.0);
		TestTrue(TEXT("Egress timeout realigns destination parking"), Fixture.HasParkingAtExit());
		FGameplayActionResult Result;
		TestTrue(TEXT("Timed-out egress retains a terminal action result"),
			Fixture.Character->GetGameplayActionComponent()->GetActionResult(
				Request.SubmissionResult.Handle,
				Result));
		TestEqual(TEXT("Timed-out egress succeeds"), Result.TerminalState, EGameplayActionState::Succeeded);
		break;
	}
	case 22:
	{
		AActor* AnchorBlocker = Spawn<AActor>(
			*Fixture.Scope.World,
			TEXT("TeleportGateAnchorBlocker"),
			Fixture.Destination->TransferAnchor->GetComponentLocation());
		UBoxComponent* BlockingBox = AnchorBlocker
			? NewObject<UBoxComponent>(AnchorBlocker, TEXT("BlockingBox"))
			: nullptr;
		if (TestNotNull(TEXT("Destination anchor blocker spawns"), AnchorBlocker)
			&& TestNotNull(TEXT("Destination blocking volume exists"), BlockingBox))
		{
			AnchorBlocker->AddInstanceComponent(BlockingBox);
			AnchorBlocker->SetRootComponent(BlockingBox);
			BlockingBox->SetBoxExtent(FVector(60.0, 60.0, 100.0));
			BlockingBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			BlockingBox->SetCollisionResponseToAllChannels(ECR_Block);
			BlockingBox->RegisterComponent();
			AnchorBlocker->SetActorLocation(
				Fixture.Destination->TransferAnchor->GetComponentLocation());
		}
		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		if (!Request.IsAccepted())
		{
			AddInfo(FString::Printf(
				TEXT("Enter rejected during preflight: request=%s query=%s submission=%s reason=%s diagnostic=%s"),
				*UEnum::GetValueAsString(Request.Status),
				*UEnum::GetValueAsString(Request.QueryStatus),
				*UEnum::GetValueAsString(Request.SubmissionResult.Status),
				*Request.SubmissionResult.ReasonTag.ToString(),
				*Request.DiagnosticMessage));
		}
		TestTrue(TEXT("Tunnel placement is deferred beyond action preflight"), Request.IsAccepted());
		TestEqual(TEXT("Failed preparation releases the pair"),
			Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		FGameplayActionResult TerminalResult;
		const bool bHasTerminalResult = Fixture.Character->GetGameplayActionComponent()->GetActionResult(
			Request.SubmissionResult.Handle,
			TerminalResult);
		TestTrue(TEXT("Failed collision-safe preparation retains a terminal result"), bHasTerminalResult);
		if (bHasTerminalResult)
		{
			TestEqual(TEXT("Unsafe tunnel placement fails the acquired Enter action"),
				TerminalResult.TerminalState, EGameplayActionState::Failed);
			TestTrue(TEXT("Terminal diagnostic identifies tunnel placement"),
				TerminalResult.DiagnosticMessage.Contains(TEXT("collision-safe"))
					|| TerminalResult.DiagnosticMessage.Contains(TEXT("tolerance")));
		}
		USmartObjectSubsystem* SmartObjects = USmartObjectSubsystem::GetCurrent(Fixture.Scope.World);
		TestTrue(TEXT("Failed preparation releases the source Smart Object claim"),
			SmartObjects && SmartObjects->CanBeClaimed(Fixture.SourceSlots[0]));
		break;
	}
	case 23:
	{
		Fixture.Source->TransferAnchor->SetRelativeLocation(FVector(0.0, 0.0, -100.0));
		AActor* DestinationFloor = Spawn<AActor>(
			*Fixture.Scope.World,
			TEXT("TeleportGateDestinationFloor"),
			Fixture.Destination->TransferAnchor->GetComponentLocation()
				+ FVector(0.0, 0.0, -10.0));
		UBoxComponent* FloorCollision = DestinationFloor
			? NewObject<UBoxComponent>(DestinationFloor, TEXT("FloorCollision"))
			: nullptr;
		if (DestinationFloor && FloorCollision)
		{
			DestinationFloor->AddInstanceComponent(FloorCollision);
			DestinationFloor->SetRootComponent(FloorCollision);
			FloorCollision->SetBoxExtent(FVector(50.0, 50.0, 10.0));
			FloorCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			FloorCollision->SetCollisionResponseToAllChannels(ECR_Block);
			FloorCollision->RegisterComponent();
			DestinationFloor->SetActorLocation(
				Fixture.Destination->TransferAnchor->GetComponentLocation()
					+ FVector(0.0, 0.0, -10.0));
		}
		FTransform SafeDestinationTransform;
		FString SafePlacementDiagnostic;
		TestTrue(TEXT("Destination floor permits a vertically adjusted safe anchor"),
			DestinationFloor && FloorCollision
				&& FParadoxTeleportGateTestAccessor::ResolveSafeTunnelAnchorPlacement(
					*Fixture.Source,
					*Fixture.Character,
					*Fixture.Destination,
					SafeDestinationTransform,
					SafePlacementDiagnostic));
		TestTrue(TEXT("Safe anchor preserves the authored horizontal tunnel point"),
			FVector::DistSquared2D(
				SafeDestinationTransform.GetLocation(),
				Fixture.Destination->TransferAnchor->GetComponentLocation())
				<= FMath::Square(Fixture.Destination->TunnelTraversalAcceptanceRadius));
		TestTrue(TEXT("Safe anchor may correct Character capsule height"),
			FMath::IsNearlyEqual(
				SafeDestinationTransform.GetLocation().Z,
				Fixture.Destination->TransferAnchor->GetComponentLocation().Z
					+ Fixture.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
				Fixture.Destination->TunnelTraversalAcceptanceRadius));
		if (FloorCollision)
		{
			FloorCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		if (DestinationFloor)
		{
			DestinationFloor->Destroy();
		}

		const FParadoxInteractionRequestResult Request = Fixture.RequestEnter();
		TestTrue(TEXT("Runtime Enter is accepted"), Request.IsAccepted());
		UParadoxEnterTeleportGateInteractionAction* Action = Fixture.GetAction(Request);
		TestNotNull(TEXT("Runtime Enter retains its native action"), Action);
		TestEqual(
			TEXT("Transfer is acquired while forced ingress runs"),
			Fixture.Source->GetTransferPhase(),
			EParadoxTransferPhase::TransferOut);
		const float IngressDistance = FVector::Dist2D(
			Fixture.Character->GetActorLocation(),
			Fixture.Source->TransferAnchor->GetComponentLocation());
		const float ExpectedIngressTimeout = 2.0f * IngressDistance
			/ Fixture.Character->GetCharacterMovement()->MaxWalkSpeed;
		TestTrue(TEXT("Ingress timeout is twice distance divided by MaxWalkSpeed"),
			Action && FMath::IsNearlyEqual(
				FParadoxTeleportGateInteractionActionTestAccessor::GetTimeoutSeconds(*Action),
				ExpectedIngressTimeout));
		Fixture.Character->ConsumeMovementInputVector();
		TestTrue(TEXT("Ingress action tick submits movement input"), Fixture.TickActiveAction());
		const FVector IngressInput = Fixture.Character->ConsumeMovementInputVector();
		TestTrue(TEXT("Ingress movement input points toward the source anchor"), IngressInput.X > 0.0);

		Fixture.Character->SetActorLocation(
			FVector(SourceCellX * 100.0, 0.0, 0.0),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		TestTrue(TEXT("Matching ingress XY commits despite the floor-height offset"),
			Fixture.TickActiveAction());
		TestEqual(
			TEXT("Action remains in Transfer-In after the teleport"),
			Fixture.Source->GetTransferPhase(),
			EParadoxTransferPhase::TransferIn);
		Fixture.Character->ConsumeMovementInputVector();
		TestTrue(TEXT("Egress action tick submits movement input"), Fixture.TickActiveAction());
		const FVector EgressInput = Fixture.Character->ConsumeMovementInputVector();
		TestTrue(TEXT("Egress movement input points toward the destination slot"), EgressInput.X < 0.0);
		TestTrue(TEXT("Reaching egress completes the action"), Fixture.CompleteEgress());

		FGameplayActionResult TerminalResult;
		const bool bHasTerminalResult = Fixture.Character->GetGameplayActionComponent()->GetActionResult(
			Request.SubmissionResult.Handle,
			TerminalResult);
		TestTrue(TEXT("Forced-input traversal retains a terminal action result"), bHasTerminalResult);
		TestEqual(TEXT("Forced-input traversal succeeds after ingress and egress"),
			TerminalResult.TerminalState, EGameplayActionState::Succeeded);
		TestTrue(TEXT("Character finishes on the destination interaction slot"),
			FMath::IsNearlyEqual(
				Fixture.Character->GetActorLocation().X,
				DestinationInteractionCellX * 100.0,
				1.0));
		break;
	}
	default:
		AddError(TEXT("Unknown Teleport Gate scenario."));
		return false;
	}

	return true;
}

#endif
