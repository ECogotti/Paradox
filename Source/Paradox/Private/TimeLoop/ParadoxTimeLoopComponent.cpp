#include "TimeLoop/ParadoxTimeLoopComponent.h"

#include "Characters/ParadoxCharacter.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Actions/GridMoveToCellActionDefinition.h"
#include "Components/CapsuleComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Data/EntityRelationPolicySet.h"
#include "EngineUtils.h"
#include "EntityRelationTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameplayActionTags.h"
#include "Journal/IntentExecutionJournal.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/GridNavigationData.h"
#include "Paradox.h"
#include "Perception/ParadoxTemporalVisionComponent.h"
#include "Playback/ParadoxCloneReplayExecutionStrategy.h"
#include "Playback/IntentReplayPlaybackSession.h"
#include "Presentation/ParadoxOutcomePresentationComponent.h"
#include "Recording/IntentReplayTrack.h"
#include "Subsystems/EntityRelationsWorldSubsystem.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimeLoop/ParadoxChronoSpawn.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"
#include "TimeLoop/ParadoxWorldStateAnchor.h"
#include "Types/IntentReplayTypes.h"
#include "Types/WorldStateTypes.h"

UParadoxTimeLoopComponent::UParadoxTimeLoopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CloneCharacterClass = AParadoxCloneCharacter::StaticClass();
	CloneControllerClass = AParadoxCloneController::StaticClass();
	TemporalRelationPolicySet = TSoftObjectPtr<UEntityRelationPolicySet>(
		FSoftObjectPath(
			TEXT("/Game/Data/EntityRelations/DA_ParadoxTimeLoopRelations.DA_ParadoxTimeLoopRelations")));
}

FParadoxTimeLoopOperationResult UParadoxTimeLoopComponent::InitializeTimeLoop()
{
	if (!bTimeLoopEnabled)
	{
		SetPhase(EParadoxTimeLoopPhase::Disabled);
		return MakeResult(
			EParadoxTimeLoopOperationStatus::Succeeded,
			TEXT("The Paradox time loop is disabled for this GameMode."));
	}
	if (CurrentPhase != EParadoxTimeLoopPhase::Disabled)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			FString::Printf(
				TEXT("Time-loop initialization was requested while phase %d is active."),
				static_cast<int32>(CurrentPhase)),
			false);
	}

	SetPhase(EParadoxTimeLoopPhase::LevelPreparation);
	DiscoverChronoSpawns();
	if (MaximumTimelineCount <= 0)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidConfiguration,
			TEXT("No enabled Chrono Spawn exists in the current level."),
			true);
	}

	PlayerCharacter = ResolvePlayerCharacter();
	if (!PlayerCharacter)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::MissingPlayer,
			TEXT("The time loop could not find a possessed Paradox Player Character."),
			true);
	}
	bPlayerCollisionWasEnabled = PlayerCharacter->GetActorEnableCollision();
	DeactivatePlayer();

	AParadoxPlayerController* PlayerController = GetWorld()
		? Cast<AParadoxPlayerController>(GetWorld()->GetFirstPlayerController())
		: nullptr;
	if (!PlayerController)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::CameraConfigurationFailed,
			TEXT("The time loop requires a Paradox Player Controller to own the free camera."),
			true);
	}
	const FParadoxCameraOperationResult CameraResult =
		PlayerController->EnsureFreeCameraInitialized(true);
	if (!CameraResult.IsSuccess())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::CameraConfigurationFailed,
			FString::Printf(
				TEXT("Free-camera initialization failed with status %d: %s"),
				static_cast<int32>(CameraResult.Status),
				*CameraResult.DiagnosticMessage),
			true);
	}
	if (!PlayerCharacter->GetGameplayActionComponent()
		|| !PlayerCharacter->GetIntentReplayComponent()
		|| !PlayerCharacter->GetTemporalEntityComponent())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::MissingComponent,
			TEXT("The player is missing a required Gameplay Actions, Intent Replay, or temporal identity component."),
			true);
	}

	FString WorldStateFailure;
	if (!PrepareWorldState(WorldStateFailure))
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::WorldStateFailed,
			WorldStateFailure,
			true);
	}

	FString EntityRelationsFailure;
	if (!ConfigureEntityRelations(EntityRelationsFailure))
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidConfiguration,
			EntityRelationsFailure,
			true);
	}

	ReapplyChronoSpawnStates();
	SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
	PARADOX_LOG_INFO(
		TEXT("Time loop initialized in world '%s' with %d playable timelines."),
		*GetNameSafe(GetWorld()),
		MaximumTimelineCount);
	return MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		TEXT("The Paradox time loop is ready for Chrono Spawn selection."));
}

FParadoxTimeLoopOperationResult UParadoxTimeLoopComponent::SelectChronoSpawn(
	AParadoxChronoSpawn* ChronoSpawn)
{
	if (!bTimeLoopEnabled)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedDisabled,
			TEXT("Chrono Spawn selection was rejected because the time loop is disabled."),
			false);
	}
	if (CurrentPhase != EParadoxTimeLoopPhase::ChronoSpawnSelection)
	{
		if (IsValid(ChronoSpawn))
		{
			OnChronoSpawnRejected.Broadcast(ChronoSpawn);
		}
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Chrono Spawn selection is only legal during ChronoSpawnSelection."),
			false);
	}
	if (!IsValid(ChronoSpawn)
		|| !ChronoSpawns.Contains(ChronoSpawn)
		|| !ChronoSpawn->IsAvailableForSelection())
	{
		if (IsValid(ChronoSpawn))
		{
			OnChronoSpawnRejected.Broadcast(ChronoSpawn);
		}
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidChronoSpawn,
			TEXT("The requested Chrono Spawn is invalid, disabled, or already occupied."),
			false);
	}

	if (HoveredChronoSpawn && HoveredChronoSpawn != ChronoSpawn)
	{
		HoveredChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Available);
	}
	HoveredChronoSpawn = nullptr;
	SelectedChronoSpawn = ChronoSpawn;
	SelectedChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Selected);
	SetPhase(EParadoxTimeLoopPhase::RunPreparation);

	FString Failure;
	if (!ActivatePlayerAtSelectedSpawn(Failure))
	{
		SelectedChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Available);
		SelectedChronoSpawn = nullptr;
		DeactivatePlayer();
		SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
		return FailOperation(
			EParadoxTimeLoopOperationStatus::MissingPlayer,
			Failure,
			false);
	}
	if (!PreparePlayerRecorder(Failure))
	{
		SelectedChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Available);
		SelectedChronoSpawn = nullptr;
		DeactivatePlayer();
		SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RecordingFailed,
			Failure,
			false);
	}

	SetPhase(EParadoxTimeLoopPhase::AwaitingSynchronizedStart);
	OnChronoSpawnSelected.Broadcast(SelectedChronoSpawn);
	const FParadoxTimeLoopOperationResult AwaitingResult = MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("Timeline %d selected Chrono Spawn '%s' and is awaiting synchronized start."),
			ConsolidatedTimelines.Num(),
			*GetNameSafe(SelectedChronoSpawn)));
	OnSynchronizedStartAwaiting.Broadcast(AwaitingResult);
	if (!PrepareTemporalDetection(Failure))
	{
		RecoverFromSynchronizedStartFailure(Failure);
		return LastOperationResult;
	}
	if (!PrepareClonePlaybacks(Failure))
	{
		RecoverFromSynchronizedStartFailure(Failure);
		return LastOperationResult;
	}
	TryReleaseSynchronizedStart();
	if (CurrentPhase == EParadoxTimeLoopPhase::ChronoSpawnSelection)
	{
		return LastOperationResult;
	}
	return MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		CurrentPhase == EParadoxTimeLoopPhase::ActiveRun
			? FString::Printf(
				TEXT("Timeline %d started synchronously from Chrono Spawn '%s'."),
				ConsolidatedTimelines.Num(),
				*GetNameSafe(SelectedChronoSpawn))
			: AwaitingResult.DiagnosticMessage);
}

