#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GridMoveToCellActionDefinition.h"
#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Camera/ParadoxCameraBoundsVolume.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Components/EntityIdentityComponent.h"
#include "Conditions/PuzzleInputStateCondition.h"
#include "Controllers/PuzzleController.h"
#include "Controllers/ParadoxCloneController.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Data/EntityRelationPolicySet.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "EntityRelationTags.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/ParadoxGameMode.h"
#include "Health/ParadoxHealthComponent.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Inventory/ParadoxDropAction.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationQueryFilter.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Oxygen/ParadoxOxygenComponent.h"
#include "Oxygen/ParadoxOxygenDepletionDamageType.h"
#include "Paradox.h"
#include "Perception/ParadoxTemporalVisionComponent.h"
#include "Playback/ParadoxCloneReplayExecutionStrategy.h"
#include "Recording/IntentReplayTrack.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Relations/ParadoxTemporalOrderingPolicy.h"
#include "SmartObjectComponent.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Tests/ParadoxTimeLoopTestTypes.h"
#include "TimeLoop/ParadoxChronoSpawn.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"
#include "TimerManager.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"

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
		TimeLoop.CurrentPhase = EParadoxTimeLoopPhase::RunPreparation;
		TimeLoop.SetPhase(EParadoxTimeLoopPhase::ActiveRun);
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
		TimeLoop.bRunFailureAcceptedForRun = false;
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

	static void ConfigureCloneDeparture(
		UParadoxTimeLoopComponent& TimeLoop,
		AParadoxCloneCharacter& Clone)
	{
		TimeLoop.bTimeLoopEnabled = true;
		TimeLoop.RuntimeClones.Add(&Clone);
		TimeLoop.CurrentPhase = EParadoxTimeLoopPhase::RunPreparation;
		TimeLoop.SetPhase(EParadoxTimeLoopPhase::ActiveRun);
	}

	static EParadoxCloneTimeTravelCompletionBehavior
	GetCloneTimeTravelCompletionBehavior(
		const UParadoxTimeLoopComponent& TimeLoop)
	{
		return TimeLoop.CloneTimeTravelCompletionBehavior;
	}

	static void SetCloneTimeTravelCompletionBehavior(
		UParadoxTimeLoopComponent& TimeLoop,
		const EParadoxCloneTimeTravelCompletionBehavior Behavior)
	{
		TimeLoop.CloneTimeTravelCompletionBehavior = Behavior;
	}

	static bool IsBarrierHeldByCloneReadiness(
		UParadoxTimeLoopComponent& TimeLoop)
	{
		TimeLoop.CurrentPhase = EParadoxTimeLoopPhase::AwaitingSynchronizedStart;
		TimeLoop.ClonePlaybackRuntimes.Reset();
		FParadoxClonePlaybackRuntime& Runtime =
			TimeLoop.ClonePlaybackRuntimes.AddDefaulted_GetRef();
		Runtime.State = EParadoxClonePlaybackState::Preparing;
		TimeLoop.TryReleaseSynchronizedStart();
		return TimeLoop.CurrentPhase
			== EParadoxTimeLoopPhase::AwaitingSynchronizedStart;
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
				GameInstance = NewObject<UGameInstance>(GEngine);
				Context->OwningGameInstance = GameInstance;
				World->SetGameInstance(GameInstance);
			}
			if (World)
			{
				// Clone reconstruction now starts the authored Behavior Tree and semantic
				// listener, both of which require the AI system present in a gameplay world.
				World->CreateAISystem();
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

		void StartPlay(
			const TSubclassOf<AGameModeBase> GameModeClass = nullptr) const
		{
			if (GameModeClass && World && World->GetWorldSettings())
			{
				World->GetWorldSettings()->DefaultGameMode = GameModeClass;
			}
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		void Advance(const float DeltaSeconds) const
		{
			if (World)
			{
				++GFrameCounter;
				World->GetTimerManager().Tick(DeltaSeconds);
			}
		}

		void AdvanceWorld(const float DeltaSeconds) const
		{
			if (World)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, DeltaSeconds);
			}
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;
	};

	AParadoxChronoSpawn* SpawnChronoSpawn(
		UWorld& World,
		const FVector& Location,
		const FName Name,
		UClass* SpawnClass = AParadoxChronoSpawn::StaticClass())
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode =
			FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AParadoxChronoSpawn* ChronoSpawn =
			World.SpawnActor<AParadoxChronoSpawn>(
				SpawnClass,
			FTransform(Location),
			Parameters);
		if (ChronoSpawn)
		{
			// Time-loop scenarios model level-authored spawn points inside a transient World.
			// The standard interaction request intentionally rejects genuinely runtime-created
			// targets, so preserve the authored provenance represented by this fixture.
			ChronoSpawn->SetFlags(RF_WasLoaded);
		}
		return ChronoSpawn;
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
		TestEqual(
			TEXT("Replay clone Time Travel enters GOAP by default"),
			FParadoxTimeLoopTestAccessor::GetCloneTimeTravelCompletionBehavior(
				*Defaults),
			EParadoxCloneTimeTravelCompletionBehavior::EnterGoap);
		TestFalse(
			TEXT("Time-loop component has no per-frame tick"),
			Defaults->PrimaryComponentTick.bCanEverTick);
		TestTrue(
			TEXT("Disabled time loop preserves existing movement"),
			Defaults->IsMovementAllowed());
		TestNull(
			TEXT("post-rewind delay configuration was removed"),
			FindFProperty<FProperty>(
				UParadoxTimeLoopComponent::StaticClass(),
				TEXT("PostRewindReplayStartDelaySeconds")));
	}

	UParadoxTimeLoopComponent* InitialSelectionProbe =
		NewObject<UParadoxTimeLoopComponent>();
	FParadoxTimeLoopTestAccessor::ConfigureCapacityState(
		*InitialSelectionProbe,
		3,
		EParadoxTimeLoopPhase::ChronoSpawnSelection);
	TestTrue(
		TEXT("first Chrono Spawn selection remains open and mandatory"),
		InitialSelectionProbe->IsChronoSpawnSelectionOpen());
	TestFalse(
		TEXT("first Chrono Spawn selection still gates movement"),
		InitialSelectionProbe->IsMovementAllowed());

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

	UParadoxTimeLoopComponent* BarrierProbe =
		NewObject<UParadoxTimeLoopComponent>();
	TestTrue(
		TEXT("Synchronized barrier waits while a clone is still preparing"),
		FParadoxTimeLoopTestAccessor::IsBarrierHeldByCloneReadiness(
			*BarrierProbe));
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
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerAstronaut.BP_PlayerAstronaut_C"));
	if (!TestNotNull(TEXT("Project player Blueprint loads"), PlayerClass))
	{
		return false;
	}
	UClass* PlayerControllerClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerController.BP_PlayerController_C"));
	UClass* GameModeClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Logic/BP_TimeLoopGameMode.BP_TimeLoopGameMode_C"));
	if (!TestNotNull(
		TEXT("Project player controller Blueprint loads"),
		PlayerControllerClass)
		|| !TestTrue(
			TEXT("Project player controller uses AParadoxPlayerController"),
			PlayerControllerClass
				&& PlayerControllerClass->IsChildOf(
					AParadoxPlayerController::StaticClass()))
		|| !TestNotNull(TEXT("Project Game Mode Blueprint loads"), GameModeClass)
		|| !TestTrue(
			TEXT("Project Game Mode uses AParadoxGameMode"),
			GameModeClass
				&& GameModeClass->IsChildOf(AParadoxGameMode::StaticClass())))
	{
		return false;
	}
	UClass* CloneClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_CloneAstronaut.BP_CloneAstronaut_C"));
	UClass* CloneControllerClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_CloneController.BP_CloneController_C"));
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
	FActorSpawnParameters PlayerControllerParameters =
		PlayerParameters;
	PlayerControllerParameters.Name =
		TEXT("TimeLoopTestPlayerController");
	AParadoxPlayerController* PlayerController =
		TestWorld.World->SpawnActor<AParadoxPlayerController>(
			PlayerControllerClass,
			FTransform::Identity,
			PlayerControllerParameters);
	AParadoxCameraBoundsVolume* CameraBounds =
		TestWorld.World->SpawnActor<AParadoxCameraBoundsVolume>();
	AActor* Coordinator = TestWorld.World->SpawnActor<AActor>();
	AParadoxChronoSpawn* Spawn0 = SpawnChronoSpawn(
		*TestWorld.World,
		FVector(0.0, 0.0, 0.0),
		TEXT("ChronoSpawn_0"),
		AParadoxChronoSpawnStateInitializationProbe::StaticClass());
	AParadoxChronoSpawn* Spawn1 = SpawnChronoSpawn(
		*TestWorld.World,
		FVector(200.0, 0.0, 0.0),
		TEXT("ChronoSpawn_1"));
	AParadoxChronoSpawn* Spawn2 = SpawnChronoSpawn(
		*TestWorld.World,
		FVector(400.0, 0.0, 0.0),
		TEXT("ChronoSpawn_2"));
	AActor* SpawnEmitterActor = TestWorld.World->SpawnActor<AActor>();
	UPuzzleEmitterComponent* SpawnEmitter = SpawnEmitterActor
		? NewObject<UPuzzleEmitterComponent>(
			SpawnEmitterActor,
			TEXT("ChronoSpawnEmitter"))
		: nullptr;
	if (SpawnEmitterActor && SpawnEmitter)
	{
		SpawnEmitterActor->AddInstanceComponent(SpawnEmitter);
		SpawnEmitter->RegisterComponent();
		SpawnEmitter->SetSignalState(
			ParadoxGameplayTags::Puzzle_Signal_Pressed,
			true,
			nullptr);
	}
	APuzzleController* SpawnPuzzleController =
		TestWorld.World->SpawnActor<APuzzleController>();
	if (SpawnPuzzleController && SpawnEmitterActor && Spawn0)
	{
		FPuzzleInputBinding& Input =
			SpawnPuzzleController->InputBindings.AddDefaulted_GetRef();
		Input.InputId = TEXT("SpawnEnabled");
		Input.EmitterActor = SpawnEmitterActor;
		Input.SignalTag = ParadoxGameplayTags::Puzzle_Signal_Pressed;
		FPuzzleReceiverBinding& Receiver =
			SpawnPuzzleController->ReceiverBindings.AddDefaulted_GetRef();
		Receiver.ReceiverActor = Spawn0;
		UPuzzleInputStateCondition* Condition =
			NewObject<UPuzzleInputStateCondition>(SpawnPuzzleController);
		Condition->InputId = Input.InputId;
		SpawnPuzzleController->RootCondition = Condition;
	}
	if (!TestNotNull(TEXT("Player spawned"), Player)
		|| !TestNotNull(
			TEXT("Player Controller with perception Listener spawned"),
			PlayerController)
		|| !TestNotNull(TEXT("Camera Bounds spawned"), CameraBounds)
		|| !TestNotNull(TEXT("Coordinator spawned"), Coordinator)
		|| !TestNotNull(TEXT("Chrono Spawn 0 spawned"), Spawn0)
		|| !TestNotNull(TEXT("Chrono Spawn 1 spawned"), Spawn1)
		|| !TestNotNull(TEXT("Chrono Spawn 2 spawned"), Spawn2)
		|| !TestNotNull(TEXT("Chrono Spawn Emitter spawned"), SpawnEmitter)
		|| !TestNotNull(
			TEXT("Chrono Spawn Puzzle Controller spawned"),
			SpawnPuzzleController))
	{
		return false;
	}
	AParadoxChronoSpawnStateInitializationProbe* Spawn0InitializationProbe =
		Cast<AParadoxChronoSpawnStateInitializationProbe>(Spawn0);
	if (!TestNotNull(
		TEXT("Chrono Spawn initialization probe exists"),
		Spawn0InitializationProbe))
	{
		return false;
	}
	PlayerController->Possess(Player);

	TestWorld.StartPlay(GameModeClass);
	AParadoxGameMode* GameMode =
		TestWorld.World->GetAuthGameMode<AParadoxGameMode>();
	UParadoxTimeLoopComponent* TimeLoop = GameMode
		? GameMode->GetTimeLoopComponent()
		: nullptr;
	if (!TestNotNull(TEXT("Paradox Game Mode exists"), GameMode)
		|| !TestNotNull(TEXT("Authoritative Time Loop exists"), TimeLoop))
	{
		return false;
	}
	FParadoxTimeLoopTestAccessor::ConfigureCloneClasses(
		*TimeLoop,
		CloneClass,
		CloneControllerClass);

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
	TestEqual(
		TEXT("Chrono Spawn initialization hook runs once after initial state reconciliation"),
		Spawn0InitializationProbe->GetInitializationCount(),
		1);
	TestEqual(
		TEXT("Initial Chrono Spawn hook receives the active free state"),
		Spawn0InitializationProbe->GetLastInitializedState(),
		EParadoxChronoSpawnState::Available);

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
	TestTrue(
		TEXT("Emitter-linked Chrono Spawn starts active from its Receiver"),
		Spawn0->GetPuzzleReceiverComponent()
			&& Spawn0->GetPuzzleReceiverComponent()->IsReceiverActive()
			&& Spawn0->IsChronoSpawnActive());
	TestTrue(
		TEXT("Chrono Spawn without incoming Emitters starts active"),
		Spawn1->IsChronoSpawnActive());
	TestNotNull(
		TEXT("Chrono Spawn owns the non-spatial Interaction Component"),
		Spawn0->GetInteractionComponent());
	TestNull(
		TEXT("Chrono Spawn requires no Smart Object Component"),
		Spawn0->FindComponentByClass<USmartObjectComponent>());
	if (UParadoxInteractionComponent* SpawnInteraction =
		Spawn0->GetInteractionComponent())
	{
		const FParadoxInteractionAvailabilityResult Availability =
			SpawnInteraction->EvaluateInteractionAvailability(
				Player,
				ParadoxGameplayTags::Interaction_ChronoSpawn_Spawn);
		TestEqual(
			TEXT("Active free Chrono Spawn exposes an in-place Spawn button"),
			Availability.Status,
			EParadoxInteractionAvailabilityStatus::AvailableInPlace);
	}

	TestTrue(
		TEXT("Initial Chrono Spawn selection starts timeline zero"),
		TimeLoop->RequestChronoSpawnInteraction(Spawn0).IsSuccess());
	const FTransform Spawn0BaselineTransform = Spawn0->GetActorTransform();
	Spawn0->SetActorLocation(FVector(900.0, 300.0, 100.0));
	Spawn0->SetChronoSpawnEnabled(false);
	const FPerceptionKnowledgeEntityId TimelineZeroPerceptionId =
		Player->GetPerceptionKnowledgeSourceComponent()->GetEntityId();
	TestTrue(
		TEXT("Original timeline zero player has a valid Perception identity"),
		TimelineZeroPerceptionId.IsValid());

	const FParadoxTimeLoopOperationResult RewindResult =
		TimeLoop->RequestTimeRewind();
	TestTrue(TEXT("Rewind succeeds"), RewindResult.IsSuccess());
	UTacticalPauseWorldSubsystem* TacticalPause =
		TestWorld.World->GetSubsystem<UTacticalPauseWorldSubsystem>();
	TestNotNull(TEXT("Tactical Pause subsystem exists"), TacticalPause);
	TestEqual(
		TEXT("Ready clones release the technical barrier without a delay"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ActiveRun);
	TestTrue(
		TEXT("Post-rewind flow forces Tactical Pause"),
		TacticalPause && TacticalPause->IsPaused());
	TestTrue(
		TEXT("Forced Tactical Pause remains resumable through Play"),
		TacticalPause && TacticalPause->CanPlay());
	TestTrue(
		TEXT("Runtime Chrono Spawn selection opens during forced pause"),
		TimeLoop->IsChronoSpawnSelectionOpen());
	TestFalse(
		TEXT("Player movement remains gated while runtime selection is open"),
		TimeLoop->IsMovementAllowed());
	TestEqual(
		TEXT("One immutable timeline is consolidated"),
		TimeLoop->GetConsolidatedTimelineCount(),
		1);
	TestEqual(
		TEXT("Selected Chrono Spawn becomes occupied"),
		Spawn0->GetChronoSpawnState(),
		EParadoxChronoSpawnState::Occupied);
	TestTrue(
		TEXT("Chrono Spawn World State restores the enabled baseline"),
		Spawn0->IsChronoSpawnEnabled());
	TestTrue(
		TEXT("Chrono Spawn World State restores the authored transform"),
		Spawn0->GetActorTransform().Equals(Spawn0BaselineTransform, 0.01f));
	TestEqual(
		TEXT("Chrono Spawn initialization hook runs again after world reset"),
		Spawn0InitializationProbe->GetInitializationCount(),
		2);
	TestEqual(
		TEXT("Post-reset initialization observes final timeline occupation"),
		Spawn0InitializationProbe->GetLastInitializedState(),
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
			TEXT("Timeline retains the original avatar Perception identity"),
			Timelines[0].AvatarPerceptionEntityId,
			TimelineZeroPerceptionId);
		TestEqual(
			TEXT("Finalized recording contains the Chrono Spawn intent"),
			Timelines[0].ReplayTrack->GetEntryCount(),
			1);
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
		if (const UPerceptionKnowledgeSourceComponent* CloneSource =
			Clone->GetPerceptionKnowledgeSourceComponent())
		{
			TestEqual(
				TEXT("Reconstructed clone inherits the original player Perception identity"),
				CloneSource->GetEntityId(),
				TimelineZeroPerceptionId);
			TestFalse(
				TEXT("Reconstructed clone keeps its inherited Perception identity dormant until the recorded spawn action"),
				CloneSource->IsSemanticallyRegistered());
		}
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
		TestNotNull(
			TEXT("Post-rewind flow prepares replay before player spawn selection"),
			Clone->GetIntentReplayComponent()->GetActivePlaybackSession());
		TestEqual(
			TEXT("Reconstructed Blueprint clone uses the path-adapting replay strategy"),
			Clone->GetIntentReplayComponent()->ExecutionStrategyClass.Get(),
			UParadoxCloneReplayExecutionStrategy::StaticClass());
		TestEqual(
			TEXT("Clone playback is authorized while the World remains paused"),
			TimeLoop->GetCurrentPhase(),
			EParadoxTimeLoopPhase::ActiveRun);
		TestFalse(
			TEXT("Clone is not controlled by the player"),
			Clone->IsPlayerControlled());
		FParadoxClonePlaybackSnapshot DormantSnapshot;
		if (TestTrue(
			TEXT("Dormant clone playback snapshot is available"),
			TimeLoop->GetClonePlaybackSnapshot(0, DormantSnapshot)))
		{
			TestEqual(
				TEXT("Clone awaits its recorded Chrono Spawn time"),
				DormantSnapshot.TemporalSpawnState,
				EParadoxTemporalSpawnState::WaitingForRecordedTime);
		}
	}

	TestTrue(
		TEXT("Available Chrono Spawn uses the generic selectable path"),
		Spawn1->GetSelectableComponent()
			&& Spawn1->GetSelectableComponent()->bCanBeHovered
			&& Spawn1->GetSelectableComponent()->bCanBeSelected);

	const FParadoxTimeLoopOperationResult OccupiedSelection =
		TimeLoop->RequestChronoSpawnInteraction(Spawn0);
	TestEqual(
		TEXT("Occupied Chrono Spawn selection is rejected"),
		OccupiedSelection.Status,
		EParadoxTimeLoopOperationStatus::InvalidChronoSpawn);
	TestEqual(
		TEXT("Rejected selection preserves selection phase"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ActiveRun);
	if (UParadoxInteractionComponent* OccupiedInteraction =
		Spawn0->GetInteractionComponent())
	{
		TestFalse(
			TEXT("Occupied Chrono Spawn disables its Spawn interaction"),
			OccupiedInteraction->CanRequestInteraction(
				Player,
				ParadoxGameplayTags::Interaction_ChronoSpawn_Spawn));
	}

	const FParadoxTimeLoopOperationResult SecondRun =
		TimeLoop->RequestChronoSpawnInteraction(Spawn1);
	TestTrue(TEXT("Second run selects an available spawn during Tactical Pause"), SecondRun.IsSuccess());
	TestEqual(
		TEXT("Selecting during Tactical Pause keeps the run active"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ActiveRun);
	TestTrue(
		TEXT("Selected player is gameplay-ready but held by World pause"),
		TimeLoop->IsMovementAllowed());
	TestTrue(
		TEXT("Selecting a spawn does not implicitly resume Tactical Pause"),
		TacticalPause && TacticalPause->IsPaused());
	TestEqual(
		TEXT("Second player run receives temporal index one"),
		Player->GetTemporalEntityComponent()->GetTemporalIndex(),
		1);
	const FPerceptionKnowledgeEntityId TimelineOnePerceptionId =
		Player->GetPerceptionKnowledgeSourceComponent()->GetEntityId();
	TestTrue(
		TEXT("Second player run receives a valid fresh Perception identity"),
		TimelineOnePerceptionId.IsValid());
	TestNotEqual(
		TEXT("Live player identity is distinct from reconstructed timeline zero"),
		TimelineOnePerceptionId,
		TimelineZeroPerceptionId);
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
	TestTrue(
		TEXT("Emitter can make the occupied timeline-zero Chrono Spawn inactive"),
		SpawnEmitter->SetSignalState(
			ParadoxGameplayTags::Puzzle_Signal_Pressed,
			false,
			nullptr));
	TestFalse(
		TEXT("Inactive Receiver prevents Chrono Spawn assignment"),
		Spawn0->IsChronoSpawnActive());
	TestTrue(
		TEXT("Inactive Chrono Spawn remains selectable for connection inspection"),
		Spawn0->GetSelectableComponent()->bCanBeSelected);
	TestEqual(
		TEXT("Play resumes the selected run"),
		TacticalPause ? TacticalPause->RequestPlay()
			: ETacticalPauseRequestResult::InvalidWorld,
		ETacticalPauseRequestResult::Succeeded);
	TestFalse(
		TEXT("World is no longer paused after Play"),
		TacticalPause && TacticalPause->IsPaused());
	for (int32 TickIndex = 0; TickIndex < 5; ++TickIndex)
	{
		TestWorld.AdvanceWorld(0.02f);
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
		TestTrue(
			TEXT("Prepared empty replay does not block the player run while its Behavior Tree owns execution"),
			PlaybackSnapshot.State == EParadoxClonePlaybackState::Ready
				|| PlaybackSnapshot.State
					== EParadoxClonePlaybackState::Playing
				|| PlaybackSnapshot.State
					== EParadoxClonePlaybackState::Completed);
		TestEqual(
			TEXT("Replay retains its Chrono Spawn source entry"),
			PlaybackSnapshot.TotalEntryCount,
			1);
		TestEqual(
			TEXT("Inactive Chrono Spawn keeps the recorded materialization pending"),
			PlaybackSnapshot.TemporalSpawnState,
			EParadoxTemporalSpawnState::PendingActivation);
		if (!Clones.IsEmpty() && Clones[0])
		{
			const UPerceptionKnowledgeSourceComponent* CloneSource =
				Clones[0]->GetPerceptionKnowledgeSourceComponent();
			TestFalse(
				TEXT("Pending clone remains absent from Perception"),
				CloneSource && CloneSource->IsSemanticallyRegistered());
			TestEqual(
				TEXT("Only the pending clone replay pauses at the missed spawn time"),
				Clones[0]->GetIntentReplayComponent()->GetPlaybackState(),
				EIntentReplayPlaybackState::Paused);
		}
	}
	TestTrue(
		TEXT("A later Emitter activation satisfies the pending spawn"),
		SpawnEmitter->SetSignalState(
			ParadoxGameplayTags::Puzzle_Signal_Pressed,
			true,
			nullptr));
	TestTrue(
		TEXT("Receiver activation re-enables the Chrono Spawn"),
		Spawn0->IsChronoSpawnActive());
	TestTrue(
		TEXT("An active occupied Chrono Spawn remains selectable for connection inspection"),
		Spawn0->IsAssignedToTimeline()
			&& !Spawn0->CanAssignToNewTimeline()
			&& Spawn0->GetSelectableComponent()->bCanBeSelected);
	if (TestTrue(
		TEXT("Clone playback remains queryable after pending activation"),
		TimeLoop->GetClonePlaybackSnapshot(0, PlaybackSnapshot)))
	{
		TestEqual(
			TEXT("Pending clone materializes when its Chrono Spawn becomes active"),
			PlaybackSnapshot.TemporalSpawnState,
			EParadoxTemporalSpawnState::Materialized);
		if (!Clones.IsEmpty() && Clones[0])
		{
			const UPerceptionKnowledgeSourceComponent* CloneSource =
				Clones[0]->GetPerceptionKnowledgeSourceComponent();
			TestTrue(
				TEXT("Materialized clone republishes its inherited Perception identity"),
				CloneSource && CloneSource->IsSemanticallyRegistered());
			TestTrue(
				TEXT("Activating the Chrono Spawn resumes its clone replay"),
				Clones[0]->GetIntentReplayComponent()->GetPlaybackState()
					!= EIntentReplayPlaybackState::Paused);
		}
	}

	const FParadoxTimeLoopOperationResult SecondRewind =
		TimeLoop->RequestTimeRewind();
	TestTrue(TEXT("Second rewind succeeds"), SecondRewind.IsSuccess());
	TestEqual(
		TEXT("Second reset releases immediately once clones are ready"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ActiveRun);
	TestTrue(
		TEXT("Every post-rewind run starts in Tactical Pause"),
		TacticalPause && TacticalPause->IsPaused());
	TestTrue(
		TEXT("Third timeline can select a spawn while clones are waiting"),
		TimeLoop->IsChronoSpawnSelectionOpen());
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
			if (Clone)
			{
				const UPerceptionKnowledgeSourceComponent* CloneSource =
					Clone->GetPerceptionKnowledgeSourceComponent();
				TestNotNull(
					*FString::Printf(
						TEXT("Clone %d has a Perception Source"),
						Index),
					CloneSource);
				if (CloneSource)
				{
					TestEqual(
						*FString::Printf(
							TEXT("Clone %d preserves its timeline Perception identity"),
							Index),
						CloneSource->GetEntityId(),
						TwoTimelines[Index].AvatarPerceptionEntityId);
				}
			}
			TestNotNull(
				*FString::Printf(TEXT("Clone %d is prepared before technical barrier release"), Index),
				Clone
					? Clone->GetIntentReplayComponent()->GetActivePlaybackSession()
					: nullptr);
		}
		TestNotEqual(
			TEXT("Timeline zero and timeline one retain distinct Perception identities"),
			TwoTimelines[0].AvatarPerceptionEntityId,
			TwoTimelines[1].AvatarPerceptionEntityId);
	}

	TestEqual(
		TEXT("Clones start without requiring a selected player spawn"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ActiveRun);
	TestTrue(
		TEXT("Runtime spawn selection remains open after clone replay starts"),
		TimeLoop->IsChronoSpawnSelectionOpen());
	TestFalse(
		TEXT("Hidden player movement remains gated during late selection"),
		TimeLoop->IsMovementAllowed());
	TestEqual(
		TEXT("Global player recording clock starts with clone replay"),
		Player->GetIntentReplayComponent()->GetRecordingState(),
		EIntentRecordingState::Recording);
	TestTrue(TEXT("Player remains hidden until late spawn selection"), Player->IsHidden());
	UPerceptionKnowledgeSourceComponent* HiddenPlayerSource =
		Player->GetPerceptionKnowledgeSourceComponent();
	TestTrue(
		TEXT("Hidden player cannot publish semantic observations"),
		HiddenPlayerSource
			&& !HiddenPlayerSource->IsSourceEnabled()
			&& !HiddenPlayerSource->IsSemanticallyRegistered());
	if (PlayerOccupancy)
	{
		TestFalse(
			TEXT("Hidden player does not occupy GridWorld during autonomous replay"),
			PlayerOccupancy->IsActive());
	}
	TestEqual(
		TEXT("Play may resume clones before the player selects a spawn"),
		TacticalPause ? TacticalPause->RequestPlay()
			: ETacticalPauseRequestResult::InvalidWorld,
		ETacticalPauseRequestResult::Succeeded);
	TestFalse(
		TEXT("Play removes the forced Tactical Pause"),
		TacticalPause && TacticalPause->IsPaused());
	TestTrue(
		TEXT("Play does not close late Chrono Spawn selection"),
		TimeLoop->IsChronoSpawnSelectionOpen());

	const FParadoxTimeLoopOperationResult LateSelection =
		TimeLoop->RequestChronoSpawnInteraction(Spawn2);
	TestTrue(TEXT("Player may select a spawn after replay already started"), LateSelection.IsSuccess());
	TestFalse(
		TEXT("Late selection closes runtime spawn selection"),
		TimeLoop->IsChronoSpawnSelectionOpen());
	TestTrue(
		TEXT("Late selection enables player movement immediately"),
		TimeLoop->IsMovementAllowed());
	TestFalse(TEXT("Late-selected player becomes visible"), Player->IsHidden());
	TestTrue(
		TEXT("Late-selected player republishes its semantic Source"),
		HiddenPlayerSource
			&& HiddenPlayerSource->IsSourceEnabled()
			&& HiddenPlayerSource->IsSemanticallyRegistered());
	if (PlayerOccupancy)
	{
		TestTrue(
			TEXT("Late-selected player immediately re-enters GridWorld occupancy"),
			PlayerOccupancy->IsActive());
	}
	TestEqual(
		TEXT("Late-selected player receives the next temporal index"),
		Player->GetTemporalEntityComponent()->GetTemporalIndex(),
		2);

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
	UClass* BalancedFilterClass = LoadObject<UClass>(
		nullptr,
		TEXT("/GridWorldSystem/AI/BP_GridQueryFilter_Balanced.BP_GridQueryFilter_Balanced_C"));
	if (!TestNotNull(
		TEXT("Balanced GridWorld filter loads for clone replay"),
		BalancedFilterClass))
	{
		return false;
	}

	FGridInjectedPath RecordedPath;
	const FGridInjectedPathValidationResult SourceStamp =
		GridWorld->CreateExactInjectedPath(
			SourceController,
			Cells,
			Cells.Last(),
			BalancedFilterClass,
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
	TestTrue(
		TEXT("Clone replay goal-contention override is enabled by default"),
		Strategy->bOverrideGoalContentionPolicy);
	TestEqual(
		TEXT("Clone replay defaults to redirecting at an occupied destination"),
		Strategy->GoalContentionPolicyOverride,
		EGridGoalContentionPolicy::RedirectOnCompletion);
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

	TArray<FGridCellId> RecordedDropCells = Cells;
	RecordedDropCells.Pop(EAllowShrinking::No);
	FGridInjectedPath RecordedDropPath;
	const FGridInjectedPathValidationResult DropPathStamp =
		GridWorld->CreateExactInjectedPath(
			SourceController,
			RecordedDropCells,
			RecordedDropCells.Last(),
			BalancedFilterClass,
			false,
			false,
			EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal,
			RecordedDropPath);
	if (!TestTrue(
		TEXT("Source controller stamps the recorded Drop approach path"),
		DropPathStamp.bIsValid))
	{
		AddError(DropPathStamp.DiagnosticMessage);
		return false;
	}
	UParadoxDropActionDefinition* DropDefinition =
		NewObject<UParadoxDropActionDefinition>(TestWorld.World);
	DropDefinition->InstanceClass = UParadoxTimeLoopReplayProbeAction::StaticClass();
	DropDefinition->ExecutionLocks.Reset();
	DropDefinition->DefaultParameters.SetValueStruct(
		ParadoxDropActionParameters::TargetCell,
		Cells.Last());
	DropDefinition->DefaultParameters.SetValueEnum(
		ParadoxDropActionParameters::PathSource,
		EGridMovePathSource::ExactInjectedPath);
	DropDefinition->DefaultParameters.SetValueStruct(
		ParadoxDropActionParameters::InjectedPath,
		RecordedDropPath);
	const FGameplayActionRequestCreationResult DropCreation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(DropDefinition);
	if (!TestTrue(TEXT("Replay Drop request is created"), DropCreation.WasCreated()))
	{
		return false;
	}
	const FGameplayActionSubmissionResult DropSubmission =
		Strategy->SubmitPreparedRequest(ActionComponent, DropCreation.Request);
	if (!TestTrue(
		TEXT("Clone replay accepts a Drop request with its recorded exact approach"),
		DropSubmission.IsAccepted()))
	{
		AddError(DropSubmission.DiagnosticMessage);
		return false;
	}
	const FGameplayActionEvent* DropAcceptedEvent =
		EventObserver->ObservedEvents.FindByPredicate(
			[&DropSubmission](const FGameplayActionEvent& Event)
			{
				return Event.Handle == DropSubmission.Handle
					&& Event.EventType == EGameplayActionEventType::Accepted;
			});
	if (TestNotNull(
		TEXT("Clone Drop emits an immutable accepted snapshot"),
		DropAcceptedEvent))
	{
		const TValueOrError<FStructView, EPropertyBagResult> RuntimePathValue =
			DropAcceptedEvent->GetParameters().GetValueStruct(
				ParadoxDropActionParameters::InjectedPath,
				FGridInjectedPath::StaticStruct());
		const FGridInjectedPath* RuntimeDropPath = RuntimePathValue.HasValue()
			? RuntimePathValue.GetValue().GetPtr<FGridInjectedPath>()
			: nullptr;
		if (TestNotNull(TEXT("Runtime clone Drop retains an exact path"), RuntimeDropPath))
		{
			TestTrue(
				TEXT("Clone Drop tolerates transient dynamic-agent revisions while preserving its exact approach"),
				RuntimeDropPath->bAllowDynamicAgentConflictsDuringValidation);
			TestEqual(
				TEXT("Clone Drop preserves every recorded approach cell"),
				RuntimeDropPath->Cells,
				RecordedDropCells);
			TestEqual(
				TEXT("Clone Drop recovery goal remains the predecessor"),
				RuntimeDropPath->RequestedGoalCell,
				RecordedDropCells.Last());
			TestNotEqual(
				TEXT("Clone Drop receives a freshly stamped path identity"),
				RuntimeDropPath->PathInstanceId,
				RecordedDropPath.PathInstanceId);
		}

		const TValueOrError<FStructView, EPropertyBagResult> RuntimeTargetValue =
			DropAcceptedEvent->GetParameters().GetValueStruct(
				ParadoxDropActionParameters::TargetCell,
				FGridCellId::StaticStruct());
		const FGridCellId* RuntimeTarget = RuntimeTargetValue.HasValue()
			? RuntimeTargetValue.GetValue().GetPtr<FGridCellId>()
			: nullptr;
		if (TestNotNull(TEXT("Clone Drop retains its semantic final cell"), RuntimeTarget))
		{
			TestEqual(TEXT("Semantic Drop target is not replaced by the approach"), *RuntimeTarget, Cells.Last());
		}
	}
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
			TEXT("Default clone replay stamps dynamic-agent tolerance before movement begins"),
			RuntimePath->bAllowDynamicAgentConflictsDuringValidation);
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
	TestEqual(
		TEXT("Clone replay overrides goal contention in the runtime request"),
		AcceptedEvent->GetParameters()
			.GetValueEnum<EGridGoalContentionPolicy>(
				GridMoveToCellActionParameters::GoalContentionPolicy)
			.GetValue(),
		EGridGoalContentionPolicy::RedirectOnCompletion);

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
	TestEqual(
		TEXT("Clone submission does not mutate the source goal-contention policy"),
		Creation.Request.GetParameters()
			.GetValueEnum<EGridGoalContentionPolicy>(
				GridMoveToCellActionParameters::GoalContentionPolicy)
			.GetValue(),
		EGridGoalContentionPolicy::Ignore);

	Strategy->bOverrideGoalContentionPolicy = false;
	const FGameplayActionSubmissionResult PreservedSubmission =
		Strategy->SubmitPreparedRequest(ActionComponent, Creation.Request);
	if (!TestTrue(
		TEXT("Clone movement is accepted when the override is disabled"),
		PreservedSubmission.IsAccepted()))
	{
		AddError(PreservedSubmission.DiagnosticMessage);
		return false;
	}
	const FGameplayActionEvent* PreservedAcceptedEvent =
		EventObserver->ObservedEvents.FindByPredicate(
			[&PreservedSubmission](const FGameplayActionEvent& Event)
			{
				return Event.Handle == PreservedSubmission.Handle
					&& Event.EventType == EGameplayActionEventType::Accepted;
			});
	if (TestNotNull(
		TEXT("Disabled override still emits an accepted snapshot"),
		PreservedAcceptedEvent))
	{
		TestEqual(
			TEXT("Disabled override preserves the recorded goal-contention policy"),
			PreservedAcceptedEvent->GetParameters()
				.GetValueEnum<EGridGoalContentionPolicy>(
					GridMoveToCellActionParameters::GoalContentionPolicy)
				.GetValue(),
			EGridGoalContentionPolicy::Ignore);
	}

	Strategy->bOverrideGoalContentionPolicy = true;
	RecipientCharacter->SetActorLocation(
		Snapshot->Cells[1].WorldCenter,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	FGridInjectedPath DifferentStartRuntimePath;
	const FGameplayActionSubmissionResult DifferentStartSubmission =
		Strategy->SubmitPreparedRequest(
			ActionComponent,
			Creation.Request);
	if (!TestTrue(
		TEXT("Replay movement from a different cell is accepted with a fresh exact path"),
		DifferentStartSubmission.IsAccepted()))
	{
		AddError(DifferentStartSubmission.DiagnosticMessage);
		return false;
	}
	const FGameplayActionEvent* DifferentStartAcceptedEvent =
		EventObserver->ObservedEvents.FindByPredicate(
			[&DifferentStartSubmission](const FGameplayActionEvent& Event)
			{
				return Event.Handle == DifferentStartSubmission.Handle
					&& Event.EventType == EGameplayActionEventType::Accepted;
			});
	if (TestNotNull(
		TEXT("Different-start recovery emits an accepted snapshot"),
		DifferentStartAcceptedEvent))
	{
		const TValueOrError<FStructView, EPropertyBagResult>
			DifferentStartPathValue =
				DifferentStartAcceptedEvent->GetParameters().GetValueStruct(
					GridMoveToCellActionParameters::InjectedPath,
					FGridInjectedPath::StaticStruct());
		const FGridInjectedPath* DifferentStartPath =
			DifferentStartPathValue.HasValue()
				? DifferentStartPathValue.GetValue()
					.GetPtr<FGridInjectedPath>()
				: nullptr;
		if (TestNotNull(
			TEXT("Different-start request contains a fresh ExactInjectedPath"),
			DifferentStartPath))
		{
			DifferentStartRuntimePath = *DifferentStartPath;
			TestTrue(
				TEXT("Different-start recovery retains dynamic-agent validation tolerance"),
				DifferentStartPath->bAllowDynamicAgentConflictsDuringValidation);
			TestEqual(
				TEXT("Fresh exact path starts at the clone's current cell"),
				DifferentStartPath->Cells[0],
				Cells[1]);
			TestEqual(
				TEXT("Fresh exact path preserves the semantic requested goal"),
				DifferentStartPath->RequestedGoalCell,
				RecordedPath.RequestedGoalCell);
			TestEqual(
				TEXT("Fresh exact path preserves the filter class"),
				DifferentStartPath->FilterClass,
				RecordedPath.FilterClass);
			TestEqual(
				TEXT("Fresh exact path preserves invalidation policy"),
				DifferentStartPath->InvalidationPolicy,
				RecordedPath.InvalidationPolicy);
			TestNotEqual(
				TEXT("Fresh exact path discards the stale runtime identity"),
				DifferentStartPath->PathInstanceId,
				RecordedPath.PathInstanceId);
		}
	}
	FGridInjectedPath DifferentStartDropRuntimePath;
	const FGameplayActionSubmissionResult DifferentStartDropSubmission =
		Strategy->SubmitPreparedRequest(ActionComponent, DropCreation.Request);
	if (!TestTrue(
		TEXT("Replay Drop from a different cell is accepted with a fresh approach path"),
		DifferentStartDropSubmission.IsAccepted()))
	{
		AddError(DifferentStartDropSubmission.DiagnosticMessage);
		return false;
	}
	const FGameplayActionEvent* DifferentStartDropAcceptedEvent =
		EventObserver->ObservedEvents.FindByPredicate(
			[&DifferentStartDropSubmission](const FGameplayActionEvent& Event)
			{
				return Event.Handle == DifferentStartDropSubmission.Handle
					&& Event.EventType == EGameplayActionEventType::Accepted;
			});
	if (TestNotNull(
		TEXT("Different-start Drop recovery emits an accepted snapshot"),
		DifferentStartDropAcceptedEvent))
	{
		const TValueOrError<FStructView, EPropertyBagResult> RecoveredDropPathValue =
			DifferentStartDropAcceptedEvent->GetParameters().GetValueStruct(
				ParadoxDropActionParameters::InjectedPath,
				FGridInjectedPath::StaticStruct());
		const FGridInjectedPath* RuntimeDropPath = RecoveredDropPathValue.HasValue()
			? RecoveredDropPathValue.GetValue().GetPtr<FGridInjectedPath>()
			: nullptr;
		if (TestNotNull(
			TEXT("Different-start Drop contains a fresh exact approach path"),
			RuntimeDropPath))
		{
			DifferentStartDropRuntimePath = *RuntimeDropPath;
			TestTrue(
				TEXT("Different-start Drop recovery retains dynamic-agent validation tolerance"),
				RuntimeDropPath->bAllowDynamicAgentConflictsDuringValidation);
			TestEqual(
				TEXT("Recovered Drop path starts at the clone's current cell"),
				RuntimeDropPath->Cells[0],
				Cells[1]);
			TestEqual(
				TEXT("Recovered Drop path still ends at the recorded predecessor"),
				RuntimeDropPath->OriginalGoalCell,
				RecordedDropCells.Last());
			TestEqual(
				TEXT("Recovered Drop fallback goal never becomes the Drop cell"),
				RuntimeDropPath->RequestedGoalCell,
				RecordedDropCells.Last());
		}
	}
	FGridTrafficGoalClaimRequest BlockingGoalClaim;
	BlockingGoalClaim.OwnerId = FGuid::NewGuid();
	BlockingGoalClaim.Claimant = SourceCharacter;
	BlockingGoalClaim.Pawn = SourceCharacter;
	BlockingGoalClaim.GoalCell =
		{Cells.Last(), Snapshot->Cells.Last().WorldCenter};
	BlockingGoalClaim.AgentRadius = 42.0f;
	BlockingGoalClaim.AgentHeight = 192.0f;
	BlockingGoalClaim.AdditionalSeparation = 5.0f;
	if (!TestTrue(
		TEXT("Another replay agent claims the recorded destination"),
		NavigationData->TryClaimTrafficGoal(BlockingGoalClaim)))
	{
		return false;
	}
	if (TestTrue(
		TEXT("Different-start movement path was captured for traffic-revision validation"),
		DifferentStartRuntimePath.IsSet()))
	{
		TestTrue(
			TEXT("Different-start movement path survives a later traffic reservation revision"),
			GridWorld->ValidateInjectedPath(
				RecipientController,
				DifferentStartRuntimePath).bIsValid);
	}
	if (TestTrue(
		TEXT("Different-start Drop path was captured for traffic-revision validation"),
		DifferentStartDropRuntimePath.IsSet()))
	{
		TestTrue(
			TEXT("Different-start Drop path survives a later traffic reservation revision"),
			GridWorld->ValidateInjectedPath(
				RecipientController,
				DifferentStartDropRuntimePath).bIsValid);
	}

	RecipientCharacter->SetActorLocation(
		Snapshot->Cells[0].WorldCenter,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	FGridInjectedPath StrictConflictingPath;
	const FGridInjectedPathValidationResult StrictConflict =
		GridWorld->CreateExactInjectedPath(
			RecipientController,
			Cells,
			Cells.Last(),
			BalancedFilterClass,
			false,
			false,
			EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal,
			StrictConflictingPath);
	TestFalse(
		TEXT("Normal exact re-stamp still rejects another agent's claim"),
		StrictConflict.bIsValid);
	TestEqual(
		TEXT("Normal re-stamp exposes the transient block"),
		StrictConflict.FailureReason,
		EGridInjectedPathFailureReason::BlockedCell);

	Strategy->bOverrideGoalContentionPolicy = true;
	const FGameplayActionSubmissionResult ConflictingSubmission =
		Strategy->SubmitPreparedRequest(ActionComponent, Creation.Request);
	if (!TestTrue(
		TEXT("Clone replay accepts the exact recorded cells across a transient agent conflict"),
		ConflictingSubmission.IsAccepted()))
	{
		AddError(ConflictingSubmission.DiagnosticMessage);
		return false;
	}
	const FGameplayActionEvent* ConflictingAcceptedEvent =
		EventObserver->ObservedEvents.FindByPredicate(
			[&ConflictingSubmission](const FGameplayActionEvent& Event)
			{
				return Event.Handle == ConflictingSubmission.Handle
					&& Event.EventType == EGameplayActionEventType::Accepted;
			});
	if (TestNotNull(
		TEXT("Dynamic-conflict replay emits an accepted snapshot"),
		ConflictingAcceptedEvent))
	{
		const TValueOrError<FStructView, EPropertyBagResult>
			ConflictingRuntimePathValue =
				ConflictingAcceptedEvent->GetParameters().GetValueStruct(
					GridMoveToCellActionParameters::InjectedPath,
					FGridInjectedPath::StaticStruct());
		const FGridInjectedPath* ConflictingRuntimePath =
			ConflictingRuntimePathValue.HasValue()
				? ConflictingRuntimePathValue.GetValue()
					.GetPtr<FGridInjectedPath>()
				: nullptr;
		if (TestNotNull(
			TEXT("Dynamic-conflict replay keeps an exact injected path"),
			ConflictingRuntimePath))
		{
			TestEqual(
				TEXT("Dynamic-conflict replay preserves every recorded cell"),
				ConflictingRuntimePath->Cells,
				RecordedPath.Cells);
			TestTrue(
				TEXT("Clone runtime path records dynamic-agent validation tolerance"),
				ConflictingRuntimePath
					->bAllowDynamicAgentConflictsDuringValidation);
			TestTrue(
				TEXT("Clone runtime path remains valid while the claim exists"),
				GridWorld->ValidateInjectedPath(
					RecipientController,
					*ConflictingRuntimePath).bIsValid);
		}
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
			"/Game/Logic/BP_TimeLoopGameMode."
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
						"/Game/Data/EntityRelations/DA_ParadoxTimeLoopRelations."
						"DA_ParadoxTimeLoopRelations")));
		}
	}

	UEntityRelationPolicySet* PolicySet = LoadObject<UEntityRelationPolicySet>(
		nullptr,
		TEXT(
			"/Game/Data/EntityRelations/DA_ParadoxTimeLoopRelations."
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
	UClass* PlayerControllerClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerController.BP_PlayerController_C"));
	UClass* GameModeClass = LoadObject<UClass>(
		nullptr,
		TEXT("/Game/Logic/BP_TimeLoopGameMode.BP_TimeLoopGameMode_C"));
	if (!TestNotNull(TEXT("Project Player Controller Blueprint loads"), PlayerControllerClass)
		|| !TestNotNull(TEXT("Project Game Mode Blueprint loads"), GameModeClass))
	{
		return false;
	}
	AParadoxPlayerCharacter* Player =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AParadoxPlayerController* PlayerController =
		TestWorld.World->SpawnActor<AParadoxPlayerController>(
			PlayerControllerClass,
			FTransform::Identity,
			SpawnParameters);
	AParadoxCameraBoundsVolume* CameraBounds =
		TestWorld.World->SpawnActor<AParadoxCameraBoundsVolume>();
	AParadoxChronoSpawn* Spawn = SpawnChronoSpawn(
		*TestWorld.World,
		FVector::ZeroVector,
		TEXT("FinalChronoSpawn"));
	if (!TestNotNull(TEXT("Player exists"), Player)
		|| !TestNotNull(TEXT("Player Controller exists"), PlayerController)
		|| !TestNotNull(TEXT("Camera Bounds exists"), CameraBounds)
		|| !TestNotNull(TEXT("Final Chrono Spawn exists"), Spawn))
	{
		return false;
	}
	PlayerController->Possess(Player);
	TestWorld.StartPlay(GameModeClass);
	AParadoxGameMode* GameMode =
		TestWorld.World->GetAuthGameMode<AParadoxGameMode>();
	UParadoxTimeLoopComponent* TimeLoop = GameMode
		? GameMode->GetTimeLoopComponent()
		: nullptr;
	if (!TestNotNull(TEXT("Paradox Game Mode exists"), GameMode)
		|| !TestNotNull(TEXT("Authoritative Time Loop exists"), TimeLoop))
	{
		return false;
	}
	TestTrue(
		TEXT("Final run selects and records its Chrono Spawn"),
		TimeLoop->RequestChronoSpawnInteraction(Spawn).IsSuccess());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPlayerDeathRunFailureTest,
	"Paradox.TimeLoop.PlayerDeathUsesRunFailureRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPlayerDeathRunFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::TimeLoop::Tests;
	FScopedTestWorld TestWorld(TEXT("ParadoxPlayerDeathFailureWorld"));
	if (!TestNotNull(TEXT("Player-death test world exists"), TestWorld.World))
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
	AActor* Authority = TestWorld.World->SpawnActor<AActor>();
	AParadoxChronoSpawn* Spawn = SpawnChronoSpawn(
		*TestWorld.World,
		FVector::ZeroVector,
		TEXT("PlayerDeathRetryChronoSpawn"));
	if (!TestNotNull(TEXT("Player exists"), Player)
		|| !TestNotNull(TEXT("time-loop authority exists"), Authority)
		|| !TestNotNull(TEXT("retry Chrono Spawn exists"), Spawn))
	{
		return false;
	}

	UParadoxTimeLoopComponent* TimeLoop =
		NewObject<UParadoxTimeLoopComponent>(
			Authority,
			TEXT("PlayerDeathTimeLoop"),
			RF_Transient);
	Authority->AddInstanceComponent(TimeLoop);
	TimeLoop->RegisterComponent();
	TestWorld.StartPlay();
	for (AActor* Actor : { static_cast<AActor*>(Player), Authority, static_cast<AActor*>(Spawn) })
	{
		if (Actor && !Actor->HasActorBegunPlay())
		{
			Actor->DispatchBeginPlay();
		}
	}

	FString Failure;
	TestTrue(
		TEXT("World State baseline is available for death recovery"),
		FParadoxTimeLoopTestAccessor::PrepareWorldState(*TimeLoop, Failure));
	const TArray<AParadoxChronoSpawn*> Spawns = { Spawn };
	FParadoxTimeLoopTestAccessor::ConfigureActiveRun(
		*TimeLoop,
		*Player,
		Spawns,
		*Spawn);

	UParadoxOxygenComponent* Oxygen = Player->GetOxygenComponent();
	TestTrue(TEXT("ActiveRun starts Player Oxygen"), Oxygen && Oxygen->IsRunConsumptionActive());
	if (Oxygen)
	{
		Oxygen->ConsumeOxygenSeconds(Oxygen->GetOxygenDurationSeconds());
	}
	TimeLoop->AcceptPlayerDeath(
		*Player,
		GetDefault<UParadoxOxygenDepletionDamageType>(),
		nullptr,
		Player);
	const FParadoxRunFailureContext Context =
		TimeLoop->GetLastRunFailureContext();
	TestTrue(TEXT("Player death publishes a valid run-failure context"), Context.IsValid());
	TestEqual(TEXT("failure reason is PlayerDeath"), Context.Reason, EParadoxRunFailureReason::PlayerDeath);
	TestEqual(TEXT("failure context retains the Player"), Context.Player.Get(), static_cast<AParadoxCharacter*>(Player));
	TestEqual(TEXT("failure context retains the damage causer"), Context.DamageCauser.Get(), static_cast<AActor*>(Player));
	TestEqual(
		TEXT("failure context retains Oxygen depletion classification"),
		Context.DamageTypeClass.Get(),
		UParadoxOxygenDepletionDamageType::StaticClass());
	TestEqual(
		TEXT("headless Player-death recovery returns to spawn selection"),
		TimeLoop->GetCurrentPhase(),
		EParadoxTimeLoopPhase::ChronoSpawnSelection);
	TestEqual(TEXT("failed death run remains unconsolidated"), TimeLoop->GetConsolidatedTimelineCount(), 0);
	TestEqual(TEXT("failed Chrono Spawn is retryable"), Spawn->GetChronoSpawnState(), EParadoxChronoSpawnState::Available);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCloneTimeTravelDepartureTest,
	"Paradox.TimeLoop.CloneTimeTravelDepartureUsesConfiguredCompletionBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCloneTimeTravelDepartureTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::TimeLoop::Tests;
	FScopedTestWorld TestWorld(TEXT("ParadoxCloneTimeTravelDepartureWorld"));
	if (!TestNotNull(TEXT("Transient clone-departure world exists"), TestWorld.World))
	{
		return false;
	}
	UGameInstance* TestGameInstance = NewObject<UGameInstance>(GEngine);
	if (!TestNotNull(TEXT("Clone-departure GameInstance exists"), TestGameInstance))
	{
		return false;
	}
	TestWorld.Context->OwningGameInstance = TestGameInstance;
	TestWorld.World->SetGameInstance(TestGameInstance);
	TestWorld.World->SetGameMode(FURL());
	TestWorld.StartPlay();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxCloneCharacter* Clone =
		TestWorld.World->SpawnActor<AParadoxCloneCharacter>(
			AParadoxCloneCharacter::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AParadoxCloneCharacter* LegacyClone =
		TestWorld.World->SpawnActor<AParadoxCloneCharacter>(
			AParadoxCloneCharacter::StaticClass(),
			FTransform(FVector(200.0, 0.0, 0.0)),
			SpawnParameters);
	AActor* Authority = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Clone exists"), Clone)
		|| !TestNotNull(TEXT("Legacy clone exists"), LegacyClone)
		|| !TestNotNull(TEXT("Time-loop authority Actor exists"), Authority))
	{
		return false;
	}
	UParadoxTimeLoopComponent* TimeLoop =
		NewObject<UParadoxTimeLoopComponent>(
			Authority,
			TEXT("CloneDepartureTimeLoop"),
			RF_Transient);
	Authority->AddInstanceComponent(TimeLoop);
	TimeLoop->RegisterComponent();
	UGridNavigationOccupancyComponent* GoapOccupancy =
		UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
			*Clone,
			42.0f,
			192.0f,
			true);
	TestTrue(
		TEXT("GOAP test clone begins with active GridWorld occupancy"),
		GoapOccupancy && GoapOccupancy->IsActive());
	FParadoxTimeLoopTestAccessor::ConfigureCloneDeparture(*TimeLoop, *Clone);
	TestTrue(
		TEXT("Clone Health is alive before GOAP handoff"),
		Clone->GetHealthComponent()
			&& Clone->GetHealthComponent()->IsAlive());
	if (UParadoxOxygenComponent* Oxygen = Clone->GetOxygenComponent())
	{
		TestTrue(
			TEXT("Clone Oxygen component has begun play"),
			Oxygen->HasBegunPlay());
		Oxygen->SetRunConsumptionActive(true);
		TestTrue(
			TEXT("Clone oxygen consumption is active before GOAP handoff"),
			Oxygen->IsRunConsumptionActive());
	}

	FString Diagnostic;
	TestTrue(
		TEXT("Default clone departure schedules GOAP handoff"),
		TimeLoop->CompleteCloneTimeTravelDeparture(*Clone, Diagnostic));
	TestEqual(
		TEXT("GOAP handoff is deferred until the action can finish"),
		Clone->GetBehaviorCoordinator()->GetCurrentMode(),
		EParadoxCloneBehaviorMode::Replay);
	TestFalse(TEXT("GOAP clone remains visible before handoff"), Clone->IsHidden());
	TestTrue(TEXT("GOAP clone retains collision before handoff"), Clone->GetActorEnableCollision());
	TestWorld.Advance(0.001f);
	TestEqual(
		TEXT("Deferred handoff enters terminal GOAP mode"),
		Clone->GetBehaviorCoordinator()->GetCurrentMode(),
		EParadoxCloneBehaviorMode::Goap);
	TestFalse(TEXT("GOAP placeholder clone remains visible"), Clone->IsHidden());
	TestTrue(TEXT("GOAP placeholder clone retains collision"), Clone->GetActorEnableCollision());
	TestEqual(
		TEXT("GOAP placeholder clone movement is disabled"),
		Clone->GetCharacterMovement()->MovementMode,
		MOVE_None);
	TestTrue(
		TEXT("GOAP placeholder clone keeps run oxygen consumption active"),
		Clone->GetOxygenComponent()
			&& Clone->GetOxygenComponent()->IsRunConsumptionActive());
	UPerceptionKnowledgeSourceComponent* GoapSource =
		Clone->GetPerceptionKnowledgeSourceComponent();
	TestTrue(
		TEXT("GOAP placeholder clone remains a semantic source"),
		GoapSource
			&& GoapSource->IsSourceEnabled()
			&& GoapSource->IsSemanticallyRegistered());
	TestTrue(
		TEXT("GOAP placeholder clone retains GridWorld occupancy"),
		GoapOccupancy && GoapOccupancy->IsActive());

	FParadoxTimeLoopTestAccessor::SetCloneTimeTravelCompletionBehavior(
		*TimeLoop,
		EParadoxCloneTimeTravelCompletionBehavior::RetireInPlace);
	UGridNavigationOccupancyComponent* LegacyOccupancy =
		UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
			*LegacyClone,
			42.0f,
			192.0f,
			true);
	FParadoxTimeLoopTestAccessor::ConfigureCloneDeparture(*TimeLoop, *LegacyClone);
	TestTrue(
		TEXT("Legacy clone retirement remains selectable"),
		TimeLoop->CompleteCloneTimeTravelDeparture(*LegacyClone, Diagnostic));
	TestTrue(TEXT("Legacy retired clone is hidden"), LegacyClone->IsHidden());
	TestFalse(
		TEXT("Legacy retired clone has no collision"),
		LegacyClone->GetActorEnableCollision());
	TestEqual(
		TEXT("Legacy retired clone movement is disabled"),
		LegacyClone->GetCharacterMovement()->MovementMode,
		MOVE_None);
	TestTrue(
		TEXT("Legacy retired clone releases GridWorld occupancy"),
		LegacyOccupancy && !LegacyOccupancy->IsActive());
	UPerceptionKnowledgeSourceComponent* Source =
		LegacyClone->GetPerceptionKnowledgeSourceComponent();
	TestTrue(
		TEXT("Legacy retired clone semantic source is disabled"),
		Source && !Source->IsSourceEnabled());
	TestTrue(
		TEXT("Legacy retired clone source is unregistered"),
		Source
			&& !Source->IsSemanticallyRegistered()
			&& !Source->IsNativeStimuliSourceRegistered());
	UParadoxTemporalVisionComponent* TemporalVision =
		LegacyClone->GetTemporalVisionComponent();
	TestNotNull(TEXT("Legacy retired clone still owns temporal sight"), TemporalVision);
	TestFalse(
		TEXT("Legacy retired clone temporal sight has no authority"),
		TemporalVision && TemporalVision->IsTemporalDetectionAuthoritative());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
