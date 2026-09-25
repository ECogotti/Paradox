#include "TimeLoop/ParadoxTimeLoopComponent.h"

#include "Actions/ParadoxChronoSpawnActionDefinition.h"
#include "Characters/ParadoxCharacter.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "Actions/GridMoveToCellActionDefinition.h"
#include "Components/CapsuleComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/IntentReplayObservationComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Data/EntityRelationPolicySet.h"
#include "Data/IntentReplayTimelineBundle.h"
#include "EngineUtils.h"
#include "EntityRelationTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/GameModeBase.h"
#include "GameplayActionTags.h"
#include "Health/ParadoxHealthComponent.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "IntentReplayTags.h"
#include "Journal/IntentExecutionJournal.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/GridNavigationData.h"
#include "Oxygen/ParadoxOxygenComponent.h"
#include "Oxygen/ParadoxOxygenDepletionDamageType.h"
#include "Oxygen/ParadoxOxygenWorldSubsystem.h"
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
#include "UObject/ConstructorHelpers.h"

UParadoxTimeLoopComponent::UParadoxTimeLoopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CloneCharacterClass = AParadoxCloneCharacter::StaticClass();
	CloneControllerClass = AParadoxCloneController::StaticClass();
	TemporalRelationPolicySet = TSoftObjectPtr<UEntityRelationPolicySet>(
		FSoftObjectPath(
			TEXT("/Game/Data/EntityRelations/DA_ParadoxTimeLoopRelations.DA_ParadoxTimeLoopRelations")));
	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition>
		ChronoSpawnDefinitionFinder(
			TEXT("/Game/Data/GameplayActions/DA_ParadoxChronoSpawn.DA_ParadoxChronoSpawn"));
	if (ChronoSpawnDefinitionFinder.Succeeded())
	{
		ChronoSpawnActionDefinition = ChronoSpawnDefinitionFinder.Object;
	}
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

	OxygenWorldSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UParadoxOxygenWorldSubsystem>()
		: nullptr;
	if (!OxygenWorldSubsystem.IsValid())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidConfiguration,
			TEXT("The Paradox Oxygen World Subsystem is unavailable."),
			true);
	}
	if (!OxygenWorldSubsystem->IsConfigurationValid())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidConfiguration,
			OxygenWorldSubsystem->GetConfigurationDiagnostic(),
			true);
	}
	OxygenWorldSubsystem->OnGlobalOxygenDepletedNative().RemoveAll(this);
	OxygenWorldSubsystem->OnGlobalOxygenDepletedNative().AddUObject(
		this,
		&UParadoxTimeLoopComponent::HandleGlobalOxygenDepleted);

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
	if (!ChronoSpawnActionDefinition
		|| !ChronoSpawnActionDefinition->IsA<UParadoxChronoSpawnActionDefinition>())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidConfiguration,
			TEXT("The Time Loop requires DA_ParadoxChronoSpawn using UParadoxChronoSpawnActionDefinition."),
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
		|| !PlayerCharacter->GetTemporalEntityComponent()
		|| !PlayerCharacter->GetHealthComponent()
		|| !PlayerCharacter->GetPerceptionKnowledgeSourceComponent())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::MissingComponent,
			TEXT("The player is missing a required Gameplay Actions, Intent Replay, Health, temporal identity, or Perception Knowledge Source component."),
			true);
	}
	FString RecorderPreparationFailure;
	if (!PreparePlayerRecorder(RecorderPreparationFailure))
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RecordingFailed,
			RecorderPreparationFailure,
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

FParadoxTimeLoopOperationResult
UParadoxTimeLoopComponent::RequestChronoSpawnInteraction(
	AParadoxChronoSpawn* ChronoSpawn)
{
	if (!bTimeLoopEnabled)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedDisabled,
			TEXT("Chrono Spawn interaction was rejected because the time loop is disabled."),
			false);
	}
	if (!IsChronoSpawnSelectionOpen())
	{
		if (IsValid(ChronoSpawn))
		{
			OnChronoSpawnRejected.Broadcast(ChronoSpawn);
		}
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("The Time Loop is not currently accepting a Chrono Spawn interaction."),
			false);
	}
	if (!IsValid(ChronoSpawn)
		|| !ChronoSpawns.Contains(ChronoSpawn)
		|| !ChronoSpawn->CanAssignToNewTimeline())
	{
		if (IsValid(ChronoSpawn))
		{
			OnChronoSpawnRejected.Broadcast(ChronoSpawn);
		}
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidChronoSpawn,
			TEXT("The requested Chrono Spawn is invalid, disabled, inactive, or already occupied."),
			false);
	}

	const bool bInitialSelection =
		CurrentPhase == EParadoxTimeLoopPhase::ChronoSpawnSelection;
	SelectedChronoSpawn = ChronoSpawn;
	bPlayerMaterializedForRun = false;
	RefreshChronoSpawnInteractionAffordances();

	FString Failure;
	if (bInitialSelection)
	{
		SetPhase(EParadoxTimeLoopPhase::RunPreparation);
		if (!BeginPlayerRecording(Failure))
		{
			SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
			SelectedChronoSpawn = nullptr;
			RefreshChronoSpawnInteractionAffordances();
			return FailOperation(
				EParadoxTimeLoopOperationStatus::RecordingFailed,
				Failure,
				false);
		}
		SetPhase(EParadoxTimeLoopPhase::AwaitingSynchronizedStart);
		const FParadoxTimeLoopOperationResult AwaitingResult = MakeResult(
			EParadoxTimeLoopOperationStatus::Succeeded,
			TEXT("Initial run recorder started; Chrono Spawn materialization and synchronized start are pending."));
		OnSynchronizedStartAwaiting.Broadcast(AwaitingResult);
		if (!PrepareTemporalDetection(Failure)
			|| !PrepareClonePlaybacks(Failure))
		{
			RecoverFromSynchronizedStartFailure(Failure);
			return LastOperationResult;
		}
	}
	else
	{
		UIntentReplayComponent* Replay = IsValid(PlayerCharacter)
			? PlayerCharacter->GetIntentReplayComponent()
			: nullptr;
		if (!Replay
			|| Replay->GetRecordingState()
				!= EIntentRecordingState::Recording)
		{
			SelectedChronoSpawn = nullptr;
			RefreshChronoSpawnInteractionAffordances();
			return FailOperation(
				EParadoxTimeLoopOperationStatus::RecordingFailed,
				TEXT("A runtime Chrono Spawn interaction requires the global run recorder to be active."),
				false);
		}
	}

	const FParadoxTimeLoopOperationResult SubmissionResult =
		SubmitChronoSpawnAction(*ChronoSpawn);
	if (!SubmissionResult.IsSuccess())
	{
		SelectedChronoSpawn = nullptr;
		RefreshChronoSpawnInteractionAffordances();
		DeactivatePlayer();
		if (bInitialSelection)
		{
			RecoverFromSynchronizedStartFailure(
				SubmissionResult.DiagnosticMessage);
			return LastOperationResult;
		}
		return SubmissionResult;
	}

	TryReleaseSynchronizedStart();
	return MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("Timeline %d recorded Chrono Spawn '%s'; materialization is %s."),
			ConsolidatedTimelines.Num(),
			*GetNameSafe(SelectedChronoSpawn),
			bPlayerMaterializedForRun ? TEXT("complete") : TEXT("pending")));
}