FParadoxTimeLoopOperationResult UParadoxTimeLoopComponent::RequestTimeRewind()
{
	if (!bTimeLoopEnabled)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedDisabled,
			TEXT("Rewind was rejected because the time loop is disabled."),
			false);
	}
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Rewind is only legal during ActiveRun."),
			false);
	}
	const bool bFinalPlayableRun =
		ConsolidatedTimelines.Num() >= MaximumTimelineCount - 1;
	if (!IsValid(PlayerCharacter) || !IsValid(SelectedChronoSpawn))
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::MissingPlayer,
			TEXT("Rewind preflight failed because the active player or selected Chrono Spawn is missing."),
			false);
	}

	UGameplayActionComponent* ActionComponent = PlayerCharacter->GetGameplayActionComponent();
	UIntentReplayComponent* ReplayComponent = PlayerCharacter->GetIntentReplayComponent();
	if (!ActionComponent || !ReplayComponent)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::MissingComponent,
			TEXT("Rewind preflight failed because Gameplay Actions or Intent Replay is missing."),
			false);
	}
	if (ReplayComponent->GetRecordingState() != EIntentRecordingState::Recording)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RecordingFailed,
			TEXT("Rewind preflight failed because the player has no active recording session."),
			false);
	}

	SetPhase(EParadoxTimeLoopPhase::RewindPreparation);
	DisableTemporalDetection(false);
	StopAndUnbindClonePlaybacks(false);
	ActionComponent->AbortAllActions(GameplayActionTags::Result_Aborted_SystemReset);
	const FIntentReplayOperationResult StopResult =
		ReplayComponent->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate);
	if (!StopResult.Succeeded())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RecordingFailed,
			TEXT("Intent Replay could not finalize the active recording after gameplay actions were aborted."),
			true);
	}

	UIntentReplayTrack* FinalizedTrack = ReplayComponent->GetLastFinalizedTrack();
	if (!IsValid(FinalizedTrack)
		|| !FinalizedTrack->IsFinalized()
		|| !FinalizedTrack->ValidateTrack().bValid)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RecordingFailed,
			TEXT("Intent Replay returned no valid immutable track after immediate finalization."),
			true);
	}

	FParadoxConsolidatedTimeline& Timeline = ConsolidatedTimelines.AddDefaulted_GetRef();
	Timeline.TemporalIndex = ConsolidatedTimelines.Num() - 1;
	Timeline.ChronoSpawn = SelectedChronoSpawn;
	Timeline.ReplayTrack = FinalizedTrack;
	SelectedChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Occupied);

	const FParadoxTimeLoopOperationResult ConsolidatedResult = MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("Timeline %d consolidated with %d recorded intents."),
			Timeline.TemporalIndex,
			FinalizedTrack->GetEntryCount()));
	OnTimelineConsolidated.Broadcast(ConsolidatedResult);
	OnRunEnded.Broadcast(ConsolidatedResult);
	PARADOX_LOG_INFO(TEXT("%s"), *ConsolidatedResult.DiagnosticMessage);

	if (bFinalPlayableRun)
	{
		DeactivatePlayer();
		LastGameOverContext = FParadoxGameOverContext();
		LastGameOverContext.EventId = FGuid::NewGuid();
		LastGameOverContext.FinalTemporalIndex = Timeline.TemporalIndex;
		LastGameOverContext.ConsolidatedTimelineCount =
			ConsolidatedTimelines.Num();
		LastGameOverContext.MaximumTimelineCount = MaximumTimelineCount;
		LastGameOverContext.DiagnosticMessage =
			TEXT("The final playable run was consolidated; no future Chrono Spawn remains.");
		SetPhase(EParadoxTimeLoopPhase::GameOver);
		const FParadoxTimeLoopOperationResult GameOverResult = MakeResult(
			EParadoxTimeLoopOperationStatus::GameOverReached,
			LastGameOverContext.DiagnosticMessage);
		OnGameOver.Broadcast(LastGameOverContext);
		PresentGameOver();
		PARADOX_LOG_INFO(TEXT("%s"), *GameOverResult.DiagnosticMessage);
		return GameOverResult;
	}

	DeactivatePlayer();
	DestroyRuntimeClones();
	SetPhase(EParadoxTimeLoopPhase::WorldReset);

	UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr;
	if (!WorldState)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::WorldStateFailed,
			TEXT("World State subsystem disappeared before reset."),
			true);
	}

	FWorldStateRestoreRequest RestoreRequest;
	RestoreRequest.Reason = TEXT("ParadoxTimeRewind");
	const FWorldStateRestoreResult RestoreResult = WorldState->RestoreBaseline(RestoreRequest);
	if (!RestoreResult.IsSuccess())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::WorldStateFailed,
			FString::Printf(
				TEXT("World State baseline restore failed with status %d at stage %d."),
				static_cast<int32>(RestoreResult.Status),
				static_cast<int32>(RestoreResult.FailureStage)),
			true);
	}

	ReapplyChronoSpawnStates();
	SetPhase(EParadoxTimeLoopPhase::TimelineReconstruction);
	FString ReconstructionFailure;
	if (!ReconstructConsolidatedClones(ReconstructionFailure))
	{
		DestroyRuntimeClones();
		return FailOperation(
			EParadoxTimeLoopOperationStatus::CloneSpawnFailed,
			ReconstructionFailure,
			true);
	}

	SelectedChronoSpawn = nullptr;
	SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
	const FParadoxTimeLoopOperationResult ResetResult = MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("World reset completed and %d consolidated clones were reconstructed."),
			RuntimeClones.Num()));
	OnWorldResetCompleted.Broadcast(ResetResult);
	PARADOX_LOG_INFO(TEXT("%s"), *ResetResult.DiagnosticMessage);
	return ResetResult;
}

FParadoxTimeLoopOperationResult
UParadoxTimeLoopComponent::ContinueParadoxRecovery(
	const FGuid ParadoxEventId)
{
	if (CurrentPhase != EParadoxTimeLoopPhase::ParadoxFailure)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Paradox recovery is only legal during ParadoxFailure."),
			false);
	}
	if (!ParadoxEventId.IsValid()
		|| ParadoxEventId != LastParadoxContext.EventId)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Paradox recovery acknowledgement belongs to a stale event."),
			false);
	}

	FString Failure;
	if (!RestoreWorldAndReconstructAfterParadox(Failure))
	{
		DestroyRuntimeClones();
		return FailOperation(
			EParadoxTimeLoopOperationStatus::ParadoxRecoveryFailed,
			Failure,
			true);
	}

	const FParadoxTimeLoopOperationResult Result = MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("Paradox recovery restored %d consolidated timeline(s); the failed Chrono Spawn is available again."),
			ConsolidatedTimelines.Num()));
	OnParadoxRecoveryCompleted.Broadcast(Result);
	OnWorldResetCompleted.Broadcast(Result);
	PARADOX_LOG_INFO(TEXT("%s"), *Result.DiagnosticMessage);
	return Result;
}

FParadoxTimeLoopOperationResult
UParadoxTimeLoopComponent::RequestLevelComplete()
{
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Level completion is only legal during ActiveRun."),
			false);
	}

	SetPhase(EParadoxTimeLoopPhase::LevelComplete);
	StopActiveRunWithoutConsolidation();
	DeactivatePlayer();
	LastLevelCompleteContext = FParadoxLevelCompleteContext();
	LastLevelCompleteContext.EventId = FGuid::NewGuid();
	LastLevelCompleteContext.CurrentTemporalIndex =
		ConsolidatedTimelines.Num();
	LastLevelCompleteContext.ConsolidatedTimelineCount =
		ConsolidatedTimelines.Num();
	LastLevelCompleteContext.DiagnosticMessage =
		TEXT("An external puzzle authority completed the level.");
	const FParadoxTimeLoopOperationResult Result = MakeResult(
		EParadoxTimeLoopOperationStatus::LevelCompleteReached,
		LastLevelCompleteContext.DiagnosticMessage);
	OnRunEnded.Broadcast(Result);
	OnLevelCompleted.Broadcast(LastLevelCompleteContext);
	PresentLevelComplete();
	PARADOX_LOG_INFO(TEXT("%s"), *Result.DiagnosticMessage);
	return Result;
}

FParadoxTimeLoopOperationResult
UParadoxTimeLoopComponent::RequestRestartLevel()
{
	if (CurrentPhase != EParadoxTimeLoopPhase::GameOver
		&& CurrentPhase != EParadoxTimeLoopPhase::LevelComplete)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Level restart is only legal after GameOver or LevelComplete."),
			false);
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InternalFailure,
			TEXT("The current World is unavailable for level restart."),
			false);
	}

	const FString LevelName =
		UGameplayStatics::GetCurrentLevelName(this, true);
	if (LevelName.IsEmpty())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InternalFailure,
			TEXT("The current map name could not be resolved for restart."),
			false);
	}

	DisableTemporalDetection(true);
	StopAndUnbindClonePlaybacks(false);
	if (AParadoxPlayerController* Controller =
		Cast<AParadoxPlayerController>(World->GetFirstPlayerController()))
	{
		if (UParadoxOutcomePresentationComponent* Presentation =
			Controller->GetOutcomePresentationComponent())
		{
			Presentation->ClearPresentation();
		}
	}
	const FParadoxTimeLoopOperationResult Result = MakeResult(
		EParadoxTimeLoopOperationStatus::RestartRequested,
		FString::Printf(
			TEXT("Restarting map '%s' from a fresh World."),
			*LevelName));
	OnRestartRequested.Broadcast(Result);
	PARADOX_LOG_INFO(TEXT("%s"), *Result.DiagnosticMessage);
	UGameplayStatics::OpenLevel(this, FName(*LevelName), true);
	return Result;
}

void UParadoxTimeLoopComponent::UpdateHoveredChronoSpawn(AParadoxChronoSpawn* ChronoSpawn)
{
	if (!bTimeLoopEnabled || CurrentPhase != EParadoxTimeLoopPhase::ChronoSpawnSelection)
	{
		if (HoveredChronoSpawn && HoveredChronoSpawn->GetChronoSpawnState() == EParadoxChronoSpawnState::Hovered)
		{
			HoveredChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Available);
		}
		HoveredChronoSpawn = nullptr;
		return;
	}

	AParadoxChronoSpawn* NewHovered =
		IsValid(ChronoSpawn) && ChronoSpawn->IsAvailableForSelection()
			? ChronoSpawn
			: nullptr;
	if (HoveredChronoSpawn == NewHovered)
	{
		return;
	}
	if (HoveredChronoSpawn
		&& HoveredChronoSpawn->GetChronoSpawnState() == EParadoxChronoSpawnState::Hovered)
	{
		HoveredChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Available);
	}
	HoveredChronoSpawn = NewHovered;
	if (HoveredChronoSpawn)
	{
		HoveredChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Hovered);
	}
}

bool UParadoxTimeLoopComponent::IsMovementAllowed() const
{
	return !bTimeLoopEnabled
		|| CurrentPhase == EParadoxTimeLoopPhase::Disabled
		|| CurrentPhase == EParadoxTimeLoopPhase::ActiveRun;
}

bool UParadoxTimeLoopComponent::IsTemporalDetectionAuthoritative() const
{
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
	{
		return false;
	}
	for (const TWeakObjectPtr<UParadoxTemporalVisionComponent>& Vision :
		TemporalVisionParticipants)
	{
		if (Vision.IsValid() && Vision->IsTemporalDetectionAuthoritative())
		{
			return true;
		}
	}
	return false;
}

