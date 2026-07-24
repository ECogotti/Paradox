#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GridMoveToCellActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Components/EntityIdentityComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "Data/EntityRelationPolicySet.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "EntityRelationTags.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameModes/ParadoxGameMode.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationQueryFilter.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Paradox.h"
#include "Perception/ParadoxTemporalVisionComponent.h"
#include "Playback/ParadoxCloneReplayExecutionStrategy.h"
#include "Recording/IntentReplayTrack.h"
#include "Relations/ParadoxTemporalOrderingPolicy.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Tests/ParadoxTimeLoopTestTypes.h"
#include "TimeLoop/ParadoxChronoSpawn.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"
#include "UObject/GarbageCollection.h"

/** Narrow friend accessor for deterministic coordinator setup in transient test worlds. */
struct FParadoxTimeLoopTestAccessor
{
	static void ConfigureActiveRun(
		UParadoxTimeLoopComponent& TimeLoop,
		AParadoxPlayerCharacter& Player,
		const TArray<AParadoxChronoSpawn*>& Spawns,
		AParadoxChronoSpawn& SelectedSpawn)
	{
		TimeLoop.bTimeLoopEnabled = true;
		TimeLoop.PlayerCharacter = &Player;
		TimeLoop.ChronoSpawns.Reset();
		for (AParadoxChronoSpawn* Spawn : Spawns)
		{
			TimeLoop.ChronoSpawns.Add(Spawn);
		}
		TimeLoop.MaximumTimelineCount = TimeLoop.ChronoSpawns.Num();
		TimeLoop.SelectedChronoSpawn = &SelectedSpawn;
		TimeLoop.CurrentPhase = EParadoxTimeLoopPhase::ActiveRun;
		TimeLoop.bPlayerCollisionWasEnabled = true;
		SelectedSpawn.SetRuntimeState(EParadoxChronoSpawnState::Selected);
	}

	static bool PrepareWorldState(
		UParadoxTimeLoopComponent& TimeLoop,
		FString& OutFailure)
	{
		return TimeLoop.PrepareWorldState(OutFailure);
	}

	static const TArray<TObjectPtr<AParadoxCloneCharacter>>& GetRuntimeClones(
		const UParadoxTimeLoopComponent& TimeLoop)
	{
		return TimeLoop.RuntimeClones;
	}

	static void ConfigureCloneClasses(
		UParadoxTimeLoopComponent& TimeLoop,
		UClass* CloneCharacterClass,
		UClass* CloneControllerClass)
	{
		TimeLoop.CloneCharacterClass = CloneCharacterClass;
		TimeLoop.CloneControllerClass = CloneControllerClass;
	}

	static void ConfigureCapacityState(
		UParadoxTimeLoopComponent& TimeLoop,
		const int32 Capacity,
		const EParadoxTimeLoopPhase Phase)
	{
		TimeLoop.bTimeLoopEnabled = true;
		TimeLoop.MaximumTimelineCount = Capacity;
		TimeLoop.CurrentPhase = Phase;
	}

	static FSoftObjectPath GetTemporalRelationPolicyPath(
		const UParadoxTimeLoopComponent& TimeLoop)
	{
		return TimeLoop.TemporalRelationPolicySet.ToSoftObjectPath();
	}

	static bool ConfigureEntityRelations(
		UParadoxTimeLoopComponent& TimeLoop,
		FString& OutFailure)
	{
		return TimeLoop.ConfigureEntityRelations(OutFailure);
	}

	static void ConfigureTemporalEvaluation(
		UParadoxTimeLoopComponent& TimeLoop,
		AParadoxPlayerCharacter& Player,
		const int32 DetectionSessionId)
	{
		TimeLoop.bTimeLoopEnabled = true;
		TimeLoop.PlayerCharacter = &Player;
		TimeLoop.CurrentPhase = EParadoxTimeLoopPhase::ActiveRun;
		TimeLoop.TemporalDetectionSessionId = DetectionSessionId;
		TimeLoop.bParadoxAcceptedForRun = false;
	}

	static void SubmitTemporalOverlap(
		UParadoxTimeLoopComponent& TimeLoop,
		const FParadoxTemporalOverlapSnapshot& Snapshot)
	{
		TimeLoop.HandleTemporalOverlapDetected(Snapshot);
	}

	static void SetGridPresence(
		UParadoxTimeLoopComponent& TimeLoop,
		AParadoxCharacter& Character,
		const bool bEnabled)
	{
		TimeLoop.SetTemporalAvatarGridPresence(Character, bEnabled);
	}
};

namespace UE::Paradox::TimeLoop::Tests
{
	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
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