FParadoxTimeLoopOperationResult UParadoxTimeLoopComponent::SelectChronoSpawn(
	AParadoxChronoSpawn* ChronoSpawn)
{
	return RequestChronoSpawnInteraction(ChronoSpawn);
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
	if (IsChronoSpawnSelectionOpen())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Rewind requires the player to select a Chrono Spawn first."),
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
	const TArray<FRecordedIntent>& FinalizedEntries =
		FinalizedTrack->GetEntries();
	int32 ChronoSpawnIntentCount = 0;
	for (const FRecordedIntent& Intent : FinalizedEntries)
	{
		if (Intent.ActionTag == ParadoxGameplayTags::Action_ChronoSpawn)
		{
			++ChronoSpawnIntentCount;
		}
	}
	if (FinalizedEntries.IsEmpty()
		|| FinalizedEntries[0].ActionTag
			!= ParadoxGameplayTags::Action_ChronoSpawn
		|| ChronoSpawnIntentCount != 1)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RecordingFailed,
			TEXT("A consolidated timeline requires exactly one Chrono Spawn action and it must be the first recorded gameplay intent."),
			true);
	}
	UIntentReplayObservationComponent* ObservationReplay =
		PlayerCharacter->GetObservationReplayComponent();
	UIntentReplayTimelineBundle* TimelineBundle = ObservationReplay
		? ObservationReplay->GetLastTimelineBundle()
		: nullptr;
	if (IsValid(TimelineBundle)
		&& (!TimelineBundle->ValidateBundle().bValid
			|| TimelineBundle->GetActionTrack() != FinalizedTrack))
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RecordingFailed,
			TEXT("The synchronized observation recorder published an invalid or mismatched Timeline Bundle for the finalized Action Track."),
			true);
	}
	if (!IsValid(TimelineBundle))
	{
		PARADOX_LOG_WARNING(
			TEXT("The finalized run has no synchronized Timeline Bundle and is being retained as a legacy action-only timeline; perceptual comparison will be unavailable for its clone."));
	}
	UPerceptionKnowledgeSourceComponent* PlayerPerceptionSource =
		PlayerCharacter->GetPerceptionKnowledgeSourceComponent();
	if (!PlayerPerceptionSource
		|| !PlayerPerceptionSource->IsSemanticallyRegistered()
		|| !PlayerPerceptionSource->GetEntityId().IsValid())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RecordingFailed,
			TEXT("The finalized player run has no registered, valid Perception Knowledge identity to transfer to its clone."),
			true);
	}
	const FPerceptionKnowledgeEntityId AvatarPerceptionEntityId =
		PlayerPerceptionSource->GetEntityId();

	FParadoxConsolidatedTimeline& Timeline = ConsolidatedTimelines.AddDefaulted_GetRef();
	Timeline.TemporalIndex = ConsolidatedTimelines.Num() - 1;
	Timeline.ChronoSpawn = SelectedChronoSpawn;
	Timeline.ReplayTrack = FinalizedTrack;
	Timeline.TimelineBundle = TimelineBundle;
	Timeline.AvatarPerceptionEntityId = AvatarPerceptionEntityId;
	SelectedChronoSpawn->SetAssignedToTimeline(true);

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
	if (OxygenWorldSubsystem.IsValid()
		&& OxygenWorldSubsystem->IsSharedGlobalEnabled()
		&& !OxygenWorldSubsystem->CommitCurrentAsRunCheckpoint())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InternalFailure,
			TEXT("The shared Oxygen reservoir could not commit the next run checkpoint after Time Travel."),
			true);
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

	ReapplyChronoSpawnStates(true);
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
	RefreshChronoSpawnInteractionAffordances();
	FString RuntimeStartFailure;
	if (!BeginPostResetRuntimeStart(RuntimeStartFailure))
	{
		RecoverFromSynchronizedStartFailure(RuntimeStartFailure);
		return LastOperationResult;
	}
	const FParadoxTimeLoopOperationResult ResetResult = MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("World reset completed, %d consolidated clones were reconstructed, and runtime Chrono Spawn selection is open."),
			RuntimeClones.Num()));
	OnWorldResetCompleted.Broadcast(ResetResult);
	PARADOX_LOG_INFO(TEXT("%s"), *ResetResult.DiagnosticMessage);
	return ResetResult;
}

bool UParadoxTimeLoopComponent::CompleteCloneTimeTravelDeparture(
	AParadoxCloneCharacter& Clone,
	FString& OutDiagnostic)
{
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Clone '%s' cannot complete Time Travel outside ActiveRun (phase %s)."),
			*GetNameSafe(&Clone),
			*UEnum::GetValueAsString(CurrentPhase));
		return false;
	}
	if (!RuntimeClones.Contains(&Clone))
	{
		OutDiagnostic = FString::Printf(
			TEXT("Clone '%s' is not owned by the current time-loop reconstruction."),
			*GetNameSafe(&Clone));
		return false;
	}
	if (CloneTimeTravelCompletionBehavior
		== EParadoxCloneTimeTravelCompletionBehavior::EnterGoap)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			OutDiagnostic = TEXT("Clone GOAP handoff cannot be scheduled without a World.");
			return false;
		}
		FTimerDelegate GoapHandoffDelegate = FTimerDelegate::CreateUObject(
			this,
			&UParadoxTimeLoopComponent::EnterCloneGoapAfterTimeTravel,
			TWeakObjectPtr<AParadoxCloneCharacter>(&Clone));
		World->GetTimerManager().SetTimerForNextTick(GoapHandoffDelegate);
		OutDiagnostic = FString::Printf(
			TEXT("Clone '%s' completed recorded Time Travel and scheduled terminal GOAP handoff."),
			*GetNameSafe(&Clone));
		PARADOX_LOG_INFO(TEXT("%s"), *OutDiagnostic);
		return true;
	}

	if (UParadoxOxygenComponent* Oxygen = Clone.GetOxygenComponent())
	{
		Oxygen->SetRunConsumptionActive(false);
	}

	bool bPerceptionDisabled = true;
	FString PerceptionFailure;
	if (AParadoxCloneController* Controller =
		Cast<AParadoxCloneController>(Clone.GetController()))
	{
		Controller->StopMovement();
		if (UPerceptionKnowledgeListenerComponent* Listener =
			Controller->GetPerceptionKnowledgeListener())
		{
			const FPerceptionKnowledgeOperationResult ListenerResult =
				Listener->SetListenerEnabled(false);
			if (!ListenerResult.IsSuccess())
			{
				bPerceptionDisabled = false;
				PerceptionFailure += FString::Printf(
					TEXT("listener='%s' "),
					*ListenerResult.Message);
			}
		}
	}
	if (UPerceptionKnowledgeSourceComponent* Source =
		Clone.GetPerceptionKnowledgeSourceComponent())
	{
		const FPerceptionKnowledgeOperationResult SourceResult =
			Source->SetSourceEnabled(false);
		if (!SourceResult.IsSuccess()
			|| Source->IsSemanticallyRegistered()
			|| Source->IsNativeStimuliSourceRegistered())
		{
			bPerceptionDisabled = false;
			PerceptionFailure += FString::Printf(
				TEXT("source='%s' "),
				*SourceResult.Message);
		}
	}
	if (UParadoxTemporalVisionComponent* Vision =
		Clone.GetTemporalVisionComponent())
	{
		Vision->DisableTemporalDetection(true);
	}

	if (UCharacterMovementComponent* Movement = Clone.GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	SetTemporalAvatarGridPresence(Clone, false);
	Clone.SetActorEnableCollision(false);
	Clone.SetActorHiddenInGame(true);

	if (!bPerceptionDisabled)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Clone '%s' was hidden and removed from GridWorld, but perceptual retirement failed: %s"),
			*GetNameSafe(&Clone),
			*PerceptionFailure);
		PARADOX_LOG_ERROR(TEXT("%s"), *OutDiagnostic);
		return false;
	}

	OutDiagnostic = FString::Printf(
		TEXT("Clone '%s' completed recorded Time Travel and was retired in place."),
		*GetNameSafe(&Clone));
	PARADOX_LOG_INFO(TEXT("%s"), *OutDiagnostic);
	return true;
}