int32 UParadoxTimeLoopComponent::GetDeduplicatedTemporalOverlapPairCount() const
{
	int32 PairCount = 0;
	for (const TWeakObjectPtr<UParadoxTemporalVisionComponent>& Vision :
		TemporalVisionParticipants)
	{
		if (Vision.IsValid())
		{
			PairCount += Vision->GetDeduplicatedOverlapActorCount();
		}
	}
	return PairCount;
}

bool UParadoxTimeLoopComponent::GetTemporalVisionDebugSnapshot(
	const int32 TemporalIndex,
	FParadoxTemporalVisionDebugSnapshot& OutSnapshot) const
{
	OutSnapshot = FParadoxTemporalVisionDebugSnapshot();
	for (const TWeakObjectPtr<UParadoxTemporalVisionComponent>& Vision :
		TemporalVisionParticipants)
	{
		if (!Vision.IsValid())
		{
			continue;
		}

		const FParadoxTemporalVisionDebugSnapshot Snapshot =
			Vision->GetDebugSnapshot();
		if (Snapshot.ObserverTemporalIndex == TemporalIndex)
		{
			OutSnapshot = Snapshot;
			return true;
		}
	}
	return false;
}

void UParadoxTimeLoopComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	DisableTemporalDetection(true);
	if (bEntityRelationsOverrideApplied)
	{
		if (UEntityRelationsWorldSubsystem* Relations = GetWorld()
			? GetWorld()->GetSubsystem<UEntityRelationsWorldSubsystem>()
			: nullptr)
		{
			Relations->ClearPolicySetOverride();
		}
		bEntityRelationsOverrideApplied = false;
	}
	Super::EndPlay(EndPlayReason);
}

FParadoxTimeLoopOperationResult UParadoxTimeLoopComponent::MakeResult(
	const EParadoxTimeLoopOperationStatus Status,
	const FString& DiagnosticMessage)
{
	LastOperationResult.Status = Status;
	LastOperationResult.Phase = CurrentPhase;
	LastOperationResult.DiagnosticMessage = DiagnosticMessage;
	return LastOperationResult;
}

FParadoxTimeLoopOperationResult UParadoxTimeLoopComponent::FailOperation(
	const EParadoxTimeLoopOperationStatus Status,
	const FString& DiagnosticMessage,
	const bool bEnterErrorPhase)
{
	if (bEnterErrorPhase)
	{
		SetPhase(EParadoxTimeLoopPhase::Error);
		PARADOX_LOG_ERROR(TEXT("%s"), *DiagnosticMessage);
	}
	else
	{
		PARADOX_LOG_WARNING(TEXT("%s"), *DiagnosticMessage);
	}
	const FParadoxTimeLoopOperationResult Result = MakeResult(Status, DiagnosticMessage);
	OnOperationFailed.Broadcast(Result);
	if (bEnterErrorPhase)
	{
		OnError.Broadcast(Result);
	}
	return Result;
}

void UParadoxTimeLoopComponent::SetPhase(const EParadoxTimeLoopPhase NewPhase)
{
	if (CurrentPhase == NewPhase)
	{
		return;
	}

	const EParadoxTimeLoopPhase PreviousPhase = CurrentPhase;
	CurrentPhase = NewPhase;
	OnPhaseChanged.Broadcast(PreviousPhase, CurrentPhase);
	PARADOX_LOG_INFO(
		TEXT("Time-loop phase changed from %d to %d in world '%s'."),
		static_cast<int32>(PreviousPhase),
		static_cast<int32>(CurrentPhase),
		*GetNameSafe(GetWorld()));
}