		~FScopedTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(true);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}

		void StartPlay() const
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	AParadoxChronoSpawn* SpawnChronoSpawn(
		UWorld& World,
		const FVector& Location,
		const FName Name)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode =
			FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<AParadoxChronoSpawn>(
			AParadoxChronoSpawn::StaticClass(),
			FTransform(Location),
			Parameters);
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeLinearSnapshot(
		const FGuid& GridId)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot =
			MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = GridId;
		Snapshot->GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
		Snapshot->Revisions.Topology = 1;
		Snapshot->Revisions.Traversal = 1;
		Snapshot->Revisions.Occupancy = 1;
		FGridRegionData& Region = Snapshot->Regions.Add(GridId);
		Region.GridId = GridId;
		Region.GridTransform = Snapshot->GridTransform;
		for (int32 CellX = 0; CellX < 3; ++CellX)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = GridId;
			Cell.Id.Coord = FGridCellCoord(CellX, 0, 0);
			Cell.WorldCenter =
				Snapshot->GridTransform.CellToWorld(Cell.Id.Coord);
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
	FParadoxTimeLoopDefaultsAndCapacityTest,
	"Paradox.TimeLoop.DefaultsCapacityAndGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTimeLoopDefaultsAndCapacityTest::RunTest(const FString& Parameters)
{
	const UParadoxTimeLoopComponent* Defaults =
		GetDefault<UParadoxTimeLoopComponent>();
	TestNotNull(TEXT("Time-loop component has a CDO"), Defaults);
	if (Defaults)
	{
		TestFalse(TEXT("Time loop is opt-in"), Defaults->IsTimeLoopEnabled());
		TestFalse(
			TEXT("Time-loop component has no per-frame tick"),
			Defaults->PrimaryComponentTick.bCanEverTick);
		TestTrue(
			TEXT("Disabled time loop preserves existing movement"),
			Defaults->IsMovementAllowed());
	}

	UParadoxTimeLoopComponent* TimeLoop =
		NewObject<UParadoxTimeLoopComponent>();
	FParadoxTimeLoopTestAccessor::ConfigureCapacityState(
		*TimeLoop,
		1,
		EParadoxTimeLoopPhase::ActiveRun);
	TestTrue(
		TEXT("Movement is allowed during ActiveRun"),
		TimeLoop->IsMovementAllowed());
	const FParadoxTimeLoopOperationResult CapacityResult =
		TimeLoop->RequestTimeRewind();
	TestEqual(
		TEXT("Final-run rewind still validates its active player"),
		CapacityResult.Status,
		EParadoxTimeLoopOperationStatus::MissingPlayer);
	TestEqual(
		TEXT("A rejected capacity request preserves ActiveRun"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ActiveRun);

	FParadoxTimeLoopTestAccessor::ConfigureCapacityState(
		*TimeLoop,
		3,
		EParadoxTimeLoopPhase::WorldReset);
	TestFalse(
		TEXT("Movement is gated during reset"),
		TimeLoop->IsMovementAllowed());
	const FParadoxTimeLoopOperationResult PhaseResult =
		TimeLoop->RequestTimeRewind();
	TestEqual(
		TEXT("Duplicate rewind is rejected by phase"),
		PhaseResult.Status,
		EParadoxTimeLoopOperationStatus::RejectedInvalidPhase);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxTimeLoopConsolidationResetTest,
	"Paradox.TimeLoop.ConsolidationResetAndCloneReconstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTimeLoopConsolidationResetTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::TimeLoop::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxTimeLoopConsolidationWorld"));
	if (!TestNotNull(TEXT("Transient test world exists"), TestWorld.World))
	{
		return false;
	}

	UClass* PlayerClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/TopDown/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter_C"));
	if (!TestNotNull(TEXT("Project player Blueprint loads"), PlayerClass))
	{
		return false;
	}
	UClass* CloneClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/TopDown/Blueprints/BP_CloneCharacter.BP_CloneCharacter_C"));
	UClass* CloneControllerClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/TopDown/Blueprints/BP_CloneController.BP_CloneController_C"));
	if (!TestNotNull(TEXT("Project clone Blueprint loads"), CloneClass)
		|| !TestTrue(
			TEXT("Project clone Blueprint uses AParadoxCloneCharacter"),
			CloneClass && CloneClass->IsChildOf(AParadoxCloneCharacter::StaticClass()))
		|| !TestNotNull(TEXT("Project clone controller Blueprint loads"), CloneControllerClass)
		|| !TestTrue(
			TEXT("Project clone controller uses AParadoxCloneController"),
			CloneControllerClass
				&& CloneControllerClass->IsChildOf(AParadoxCloneController::StaticClass())))
	{
		return false;
	}

	FActorSpawnParameters PlayerParameters;
	PlayerParameters.Name = TEXT("TimeLoopTestPlayer");
	PlayerParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxPlayerCharacter* Player = TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
		PlayerClass,
		FTransform::Identity,
		PlayerParameters);
	AActor* Coordinator = TestWorld.World->SpawnActor<AActor>();
	AParadoxChronoSpawn* Spawn0 = SpawnChronoSpawn(
		*TestWorld.World,
		FVector(0.0, 0.0, 0.0),
		TEXT("ChronoSpawn_0"));
	AParadoxChronoSpawn* Spawn1 = SpawnChronoSpawn(
		*TestWorld.World,
		FVector(200.0, 0.0, 0.0),
		TEXT("ChronoSpawn_1"));
	AParadoxChronoSpawn* Spawn2 = SpawnChronoSpawn(
		*TestWorld.World,
		FVector(400.0, 0.0, 0.0),
		TEXT("ChronoSpawn_2"));
	if (!TestNotNull(TEXT("Player spawned"), Player)
		|| !TestNotNull(TEXT("Coordinator spawned"), Coordinator)
		|| !TestNotNull(TEXT("Chrono Spawn 0 spawned"), Spawn0)
		|| !TestNotNull(TEXT("Chrono Spawn 1 spawned"), Spawn1)
		|| !TestNotNull(TEXT("Chrono Spawn 2 spawned"), Spawn2))
	{
		return false;
	}

	UParadoxTimeLoopComponent* TimeLoop =
		NewObject<UParadoxTimeLoopComponent>(
			Coordinator,
			TEXT("TimeLoopTestComponent"),
			RF_Transient);
	Coordinator->AddInstanceComponent(TimeLoop);
	TimeLoop->RegisterComponent();
	FParadoxTimeLoopTestAccessor::ConfigureCloneClasses(
		*TimeLoop,
		CloneClass,
		CloneControllerClass);
	TestWorld.StartPlay();

	TestNotNull(
		TEXT("Character owns generic Entity Relations identity"),
		Player->GetEntityIdentityComponent());
	TestNotNull(
		TEXT("Character owns project temporal identity"),
		Player->GetTemporalEntityComponent());
	TestNull(
		TEXT("Player is not an ordinary World State participant"),
		Player->FindComponentByClass<UWorldStateParticipantComponent>());
	TestTrue(
		TEXT("Intent Replay initializes at BeginPlay"),
		Player->GetIntentReplayComponent()->IsIntentReplayInitialized());
	UGridNavigationOccupancyComponent* PlayerOccupancy =
		UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
			*Player,
			42.0f,
			192.0f,
			true);
	if (TestNotNull(
		TEXT("Test player receives GridWorld occupancy"),
		PlayerOccupancy))
	{
		TestTrue(
			TEXT("Player GridWorld occupancy starts active"),
			PlayerOccupancy->IsActive());
		FParadoxTimeLoopTestAccessor::SetGridPresence(
			*TimeLoop,
			*Player,
			false);
		TestFalse(
			TEXT("Inactive temporal player leaves GridWorld occupancy"),
			PlayerOccupancy->IsActive());
		FParadoxTimeLoopTestAccessor::SetGridPresence(
			*TimeLoop,
			*Player,
			true);
		TestTrue(
			TEXT("Reactivated temporal player republishes GridWorld occupancy"),
			PlayerOccupancy->IsActive());
	}

	FString WorldStateFailure;
	TestTrue(
		TEXT("World State registration finalizes and baseline captures"),
		FParadoxTimeLoopTestAccessor::PrepareWorldState(
			*TimeLoop,
			WorldStateFailure));
	if (!WorldStateFailure.IsEmpty())
	{
		AddInfo(WorldStateFailure);
	}

	AActor* AdoptingCoordinator = TestWorld.World->SpawnActor<AActor>();
	UParadoxTimeLoopComponent* AdoptingTimeLoop =
		NewObject<UParadoxTimeLoopComponent>(
			AdoptingCoordinator,
			TEXT("AdoptingTimeLoopTestComponent"),
			RF_Transient);
	AdoptingCoordinator->AddInstanceComponent(AdoptingTimeLoop);
	AdoptingTimeLoop->RegisterComponent();
	FString AdoptFailure;
	TestTrue(
		TEXT("A second authority can adopt an existing valid baseline"),
		FParadoxTimeLoopTestAccessor::PrepareWorldState(
			*AdoptingTimeLoop,
			AdoptFailure));
	if (!AdoptFailure.IsEmpty())
	{
		AddInfo(AdoptFailure);
	}

	const TArray<AParadoxChronoSpawn*> Spawns = { Spawn0, Spawn1, Spawn2 };
	FParadoxTimeLoopTestAccessor::ConfigureActiveRun(
		*TimeLoop,
		*Player,
		Spawns,
		*Spawn0);
	TestTrue(
		TEXT("Player temporal identity accepts timeline zero"),
		Player->GetTemporalEntityComponent()->AssignPlayer(0));

	FIntentRecordingOptions RecordingOptions;
	RecordingOptions.SourceLabel = TEXT("ParadoxAutomationTimeline_0");
	TestTrue(
		TEXT("Empty but valid player recording starts"),
		Player->GetIntentReplayComponent()
			->StartRecording(RecordingOptions)
			.Succeeded());

	const FParadoxTimeLoopOperationResult RewindResult =
		TimeLoop->RequestTimeRewind();
	TestTrue(TEXT("Rewind succeeds"), RewindResult.IsSuccess());
	TestEqual(
		TEXT("Coordinator returns to Chrono Spawn selection"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ChronoSpawnSelection);
	TestEqual(
		TEXT("One immutable timeline is consolidated"),
		TimeLoop->GetConsolidatedTimelineCount(),
		1);
	TestEqual(
		TEXT("Selected Chrono Spawn becomes occupied"),
		Spawn0->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Occupied);
	TestEqual(
		TEXT("Unused Chrono Spawn remains available"),
		Spawn1->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Available);
	TestTrue(TEXT("Player is inactive after reset"), Player->IsHidden());
	if (PlayerOccupancy)
	{
		TestFalse(
			TEXT("Hidden player cannot leave ghost GridWorld occupancy"),
			PlayerOccupancy->IsActive());
	}

	const TArray<FParadoxConsolidatedTimeline> Timelines =
		TimeLoop->GetConsolidatedTimelines();
	TWeakObjectPtr<UIntentReplayTrack> ConsolidatedTrack;
	if (TestEqual(TEXT("Timeline copy contains one record"), Timelines.Num(), 1))
	{
		TestEqual(TEXT("Timeline index is stable"), Timelines[0].TemporalIndex, 0);
		TestTrue(TEXT("Timeline retains a valid track"), Timelines[0].IsValid());
		TestEqual(
			TEXT("Empty finalized recordings remain valid"),
			Timelines[0].ReplayTrack->GetEntryCount(),
			0);
		ConsolidatedTrack = Timelines[0].ReplayTrack;
	}
	CollectGarbage(RF_NoFlags);
	TestTrue(
		TEXT("The consolidated track remains referenced across garbage collection"),
		ConsolidatedTrack.IsValid());

	const TArray<TObjectPtr<AParadoxCloneCharacter>>& Clones =
		FParadoxTimeLoopTestAccessor::GetRuntimeClones(*TimeLoop);
	if (TestEqual(TEXT("One clone is reconstructed"), Clones.Num(), 1)
		&& TestNotNull(TEXT("Reconstructed clone exists"), Clones[0].Get()))
	{
		const AParadoxCloneCharacter* Clone = Clones[0];
		TestTrue(
			TEXT("Coordinator reconstructs the configured clone Blueprint"),
			Clone->IsA(CloneClass));
		TArray<UWorldStateParticipantComponent*> CloneParticipants;
		Clone->GetComponents(CloneParticipants);
		TestEqual(
			TEXT("Clone Blueprint has exactly one World State participant"),
			CloneParticipants.Num(),
			1);
		TestNotNull(
			TEXT("Clone uses the configured dedicated controller Blueprint"),
			Clone->GetController());
		TestNotNull(
			TEXT("Clone owns authoritative temporal vision"),
			Clone->GetTemporalVisionComponent());
		TestNull(
			TEXT("Player does not own clone-only temporal vision"),
			Player->FindComponentByClass<UParadoxTemporalVisionComponent>());
		if (Clone->GetController())
		{
			TestTrue(
				TEXT("Clone controller derives from the configured clone controller"),
				Clone->GetController()->IsA(CloneControllerClass));
		}
		const UWorldStateParticipantComponent* Participant =
			Clone->GetWorldStateParticipantComponent();
		TestNotNull(TEXT("Clone participates in World State"), Participant);
		if (Participant)
		{
			const UWorldStateSubsystem* WorldState =
				TestWorld.World->GetSubsystem<UWorldStateSubsystem>();
			const TArray<FWorldStateParticipantSummary> Participants =
				WorldState ? WorldState->GetParticipantStateSummaries()
					: TArray<FWorldStateParticipantSummary>();
			TestTrue(
				TEXT("Reconstructed clone receives a stable World State identity"),
				Participant->GetParticipantId().IsValid());
			const bool bCloneIsRegistered = Participants.ContainsByPredicate(
				[Participant](const FWorldStateParticipantSummary& Summary)
				{
					return Summary.ParticipantId == Participant->GetParticipantId()
						&& Summary.bRegistered;
				});
			if (!bCloneIsRegistered)
			{
				AddInfo(FString::Printf(
					TEXT("Clone participant %s was not found among %d registry entries."),
					*Participant->GetParticipantId().ToString(),
					Participants.Num()));
				for (const FWorldStateParticipantSummary& Summary : Participants)
				{
					AddInfo(FString::Printf(
						TEXT("Registered participant %s: %s"),
						*Summary.ParticipantId.ToString(),
						*Summary.ActorPath));
				}
			}
			TestTrue(
				TEXT("Reconstructed clone is present in the World State registry"),
				bCloneIsRegistered);
		}
		const UParadoxTemporalEntityComponent* Temporal =
			Clone->GetTemporalEntityComponent();
		TestNotNull(TEXT("Clone has temporal identity"), Temporal);
		if (Temporal)
		{
			TestEqual(
				TEXT("Clone role is Clone"),
				Temporal->GetTemporalRole(),
				EParadoxTemporalEntityRole::Clone);
			TestEqual(
				TEXT("Clone temporal index is zero"),
				Temporal->GetTemporalIndex(),
				0);
			TestTrue(
				TEXT("Clone references the consolidated immutable track"),
				Timelines.Num() == 1
					&& Temporal->GetAssignedReplayTrack() == Timelines[0].ReplayTrack);
		}
		TestNull(
			TEXT("Milestone 4 does not prepare a replay session"),
			Clone->GetIntentReplayComponent()->GetActivePlaybackSession());
		TestEqual(
			TEXT("Reconstructed Blueprint clone uses the path-adapting replay strategy"),
			Clone->GetIntentReplayComponent()->ExecutionStrategyClass.Get(),
			UParadoxCloneReplayExecutionStrategy::StaticClass());
		TestEqual(
			TEXT("Clone remains stationary before synchronized playback"),
			Clone->GetCharacterMovement()->MovementMode,
			MOVE_None);
		TestFalse(
			TEXT("Clone is not controlled by the player"),
			Clone->IsPlayerControlled());
	}

	TimeLoop->UpdateHoveredChronoSpawn(Spawn1);
	TestEqual(
		TEXT("Available Chrono Spawn enters Hovered presentation state"),
		Spawn1->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Hovered);
	TimeLoop->UpdateHoveredChronoSpawn(Spawn2);
	TestEqual(
		TEXT("Previous hover returns to Available"),
		Spawn1->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Available);
	TestEqual(
		TEXT("New hover is applied"),
		Spawn2->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Hovered);
	TimeLoop->UpdateHoveredChronoSpawn(nullptr);

	const FParadoxTimeLoopOperationResult OccupiedSelection =
		TimeLoop->SelectChronoSpawn(Spawn0);
	TestEqual(
		TEXT("Occupied Chrono Spawn selection is rejected"),
		OccupiedSelection.Status,
		EParadoxTimeLoopOperationStatus::InvalidChronoSpawn);
	TestEqual(
		TEXT("Rejected selection preserves selection phase"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ChronoSpawnSelection);

	const FParadoxTimeLoopOperationResult SecondRun =
		TimeLoop->SelectChronoSpawn(Spawn1);
	TestTrue(TEXT("Second run starts from an available spawn"), SecondRun.IsSuccess());
	TestEqual(
		TEXT("Synchronous empty-track barrier reaches Active Run"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ActiveRun);
	TestEqual(
		TEXT("Second player run receives temporal index one"),
		Player->GetTemporalEntityComponent()->GetTemporalIndex(),
		1);
	TestEqual(
		TEXT("Second run starts recording"),
		Player->GetIntentReplayComponent()->GetRecordingState(),
		EIntentRecordingState::Recording);
	if (PlayerOccupancy)
	{
		TestTrue(
			TEXT("Selected player re-enters GridWorld occupancy at the new spawn"),
			PlayerOccupancy->IsActive());
	}
	TestEqual(
		TEXT("One consolidated clone participates in synchronized start"),
		TimeLoop->GetClonePlaybackParticipantCount(),
		1);
	FParadoxClonePlaybackSnapshot PlaybackSnapshot;
	if (TestTrue(
		TEXT("Clone playback snapshot is available by temporal index"),
		TimeLoop->GetClonePlaybackSnapshot(0, PlaybackSnapshot)))
	{
		TestEqual(
			TEXT("Empty replay completes without blocking the player run"),
			PlaybackSnapshot.State,
			EParadoxClonePlaybackState::Completed);
		TestEqual(
			TEXT("Empty replay retains zero source entries"),
			PlaybackSnapshot.TotalEntryCount,
			0);
	}

	const FParadoxTimeLoopOperationResult SecondRewind =
		TimeLoop->RequestTimeRewind();
	TestTrue(TEXT("Second rewind succeeds"), SecondRewind.IsSuccess());
	TestEqual(
		TEXT("Two timelines are consolidated"),
		TimeLoop->GetConsolidatedTimelineCount(),
		2);
	TestEqual(
		TEXT("Second Chrono Spawn becomes occupied"),
		Spawn1->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Occupied);

	const TArray<FParadoxConsolidatedTimeline> TwoTimelines =
		TimeLoop->GetConsolidatedTimelines();
	const TArray<TObjectPtr<AParadoxCloneCharacter>>& TwoClones =
		FParadoxTimeLoopTestAccessor::GetRuntimeClones(*TimeLoop);
	if (TestEqual(TEXT("Two clones are reconstructed"), TwoClones.Num(), 2)
		&& TestEqual(TEXT("Two timeline records are exposed as copies"), TwoTimelines.Num(), 2))
	{
		for (int32 Index = 0; Index < TwoClones.Num(); ++Index)
		{
			const AParadoxCloneCharacter* Clone = TwoClones[Index];
			const UParadoxTemporalEntityComponent* Temporal =
				Clone ? Clone->GetTemporalEntityComponent() : nullptr;
			TestNotNull(
				*FString::Printf(TEXT("Clone %d has temporal identity"), Index),
				Temporal);
			if (Temporal)
			{
				TestEqual(
					*FString::Printf(TEXT("Clone %d preserves ordered temporal index"), Index),
					Temporal->GetTemporalIndex(),
					Index);
				TestTrue(
					*FString::Printf(TEXT("Clone %d owns its matching track"), Index),
					Temporal->GetAssignedReplayTrack() == TwoTimelines[Index].ReplayTrack);
			}
			TestNull(
				*FString::Printf(TEXT("Clone %d has no premature playback"), Index),
				Clone
					? Clone->GetIntentReplayComponent()->GetActivePlaybackSession()
					: nullptr);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCloneReplayExactPathRestampTest,
	"Paradox.TimeLoop.CloneReplayRestampsExactGridPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCloneReplayExactPathRestampTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::TimeLoop::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxCloneReplayRestampWorld"));
	if (!TestNotNull(TEXT("Transient replay test world exists"), TestWorld.World))
	{
		return false;
	}

	AGridNavigationData* NavigationData =
		TestWorld.World->SpawnActor<AGridNavigationData>();
	if (!TestNotNull(TEXT("GridWorld navigation authority exists"), NavigationData))
	{
		return false;
	}
	const FGuid GridId = FGuid::NewGuid();
	FString PublishFailure;
	if (!TestTrue(
		TEXT("Linear GridWorld snapshot publishes"),
		NavigationData->PublishSnapshot(
			MakeLinearSnapshot(GridId),
			&PublishFailure)))
	{
		AddError(PublishFailure);
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxCloneCharacter* SourceCharacter =
		TestWorld.World->SpawnActor<AParadoxCloneCharacter>(
			AParadoxCloneCharacter::StaticClass(),
			FTransform(FVector::ZeroVector),
			SpawnParameters);
	AParadoxCloneCharacter* RecipientCharacter =
		TestWorld.World->SpawnActor<AParadoxCloneCharacter>(
			AParadoxCloneCharacter::StaticClass(),
			FTransform(FVector(5.0, 0.0, 0.0)),
			SpawnParameters);
	AParadoxCloneController* SourceController =
		TestWorld.World->SpawnActor<AParadoxCloneController>();
	AParadoxCloneController* RecipientController =
		TestWorld.World->SpawnActor<AParadoxCloneController>();
	if (!TestNotNull(TEXT("Source character exists"), SourceCharacter)
		|| !TestNotNull(TEXT("Recipient clone exists"), RecipientCharacter)
		|| !TestNotNull(TEXT("Source controller exists"), SourceController)
		|| !TestNotNull(TEXT("Recipient controller exists"), RecipientController))
	{
		return false;
	}
	SourceController->Possess(SourceCharacter);
	RecipientController->Possess(RecipientCharacter);
	TestWorld.StartPlay();

	UGridWorldSubsystem* GridWorld =
		TestWorld.World->GetSubsystem<UGridWorldSubsystem>();
	if (!TestNotNull(TEXT("GridWorld subsystem exists"), GridWorld))
	{
		return false;
	}
	const FGridWorldSnapshotPtr Snapshot = NavigationData->GetSnapshot();
	TArray<FGridCellId> Cells;
	for (const FGridCellData& Cell : Snapshot->Cells)
	{
		Cells.Add(Cell.Id);
	}

	FGridInjectedPath RecordedPath;
	const FGridInjectedPathValidationResult SourceStamp =
		GridWorld->CreateExactInjectedPath(
			SourceController,
			Cells,
			Cells.Last(),
			UGridNavigationQueryFilter::StaticClass(),
			false,
			false,
			EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal,
			RecordedPath);
	if (!TestTrue(
		TEXT("Source controller stamps the recorded exact path"),
		SourceStamp.bIsValid))
	{
		AddError(SourceStamp.DiagnosticMessage);
		return false;
	}

	const FGridInjectedPathValidationResult UnadaptedValidation =
		GridWorld->ValidateInjectedPath(RecipientController, RecordedPath);
	TestFalse(
		TEXT("Controller-bound source payload is invalid for the clone"),
		UnadaptedValidation.bIsValid);
	TestEqual(
		TEXT("The mismatch identifies the controller-specific filter"),
		UnadaptedValidation.FailureReason,
		EGridInjectedPathFailureReason::FilterMismatch);

	UGridMoveToCellActionDefinition* Definition =
		NewObject<UGridMoveToCellActionDefinition>(TestWorld.World);
	Definition->DefaultParameters.SetValueEnum(
		GridMoveToCellActionParameters::PathSource,
		EGridMovePathSource::ExactInjectedPath);
	Definition->DefaultParameters.SetValueStruct(
		GridMoveToCellActionParameters::InjectedPath,
		RecordedPath);
	Definition->DefaultParameters.SetValueEnum(
		GridMoveToCellActionParameters::GoalContentionPolicy,
		EGridGoalContentionPolicy::Ignore);
	const FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(Definition);
	if (!TestTrue(TEXT("Replay move request is created"), Creation.WasCreated()))
	{
		return false;
	}

	UParadoxCloneReplayExecutionStrategy* Strategy =
		NewObject<UParadoxCloneReplayExecutionStrategy>(TestWorld.World);
	UGameplayActionComponent* ActionComponent =
		RecipientCharacter->GetGameplayActionComponent();
	TestEqual(
		TEXT("Recipient action scheduler pauses before snapshot inspection"),
		ActionComponent->PauseActions(),
		EGameplayActionOperationResult::Succeeded);
	UParadoxTimeLoopActionEventObserver* EventObserver =
		NewObject<UParadoxTimeLoopActionEventObserver>(TestWorld.World);
	ActionComponent->OnActionEvent.AddDynamic(
		EventObserver,
		&UParadoxTimeLoopActionEventObserver::HandleActionEvent);
	const FGameplayActionSubmissionResult Submission =
		Strategy->SubmitPreparedRequest(ActionComponent, Creation.Request);
	if (!TestTrue(
		TEXT("Adapted clone movement request is accepted"),
		Submission.IsAccepted()))
	{
		AddError(Submission.DiagnosticMessage);
		return false;
	}

	const FGameplayActionEvent* AcceptedEvent =
		EventObserver->ObservedEvents.FindByPredicate(
			[&Submission](const FGameplayActionEvent& Event)
			{
				return Event.Handle == Submission.Handle
					&& Event.EventType == EGameplayActionEventType::Accepted;
			});
	if (!TestNotNull(
		TEXT("Clone movement emits an immutable accepted snapshot"),
		AcceptedEvent))
	{
		return false;
	}
	const TValueOrError<FStructView, EPropertyBagResult> RuntimePathValue =
		AcceptedEvent->GetParameters().GetValueStruct(
			GridMoveToCellActionParameters::InjectedPath,
			FGridInjectedPath::StaticStruct());
	const FGridInjectedPath* RuntimePath = RuntimePathValue.HasValue()
		? RuntimePathValue.GetValue().GetPtr<FGridInjectedPath>()
		: nullptr;
	if (TestNotNull(TEXT("Runtime clone request contains an exact path"), RuntimePath))
	{
		TestTrue(
			TEXT("Runtime path validates for the recipient clone"),
			GridWorld->ValidateInjectedPath(
				RecipientController,
				*RuntimePath).bIsValid);
		TestNotEqual(
			TEXT("Runtime clone path receives a new opaque identity"),
			RuntimePath->PathInstanceId,
			RecordedPath.PathInstanceId);
		TestEqual(
			TEXT("Runtime clone path preserves the recorded cells"),
			RuntimePath->Cells,
			RecordedPath.Cells);
	}

	const TValueOrError<FStructView, EPropertyBagResult> OriginalRequestPathValue =
		Creation.Request.GetParameters().GetValueStruct(
			GridMoveToCellActionParameters::InjectedPath,
			FGridInjectedPath::StaticStruct());
	const FGridInjectedPath* OriginalRequestPath =
		OriginalRequestPathValue.HasValue()
			? OriginalRequestPathValue.GetValue().GetPtr<FGridInjectedPath>()
			: nullptr;
	if (TestNotNull(
		TEXT("Prepared source request still contains its original path"),
		OriginalRequestPath))
	{
		TestEqual(
			TEXT("Clone submission does not mutate the prepared source request"),
			OriginalRequestPath->PathInstanceId,
			RecordedPath.PathInstanceId);
		TestEqual(
			TEXT("Clone submission preserves the source filter signature"),
			OriginalRequestPath->FilterSignature,
			RecordedPath.FilterSignature);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxTemporalOrderingPolicyTest,
	"Paradox.TimeLoop.TemporalOrderingPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTemporalOrderingPolicyTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::TimeLoop::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxTemporalOrderingWorld"));
	if (!TestNotNull(TEXT("Transient test world exists"), TestWorld.World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxPlayerCharacter* Observer =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform(FVector::ZeroVector),
			SpawnParameters);
	AParadoxPlayerCharacter* Target =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform(FVector(100.0f, 0.0f, 0.0f)),
			SpawnParameters);
	if (!TestNotNull(TEXT("Observer exists"), Observer)
		|| !TestNotNull(TEXT("Target exists"), Target))
	{
		return false;
	}
	TestWorld.StartPlay();

	TestTrue(
		TEXT("Observer receives T0"),
		Observer->GetTemporalEntityComponent()->AssignPlayer(0));
	TestTrue(
		TEXT("Target receives T1"),
		Target->GetTemporalEntityComponent()->AssignPlayer(1));

	FEntityRelationPolicyContext Context;
	Context.Source.Actor = Observer;
	Context.Source.EntityId = FEntityRelationId::NewId();
	Context.Target.Actor = Target;
	Context.Target.EntityId = FEntityRelationId::NewId();
	Context.QueryContext.Domain =
		EntityRelationTags::Domain_VisualPerception;
	UParadoxTemporalOrderingPolicy* Policy =
		NewObject<UParadoxTemporalOrderingPolicy>(TestWorld.World);

	FEntityRelationContribution Contribution;
	FString Failure;
	TestTrue(
		TEXT("T0 to T1 policy evaluation succeeds"),
		Policy->EvaluatePolicy(Context, Contribution, Failure));
	TestEqual(
		TEXT("A past observer is denied a future target"),
		Contribution.Decision,
		EEntityRelationDecision::Deny);
	TestTrue(
		TEXT("Future observation has the project outcome tag"),
		Contribution.OutcomeTags.HasTagExact(
			ParadoxGameplayTags::Relation_Outcome_FutureObserved));
	TestFalse(
		TEXT("Temporal ordering policy is deliberately non-cacheable"),
		Policy->IsCacheable());

	Swap(Context.Source, Context.Target);
	Contribution = FEntityRelationContribution();
	Failure.Reset();
	TestTrue(
		TEXT("T1 to T0 policy evaluation succeeds"),
		Policy->EvaluatePolicy(Context, Contribution, Failure));
	TestEqual(
		TEXT("A future observer seeing the past is safe"),
		Contribution.Decision,
		EEntityRelationDecision::Allow);
	TestFalse(
		TEXT("Safe temporal order has no paradox outcome"),
		Contribution.OutcomeTags.HasTagExact(
			ParadoxGameplayTags::Relation_Outcome_FutureObserved));

	Context.Source.EntityId = FEntityRelationId();
	Contribution = FEntityRelationContribution();
	Failure.Reset();
	TestFalse(
		TEXT("Invalid Entity Relations identity is rejected by policy preflight"),
		Policy->EvaluatePolicy(Context, Contribution, Failure));
	TestFalse(
		TEXT("Invalid Entity Relations identity contributes no decision"),
		Contribution.HasContribution());
	TestFalse(
		TEXT("Invalid Entity Relations identity provides diagnostics"),
		Failure.IsEmpty());

	Target->GetTemporalEntityComponent()->ClearTemporalAssignment();
	Context.Source.Actor = Observer;
	Context.Source.EntityId = FEntityRelationId::NewId();
	Context.Target.Actor = Target;
	Context.Target.EntityId = FEntityRelationId::NewId();
	Contribution = FEntityRelationContribution();
	Failure.Reset();
	TestTrue(
		TEXT("Invalid temporal metadata remains a diagnosable policy evaluation"),
		Policy->EvaluatePolicy(Context, Contribution, Failure));
	TestFalse(
		TEXT("Invalid temporal metadata contributes no decision"),
		Contribution.HasContribution());
	TestFalse(
		TEXT("Invalid temporal metadata provides diagnostics"),
		Contribution.DebugMessage.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxTemporalRelationAssetTest,
	"Paradox.TimeLoop.TemporalRelationAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTemporalRelationAssetTest::RunTest(const FString& Parameters)
{
	UClass* TimeLoopGameModeClass = LoadClass<AParadoxGameMode>(
		nullptr,
		TEXT(
			"/Game/TopDown/Blueprints/BP_TimeLoopGameMode."
			"BP_TimeLoopGameMode_C"));
	const AParadoxGameMode* TimeLoopGameModeDefaults =
		TimeLoopGameModeClass
			? Cast<AParadoxGameMode>(TimeLoopGameModeClass->GetDefaultObject())
			: nullptr;
	if (TestNotNull(
		TEXT("BP_TimeLoopGameMode loads"),
		TimeLoopGameModeDefaults))
	{
		const UParadoxTimeLoopComponent* ConfiguredLoop =
			TimeLoopGameModeDefaults->GetTimeLoopComponent();
		if (TestNotNull(
			TEXT("BP_TimeLoopGameMode owns its time-loop authority"),
			ConfiguredLoop))
		{
			TestEqual(
				TEXT("BP_TimeLoopGameMode selects the Paradox relation asset"),
				FParadoxTimeLoopTestAccessor::GetTemporalRelationPolicyPath(
					*ConfiguredLoop)
					.ToString(),
				FString(
					TEXT(
						"/Game/TopDown/Data/DA_ParadoxTimeLoopRelations."
						"DA_ParadoxTimeLoopRelations")));
		}
	}

	UEntityRelationPolicySet* PolicySet = LoadObject<UEntityRelationPolicySet>(
		nullptr,
		TEXT(
			"/Game/TopDown/Data/DA_ParadoxTimeLoopRelations."
			"DA_ParadoxTimeLoopRelations"));
	if (!TestNotNull(TEXT("Time-loop relation Policy Set loads"), PolicySet))
	{
		return false;
	}

	TestTrue(
		TEXT("Time-loop relation Policy Set validates"),
		PolicySet->ValidatePolicySet().IsValid());
	const TArray<TObjectPtr<UEntityRelationPolicy>>& Policies =
		PolicySet->GetPolicies();
	TestEqual(
		TEXT("Time-loop relation Policy Set contains exactly one policy"),
		Policies.Num(),
		1);
	if (Policies.Num() == 1)
	{
		TestTrue(
			TEXT("The configured policy is Paradox temporal ordering"),
			Policies[0] &&
				Policies[0]->IsA<UParadoxTemporalOrderingPolicy>());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxTemporalParadoxAcceptanceTest,
	"Paradox.TimeLoop.TemporalParadoxAcceptanceAndDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTemporalParadoxAcceptanceTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::TimeLoop::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxTemporalAcceptanceWorld"));
	if (!TestNotNull(TEXT("Transient test world exists"), TestWorld.World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxCloneCharacter* Observer =
		TestWorld.World->SpawnActor<AParadoxCloneCharacter>(
			AParadoxCloneCharacter::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AParadoxPlayerCharacter* Target =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform(FVector(100.0f, 0.0f, 0.0f)),
			SpawnParameters);
	AActor* Coordinator = TestWorld.World->SpawnActor<AActor>();
	AParadoxChronoSpawn* Spawn = SpawnChronoSpawn(
		*TestWorld.World,
		FVector::ZeroVector,
		TEXT("ParadoxRetryChronoSpawn"));
	if (!TestNotNull(TEXT("Temporal observer exists"), Observer)
		|| !TestNotNull(TEXT("Temporal target exists"), Target)
		|| !TestNotNull(TEXT("Coordinator exists"), Coordinator)
		|| !TestNotNull(TEXT("Retry Chrono Spawn exists"), Spawn))
	{
		return false;
	}

	UParadoxTimeLoopComponent* TimeLoop =
		NewObject<UParadoxTimeLoopComponent>(
			Coordinator,
			TEXT("TemporalAcceptanceTimeLoop"),
			RF_Transient);
	Coordinator->AddInstanceComponent(TimeLoop);
	TimeLoop->RegisterComponent();
	TestWorld.StartPlay();
	for (AActor* Actor : { static_cast<AActor*>(Observer),
			static_cast<AActor*>(Target),
			Coordinator,
			static_cast<AActor*>(Spawn) })
	{
		if (Actor && !Actor->HasActorBegunPlay())
		{
			Actor->DispatchBeginPlay();
		}
	}

	TestTrue(
		TEXT("Observer Entity Relations identity is registered"),
		Observer->GetEntityIdentityComponent()->GetEntityId().IsValid());
	TestTrue(
		TEXT("Target Entity Relations identity is registered"),
		Target->GetEntityIdentityComponent()->GetEntityId().IsValid());
	TestTrue(
		TEXT("Observer receives T0"),
		Observer->GetTemporalEntityComponent()->AssignPlayer(0));
	TestTrue(
		TEXT("Target receives T1"),
		Target->GetTemporalEntityComponent()->AssignPlayer(1));

	FString Failure;
	TestTrue(
		TEXT("World State baseline is available for immediate headless recovery"),
		FParadoxTimeLoopTestAccessor::PrepareWorldState(*TimeLoop, Failure));
	TestTrue(
		TEXT("Temporal Entity Relations policy is configured"),
		FParadoxTimeLoopTestAccessor::ConfigureEntityRelations(
			*TimeLoop,
			Failure));
	const TArray<AParadoxChronoSpawn*> Spawns = { Spawn };
	FParadoxTimeLoopTestAccessor::ConfigureActiveRun(
		*TimeLoop,
		*Target,
		Spawns,
		*Spawn);
	FParadoxTimeLoopTestAccessor::ConfigureTemporalEvaluation(
		*TimeLoop,
		*Target,
		101);

	FParadoxTemporalOverlapSnapshot Candidate;
	Candidate.Observer = Observer;
	Candidate.ObserverComponent = Observer->GetTemporalVisionComponent();
	Candidate.Target = Target;
	Candidate.TargetComponent = Target->GetCapsuleComponent();
	Candidate.ObserverTemporalIndex = 0;
	Candidate.TargetTemporalIndex = 1;
	Candidate.OverlappingComponentCount = 1;
	Candidate.DetectionSessionId = 101;
	Candidate.bDetectionAuthoritative = true;
	Candidate.ObserverLocation = Observer->GetActorLocation();
	Candidate.TargetLocation = Target->GetActorLocation();
	FParadoxTimeLoopTestAccessor::SubmitTemporalOverlap(
		*TimeLoop,
		Candidate);

	const FParadoxContext AcceptedContext =
		TimeLoop->GetLastParadoxContext();
	TestTrue(
		TEXT("T0 observing T1 creates a complete paradox context"),
		AcceptedContext.IsValid());
	TestEqual(
		TEXT("Paradox context retains the physical observer component"),
		AcceptedContext.ObserverComponent.Get(),
		Candidate.ObserverComponent.Get());
	TestEqual(
		TEXT("Paradox context retains the physical target component"),
		AcceptedContext.TargetComponent.Get(),
		Candidate.TargetComponent.Get());
	TestTrue(
		TEXT("Paradox context retains the Entity Relations outcome"),
		AcceptedContext.RelationResult.OutcomeTags.HasTagExact(
			ParadoxGameplayTags::Relation_Outcome_FutureObserved));
	TestEqual(
		TEXT("Headless paradox recovery returns to selection"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ChronoSpawnSelection);
	TestEqual(
		TEXT("Failed run leaves its Chrono Spawn retryable"),
		Spawn->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Available);
	TestEqual(
		TEXT("Failed run does not consolidate a partial track"),
		TimeLoop->GetConsolidatedTimelineCount(),
		0);

	TestTrue(
		TEXT("Observer can be reassigned to T1"),
		Observer->GetTemporalEntityComponent()->AssignPlayer(1));
	TestTrue(
		TEXT("Target can be reassigned to T0"),
		Target->GetTemporalEntityComponent()->AssignPlayer(0));
	FParadoxTimeLoopTestAccessor::ConfigureActiveRun(
		*TimeLoop,
		*Target,
		Spawns,
		*Spawn);
	FParadoxTimeLoopTestAccessor::ConfigureTemporalEvaluation(
		*TimeLoop,
		*Target,
		102);
	Candidate.ObserverTemporalIndex = 1;
	Candidate.TargetTemporalIndex = 0;
	Candidate.DetectionSessionId = 102;
	FParadoxTimeLoopTestAccessor::SubmitTemporalOverlap(
		*TimeLoop,
		Candidate);

	TestEqual(
		TEXT("T1 observing T0 remains in ActiveRun"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ActiveRun);
	TestEqual(
		TEXT("Reverse temporal direction is diagnosed as safe"),
		TimeLoop->GetLastTemporalCandidate().Disposition,
		EParadoxTemporalCandidateDisposition::SafeTemporalOrder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxFinalTimelineGameOverTest,
	"Paradox.TimeLoop.FinalTimelineConsolidatesThenGameOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxFinalTimelineGameOverTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::TimeLoop::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxFinalTimelineWorld"));
	if (!TestNotNull(TEXT("Transient test world exists"), TestWorld.World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxPlayerCharacter* Player =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AActor* Coordinator = TestWorld.World->SpawnActor<AActor>();
	AParadoxChronoSpawn* Spawn = SpawnChronoSpawn(
		*TestWorld.World,
		FVector::ZeroVector,
		TEXT("FinalChronoSpawn"));
	if (!TestNotNull(TEXT("Player exists"), Player)
		|| !TestNotNull(TEXT("Coordinator exists"), Coordinator)
		|| !TestNotNull(TEXT("Final Chrono Spawn exists"), Spawn))
	{
		return false;
	}

	UParadoxTimeLoopComponent* TimeLoop =
		NewObject<UParadoxTimeLoopComponent>(
			Coordinator,
			TEXT("FinalTimelineTimeLoop"),
			RF_Transient);
	Coordinator->AddInstanceComponent(TimeLoop);
	TimeLoop->RegisterComponent();
	TestWorld.StartPlay();

	const TArray<AParadoxChronoSpawn*> Spawns = { Spawn };
	FParadoxTimeLoopTestAccessor::ConfigureActiveRun(
		*TimeLoop,
		*Player,
		Spawns,
		*Spawn);
	TestTrue(
		TEXT("Player receives final temporal index"),
		Player->GetTemporalEntityComponent()->AssignPlayer(0));
	FIntentRecordingOptions RecordingOptions;
	RecordingOptions.SourceLabel = TEXT("ParadoxAutomationFinalTimeline");
	TestTrue(
		TEXT("Final run recording starts"),
		Player->GetIntentReplayComponent()
			->StartRecording(RecordingOptions)
			.Succeeded());

	const FParadoxTimeLoopOperationResult Result =
		TimeLoop->RequestTimeRewind();
	TestEqual(
		TEXT("Final rewind reports Game Over"),
		Result.Status,
		EParadoxTimeLoopOperationStatus::GameOverReached);
	TestEqual(
		TEXT("Final rewind enters GameOver"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::GameOver);
	TestEqual(
		TEXT("Final track remains consolidated"),
		TimeLoop->GetConsolidatedTimelineCount(),
		1);
	TestEqual(
		TEXT("Final Chrono Spawn remains occupied"),
		Spawn->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Occupied);
	TestTrue(
		TEXT("Game Over context has a stable event ID"),
		TimeLoop->GetLastGameOverContext().EventId.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxExternalLevelCompleteTest,
	"Paradox.TimeLoop.ExternalLevelComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxExternalLevelCompleteTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::TimeLoop::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxLevelCompleteWorld"));
	if (!TestNotNull(TEXT("Transient test world exists"), TestWorld.World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxPlayerCharacter* Player =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AActor* Coordinator = TestWorld.World->SpawnActor<AActor>();
	AParadoxChronoSpawn* Spawn = SpawnChronoSpawn(
		*TestWorld.World,
		FVector::ZeroVector,
		TEXT("CompleteChronoSpawn"));
	if (!TestNotNull(TEXT("Player exists"), Player)
		|| !TestNotNull(TEXT("Coordinator exists"), Coordinator)
		|| !TestNotNull(TEXT("Chrono Spawn exists"), Spawn))
	{
		return false;
	}

	UParadoxTimeLoopComponent* TimeLoop =
		NewObject<UParadoxTimeLoopComponent>(
			Coordinator,
			TEXT("LevelCompleteTimeLoop"),
			RF_Transient);
	Coordinator->AddInstanceComponent(TimeLoop);
	TimeLoop->RegisterComponent();
	TestWorld.StartPlay();
	const TArray<AParadoxChronoSpawn*> Spawns = { Spawn };
	FParadoxTimeLoopTestAccessor::ConfigureActiveRun(
		*TimeLoop,
		*Player,
		Spawns,
		*Spawn);
	TestTrue(
		TEXT("Player receives temporal index zero"),
		Player->GetTemporalEntityComponent()->AssignPlayer(0));
	FIntentRecordingOptions RecordingOptions;
	RecordingOptions.SourceLabel = TEXT("ParadoxAutomationLevelComplete");
	TestTrue(
		TEXT("Active run recording starts"),
		Player->GetIntentReplayComponent()
			->StartRecording(RecordingOptions)
			.Succeeded());

	const FParadoxTimeLoopOperationResult Result =
		TimeLoop->RequestLevelComplete();
	TestEqual(
		TEXT("External completion reports Level Complete"),
		Result.Status,
		EParadoxTimeLoopOperationStatus::LevelCompleteReached);
	TestEqual(
		TEXT("External completion enters LevelComplete"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::LevelComplete);
	TestEqual(
		TEXT("Partial active run is not consolidated"),
		TimeLoop->GetConsolidatedTimelineCount(),
		0);
	TestTrue(
		TEXT("Level Complete context has a stable event ID"),
		TimeLoop->GetLastLevelCompleteContext().EventId.IsValid());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