void UParadoxTimeLoopComponent::EnterCloneGoapAfterTimeTravel(
	const TWeakObjectPtr<AParadoxCloneCharacter> WeakClone)
{
	AParadoxCloneCharacter* Clone = WeakClone.Get();
	if (!IsValid(Clone)
		|| CurrentPhase != EParadoxTimeLoopPhase::ActiveRun
		|| !RuntimeClones.Contains(Clone))
	{
		return;
	}

	UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
		Clone->GetBehaviorCoordinator();
	const FParadoxCloneBehaviorOperationResult HandoffResult = Coordinator
		? Coordinator->RequestEnterGoapMode()
		: FParadoxCloneBehaviorOperationResult();
	if (Coordinator && HandoffResult.IsSuccess())
	{
		SetClonePlaybackMovementEnabled(*Clone, false);
		if (UParadoxOxygenComponent* Oxygen = Clone->GetOxygenComponent())
		{
			// Terminal GOAP is still a live in-world temporal participant.
			Oxygen->SetRunConsumptionActive(true);
		}
		PARADOX_LOG_INFO(
			TEXT("Clone '%s' entered terminal GOAP placeholder mode after recorded Time Travel."),
			*GetNameSafe(Clone));
		return;
	}

	FParadoxClonePlaybackRuntime* Runtime =
		ClonePlaybackRuntimes.FindByPredicate(
			[Clone](const FParadoxClonePlaybackRuntime& Candidate)
			{
				return Candidate.Clone.Get() == Clone;
			});
	const FString Diagnostic = Coordinator
		? HandoffResult.DiagnosticMessage
		: TEXT("The clone has no behavior coordinator for terminal GOAP handoff.");
	if (Runtime)
	{
		FIntentReplayFailure Failure;
		Failure.DiagnosticMessage = Diagnostic;
		const EIntentReplayPlaybackState ExecutorState = Runtime->ReplayComponent.IsValid()
			? Runtime->ReplayComponent->GetPlaybackState()
			: EIntentReplayPlaybackState::Failed;
		MarkClonePlaybackFailed(*Runtime, Failure, ExecutorState);
	}
	else
	{
		SetClonePlaybackMovementEnabled(*Clone, false);
	}
	PARADOX_LOG_ERROR(
		TEXT("Clone '%s' could not enter terminal GOAP placeholder mode: %s"),
		*GetNameSafe(Clone),
		*Diagnostic);
}

FParadoxTimeLoopOperationResult
UParadoxTimeLoopComponent::ContinueParadoxRecovery(
	const FGuid ParadoxEventId)
{
	if (LastRunFailureContext.Reason
		!= EParadoxRunFailureReason::TemporalParadox)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Paradox recovery cannot acknowledge a non-paradox run failure."),
			false);
	}
	return ContinueRunFailureRecovery(ParadoxEventId);
}

FParadoxTimeLoopOperationResult
UParadoxTimeLoopComponent::ContinueRunFailureRecovery(
	const FGuid FailureEventId)
{
	if (CurrentPhase != EParadoxTimeLoopPhase::ParadoxFailure)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Run-failure recovery is only legal during ParadoxFailure."),
			false);
	}
	if (!FailureEventId.IsValid()
		|| FailureEventId != LastRunFailureContext.EventId)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			TEXT("Run-failure recovery acknowledgement belongs to a stale event."),
			false);
	}

	FString Failure;
	if (!RestoreWorldAndReconstructAfterRunFailure(Failure))
	{
		if (CurrentPhase == EParadoxTimeLoopPhase::ChronoSpawnSelection
			&& LastOperationResult.Status
				== EParadoxTimeLoopOperationStatus::SynchronizedStartFailed)
		{
			return LastOperationResult;
		}
		DestroyRuntimeClones();
		return FailOperation(
			LastRunFailureContext.Reason
				== EParadoxRunFailureReason::TemporalParadox
				? EParadoxTimeLoopOperationStatus::ParadoxRecoveryFailed
				: EParadoxTimeLoopOperationStatus::RunFailureRecoveryFailed,
			Failure,
			true);
	}

	const FParadoxTimeLoopOperationResult Result = MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("Run-failure recovery restored %d consolidated timeline(s); the failed Chrono Spawn is available again."),
			ConsolidatedTimelines.Num()));
	OnRunFailureRecoveryCompleted.Broadcast(Result);
	if (LastRunFailureContext.Reason
		== EParadoxRunFailureReason::TemporalParadox)
	{
		OnParadoxRecoveryCompleted.Broadcast(Result);
	}
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

bool UParadoxTimeLoopComponent::IsMovementAllowed() const
{
	return !bTimeLoopEnabled
		|| CurrentPhase == EParadoxTimeLoopPhase::Disabled
		|| (CurrentPhase == EParadoxTimeLoopPhase::ActiveRun
			&& !IsChronoSpawnSelectionOpen());
}