void UParadoxTimeLoopComponent::DiscoverChronoSpawns()
{
	ChronoSpawns.Reset();
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AParadoxChronoSpawn> It(World); It; ++It)
		{
			if (It->IsChronoSpawnEnabled())
			{
				ChronoSpawns.Add(*It);
			}
		}
	}

	ChronoSpawns.Sort([](
		const AParadoxChronoSpawn& Left,
		const AParadoxChronoSpawn& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	MaximumTimelineCount = ChronoSpawns.Num();
}

bool UParadoxTimeLoopComponent::PrepareWorldState(FString& OutFailure)
{
	UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr;
	if (!WorldState)
	{
		OutFailure = TEXT("The current world does not provide a World State subsystem.");
		return false;
	}

	EWorldStateSubsystemState State = WorldState->GetWorldStateSubsystemState();
	if (State == EWorldStateSubsystemState::Ready && WorldState->HasBaseline())
	{
		return true;
	}

	if (State == EWorldStateSubsystemState::Registering
		&& WorldState->GetParticipantStateSummaries().IsEmpty()
		&& !EnsureWorldStateAnchor(OutFailure))
	{
		return false;
	}

	if (State == EWorldStateSubsystemState::Registering)
	{
		const FWorldStateOperationResult FinalizeResult =
			WorldState->FinalizeWorldStateRegistration();
		if (!FinalizeResult.IsSuccess())
		{
			OutFailure = FString::Printf(
				TEXT("World State registration finalization failed with status %d."),
				static_cast<int32>(FinalizeResult.Status));
			return false;
		}
		State = WorldState->GetWorldStateSubsystemState();
	}

	if (State == EWorldStateSubsystemState::ReadyWithoutBaseline)
	{
		FWorldStateCaptureRequest CaptureRequest;
		CaptureRequest.Label = TEXT("ParadoxInitialBaseline");
		CaptureRequest.Scope.Kind = EWorldStateRestoreScopeKind::CompleteSnapshot;
		const FWorldStateCaptureResult CaptureResult =
			WorldState->CaptureBaseline(CaptureRequest);
		if (!CaptureResult.IsSuccess())
		{
			const FString IssueMessage = CaptureResult.Issues.IsEmpty()
				? TEXT("No structured issue was supplied.")
				: CaptureResult.Issues[0].Message;
			OutFailure = FString::Printf(
				TEXT("World State baseline capture failed with status %d: %s"),
				static_cast<int32>(CaptureResult.Status),
				*IssueMessage);
			return false;
		}
		State = WorldState->GetWorldStateSubsystemState();
	}

	if (State != EWorldStateSubsystemState::Ready || !WorldState->HasBaseline())
	{
		OutFailure = FString::Printf(
			TEXT("World State is not ready with a baseline (state %d)."),
			static_cast<int32>(State));
		return false;
	}
	return true;
}

bool UParadoxTimeLoopComponent::EnsureWorldStateAnchor(FString& OutFailure)
{
	if (IsValid(WorldStateAnchor))
	{
		return true;
	}

	UWorld* World = GetWorld();
	UWorldStateSubsystem* WorldState = World
		? World->GetSubsystem<UWorldStateSubsystem>()
		: nullptr;
	if (!World || !WorldState)
	{
		OutFailure = TEXT("The World State anchor cannot be created without a valid world subsystem.");
		return false;
	}
	if (WorldState->GetWorldStateSubsystemState() != EWorldStateSubsystemState::Registering)
	{
		OutFailure = TEXT("The World State anchor must be created before registration is finalized.");
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.Name = TEXT("ParadoxWorldStateAnchor");
	Parameters.NameMode =
		FActorSpawnParameters::ESpawnActorNameMode::Requested;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	WorldStateAnchor = World->SpawnActor<AParadoxWorldStateAnchor>(
		AParadoxWorldStateAnchor::StaticClass(),
		FTransform::Identity,
		Parameters);
	if (!WorldStateAnchor)
	{
		OutFailure = TEXT("Failed to spawn the loop-owned World State anchor.");
		return false;
	}
	if (!WorldStateAnchor->HasActorBegunPlay())
	{
		WorldStateAnchor->DispatchBeginPlay();
	}
	if (WorldState->GetParticipantStateSummaries().IsEmpty())
	{
		OutFailure = TEXT("The loop-owned World State anchor did not register before baseline capture.");
		return false;
	}
	return true;
}

AParadoxPlayerCharacter* UParadoxTimeLoopComponent::ResolvePlayerCharacter() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return PlayerController ? Cast<AParadoxPlayerCharacter>(PlayerController->GetPawn()) : nullptr;
}

bool UParadoxTimeLoopComponent::ActivatePlayerAtSelectedSpawn(FString& OutFailure)
{
	if (!IsValid(PlayerCharacter) || !IsValid(SelectedChronoSpawn))
	{
		OutFailure = TEXT("The player or selected Chrono Spawn is invalid.");
		return false;
	}

	UParadoxTemporalEntityComponent* TemporalComponent =
		PlayerCharacter->GetTemporalEntityComponent();
	if (!TemporalComponent
		|| !TemporalComponent->AssignPlayer(ConsolidatedTimelines.Num()))
	{
		OutFailure = TEXT("The player temporal identity could not be assigned.");
		return false;
	}

	PlayerCharacter->SetActorTransform(
		SelectedChronoSpawn->GetActorTransform(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	SetTemporalAvatarGridPresence(*PlayerCharacter, true);
	PlayerCharacter->SetActorHiddenInGame(false);
	PlayerCharacter->SetActorEnableCollision(bPlayerCollisionWasEnabled);
	if (UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	return true;
}

void UParadoxTimeLoopComponent::DeactivatePlayer()
{
	if (!IsValid(PlayerCharacter))
	{
		return;
	}

	if (UGameplayActionComponent* Actions = PlayerCharacter->GetGameplayActionComponent())
	{
		Actions->AbortAllActions(GameplayActionTags::Result_Aborted_SystemReset);
	}
	if (UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	SetTemporalAvatarGridPresence(*PlayerCharacter, false);
	PlayerCharacter->SetActorEnableCollision(false);
	PlayerCharacter->SetActorHiddenInGame(true);
	if (UParadoxTemporalEntityComponent* Temporal = PlayerCharacter->GetTemporalEntityComponent())
	{
		Temporal->ClearTemporalAssignment();
	}
}

void UParadoxTimeLoopComponent::SetTemporalAvatarGridPresence(
	AParadoxCharacter& Character,
	const bool bEnabled) const
{
	UWorld* World = Character.GetWorld();
	UGridWorldSubsystem* GridWorld = World
		? World->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	AGridNavigationData* NavigationData = GridWorld
		? GridWorld->GetNavigationData()
		: nullptr;

	TInlineComponentArray<UGridNavigationOccupancyComponent*> OccupancyComponents(
		&Character);
	for (UGridNavigationOccupancyComponent* Occupancy : OccupancyComponents)
	{
		if (!IsValid(Occupancy) || Occupancy->bIsReservation)
		{
			continue;
		}

		if (!bEnabled
			&& NavigationData
			&& Occupancy->OccupantId.IsValid())
		{
			// Parking records outlive an individual move task. Explicitly remove them when the
			// time-loop avatar becomes inactive so a hidden or soon-to-be-destroyed Pawn cannot
			// contend with a reconstructed timeline.
			NavigationData->ReleaseTrafficCorridor(
				Occupancy->OccupantId,
				nullptr,
				false);
		}

		if (Occupancy->IsActive() != bEnabled)
		{
			Occupancy->SetOccupancyEnabled(bEnabled);
		}
		else if (bEnabled)
		{
			// Activation follows teleport, so an already-active authored component must publish
			// the avatar's new cell before synchronized replay can submit its first request.
			Occupancy->RefreshOccupancy();
		}
	}
}

bool UParadoxTimeLoopComponent::PreparePlayerRecorder(FString& OutFailure)
{
	UIntentReplayComponent* Replay = IsValid(PlayerCharacter)
		? PlayerCharacter->GetIntentReplayComponent()
		: nullptr;
	if (!Replay)
	{
		OutFailure = TEXT("The player has no Intent Replay component.");
		return false;
	}
	if (!Replay->IsIntentReplayInitialized())
	{
		const FIntentReplayOperationResult InitializeResult =
			Replay->InitializeIntentReplay();
		if (!InitializeResult.Succeeded())
		{
			OutFailure = FString::Printf(
				TEXT("Intent Replay initialization failed for the player: %s"),
				*InitializeResult.Failure.DiagnosticMessage);
			return false;
		}
	}
	if (Replay->GetActiveRecordingSession())
	{
		OutFailure =
			TEXT("The player already owns a non-terminal recording session during run preparation.");
		return false;
	}
	return true;
}

bool UParadoxTimeLoopComponent::BeginPlayerRecording(FString& OutFailure)
{
	UIntentReplayComponent* Replay = IsValid(PlayerCharacter)
		? PlayerCharacter->GetIntentReplayComponent()
		: nullptr;
	if (!Replay)
	{
		OutFailure = TEXT("The player has no Intent Replay component.");
		return false;
	}
	if (!Replay->IsIntentReplayInitialized())
	{
		const FIntentReplayOperationResult InitializeResult =
			Replay->InitializeIntentReplay();
		if (!InitializeResult.Succeeded())
		{
			OutFailure = TEXT("Intent Replay initialization failed for the player.");
			return false;
		}
	}

	FIntentRecordingOptions Options;
	Options.SourceLabel = FString::Printf(
		TEXT("ParadoxTimeline_%d"),
		ConsolidatedTimelines.Num());
	const FIntentRecordingStartResult StartResult = Replay->StartRecording(Options);
	if (!StartResult.Succeeded())
	{
		OutFailure = FString::Printf(
			TEXT("Intent Replay rejected StartRecording with status %d."),
			static_cast<int32>(StartResult.Status));
		return false;
	}
	return true;
}

bool UParadoxTimeLoopComponent::ConfigureEntityRelations(FString& OutFailure)
{
	OutFailure.Reset();
	UWorld* World = GetWorld();
	UEntityRelationsWorldSubsystem* Relations =
		World ? World->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr;
	if (!Relations)
	{
		OutFailure = TEXT("Entity Relations subsystem is unavailable in the time-loop World.");
		return false;
	}

	UEntityRelationPolicySet* PolicySet =
		TemporalRelationPolicySet.LoadSynchronous();
	if (!PolicySet)
	{
		OutFailure = FString::Printf(
			TEXT("Temporal relation Policy Set '%s' could not be loaded."),
			*TemporalRelationPolicySet.ToSoftObjectPath().ToString());
		return false;
	}
	const FEntityRelationValidationResult Validation =
		PolicySet->ValidatePolicySet();
	if (!Validation.IsValid())
	{
		const FString FirstIssue = Validation.Issues.IsEmpty()
			? TEXT("unknown validation failure")
			: Validation.Issues[0].Message;
		OutFailure = FString::Printf(
			TEXT("Temporal relation Policy Set '%s' is invalid: %s"),
			*GetNameSafe(PolicySet),
			*FirstIssue);
		return false;
	}
	if (!Relations->SetPolicySetOverride(PolicySet))
	{
		OutFailure = FString::Printf(
			TEXT("Entity Relations rejected temporal Policy Set '%s'."),
			*GetNameSafe(PolicySet));
		return false;
	}
	bEntityRelationsOverrideApplied = true;
	return true;
}

bool UParadoxTimeLoopComponent::PrepareTemporalDetection(FString& OutFailure)
{
	OutFailure.Reset();
	DisableTemporalDetection(true);
	if (RuntimeClones.Num() != ConsolidatedTimelines.Num())
	{
		OutFailure = FString::Printf(
			TEXT("Temporal detection expected %d reconstructed clones but found %d."),
			ConsolidatedTimelines.Num(),
			RuntimeClones.Num());
		return false;
	}

	for (AParadoxCloneCharacter* Clone : RuntimeClones)
	{
		if (!IsValid(Clone))
		{
			OutFailure = TEXT("A reconstructed clone became invalid before temporal detection setup.");
			DisableTemporalDetection(true);
			return false;
		}
		UParadoxTemporalVisionComponent* Vision =
			Clone->GetTemporalVisionComponent();
		if (!Vision)
		{
			OutFailure = FString::Printf(
				TEXT("Clone '%s' has no Paradox Temporal Vision component."),
				*GetNameSafe(Clone));
			DisableTemporalDetection(true);
			return false;
		}

		Vision->OnTemporalOverlapDetected.RemoveDynamic(
			this,
			&UParadoxTimeLoopComponent::HandleTemporalOverlapDetected);
		Vision->OnTemporalOverlapDetected.AddDynamic(
			this,
			&UParadoxTimeLoopComponent::HandleTemporalOverlapDetected);
		if (!Vision->PrepareTemporalVision(OutFailure))
		{
			OutFailure = FString::Printf(
				TEXT("Clone '%s' failed Temporal Vision preparation: %s"),
				*GetNameSafe(Clone),
				*OutFailure);
			DisableTemporalDetection(true);
			return false;
		}
		TemporalVisionParticipants.Add(Vision);
	}
	return true;
}

void UParadoxTimeLoopComponent::EnableTemporalDetection()
{
	bParadoxAcceptedForRun = false;
	++TemporalDetectionSessionId;
	if (TemporalDetectionSessionId <= 0)
	{
		TemporalDetectionSessionId = 1;
	}

	for (const TWeakObjectPtr<UParadoxTemporalVisionComponent>& WeakVision :
		TemporalVisionParticipants)
	{
		if (UParadoxTemporalVisionComponent* Vision = WeakVision.Get())
		{
			Vision->EnableTemporalDetection(TemporalDetectionSessionId);
			if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
			{
				break;
			}
		}
	}
}

void UParadoxTimeLoopComponent::DisableTemporalDetection(
	const bool bClearParticipants)
{
	for (const TWeakObjectPtr<UParadoxTemporalVisionComponent>& WeakVision :
		TemporalVisionParticipants)
	{
		if (UParadoxTemporalVisionComponent* Vision = WeakVision.Get())
		{
			Vision->DisableTemporalDetection(true);
			if (bClearParticipants)
			{
				Vision->OnTemporalOverlapDetected.RemoveDynamic(
					this,
					&UParadoxTimeLoopComponent::HandleTemporalOverlapDetected);
			}
		}
	}
	if (bClearParticipants)
	{
		TemporalVisionParticipants.Reset();
	}
}

void UParadoxTimeLoopComponent::IgnoreTemporalCandidate(
	const FParadoxTemporalOverlapSnapshot& PhysicalOverlap,
	const EParadoxTemporalCandidateDisposition Disposition,
	const FString& DiagnosticMessage,
	const FEntityRelationResult* RelationResult)
{
	LastTemporalCandidate = FParadoxTemporalCandidateSnapshot();
	LastTemporalCandidate.PhysicalOverlap = PhysicalOverlap;
	LastTemporalCandidate.Disposition = Disposition;
	LastTemporalCandidate.DiagnosticMessage = DiagnosticMessage;
	if (RelationResult)
	{
		LastTemporalCandidate.RelationResult = *RelationResult;
		LastTemporalCandidate.RelationStatus = RelationResult->Status;
		LastTemporalCandidate.RelationDecision = RelationResult->Decision;
		LastTemporalCandidate.OutcomeTags = RelationResult->OutcomeTags;
	}
	OnTemporalCandidateIgnored.Broadcast(LastTemporalCandidate);
}

void UParadoxTimeLoopComponent::HandleTemporalOverlapDetected(
	const FParadoxTemporalOverlapSnapshot& Snapshot)
{
	OnTemporalOverlapDetected.Broadcast(Snapshot);
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun
		|| bParadoxAcceptedForRun)
	{
		IgnoreTemporalCandidate(
			Snapshot,
			EParadoxTemporalCandidateDisposition::IgnoredInactivePhase,
			TEXT("Temporal candidate arrived outside the authoritative ActiveRun window."));
		return;
	}
	if (!Snapshot.bDetectionAuthoritative
		|| Snapshot.DetectionSessionId != TemporalDetectionSessionId)
	{
		IgnoreTemporalCandidate(
			Snapshot,
			EParadoxTemporalCandidateDisposition::IgnoredStaleSession,
			TEXT("Temporal candidate belongs to a stale or non-authoritative detection session."));
		return;
	}

	AParadoxCharacter* Observer = Cast<AParadoxCharacter>(Snapshot.Observer);
	AParadoxCharacter* Target = Cast<AParadoxCharacter>(Snapshot.Target);
	if (Snapshot.Observer == Snapshot.Target)
	{
		IgnoreTemporalCandidate(
			Snapshot,
			EParadoxTemporalCandidateDisposition::IgnoredSelf,
			TEXT("A Temporal Vision mesh cannot perceive its owning temporal entity."));
		return;
	}
	if (!Observer || !Target)
	{
		IgnoreTemporalCandidate(
			Snapshot,
			EParadoxTemporalCandidateDisposition::IgnoredNonTemporalActor,
			TEXT("The overlapped Actor is not a Paradox temporal Character."));
		return;
	}

	const UParadoxTemporalEntityComponent* ObserverTemporal =
		Observer->GetTemporalEntityComponent();
	const UParadoxTemporalEntityComponent* TargetTemporal =
		Target->GetTemporalEntityComponent();
	if (!ObserverTemporal
		|| !TargetTemporal
		|| !ObserverTemporal->HasValidTemporalIndex()
		|| !TargetTemporal->HasValidTemporalIndex())
	{
		IgnoreTemporalCandidate(
			Snapshot,
			EParadoxTemporalCandidateDisposition::IgnoredInvalidTemporalIndex,
			TEXT("Observer or target has no valid Temporal Index."));
		return;
	}

	UEntityRelationsWorldSubsystem* Relations = GetWorld()
		? GetWorld()->GetSubsystem<UEntityRelationsWorldSubsystem>()
		: nullptr;
	if (!Relations)
	{
		IgnoreTemporalCandidate(
			Snapshot,
			EParadoxTemporalCandidateDisposition::RelationQueryFailed,
			TEXT("Entity Relations subsystem disappeared during ActiveRun."));
		return;
	}

	FEntityRelationQueryContext QueryContext;
	QueryContext.Domain = EntityRelationTags::Domain_VisualPerception;
	QueryContext.bAllowCache = false;
	const FEntityRelationResult RelationResult =
		Relations->EvaluateRelationByActor(Observer, Target, QueryContext);
	if (!RelationResult.IsSuccess())
	{
		IgnoreTemporalCandidate(
			Snapshot,
			EParadoxTemporalCandidateDisposition::RelationQueryFailed,
			FString::Printf(
				TEXT("Entity Relations query failed with status %d."),
				static_cast<int32>(RelationResult.Status)),
			&RelationResult);
		return;
	}

	const int32 ObserverIndex = ObserverTemporal->GetTemporalIndex();
	const int32 TargetIndex = TargetTemporal->GetTemporalIndex();
	const bool bFutureOutcome = RelationResult.OutcomeTags.HasTagExact(
		ParadoxGameplayTags::Relation_Outcome_FutureObserved);
	if (!bFutureOutcome || ObserverIndex >= TargetIndex)
	{
		IgnoreTemporalCandidate(
			Snapshot,
			EParadoxTemporalCandidateDisposition::SafeTemporalOrder,
			FString::Printf(
				TEXT("T%d observing T%d is temporally safe."),
				ObserverIndex,
				TargetIndex),
			&RelationResult);
		return;
	}

	LastTemporalCandidate = FParadoxTemporalCandidateSnapshot();
	LastTemporalCandidate.PhysicalOverlap = Snapshot;
	LastTemporalCandidate.Disposition =
		EParadoxTemporalCandidateDisposition::ParadoxAccepted;
	LastTemporalCandidate.RelationStatus = RelationResult.Status;
	LastTemporalCandidate.RelationDecision = RelationResult.Decision;
	LastTemporalCandidate.OutcomeTags = RelationResult.OutcomeTags;
	LastTemporalCandidate.RelationResult = RelationResult;
	LastTemporalCandidate.DiagnosticMessage = FString::Printf(
		TEXT("T%d observed future temporal entity T%d."),
		ObserverIndex,
		TargetIndex);
	AcceptParadox(LastTemporalCandidate);
}

void UParadoxTimeLoopComponent::AcceptParadox(
	const FParadoxTemporalCandidateSnapshot& Candidate)
{
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun
		|| bParadoxAcceptedForRun)
	{
		return;
	}
	bParadoxAcceptedForRun = true;

	AParadoxCharacter* Observer =
		Cast<AParadoxCharacter>(Candidate.PhysicalOverlap.Observer);
	AParadoxCharacter* Target =
		Cast<AParadoxCharacter>(Candidate.PhysicalOverlap.Target);
	LastParadoxContext = FParadoxContext();
	LastParadoxContext.EventId = FGuid::NewGuid();
	LastParadoxContext.Observer = Observer;
	LastParadoxContext.ObserverComponent =
		Candidate.PhysicalOverlap.ObserverComponent;
	LastParadoxContext.Target = Target;
	LastParadoxContext.TargetComponent =
		Candidate.PhysicalOverlap.TargetComponent;
	LastParadoxContext.ObserverTemporalIndex =
		Candidate.PhysicalOverlap.ObserverTemporalIndex;
	LastParadoxContext.TargetTemporalIndex =
		Candidate.PhysicalOverlap.TargetTemporalIndex;
	LastParadoxContext.CurrentGeneration = ConsolidatedTimelines.Num();
	LastParadoxContext.DetectionSessionId =
		Candidate.PhysicalOverlap.DetectionSessionId;
	LastParadoxContext.Cause =
		ParadoxGameplayTags::Relation_Outcome_FutureObserved;
	LastParadoxContext.RelationResult = Candidate.RelationResult;
	LastParadoxContext.ObserverLocation =
		Candidate.PhysicalOverlap.ObserverLocation;
	LastParadoxContext.TargetLocation =
		Candidate.PhysicalOverlap.TargetLocation;
	LastParadoxContext.DiagnosticMessage =
		Candidate.DiagnosticMessage;

	SetPhase(EParadoxTimeLoopPhase::ParadoxFailure);
	StopActiveRunWithoutConsolidation();

	const FParadoxTimeLoopOperationResult Result = MakeResult(
		EParadoxTimeLoopOperationStatus::ParadoxAccepted,
		FString::Printf(
			TEXT("Timeline collapse: T%d witnessed T%d."),
			LastParadoxContext.ObserverTemporalIndex,
			LastParadoxContext.TargetTemporalIndex));
	OnRunEnded.Broadcast(Result);
	OnParadoxAccepted.Broadcast(LastParadoxContext);
	PARADOX_LOG_WARNING(TEXT("%s"), *Result.DiagnosticMessage);
	PresentParadoxOrRecoverImmediately();
}

bool UParadoxTimeLoopComponent::RestoreWorldAndReconstructAfterParadox(
	FString& OutFailure)
{
	OutFailure.Reset();
	DeactivatePlayer();
	DestroyRuntimeClones();
	SetPhase(EParadoxTimeLoopPhase::WorldReset);

	UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr;
	if (!WorldState)
	{
		OutFailure =
			TEXT("World State subsystem disappeared during paradox recovery.");
		return false;
	}
	FWorldStateRestoreRequest RestoreRequest;
	RestoreRequest.Reason = TEXT("ParadoxFailure");
	const FWorldStateRestoreResult RestoreResult =
		WorldState->RestoreBaseline(RestoreRequest);
	if (!RestoreResult.IsSuccess())
	{
		OutFailure = FString::Printf(
			TEXT("Paradox World State restore failed with status %d at stage %d."),
			static_cast<int32>(RestoreResult.Status),
			static_cast<int32>(RestoreResult.FailureStage));
		return false;
	}

	ReapplyChronoSpawnStates();
	SetPhase(EParadoxTimeLoopPhase::TimelineReconstruction);
	if (!ReconstructConsolidatedClones(OutFailure))
	{
		return false;
	}

	SelectedChronoSpawn = nullptr;
	SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
	return true;
}

void UParadoxTimeLoopComponent::PresentParadoxOrRecoverImmediately()
{
	AParadoxPlayerController* Controller = GetWorld()
		? Cast<AParadoxPlayerController>(
			GetWorld()->GetFirstPlayerController())
		: nullptr;
	UParadoxOutcomePresentationComponent* Presentation =
		Controller
			? Controller->GetOutcomePresentationComponent()
			: nullptr;
	if (Presentation
		&& Presentation->BeginParadoxPresentation(
			LastParadoxContext))
	{
		return;
	}
	ContinueParadoxRecovery(LastParadoxContext.EventId);
}

void UParadoxTimeLoopComponent::PresentGameOver()
{
	AParadoxPlayerController* Controller = GetWorld()
		? Cast<AParadoxPlayerController>(
			GetWorld()->GetFirstPlayerController())
		: nullptr;
	if (Controller)
	{
		if (UParadoxOutcomePresentationComponent* Presentation =
			Controller->GetOutcomePresentationComponent())
		{
			Presentation->PresentGameOver(LastGameOverContext);
		}
	}
}

void UParadoxTimeLoopComponent::PresentLevelComplete()
{
	AParadoxPlayerController* Controller = GetWorld()
		? Cast<AParadoxPlayerController>(
			GetWorld()->GetFirstPlayerController())
		: nullptr;
	if (Controller)
	{
		if (UParadoxOutcomePresentationComponent* Presentation =
			Controller->GetOutcomePresentationComponent())
		{
			Presentation->PresentLevelComplete(
				LastLevelCompleteContext);
		}
	}
}

void UParadoxTimeLoopComponent::StopActiveRunWithoutConsolidation()
{
	DisableTemporalDetection(false);
	StopAndUnbindClonePlaybacks(false);
	if (UTacticalPauseWorldSubsystem* TacticalPause = GetWorld()
		? GetWorld()->GetSubsystem<UTacticalPauseWorldSubsystem>()
		: nullptr;
		TacticalPause && TacticalPause->IsPaused())
	{
		TacticalPause->RequestPlay();
	}
	if (!IsValid(PlayerCharacter))
	{
		return;
	}
	if (UGameplayActionComponent* Actions =
		PlayerCharacter->GetGameplayActionComponent())
	{
		Actions->AbortAllActions(
			GameplayActionTags::Result_Aborted_SystemReset);
	}
	if (UIntentReplayComponent* Replay =
		PlayerCharacter->GetIntentReplayComponent();
		Replay && Replay->GetActiveRecordingSession())
	{
		Replay->CancelRecording();
	}
}

bool UParadoxTimeLoopComponent::PrepareClonePlaybacks(FString& OutFailure)
{
	ClonePlaybackRuntimes.Reset();
	if (RuntimeClones.Num() != ConsolidatedTimelines.Num())
	{
		OutFailure = FString::Printf(
			TEXT("Synchronized start expected %d reconstructed clones but found %d."),
			ConsolidatedTimelines.Num(),
			RuntimeClones.Num());
		return false;
	}

	TArray<AParadoxCloneCharacter*> OrderedClones;
	OrderedClones.Reserve(RuntimeClones.Num());
	for (AParadoxCloneCharacter* Clone : RuntimeClones)
	{
		if (IsValid(Clone))
		{
			OrderedClones.Add(Clone);
		}
	}
	OrderedClones.Sort([](
		const AParadoxCloneCharacter& Left,
		const AParadoxCloneCharacter& Right)
	{
		const UParadoxTemporalEntityComponent* LeftTemporal =
			Left.GetTemporalEntityComponent();
		const UParadoxTemporalEntityComponent* RightTemporal =
			Right.GetTemporalEntityComponent();
		return (LeftTemporal ? LeftTemporal->GetTemporalIndex() : INDEX_NONE)
			< (RightTemporal ? RightTemporal->GetTemporalIndex() : INDEX_NONE);
	});

	for (AParadoxCloneCharacter* Clone : OrderedClones)
	{
		UParadoxTemporalEntityComponent* Temporal =
			Clone->GetTemporalEntityComponent();
		UIntentReplayComponent* Replay =
			Clone->GetIntentReplayComponent();
		FParadoxClonePlaybackRuntime& Runtime =
			ClonePlaybackRuntimes.AddDefaulted_GetRef();
		Runtime.Clone = Clone;
		Runtime.ReplayComponent = Replay;
		Runtime.TemporalIndex =
			Temporal ? Temporal->GetTemporalIndex() : INDEX_NONE;

		if (!Temporal
			|| !Temporal->HasValidTemporalIndex()
			|| !IsValid(Temporal->GetAssignedReplayTrack())
			|| !Replay)
		{
			FIntentReplayFailure Failure;
			Failure.DiagnosticMessage = FString::Printf(
				TEXT("Clone '%s' is missing a temporal assignment, immutable track, or Intent Replay component."),
				*GetNameSafe(Clone));
			MarkClonePlaybackFailed(
				Runtime,
				Failure,
				EIntentReplayPlaybackState::Failed);
			continue;
		}

		if (!Replay->IsIntentReplayInitialized())
		{
			const FIntentReplayOperationResult InitializeResult =
				Replay->InitializeIntentReplay();
			if (!InitializeResult.Succeeded())
			{
				MarkClonePlaybackFailed(
					Runtime,
					InitializeResult.Failure,
					EIntentReplayPlaybackState::Failed);
				continue;
			}
		}

		BindClonePlaybackDelegates(*Replay);
		FIntentReplayPlaybackOptions Options;
		Options.CompatibilityPolicy =
			EIntentReplayCompatibilityPolicy::StrictRecordedSchema;
		Options.SubmissionFailurePolicy =
			EIntentReplaySubmissionFailurePolicy::StopPlayback;
		Options.TerminalFailurePolicy =
			EIntentReplayTerminalFailurePolicy::StopPlayback;
		Options.bPauseBoundActions = false;

		const FIntentReplayPrepareResult PrepareResult =
			Replay->PrepareReplay(
				Temporal->GetAssignedReplayTrack(),
				Options);
		Runtime.SessionId = PrepareResult.SessionId;
		if (PrepareResult.Status == EIntentReplayPrepareStatus::Ready)
		{
			Runtime.State = EParadoxClonePlaybackState::Ready;
			OnClonePlaybackReady.Broadcast(
				MakeClonePlaybackSnapshot(Runtime));
		}
		else if (PrepareResult.Status == EIntentReplayPrepareStatus::Preparing)
		{
			Runtime.State = EParadoxClonePlaybackState::Preparing;
		}
		else
		{
			MarkClonePlaybackFailed(
				Runtime,
				PrepareResult.Failure,
				EIntentReplayPlaybackState::Failed);
		}
	}
	return true;
}

void UParadoxTimeLoopComponent::TryReleaseSynchronizedStart()
{
	if (CurrentPhase != EParadoxTimeLoopPhase::AwaitingSynchronizedStart
		|| !IsSynchronizedStartBarrierResolved())
	{
		return;
	}

	FString RecordingFailure;
	if (!BeginPlayerRecording(RecordingFailure))
	{
		RecoverFromSynchronizedStartFailure(RecordingFailure);
		return;
	}

	for (FParadoxClonePlaybackRuntime& Runtime : ClonePlaybackRuntimes)
	{
		if (Runtime.State != EParadoxClonePlaybackState::Ready)
		{
			continue;
		}
		AParadoxCloneCharacter* Clone = Runtime.Clone.Get();
		UIntentReplayComponent* Replay = Runtime.ReplayComponent.Get();
		if (!IsValid(Clone) || !Replay)
		{
			FIntentReplayFailure Failure;
			Failure.DiagnosticMessage =
				TEXT("A ready clone or its Intent Replay component became invalid before synchronized start.");
			MarkClonePlaybackFailed(
				Runtime,
				Failure,
				EIntentReplayPlaybackState::Failed);
			continue;
		}

		SetClonePlaybackMovementEnabled(*Clone, true);
		const FIntentReplayOperationResult StartResult =
			Replay->StartReplay();
		if (!StartResult.Succeeded())
		{
			MarkClonePlaybackFailed(
				Runtime,
				StartResult.Failure,
				Replay->GetPlaybackState());
			continue;
		}
		if (Runtime.State == EParadoxClonePlaybackState::Ready)
		{
			Runtime.State = Replay->GetPlaybackState()
					== EIntentReplayPlaybackState::Completed
				? EParadoxClonePlaybackState::Completed
				: EParadoxClonePlaybackState::Playing;
			BroadcastClonePlaybackState(Runtime);
		}
	}

	SetPhase(EParadoxTimeLoopPhase::ActiveRun);
	EnableTemporalDetection();
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
	{
		return;
	}
	const FParadoxTimeLoopOperationResult Result = MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("Timeline %d started with %d clone playback participant(s)."),
			ConsolidatedTimelines.Num(),
			ClonePlaybackRuntimes.Num()));
	OnRunStarted.Broadcast(Result);
	PARADOX_LOG_INFO(TEXT("%s"), *Result.DiagnosticMessage);
}

void UParadoxTimeLoopComponent::RecoverFromSynchronizedStartFailure(
	const FString& DiagnosticMessage)
{
	DisableTemporalDetection(true);
	StopAndUnbindClonePlaybacks(false);
	ClonePlaybackRuntimes.Reset();
	if (IsValid(PlayerCharacter))
	{
		if (UIntentReplayComponent* Replay =
			PlayerCharacter->GetIntentReplayComponent();
			Replay && Replay->GetActiveRecordingSession())
		{
			Replay->CancelRecording();
		}
	}
	if (IsValid(SelectedChronoSpawn)
		&& SelectedChronoSpawn->GetChronoSpawnState()
			== EParadoxChronoSpawnState::Selected)
	{
		SelectedChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Available);
	}
	SelectedChronoSpawn = nullptr;
	DeactivatePlayer();
	SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
	FailOperation(
		EParadoxTimeLoopOperationStatus::SynchronizedStartFailed,
		FString::Printf(
			TEXT("Synchronized run start failed: %s"),
			*DiagnosticMessage),
		false);
}

void UParadoxTimeLoopComponent::StopAndUnbindClonePlaybacks(
	const bool bBroadcastStopped)
{
	for (FParadoxClonePlaybackRuntime& Runtime : ClonePlaybackRuntimes)
	{
		UIntentReplayComponent* Replay = Runtime.ReplayComponent.Get();
		AParadoxCloneCharacter* Clone = Runtime.Clone.Get();
		if (!Replay)
		{
			continue;
		}
		if (!bBroadcastStopped)
		{
			UnbindClonePlaybackDelegates(*Replay);
		}
		const EIntentReplayPlaybackState PlaybackState =
			Replay->GetPlaybackState();
		if (PlaybackState == EIntentReplayPlaybackState::Preparing
			|| PlaybackState == EIntentReplayPlaybackState::Ready
			|| PlaybackState == EIntentReplayPlaybackState::Playing
			|| PlaybackState == EIntentReplayPlaybackState::Paused)
		{
			Replay->StopReplay();
			if (Runtime.State != EParadoxClonePlaybackState::Failed)
			{
				Runtime.State = EParadoxClonePlaybackState::Stopped;
				if (bBroadcastStopped)
				{
					OnClonePlaybackStopped.Broadcast(
						MakeClonePlaybackSnapshot(Runtime));
				}
			}
		}
		if (IsValid(Clone))
		{
			SetClonePlaybackMovementEnabled(*Clone, false);
		}
		if (bBroadcastStopped)
		{
			UnbindClonePlaybackDelegates(*Replay);
		}
	}
}