bool UParadoxTimeLoopComponent::IsChronoSpawnSelectionOpen() const
{
	if (!bTimeLoopEnabled || IsValid(SelectedChronoSpawn))
	{
		return false;
	}
	return CurrentPhase == EParadoxTimeLoopPhase::ChronoSpawnSelection
		|| (ConsolidatedTimelines.Num() > 0
			&& (CurrentPhase == EParadoxTimeLoopPhase::AwaitingSynchronizedStart
				|| CurrentPhase == EParadoxTimeLoopPhase::ActiveRun));
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
	if (OxygenWorldSubsystem.IsValid())
	{
		OxygenWorldSubsystem->OnGlobalOxygenDepletedNative().RemoveAll(this);
	}
	OxygenWorldSubsystem.Reset();
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
	if (PreviousPhase == EParadoxTimeLoopPhase::ActiveRun
		&& CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
	{
		SetTemporalOxygenConsumptionActive(false);
	}
	else if (PreviousPhase != EParadoxTimeLoopPhase::ActiveRun
		&& CurrentPhase == EParadoxTimeLoopPhase::ActiveRun)
	{
		SetTemporalOxygenConsumptionActive(true);
	}
	OnPhaseChanged.Broadcast(PreviousPhase, CurrentPhase);
	RefreshChronoSpawnInteractionAffordances();
	PARADOX_LOG_INFO(
		TEXT("Time-loop phase changed from %d to %d in world '%s'."),
		static_cast<int32>(PreviousPhase),
		static_cast<int32>(CurrentPhase),
		*GetNameSafe(GetWorld()));
}

void UParadoxTimeLoopComponent::RefreshChronoSpawnInteractionAffordances() const
{
	for (const AParadoxChronoSpawn* Spawn : ChronoSpawns)
	{
		if (!IsValid(Spawn))
		{
			continue;
		}
		if (UParadoxInteractionComponent* Interaction =
			Spawn->GetInteractionComponent())
		{
			Interaction->NotifyInteractionAffordanceChanged();
		}
	}
}

void UParadoxTimeLoopComponent::SetTemporalOxygenConsumptionActive(
	const bool bActive)
{
	if (IsValid(PlayerCharacter))
	{
		if (UParadoxOxygenComponent* Oxygen =
			PlayerCharacter->GetOxygenComponent())
		{
			Oxygen->SetRunConsumptionActive(
				bActive
				&& IsValid(SelectedChronoSpawn)
				&& !PlayerCharacter->IsHidden());
		}
		else
		{
			PARADOX_LOG_ERROR(
				TEXT("Player '%s' has no Oxygen component during time-loop phase transition."),
				*GetNameSafe(PlayerCharacter));
		}
	}

	for (AParadoxCloneCharacter* Clone : RuntimeClones)
	{
		if (!IsValid(Clone))
		{
			continue;
		}
		if (UParadoxOxygenComponent* Oxygen = Clone->GetOxygenComponent())
		{
			const UParadoxTemporalEntityComponent* Temporal =
				Clone->GetTemporalEntityComponent();
			const FParadoxClonePlaybackRuntime* Runtime = Temporal
				? FindClonePlaybackRuntime(Temporal->GetTemporalIndex())
				: nullptr;
			Oxygen->SetRunConsumptionActive(
				bActive
				&& Runtime
				&& Runtime->TemporalSpawnState
					== EParadoxTemporalSpawnState::Materialized);
		}
		else
		{
			PARADOX_LOG_ERROR(
				TEXT("Clone '%s' has no Oxygen component during time-loop phase transition."),
				*GetNameSafe(Clone));
		}
	}
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
	UParadoxHealthComponent* Health = PlayerCharacter->GetHealthComponent();
	UParadoxOxygenComponent* Oxygen = PlayerCharacter->GetOxygenComponent();
	if (!Health || !Oxygen)
	{
		OutFailure = TEXT("The player requires Health and Oxygen components for the new run.");
		return false;
	}
	Health->ResetHealth();
	if (!Oxygen->IsUsingSharedGlobalOxygen())
	{
		Oxygen->ResetOxygen();
	}

	UParadoxTemporalEntityComponent* TemporalComponent =
		PlayerCharacter->GetTemporalEntityComponent();
	if (!TemporalComponent
		|| !TemporalComponent->AssignPlayer(ConsolidatedTimelines.Num()))
	{
		OutFailure = TEXT("The player temporal identity could not be assigned.");
		return false;
	}

	UPerceptionKnowledgeSourceComponent* PerceptionSource =
		PlayerCharacter->GetPerceptionKnowledgeSourceComponent();
	if (!PerceptionSource)
	{
		OutFailure =
			TEXT("The player has no Perception Knowledge Source to identify the new run.");
		return false;
	}
	const FPerceptionKnowledgeOperationResult DisableSourceResult =
		PerceptionSource->SetSourceEnabled(false);
	if (!DisableSourceResult.IsSuccess())
	{
		OutFailure = FString::Printf(
			TEXT("Player Perception Source could not be disabled before assigning a new run identity: %s"),
			*DisableSourceResult.Message);
		return false;
	}
	const FPerceptionKnowledgeEntityId NewRunEntityId =
		FPerceptionKnowledgeEntityId::NewId();
	const FPerceptionKnowledgeOperationResult IdentityResult =
		PerceptionSource->AssignEntityId(NewRunEntityId);
	if (!IdentityResult.IsSuccess())
	{
		OutFailure = FString::Printf(
			TEXT("Player Perception Source rejected the new run identity %s: %s"),
			*NewRunEntityId.ToString(),
			*IdentityResult.Message);
		return false;
	}
	const FPerceptionKnowledgeOperationResult EnableSourceResult =
		PerceptionSource->SetSourceEnabled(true);
	if (!EnableSourceResult.IsSuccess()
		|| !PerceptionSource->IsSemanticallyRegistered())
	{
		OutFailure = FString::Printf(
			TEXT("Player Perception Source could not register identity %s for the new run: %s"),
			*NewRunEntityId.ToString(),
			*EnableSourceResult.Message);
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
		PlayerCharacter->UnCrouch();
		if (PlayerCharacter->IsCrouched())
		{
			// ACharacter::UnCrouch records the persistent desire for the next movement update.
			// Apply the matching Character Movement operation now as the deterministic run baseline.
			Movement->UnCrouch(false);
		}
		if (PlayerCharacter->IsCrouched() || Movement->bWantsToCrouch)
		{
			OutFailure =
				TEXT("The player could not establish the standing baseline for the new run.");
			return false;
		}
	}
	FString ListenerFailure;
	if (!SetPlayerPerceptionListenerEnabled(true, ListenerFailure))
	{
		OutFailure = ListenerFailure;
		return false;
	}
	bPlayerMaterializedForRun = true;
	return true;
}

FParadoxTimeLoopOperationResult UParadoxTimeLoopComponent::SubmitChronoSpawnAction(
	AParadoxChronoSpawn& ChronoSpawn)
{
	UGameplayActionComponent* Actions = IsValid(PlayerCharacter)
		? PlayerCharacter->GetGameplayActionComponent()
		: nullptr;
	UParadoxInteractionComponent* Interaction =
		ChronoSpawn.GetInteractionComponent();
	if (!Actions || !Interaction || !ChronoSpawnActionDefinition)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::MissingComponent,
			TEXT("Chrono Spawn submission requires player Gameplay Actions, the target Interaction Component, and the configured Definition."),
			false);
	}

	// Post-reset Chrono Spawn selection happens while Tactical Pause owns the
	// player's scheduler pause. This system action must materialize immediately,
	// without releasing any player-planned work. A fresh temporal avatar has no
	// other runtime actions; enforce that invariant before opening the scheduler
	// for this one synchronous submission, then restore the owned pause.
	const bool bRestoreSchedulerPause = Actions->IsActionsPaused();
	if (bRestoreSchedulerPause)
	{
		if (!Actions->GetActiveActionHandles().IsEmpty()
			|| !Actions->GetQueuedActionHandles().IsEmpty())
		{
			return FailOperation(
				EParadoxTimeLoopOperationStatus::InvalidConfiguration,
				TEXT("Chrono Spawn cannot open the paused player scheduler while another action is active or queued."),
				false);
		}
		if (Actions->ResumeActions()
			!= EGameplayActionOperationResult::Succeeded)
		{
			return FailOperation(
				EParadoxTimeLoopOperationStatus::InvalidConfiguration,
				TEXT("Chrono Spawn could not temporarily open the paused player scheduler."),
				false);
		}
	}
	const FParadoxInteractionRequestResult Submission =
		Interaction->RequestInteraction(
			PlayerCharacter,
			ParadoxGameplayTags::Interaction_ChronoSpawn_Spawn,
			ParadoxGameplayTags::Origin_Player,
			this);
	if (bRestoreSchedulerPause
		&& Actions->PauseActions()
			!= EGameplayActionOperationResult::Succeeded)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidConfiguration,
			TEXT("Chrono Spawn submission completed but the player scheduler pause could not be restored."),
			true);
	}
	if (!Submission.IsAccepted())
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::InvalidChronoSpawn,
			Submission.DiagnosticMessage,
			false);
	}
	return MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		TEXT("Chrono Spawn Gameplay Action was accepted and recorded."));
}