void UParadoxTimeLoopComponent::BindClonePlaybackDelegates(
	UIntentReplayComponent& ReplayComponent)
{
	ReplayComponent.OnReplayPrepared.AddUniqueDynamic(
		this,
		&UParadoxTimeLoopComponent::HandleCloneReplayPrepared);
	ReplayComponent.OnReplayStarted.AddUniqueDynamic(
		this,
		&UParadoxTimeLoopComponent::HandleCloneReplayStarted);
	ReplayComponent.OnReplayCompleted.AddUniqueDynamic(
		this,
		&UParadoxTimeLoopComponent::HandleCloneReplayCompleted);
	ReplayComponent.OnReplayFailed.AddUniqueDynamic(
		this,
		&UParadoxTimeLoopComponent::HandleCloneReplayFailed);
	ReplayComponent.OnReplayStopped.AddUniqueDynamic(
		this,
		&UParadoxTimeLoopComponent::HandleCloneReplayStopped);
}

void UParadoxTimeLoopComponent::UnbindClonePlaybackDelegates(
	UIntentReplayComponent& ReplayComponent)
{
	ReplayComponent.OnReplayPrepared.RemoveAll(this);
	ReplayComponent.OnReplayStarted.RemoveAll(this);
	ReplayComponent.OnReplayCompleted.RemoveAll(this);
	ReplayComponent.OnReplayFailed.RemoveAll(this);
	ReplayComponent.OnReplayStopped.RemoveAll(this);
}

FParadoxClonePlaybackRuntime*
UParadoxTimeLoopComponent::FindClonePlaybackRuntime(
	const FIntentReplayPlaybackSessionId SessionId)
{
	return ClonePlaybackRuntimes.FindByPredicate(
		[&SessionId](const FParadoxClonePlaybackRuntime& Runtime)
		{
			return Runtime.SessionId == SessionId;
		});
}

const FParadoxClonePlaybackRuntime*
UParadoxTimeLoopComponent::FindClonePlaybackRuntime(
	const int32 TemporalIndex) const
{
	return ClonePlaybackRuntimes.FindByPredicate(
		[TemporalIndex](const FParadoxClonePlaybackRuntime& Runtime)
		{
			return Runtime.TemporalIndex == TemporalIndex;
		});
}

bool UParadoxTimeLoopComponent::IsSynchronizedStartBarrierResolved() const
{
	for (const FParadoxClonePlaybackRuntime& Runtime : ClonePlaybackRuntimes)
	{
		if (Runtime.State != EParadoxClonePlaybackState::Ready
			&& Runtime.State != EParadoxClonePlaybackState::Failed)
		{
			return false;
		}
	}
	return true;
}

FParadoxClonePlaybackSnapshot
UParadoxTimeLoopComponent::MakeClonePlaybackSnapshot(
	const FParadoxClonePlaybackRuntime& Runtime) const
{
	FParadoxClonePlaybackSnapshot Snapshot;
	Snapshot.Clone = Runtime.Clone.Get();
	Snapshot.TemporalIndex = Runtime.TemporalIndex;
	Snapshot.State = Runtime.State;
	Snapshot.SessionId = Runtime.SessionId;
	if (const UIntentReplayComponent* Replay =
		Runtime.ReplayComponent.Get())
	{
		if (const UIntentReplayPlaybackSession* Session =
			Replay->GetActivePlaybackSession())
		{
			Snapshot.ProcessedEntryCount =
				Session->GetNextEntryIndex();
			if (const UIntentReplayTrack* Track =
				Session->GetSourceTrack())
			{
				Snapshot.TotalEntryCount = Track->GetEntryCount();
			}
		}
	}
	return Snapshot;
}

void UParadoxTimeLoopComponent::BroadcastClonePlaybackState(
	FParadoxClonePlaybackRuntime& Runtime)
{
	const FParadoxClonePlaybackSnapshot Snapshot =
		MakeClonePlaybackSnapshot(Runtime);
	switch (Runtime.State)
	{
	case EParadoxClonePlaybackState::Ready:
		OnClonePlaybackReady.Broadcast(Snapshot);
		break;
	case EParadoxClonePlaybackState::Playing:
		OnClonePlaybackStarted.Broadcast(Snapshot);
		break;
	case EParadoxClonePlaybackState::Completed:
		OnClonePlaybackCompleted.Broadcast(Snapshot);
		break;
	case EParadoxClonePlaybackState::Stopped:
		OnClonePlaybackStopped.Broadcast(Snapshot);
		break;
	default:
		break;
	}
}

void UParadoxTimeLoopComponent::MarkClonePlaybackFailed(
	FParadoxClonePlaybackRuntime& Runtime,
	const FIntentReplayFailure& Failure,
	const EIntentReplayPlaybackState ExecutorState)
{
	Runtime.State = EParadoxClonePlaybackState::Failed;
	Runtime.LastFailure = BuildClonePlaybackFailure(
		Runtime,
		Failure,
		ExecutorState);
	LastClonePlaybackFailure = Runtime.LastFailure;
	if (AParadoxCloneCharacter* Clone = Runtime.Clone.Get();
		IsValid(Clone))
	{
		SetClonePlaybackMovementEnabled(*Clone, false);
	}
	OnClonePlaybackFailed.Broadcast(Runtime.LastFailure);
	PARADOX_LOG_WARNING(
		TEXT("Clone T%d playback entered stationary fallback: %s"),
		Runtime.TemporalIndex,
		*Runtime.LastFailure.DiagnosticMessage);
}

void UParadoxTimeLoopComponent::SetClonePlaybackMovementEnabled(
	AParadoxCloneCharacter& Clone,
	const bool bEnabled) const
{
	if (!bEnabled)
	{
		if (UGameplayActionComponent* Actions =
			Clone.GetGameplayActionComponent())
		{
			Actions->AbortAllActions(
				GameplayActionTags::Result_Aborted_SystemReset);
		}
	}
	if (AController* Controller = Clone.GetController())
	{
		Controller->StopMovement();
	}
	if (UCharacterMovementComponent* Movement =
		Clone.GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		if (bEnabled)
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
		else
		{
			Movement->DisableMovement();
		}
	}
}

FParadoxClonePlaybackFailure
UParadoxTimeLoopComponent::BuildClonePlaybackFailure(
	const FParadoxClonePlaybackRuntime& Runtime,
	const FIntentReplayFailure& Failure,
	const EIntentReplayPlaybackState ExecutorState) const
{
	FParadoxClonePlaybackFailure Result;
	Result.Clone = Runtime.Clone.Get();
	Result.TemporalIndex = Runtime.TemporalIndex;
	Result.SessionId = Runtime.SessionId;
	Result.ExecutorState = ExecutorState;
	Result.RecordedIntentId = Failure.RecordedIntentId;
	Result.ReasonTag = Failure.ReasonTag;
	Result.DiagnosticMessage = Failure.DiagnosticMessage;
	if (const AParadoxCloneCharacter* Clone = Runtime.Clone.Get())
	{
		Result.CloneWorldLocation = Clone->GetActorLocation();
	}

	const UIntentReplayComponent* Replay =
		Runtime.ReplayComponent.Get();
	const UIntentReplayPlaybackSession* Session =
		Replay ? Replay->GetActivePlaybackSession() : nullptr;
	if (Session && Session->GetExecutionJournal())
	{
		const TArray<FIntentExecutionEvent> Events =
			Session->GetExecutionJournal()->GetEvents();
		for (int32 Index = Events.Num() - 1; Index >= 0; --Index)
		{
			const FIntentExecutionEvent& Event = Events[Index];
			if (!Event.bHasActionEvent
				|| !Event.ActionEvent.bHasResult
				|| (Failure.RecordedIntentId.IsValid()
					&& Event.RecordedIntentId
						!= Failure.RecordedIntentId))
			{
				continue;
			}
			Result.RecordedIntentId = Event.RecordedIntentId;
			Result.ActionTag = Event.ActionEvent.ActionTag;
			Result.ReasonTag = Event.ActionEvent.Result.ReasonTag;
			if (!Event.ActionEvent.Result.DiagnosticMessage.IsEmpty())
			{
				Result.DiagnosticMessage =
					Event.ActionEvent.Result.DiagnosticMessage;
			}
			break;
		}
	}

	const UIntentReplayTrack* Track =
		Session ? Session->GetSourceTrack() : nullptr;
	if (Track && Result.RecordedIntentId.IsValid())
	{
		const TArray<FRecordedIntent>& Entries = Track->GetEntries();
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FRecordedIntent& Entry = Entries[Index];
			if (Entry.RecordedIntentId != Result.RecordedIntentId)
			{
				continue;
			}
			Result.TrackEntryIndex = Index;
			if (!Result.ActionTag.IsValid())
			{
				Result.ActionTag = Entry.ActionTag;
			}
			const TValueOrError<FStructView, EPropertyBagResult>
				GoalLocation =
					Entry.GetParameters().GetValueStruct(
						GridMoveToCellActionParameters::GoalLocation);
			if (GoalLocation.HasValue())
			{
				if (const FVector* Destination =
					GoalLocation.GetValue().GetPtr<FVector>())
				{
					Result.bHasIntendedDestination = true;
					Result.IntendedDestination = *Destination;
				}
			}
			break;
		}
	}
	if (Result.DiagnosticMessage.IsEmpty())
	{
		Result.DiagnosticMessage =
			TEXT("Intent Replay reported a clone playback failure without additional diagnostics.");
	}
	return Result;
}