void UParadoxTimeLoopComponent::DeactivatePlayer()
{
	if (!IsValid(PlayerCharacter))
	{
		return;
	}
	bPlayerMaterializedForRun = false;
	FString ListenerFailure;
	if (!SetPlayerPerceptionListenerEnabled(false, ListenerFailure))
	{
		PARADOX_LOG_ERROR(TEXT("%s"), *ListenerFailure);
	}

	if (UGameplayActionComponent* Actions = PlayerCharacter->GetGameplayActionComponent())
	{
		Actions->AbortAllActions(GameplayActionTags::Result_Aborted_SystemReset);
	}
	if (UPerceptionKnowledgeSourceComponent* PerceptionSource =
		PlayerCharacter->GetPerceptionKnowledgeSourceComponent())
	{
		const FPerceptionKnowledgeOperationResult DisableResult =
			PerceptionSource->SetSourceEnabled(false);
		if (!DisableResult.IsSuccess()
			|| PerceptionSource->IsSemanticallyRegistered()
			|| PerceptionSource->IsNativeStimuliSourceRegistered())
		{
			PARADOX_LOG_ERROR(
				TEXT("Player '%s' Perception Source could not fully unregister identity %s during deactivation: %s"),
				*GetNameSafe(PlayerCharacter),
				*PerceptionSource->GetEntityId().ToString(),
				*DisableResult.Message);
		}
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

bool UParadoxTimeLoopComponent::CanStartChronoSpawnAction(
	const AParadoxCharacter& TemporalAvatar,
	const AParadoxChronoSpawn& ChronoSpawn,
	const FGameplayTag OriginTag,
	FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	if (!bTimeLoopEnabled
		|| !ChronoSpawns.Contains(&ChronoSpawn)
		|| !ChronoSpawn.IsChronoSpawnEnabled())
	{
		OutDiagnostic =
			TEXT("The Chrono Spawn is not an enabled target owned by this Time Loop.");
		return false;
	}

	if (OriginTag == IntentReplayTags::Origin_Replay)
	{
		const AParadoxCloneCharacter* Clone =
			Cast<AParadoxCloneCharacter>(&TemporalAvatar);
		const UParadoxTemporalEntityComponent* Temporal = Clone
			? Clone->GetTemporalEntityComponent()
			: nullptr;
		const FParadoxClonePlaybackRuntime* Runtime = Temporal
			? FindClonePlaybackRuntime(Temporal->GetTemporalIndex())
			: nullptr;
		if (!Clone || !Runtime || Runtime->ChronoSpawn.Get() != &ChronoSpawn)
		{
			OutDiagnostic =
				TEXT("Replay Chrono Spawn does not match the clone's consolidated timeline assignment.");
			return false;
		}
		return true;
	}

	if (&TemporalAvatar != PlayerCharacter)
	{
		OutDiagnostic =
			TEXT("Only the authoritative Player or an Intent Replay clone can execute Chrono Spawn.");
		return false;
	}
	if (!ChronoSpawn.CanAssignToNewTimeline())
	{
		OutDiagnostic =
			TEXT("The Chrono Spawn is inactive, disabled, or already occupied.");
		return false;
	}

	// Availability preflight intentionally uses no Origin. It previews the button before the
	// Time Loop begins recording or commits a selected spawn.
	if (!OriginTag.IsValid())
	{
		if (!IsChronoSpawnSelectionOpen())
		{
			OutDiagnostic =
				TEXT("The Time Loop is not waiting for a new Chrono Spawn.");
			return false;
		}
		return true;
	}

	if (OriginTag != ParadoxGameplayTags::Origin_Player
		|| SelectedChronoSpawn != &ChronoSpawn
		|| bPlayerMaterializedForRun)
	{
		OutDiagnostic =
			TEXT("Player Chrono Spawn execution was not prepared by the Time Loop.");
		return false;
	}
	const UIntentReplayComponent* Replay =
		PlayerCharacter ? PlayerCharacter->GetIntentReplayComponent() : nullptr;
	if (!Replay
		|| Replay->GetRecordingState() != EIntentRecordingState::Recording)
	{
		OutDiagnostic =
			TEXT("Player Chrono Spawn execution requires an active recording session.");
		return false;
	}
	return true;
}

EParadoxChronoSpawnExecutionResult
UParadoxTimeLoopComponent::TryExecuteChronoSpawnAction(
	AParadoxCharacter& TemporalAvatar,
	AParadoxChronoSpawn& ChronoSpawn,
	FString& OutDiagnostic)
{
	OutDiagnostic.Reset();
	if (!ChronoSpawns.Contains(&ChronoSpawn)
		|| !ChronoSpawn.IsChronoSpawnEnabled())
	{
		OutDiagnostic =
			TEXT("Chrono Spawn action target is not an enabled spawn owned by this Time Loop.");
		return EParadoxChronoSpawnExecutionResult::Failed;
	}
	if (!ChronoSpawn.IsChronoSpawnActive())
	{
		if (AParadoxCloneCharacter* Clone =
			Cast<AParadoxCloneCharacter>(&TemporalAvatar))
		{
			const UParadoxTemporalEntityComponent* Temporal =
				Clone->GetTemporalEntityComponent();
			if (FParadoxClonePlaybackRuntime* Runtime = Temporal
				? FindClonePlaybackRuntime(Temporal->GetTemporalIndex())
				: nullptr)
			{
				SetCloneTemporalSpawnState(
					*Runtime,
					EParadoxTemporalSpawnState::PendingActivation);
			}
		}
		OutDiagnostic = FString::Printf(
			TEXT("Chrono Spawn '%s' is inactive; temporal materialization remains pending."),
			*GetNameSafe(&ChronoSpawn));
		return EParadoxChronoSpawnExecutionResult::PendingActivation;
	}

	if (&TemporalAvatar == PlayerCharacter)
	{
		if (SelectedChronoSpawn != &ChronoSpawn
			|| !ChronoSpawn.CanAssignToNewTimeline())
		{
			OutDiagnostic =
				TEXT("Player Chrono Spawn action no longer matches the selected free spawn.");
			return EParadoxChronoSpawnExecutionResult::Failed;
		}
		if (!ActivatePlayerAtSelectedSpawn(OutDiagnostic))
		{
			return EParadoxChronoSpawnExecutionResult::Failed;
		}
		SetPlayerMovementEnabled(CurrentPhase == EParadoxTimeLoopPhase::ActiveRun);
		if (CurrentPhase == EParadoxTimeLoopPhase::ActiveRun)
		{
			SetTemporalOxygenConsumptionActive(true);
			RefreshTemporalDetectionAfterPlayerActivation();
		}
		OnChronoSpawnSelected.Broadcast(&ChronoSpawn);
		OutDiagnostic = FString::Printf(
			TEXT("Player materialized at Chrono Spawn '%s'."),
			*GetNameSafe(&ChronoSpawn));
		return EParadoxChronoSpawnExecutionResult::Materialized;
	}

	AParadoxCloneCharacter* Clone =
		Cast<AParadoxCloneCharacter>(&TemporalAvatar);
	const UParadoxTemporalEntityComponent* Temporal = Clone
		? Clone->GetTemporalEntityComponent()
		: nullptr;
	FParadoxClonePlaybackRuntime* Runtime = Temporal
		? FindClonePlaybackRuntime(Temporal->GetTemporalIndex())
		: nullptr;
	if (!Clone || !Runtime || Runtime->ChronoSpawn.Get() != &ChronoSpawn)
	{
		OutDiagnostic =
			TEXT("Clone Chrono Spawn action does not match its consolidated timeline assignment.");
		return EParadoxChronoSpawnExecutionResult::Failed;
	}
	if (!MaterializeCloneAtChronoSpawn(*Runtime, ChronoSpawn, OutDiagnostic))
	{
		SetCloneTemporalSpawnState(
			*Runtime,
			EParadoxTemporalSpawnState::Failed);
		return EParadoxChronoSpawnExecutionResult::Failed;
	}
	return EParadoxChronoSpawnExecutionResult::Materialized;
}

void UParadoxTimeLoopComponent::CancelPendingChronoSpawnAction(
	AParadoxCharacter& TemporalAvatar)
{
	const AParadoxCloneCharacter* Clone =
		Cast<AParadoxCloneCharacter>(&TemporalAvatar);
	const UParadoxTemporalEntityComponent* Temporal = Clone
		? Clone->GetTemporalEntityComponent()
		: nullptr;
	if (FParadoxClonePlaybackRuntime* Runtime = Temporal
		? FindClonePlaybackRuntime(Temporal->GetTemporalIndex())
		: nullptr)
	{
		if (Runtime->TemporalSpawnState
			== EParadoxTemporalSpawnState::PendingActivation)
		{
			SetCloneTemporalSpawnState(
				*Runtime,
				EParadoxTemporalSpawnState::Dormant);
		}
	}
}

void UParadoxTimeLoopComponent::SetPlayerMovementEnabled(
	const bool bEnabled) const
{
	if (!IsValid(PlayerCharacter))
	{
		return;
	}
	if (!bEnabled)
	{
		if (UGameplayActionComponent* Actions =
			PlayerCharacter->GetGameplayActionComponent())
		{
			Actions->AbortAllActions(
				GameplayActionTags::Result_Aborted_SystemReset);
		}
	}
	if (AController* Controller = PlayerCharacter->GetController())
	{
		Controller->StopMovement();
	}
	if (UCharacterMovementComponent* Movement =
		PlayerCharacter->GetCharacterMovement())
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

bool UParadoxTimeLoopComponent::SetPlayerPerceptionListenerEnabled(
	const bool bEnabled,
	FString& OutFailure) const
{
	OutFailure.Reset();
	AParadoxPlayerController* Controller = GetWorld()
		? Cast<AParadoxPlayerController>(GetWorld()->GetFirstPlayerController())
		: nullptr;
	UPerceptionKnowledgeListenerComponent* Listener = Controller
		? Controller->GetPerceptionKnowledgeListener()
		: nullptr;
	if (!Listener)
	{
		if (!bEnabled)
		{
			// A controller without a listener is already perceptually inactive. This also
			// keeps terminal/reset cleanup safe for lightweight test and fallback controllers.
			return true;
		}
		OutFailure = TEXT("The player controller has no Perception Knowledge Listener.");
		return false;
	}
	const FPerceptionKnowledgeOperationResult Result =
		Listener->SetListenerEnabled(bEnabled);
	if (!Result.IsSuccess())
	{
		OutFailure = FString::Printf(
			TEXT("Player Perception Listener could not be %s: %s"),
			bEnabled ? TEXT("enabled") : TEXT("disabled"),
			*Result.Message);
		return false;
	}
	return true;
}

void UParadoxTimeLoopComponent::RefreshTemporalDetectionAfterPlayerActivation()
{
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
	{
		return;
	}
	for (const TWeakObjectPtr<UParadoxTemporalVisionComponent>& WeakVision :
		TemporalVisionParticipants)
	{
		if (UParadoxTemporalVisionComponent* Vision = WeakVision.Get())
		{
			Vision->RefreshTemporalCandidateFilter();
			if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun)
			{
				break;
			}
		}
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
	UIntentReplayObservationComponent* Observation =
		PlayerCharacter->GetObservationReplayComponent();
	AParadoxPlayerController* Controller = GetWorld()
		? Cast<AParadoxPlayerController>(GetWorld()->GetFirstPlayerController())
		: nullptr;
	UPerceptionKnowledgeListenerComponent* Listener = Controller
		? Controller->GetPerceptionKnowledgeListener()
		: nullptr;
	if (!Observation || !Listener)
	{
		OutFailure =
			TEXT("The player is missing synchronized Observation Replay or its controller-owned PerceptionKnowledge listener.");
		return false;
	}
	Observation->SetIntentReplaySource(Replay);
	Observation->SetPerceptionKnowledgeListener(Listener);
	const FIntentReplayObservationOperationResult ObservationInitialization =
		Observation->InitializeObservationReplay();
	if (!ObservationInitialization.Succeeded())
	{
		OutFailure = FString::Printf(
			TEXT("Player Observation Replay initialization failed: %s"),
			*ObservationInitialization.DiagnosticMessage);
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

bool UParadoxTimeLoopComponent::BeginPostResetRuntimeStart(
	FString& OutFailure)
{
	OutFailure.Reset();
	if (ConsolidatedTimelines.IsEmpty())
	{
		SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
		return true;
	}
	if (!PreparePlayerRecorder(OutFailure))
	{
		return false;
	}
	if (!BeginPlayerRecording(OutFailure))
	{
		return false;
	}

	SetPhase(EParadoxTimeLoopPhase::AwaitingSynchronizedStart);
	if (!RequestPostResetTacticalPause(OutFailure))
	{
		return false;
	}
	const FParadoxTimeLoopOperationResult AwaitingResult = MakeResult(
		EParadoxTimeLoopOperationStatus::Succeeded,
		FString::Printf(
			TEXT("Timeline %d is tactically paused while awaiting clone readiness; Chrono Spawn selection remains open."),
			ConsolidatedTimelines.Num()));
	OnSynchronizedStartAwaiting.Broadcast(AwaitingResult);

	if (!PrepareTemporalDetection(OutFailure)
		|| !PrepareClonePlaybacks(OutFailure))
	{
		return false;
	}

	TryReleaseSynchronizedStart();
	return true;
}

bool UParadoxTimeLoopComponent::RequestPostResetTacticalPause(
	FString& OutFailure)
{
	bPostResetTacticalPauseAcquiredForPendingStart = false;
	UTacticalPauseWorldSubsystem* TacticalPause = GetWorld()
		? GetWorld()->GetSubsystem<UTacticalPauseWorldSubsystem>()
		: nullptr;
	if (!TacticalPause)
	{
		OutFailure =
			TEXT("The Tactical Pause World Subsystem is unavailable for post-reset start.");
		return false;
	}

	const ETacticalPauseRequestResult PauseResult =
		TacticalPause->RequestPause();
	if (PauseResult == ETacticalPauseRequestResult::Succeeded)
	{
		bPostResetTacticalPauseAcquiredForPendingStart = true;
		return true;
	}
	if (PauseResult == ETacticalPauseRequestResult::AlreadyInRequestedState
		&& TacticalPause->IsPaused()
		&& TacticalPause->CanPlay())
	{
		return true;
	}

	OutFailure = FString::Printf(
		TEXT("Tactical Pause could not establish an owned, resumable post-reset pause (result %s)."),
		*UEnum::GetValueAsString(PauseResult));
	return false;
}

void UParadoxTimeLoopComponent::ReleasePendingPostResetTacticalPause()
{
	if (!bPostResetTacticalPauseAcquiredForPendingStart)
	{
		return;
	}
	bPostResetTacticalPauseAcquiredForPendingStart = false;
	UTacticalPauseWorldSubsystem* TacticalPause = GetWorld()
		? GetWorld()->GetSubsystem<UTacticalPauseWorldSubsystem>()
		: nullptr;
	if (TacticalPause && TacticalPause->IsPaused() && TacticalPause->CanPlay())
	{
		const ETacticalPauseRequestResult PlayResult =
			TacticalPause->RequestPlay();
		if (PlayResult != ETacticalPauseRequestResult::Succeeded
			&& PlayResult != ETacticalPauseRequestResult::AlreadyInRequestedState)
		{
			PARADOX_LOG_ERROR(
				TEXT("Failed to release post-reset Tactical Pause after synchronized-start rollback (result %s)."),
				*UEnum::GetValueAsString(PlayResult));
		}
	}
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
	bRunFailureAcceptedForRun = false;
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
			const AParadoxCloneCharacter* Clone =
				Cast<AParadoxCloneCharacter>(Vision->GetOwner());
			const UParadoxTemporalEntityComponent* Temporal = Clone
				? Clone->GetTemporalEntityComponent()
				: nullptr;
			const FParadoxClonePlaybackRuntime* Runtime = Temporal
				? FindClonePlaybackRuntime(Temporal->GetTemporalIndex())
				: nullptr;
			if (!Runtime
				|| Runtime->TemporalSpawnState
					!= EParadoxTemporalSpawnState::Materialized)
			{
				Vision->DisableTemporalDetection(true);
				continue;
			}
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
		|| bRunFailureAcceptedForRun)
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

FParadoxTimeLoopOperationResult UParadoxTimeLoopComponent::AcceptPlayerDeath(
	AParadoxPlayerCharacter& DeadPlayer,
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	if (!bTimeLoopEnabled
		|| CurrentPhase != EParadoxTimeLoopPhase::ActiveRun
		|| PlayerCharacter != &DeadPlayer
		|| bRunFailureAcceptedForRun)
	{
		return FailOperation(
			EParadoxTimeLoopOperationStatus::RejectedInvalidPhase,
			FString::Printf(
				TEXT("Player death for '%s' was received outside its authoritative ActiveRun."),
				*GetNameSafe(&DeadPlayer)),
			false);
	}

	FParadoxRunFailureContext Context;
	Context.EventId = FGuid::NewGuid();
	Context.Reason = EParadoxRunFailureReason::PlayerDeath;
	Context.Player = &DeadPlayer;
	Context.DamageTypeClass = DamageType
		? DamageType->GetClass()
		: UDamageType::StaticClass();
	Context.InstigatedBy = InstigatedBy;
	Context.DamageCauser = DamageCauser;
	Context.DiagnosticMessage = FString::Printf(
		TEXT("Player '%s' died during the active timeline."),
		*GetNameSafe(&DeadPlayer));
	EnterRunFailure(
		Context,
		EParadoxTimeLoopOperationStatus::PlayerDeathAccepted);
	PresentRunFailureOrRecoverImmediately();
	return LastOperationResult;
}

void UParadoxTimeLoopComponent::HandleGlobalOxygenDepleted()
{
	if (!bTimeLoopEnabled
		|| CurrentPhase != EParadoxTimeLoopPhase::ActiveRun
		|| bRunFailureAcceptedForRun
		|| !OxygenWorldSubsystem.IsValid()
		|| !OxygenWorldSubsystem->IsSharedGlobalEnabled())
	{
		return;
	}

	FParadoxRunFailureContext Context;
	Context.EventId = FGuid::NewGuid();
	Context.Reason = EParadoxRunFailureReason::GlobalOxygenDepleted;
	Context.Player = PlayerCharacter;
	Context.DamageTypeClass =
		UParadoxOxygenDepletionDamageType::StaticClass();
	Context.DiagnosticMessage = FString::Printf(
		TEXT("The shared Oxygen reservoir depleted during timeline %d."),
		ConsolidatedTimelines.Num());
	EnterRunFailure(
		Context,
		EParadoxTimeLoopOperationStatus::GlobalOxygenDepletionAccepted);
	PresentRunFailureOrRecoverImmediately();
}

void UParadoxTimeLoopComponent::AcceptParadox(
	const FParadoxTemporalCandidateSnapshot& Candidate)
{
	if (CurrentPhase != EParadoxTimeLoopPhase::ActiveRun
		|| bRunFailureAcceptedForRun)
	{
		return;
	}

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

	FParadoxRunFailureContext FailureContext;
	FailureContext.EventId = LastParadoxContext.EventId;
	FailureContext.Reason = EParadoxRunFailureReason::TemporalParadox;
	FailureContext.Player = PlayerCharacter;
	FailureContext.ParadoxContext = LastParadoxContext;
	FailureContext.DiagnosticMessage = FString::Printf(
		TEXT("Timeline collapse: T%d witnessed T%d."),
		LastParadoxContext.ObserverTemporalIndex,
		LastParadoxContext.TargetTemporalIndex);
	EnterRunFailure(
		FailureContext,
		EParadoxTimeLoopOperationStatus::ParadoxAccepted);
	OnParadoxAccepted.Broadcast(LastParadoxContext);
	PresentRunFailureOrRecoverImmediately();
}

void UParadoxTimeLoopComponent::EnterRunFailure(
	const FParadoxRunFailureContext& Context,
	const EParadoxTimeLoopOperationStatus Status)
{
	bRunFailureAcceptedForRun = true;
	LastRunFailureContext = Context;
	SetPhase(EParadoxTimeLoopPhase::ParadoxFailure);
	StopActiveRunWithoutConsolidation();
	const FParadoxTimeLoopOperationResult Result = MakeResult(
		Status,
		Context.DiagnosticMessage);
	OnRunEnded.Broadcast(Result);
	OnRunFailureAccepted.Broadcast(LastRunFailureContext);
	PARADOX_LOG_WARNING(TEXT("%s"), *Result.DiagnosticMessage);
}

bool UParadoxTimeLoopComponent::RestoreWorldAndReconstructAfterRunFailure(
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
			TEXT("World State subsystem disappeared during run-failure recovery.");
		return false;
	}
	FWorldStateRestoreRequest RestoreRequest;
	switch (LastRunFailureContext.Reason)
	{
	case EParadoxRunFailureReason::PlayerDeath:
		RestoreRequest.Reason = TEXT("PlayerDeath");
		break;
	case EParadoxRunFailureReason::GlobalOxygenDepleted:
		RestoreRequest.Reason = TEXT("GlobalOxygenDepleted");
		break;
	case EParadoxRunFailureReason::TemporalParadox:
	default:
		RestoreRequest.Reason = TEXT("ParadoxFailure");
		break;
	}
	const FWorldStateRestoreResult RestoreResult =
		WorldState->RestoreBaseline(RestoreRequest);
	if (!RestoreResult.IsSuccess())
	{
		OutFailure = FString::Printf(
			TEXT("Run-failure World State restore failed with status %d at stage %d."),
			static_cast<int32>(RestoreResult.Status),
			static_cast<int32>(RestoreResult.FailureStage));
		return false;
	}
	if (OxygenWorldSubsystem.IsValid()
		&& !OxygenWorldSubsystem->RestoreRunCheckpoint())
	{
		OutFailure =
			TEXT("The shared Oxygen reservoir could not restore the failed run checkpoint.");
		return false;
	}

	ReapplyChronoSpawnStates(true);
	SetPhase(EParadoxTimeLoopPhase::TimelineReconstruction);
	if (!ReconstructConsolidatedClones(OutFailure))
	{
		return false;
	}

	SelectedChronoSpawn = nullptr;
	RefreshChronoSpawnInteractionAffordances();
	if (ConsolidatedTimelines.IsEmpty())
	{
		SetPhase(EParadoxTimeLoopPhase::ChronoSpawnSelection);
		return true;
	}
	if (!BeginPostResetRuntimeStart(OutFailure))
	{
		RecoverFromSynchronizedStartFailure(OutFailure);
		return false;
	}
	return true;
}

void UParadoxTimeLoopComponent::PresentRunFailureOrRecoverImmediately()
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
		&& Presentation->BeginRunFailurePresentation(
			LastRunFailureContext))
	{
		return;
	}
	ContinueRunFailureRecovery(LastRunFailureContext.EventId);
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
		const FParadoxConsolidatedTimeline* SourceTimeline =
			ConsolidatedTimelines.FindByPredicate(
				[&Runtime](const FParadoxConsolidatedTimeline& Timeline)
				{
					return Timeline.TemporalIndex == Runtime.TemporalIndex;
				});
		Runtime.TimelineBundle = SourceTimeline
			? SourceTimeline->TimelineBundle
			: nullptr;
		Runtime.ChronoSpawn = SourceTimeline
			? SourceTimeline->ChronoSpawn
			: nullptr;
		Runtime.TemporalSpawnState =
			EParadoxTemporalSpawnState::WaitingForRecordedTime;

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
			UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
				Clone->GetBehaviorCoordinator();
			AParadoxCloneController* Controller =
				Cast<AParadoxCloneController>(Clone->GetController());
			const FParadoxCloneBehaviorOperationResult BehaviorInitialization =
				Coordinator
					? Coordinator->InitializeForRun(
						Runtime.TimelineBundle.Get())
					: FParadoxCloneBehaviorOperationResult();
			FString BehaviorTreeFailure;
			if (!Coordinator || !BehaviorInitialization.IsSuccess()
				|| !Controller
				|| !Controller->StartCloneBehaviorTree(
					BehaviorTreeFailure))
			{
				FIntentReplayFailure Failure;
				Failure.DiagnosticMessage = !BehaviorInitialization.IsSuccess()
					? BehaviorInitialization.DiagnosticMessage
					: BehaviorTreeFailure;
				MarkClonePlaybackFailed(
					Runtime,
					Failure,
					EIntentReplayPlaybackState::Failed);
				continue;
			}
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

		UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
			Clone->GetBehaviorCoordinator();
		const FParadoxCloneBehaviorOperationResult Authorization =
			Coordinator
				? Coordinator->AuthorizeReplayStart()
				: FParadoxCloneBehaviorOperationResult();
		if (!Coordinator || !Authorization.IsSuccess())
		{
			FIntentReplayFailure Failure;
			Failure.DiagnosticMessage = Authorization.DiagnosticMessage;
			MarkClonePlaybackFailed(
				Runtime,
				Failure,
				Replay->GetPlaybackState());
			continue;
		}
	}

	if (bPlayerMaterializedForRun)
	{
		SetPlayerMovementEnabled(true);
	}
	SetPhase(EParadoxTimeLoopPhase::ActiveRun);
	bPostResetTacticalPauseAcquiredForPendingStart = false;
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
	ReleasePendingPostResetTacticalPause();
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
		&& !SelectedChronoSpawn->IsAssignedToTimeline())
	{
		SelectedChronoSpawn->SetAssignedToTimeline(false);
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

FParadoxClonePlaybackRuntime*
UParadoxTimeLoopComponent::FindClonePlaybackRuntime(
	const int32 TemporalIndex)
{
	return ClonePlaybackRuntimes.FindByPredicate(
		[TemporalIndex](const FParadoxClonePlaybackRuntime& Runtime)
		{
			return Runtime.TemporalIndex == TemporalIndex;
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
	Snapshot.TemporalSpawnState = Runtime.TemporalSpawnState;
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
	if (Runtime.TemporalSpawnState
		!= EParadoxTemporalSpawnState::Materialized)
	{
		SetCloneTemporalSpawnState(
			Runtime,
			EParadoxTemporalSpawnState::Failed);
	}
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

bool UParadoxTimeLoopComponent::SetCloneGameplayPresence(
	AParadoxCloneCharacter& Clone,
	const bool bEnabled,
	FString& OutFailure)
{
	OutFailure.Reset();
	SetClonePlaybackMovementEnabled(Clone, bEnabled);
	SetTemporalAvatarGridPresence(Clone, bEnabled);
	Clone.SetActorEnableCollision(bEnabled);
	Clone.SetActorHiddenInGame(!bEnabled);

	if (UPerceptionKnowledgeSourceComponent* Source =
		Clone.GetPerceptionKnowledgeSourceComponent())
	{
		const FPerceptionKnowledgeOperationResult SourceResult =
			Source->SetSourceEnabled(bEnabled);
		if (!SourceResult.IsSuccess())
		{
			OutFailure = FString::Printf(
				TEXT("Clone '%s' could not %s its Perception Source: %s"),
				*GetNameSafe(&Clone),
				bEnabled ? TEXT("enable") : TEXT("disable"),
				*SourceResult.Message);
			return false;
		}
	}
	else
	{
		OutFailure = FString::Printf(
			TEXT("Clone '%s' has no Perception Source."),
			*GetNameSafe(&Clone));
		return false;
	}

	AParadoxCloneController* Controller =
		Cast<AParadoxCloneController>(Clone.GetController());
	UPerceptionKnowledgeListenerComponent* Listener = Controller
		? Controller->GetPerceptionKnowledgeListener()
		: nullptr;
	if (!Listener)
	{
		OutFailure = FString::Printf(
			TEXT("Clone '%s' has no controller-owned Perception Listener."),
			*GetNameSafe(&Clone));
		return false;
	}
	const FPerceptionKnowledgeOperationResult ListenerResult =
		Listener->SetListenerEnabled(bEnabled);
	if (!ListenerResult.IsSuccess())
	{
		OutFailure = FString::Printf(
			TEXT("Clone '%s' could not %s its Perception Listener: %s"),
			*GetNameSafe(&Clone),
			bEnabled ? TEXT("enable") : TEXT("disable"),
			*ListenerResult.Message);
		return false;
	}

	if (UParadoxOxygenComponent* Oxygen = Clone.GetOxygenComponent())
	{
		Oxygen->SetRunConsumptionActive(
			bEnabled && CurrentPhase == EParadoxTimeLoopPhase::ActiveRun);
	}
	if (UParadoxTemporalVisionComponent* Vision =
		Clone.GetTemporalVisionComponent())
	{
		if (bEnabled && CurrentPhase == EParadoxTimeLoopPhase::ActiveRun)
		{
			Vision->EnableTemporalDetection(TemporalDetectionSessionId);
		}
		else
		{
			Vision->DisableTemporalDetection(true);
		}
	}
	return true;
}

bool UParadoxTimeLoopComponent::MaterializeCloneAtChronoSpawn(
	FParadoxClonePlaybackRuntime& Runtime,
	AParadoxChronoSpawn& ChronoSpawn,
	FString& OutFailure)
{
	AParadoxCloneCharacter* Clone = Runtime.Clone.Get();
	if (!IsValid(Clone))
	{
		OutFailure = TEXT("The pending temporal clone no longer exists.");
		return false;
	}
	if (Runtime.TemporalSpawnState == EParadoxTemporalSpawnState::Materialized)
	{
		OutFailure = FString::Printf(
			TEXT("Clone T%d was already materialized."),
			Runtime.TemporalIndex);
		return true;
	}

	Clone->SetActorTransform(
		ChronoSpawn.GetActorTransform(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (!SetCloneGameplayPresence(*Clone, true, OutFailure))
	{
		return false;
	}
	SetCloneTemporalSpawnState(
		Runtime,
		EParadoxTemporalSpawnState::Materialized);
	RefreshTemporalDetectionAfterPlayerActivation();
	OutFailure = FString::Printf(
		TEXT("Clone T%d materialized at Chrono Spawn '%s'."),
		Runtime.TemporalIndex,
		*GetNameSafe(&ChronoSpawn));
	return true;
}

void UParadoxTimeLoopComponent::SetCloneTemporalSpawnState(
	FParadoxClonePlaybackRuntime& Runtime,
	const EParadoxTemporalSpawnState NewState)
{
	if (Runtime.TemporalSpawnState == NewState)
	{
		return;
	}
	Runtime.TemporalSpawnState = NewState;
	OnCloneTemporalSpawnStateChanged.Broadcast(
		MakeClonePlaybackSnapshot(Runtime));
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
		AParadoxCloneCharacter* MutableClone = Runtime->Clone.Get();
		UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
			MutableClone ? MutableClone->GetBehaviorCoordinator() : nullptr;
		AParadoxCloneController* Controller = MutableClone
			? Cast<AParadoxCloneController>(MutableClone->GetController())
			: nullptr;
		const FParadoxCloneBehaviorOperationResult BehaviorInitialization =
			Coordinator
				? Coordinator->InitializeForRun(
					Runtime->TimelineBundle.Get())
				: FParadoxCloneBehaviorOperationResult();
		FString BehaviorTreeFailure;
		if (!Coordinator || !BehaviorInitialization.IsSuccess()
			|| !Controller
			|| !Controller->StartCloneBehaviorTree(
				BehaviorTreeFailure))
		{
			FIntentReplayFailure Failure;
			Failure.DiagnosticMessage =
				!BehaviorInitialization.IsSuccess()
					? BehaviorInitialization.DiagnosticMessage
					: BehaviorTreeFailure;
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
		UPerceptionKnowledgeSourceComponent* ClonePerceptionSource =
			Clone->GetPerceptionKnowledgeSourceComponent();
		if (!ClonePerceptionSource)
		{
			OutFailure = FString::Printf(
				TEXT("Clone %d has no Perception Knowledge Source."),
				Timeline.TemporalIndex);
			Clone->Destroy();
			return false;
		}
		if (Timeline.AvatarPerceptionEntityId.IsValid())
		{
			const FPerceptionKnowledgeOperationResult DisableSourceResult =
				ClonePerceptionSource->SetSourceEnabled(false);
			const FPerceptionKnowledgeOperationResult IdentityResult =
				DisableSourceResult.IsSuccess()
					? ClonePerceptionSource->AssignEntityId(
						Timeline.AvatarPerceptionEntityId)
					: DisableSourceResult;
			if (!DisableSourceResult.IsSuccess()
				|| !IdentityResult.IsSuccess())
			{
				OutFailure = FString::Printf(
					TEXT("Clone %d could not inherit Perception Entity ID %s before spawning: disable='%s', assign='%s'."),
					Timeline.TemporalIndex,
					*Timeline.AvatarPerceptionEntityId.ToString(),
					*DisableSourceResult.Message,
					*IdentityResult.Message);
				Clone->Destroy();
				return false;
			}
		}
		else
		{
			PARADOX_LOG_WARNING(
				TEXT("Legacy action-only timeline %d has no stable Perception Entity ID; clone '%s' uses a fresh identity and has no perceptual comparison."),
				Timeline.TemporalIndex,
				*GetNameSafe(Clone));
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
		if (Timeline.AvatarPerceptionEntityId.IsValid()
			&& (ClonePerceptionSource->IsSemanticallyRegistered()
				|| ClonePerceptionSource->GetEntityId()
					!= Timeline.AvatarPerceptionEntityId))
		{
			OutFailure = FString::Printf(
				TEXT("Dormant clone %d did not retain its disabled inherited Perception Entity ID %s (current %s, registered=%s)."),
				Timeline.TemporalIndex,
				*Timeline.AvatarPerceptionEntityId.ToString(),
				*ClonePerceptionSource->GetEntityId().ToString(),
				ClonePerceptionSource->IsSemanticallyRegistered()
					? TEXT("true")
					: TEXT("false"));
			Clone->Destroy();
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
		if (!SetCloneGameplayPresence(*Clone, false, OutFailure))
		{
			Clone->Destroy();
			return false;
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

void UParadoxTimeLoopComponent::ReapplyChronoSpawnStates(
	const bool bNotifyStateInitialized)
{
	for (AParadoxChronoSpawn* Spawn : ChronoSpawns)
	{
		if (IsValid(Spawn))
		{
			Spawn->SetAssignedToTimeline(false);
			Spawn->RefreshActivationState();
		}
	}
	for (const FParadoxConsolidatedTimeline& Timeline : ConsolidatedTimelines)
	{
		if (IsValid(Timeline.ChronoSpawn))
		{
			Timeline.ChronoSpawn->SetAssignedToTimeline(true);
		}
	}
	if (bNotifyStateInitialized)
	{
		for (AParadoxChronoSpawn* Spawn : ChronoSpawns)
		{
			if (IsValid(Spawn))
			{
				Spawn->NotifyStateInitialized();
			}
		}
	}
}

bool UParadoxTimeLoopComponent::IsConfiguredCloneClassUsable() const
{
	return CloneCharacterClass != nullptr;
}