bool UParadoxTimeLoopComponent::GetClonePlaybackSnapshot(
	const int32 TemporalIndex,
	FParadoxClonePlaybackSnapshot& OutSnapshot) const
{
	const FParadoxClonePlaybackRuntime* Runtime =
		FindClonePlaybackRuntime(TemporalIndex);
	if (!Runtime)
	{
		return false;
	}
	OutSnapshot = MakeClonePlaybackSnapshot(*Runtime);
	return true;
}

void UParadoxTimeLoopComponent::HandleCloneReplayPrepared(
	const FIntentReplayPlaybackSessionId SessionId,
	UIntentReplayTrack* Track)
{
	FParadoxClonePlaybackRuntime* Runtime =
		FindClonePlaybackRuntime(SessionId);
	if (!Runtime
		|| Runtime->State != EParadoxClonePlaybackState::Preparing
		|| CurrentPhase
			!= EParadoxTimeLoopPhase::AwaitingSynchronizedStart)
	{
		return;
	}
	const AParadoxCloneCharacter* Clone = Runtime->Clone.Get();
	const UParadoxTemporalEntityComponent* Temporal =
		Clone ? Clone->GetTemporalEntityComponent() : nullptr;
	if (!Temporal || Temporal->GetAssignedReplayTrack() != Track)
	{
		FIntentReplayFailure Failure;
		Failure.DiagnosticMessage =
			TEXT("Async replay preparation completed with a stale or mismatched source track.");
		MarkClonePlaybackFailed(
			*Runtime,
			Failure,
			EIntentReplayPlaybackState::Failed);
	}
	else
	{
		Runtime->State = EParadoxClonePlaybackState::Ready;
		OnClonePlaybackReady.Broadcast(
			MakeClonePlaybackSnapshot(*Runtime));
	}
	TryReleaseSynchronizedStart();
}

void UParadoxTimeLoopComponent::HandleCloneReplayStarted(
	const FIntentReplayPlaybackSessionId SessionId)
{
	if (FParadoxClonePlaybackRuntime* Runtime =
		FindClonePlaybackRuntime(SessionId))
	{
		Runtime->State = EParadoxClonePlaybackState::Playing;
		OnClonePlaybackStarted.Broadcast(
			MakeClonePlaybackSnapshot(*Runtime));
	}
}

void UParadoxTimeLoopComponent::HandleCloneReplayCompleted(
	const FIntentReplayResult& Result)
{
	if (FParadoxClonePlaybackRuntime* Runtime =
		FindClonePlaybackRuntime(Result.SessionId))
	{
		Runtime->State = EParadoxClonePlaybackState::Completed;
		if (AParadoxCloneCharacter* Clone = Runtime->Clone.Get();
			IsValid(Clone))
		{
			SetClonePlaybackMovementEnabled(*Clone, false);
		}
		OnClonePlaybackCompleted.Broadcast(
			MakeClonePlaybackSnapshot(*Runtime));
	}
}

void UParadoxTimeLoopComponent::HandleCloneReplayFailed(
	const FIntentReplayResult& Result)
{
	if (FParadoxClonePlaybackRuntime* Runtime =
		FindClonePlaybackRuntime(Result.SessionId))
	{
		MarkClonePlaybackFailed(
			*Runtime,
			Result.Failure,
			EIntentReplayPlaybackState::Failed);
	}
}

void UParadoxTimeLoopComponent::HandleCloneReplayStopped(
	const FIntentReplayResult& Result)
{
	if (FParadoxClonePlaybackRuntime* Runtime =
		FindClonePlaybackRuntime(Result.SessionId))
	{
		if (Runtime->State != EParadoxClonePlaybackState::Failed)
		{
			Runtime->State = EParadoxClonePlaybackState::Stopped;
			if (AParadoxCloneCharacter* Clone =
				Runtime->Clone.Get();
				IsValid(Clone))
			{
				SetClonePlaybackMovementEnabled(*Clone, false);
			}
			OnClonePlaybackStopped.Broadcast(
				MakeClonePlaybackSnapshot(*Runtime));
		}
	}
}

bool UParadoxTimeLoopComponent::ReconstructConsolidatedClones(FString& OutFailure)
{
	if (!IsConfiguredCloneClassUsable())
	{
		OutFailure = TEXT("No usable Paradox Clone Character class is configured.");
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		OutFailure = TEXT("The world became invalid during clone reconstruction.");
		return false;
	}

	TArray<FParadoxConsolidatedTimeline> OrderedTimelines = ConsolidatedTimelines;
	OrderedTimelines.Sort([](
		const FParadoxConsolidatedTimeline& Left,
		const FParadoxConsolidatedTimeline& Right)
	{
		return Left.TemporalIndex < Right.TemporalIndex;
	});

	for (const FParadoxConsolidatedTimeline& Timeline : OrderedTimelines)
	{
		if (!Timeline.IsValid())
		{
			OutFailure = FString::Printf(
				TEXT("Timeline %d is invalid during clone reconstruction."),
				Timeline.TemporalIndex);
			return false;
		}

		const FTransform CloneTransform = Timeline.ChronoSpawn->GetActorTransform();
		AParadoxCloneCharacter* Clone =
			World->SpawnActorDeferred<AParadoxCloneCharacter>(
			CloneCharacterClass,
			CloneTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
			ESpawnActorScaleMethod::MultiplyWithRoot);
		if (!Clone)
		{
			OutFailure = FString::Printf(
				TEXT("Failed to spawn clone for timeline %d."),
				Timeline.TemporalIndex);
			return false;
		}
		Clone->AutoPossessPlayer = EAutoReceiveInput::Disabled;
		if (CloneControllerClass)
		{
			Clone->AIControllerClass = CloneControllerClass;
		}
		Clone->AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
		if (UIntentReplayComponent* Replay = Clone->GetIntentReplayComponent())
		{
			// Set this after Blueprint defaults have been applied but before deferred component
			// initialization, preventing an asset override from bypassing clone path adaptation.
			Replay->ExecutionStrategyClass =
				UParadoxCloneReplayExecutionStrategy::StaticClass();
		}
		Clone->FinishSpawning(
			CloneTransform,
			false,
			nullptr,
			ESpawnActorScaleMethod::MultiplyWithRoot);
		// Gameplay worlds dispatch this during SpawnActor. Explicit dispatch also covers
		// reconstruction in initialized transient worlds so lifecycle-owned components such as the
		// clone's World State participant register before the clone is announced as reconstructed.
		if (!Clone->HasActorBegunPlay())
		{
			Clone->DispatchBeginPlay();
		}
		if (!IsValid(Clone) || !Clone->HasActorBegunPlay())
		{
			OutFailure = FString::Printf(
				TEXT("Clone %d did not complete BeginPlay during reconstruction."),
				Timeline.TemporalIndex);
			return false;
		}
		RuntimeClones.Add(Clone);

		Clone->SpawnDefaultController();
		if (CloneControllerClass
			&& (!Clone->GetController()
				|| !Clone->GetController()->IsA(CloneControllerClass)))
		{
			OutFailure = FString::Printf(
				TEXT("Clone %d did not acquire the configured clone controller class '%s'."),
				Timeline.TemporalIndex,
				*GetNameSafe(CloneControllerClass));
			return false;
		}
		Clone->SetActorHiddenInGame(false);
		if (UCharacterMovementComponent* Movement = Clone->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}

		UParadoxTemporalEntityComponent* Temporal =
			Clone->GetTemporalEntityComponent();
		if (!Temporal
			|| !Temporal->AssignClone(Timeline.TemporalIndex, Timeline.ReplayTrack))
		{
			OutFailure = FString::Printf(
				TEXT("Clone temporal assignment failed for timeline %d."),
				Timeline.TemporalIndex);
			return false;
		}

		OnCloneReconstructed.Broadcast(Clone, Timeline.TemporalIndex);
	}
	return true;
}

void UParadoxTimeLoopComponent::DestroyRuntimeClones()
{
	DisableTemporalDetection(true);
	StopAndUnbindClonePlaybacks(false);
	for (AParadoxCloneCharacter* Clone : RuntimeClones)
	{
		if (IsValid(Clone))
		{
			SetTemporalAvatarGridPresence(*Clone, false);
			Clone->Destroy();
		}
	}
	RuntimeClones.Reset();
	ClonePlaybackRuntimes.Reset();
}

void UParadoxTimeLoopComponent::ReapplyChronoSpawnStates()
{
	for (AParadoxChronoSpawn* Spawn : ChronoSpawns)
	{
		if (IsValid(Spawn))
		{
			Spawn->SetRuntimeState(
				Spawn->IsChronoSpawnEnabled()
					? EParadoxChronoSpawnState::Available
					: EParadoxChronoSpawnState::Disabled);
		}
	}
	for (const FParadoxConsolidatedTimeline& Timeline : ConsolidatedTimelines)
	{
		if (IsValid(Timeline.ChronoSpawn))
		{
			Timeline.ChronoSpawn->SetRuntimeState(EParadoxChronoSpawnState::Occupied);
		}
	}
	HoveredChronoSpawn = nullptr;
}

bool UParadoxTimeLoopComponent::IsConfiguredCloneClassUsable() const
{
	return CloneCharacterClass != nullptr;
}
