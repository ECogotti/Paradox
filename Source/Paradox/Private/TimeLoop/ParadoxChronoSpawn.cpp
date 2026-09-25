#include "TimeLoop/ParadoxChronoSpawn.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Engine/StaticMesh.h"
#include "Actions/GameplayActionDefinition.h"
#include "Actions/ParadoxChronoSpawnActionDefinition.h"
#include "Graph/PuzzleGraphSubsystem.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxInteractionWidgetBase.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Receivers/PuzzleReceiverTypes.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxChronoSpawn"

AParadoxChronoSpawn::AParadoxChronoSpawn()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SelectionMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectionMesh"));
	SelectionMesh->SetupAttachment(SceneRoot);
	SelectionMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SelectionMesh->SetCollisionObjectType(ECC_WorldDynamic);
	SelectionMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	SelectionMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SelectionMesh->SetGenerateOverlapEvents(false);
	SelectionMesh->SetCastShadow(false);
	SelectionMesh->SetRelativeScale3D(FVector(1.f, 1.f, 1.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMeshFinder.Succeeded())
	{
		SelectionMesh->SetStaticMesh(CylinderMeshFinder.Object);
	}

	SelectableComponent = CreateDefaultSubobject<UParadoxSelectableComponent>(TEXT("SelectableComponent"));
	SelectableComponent->bShowInteractionCellsWhenSelected = false;
	SelectableComponent->bShowPuzzleConnectionsWhenSelected = true;
	SelectableComponent->SelectionWidgetClass = nullptr;

	InteractionComponent = CreateDefaultSubobject<UParadoxInteractionComponent>(
		TEXT("InteractionComponent"));
	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition>
		ChronoSpawnDefinitionFinder(
			TEXT("/Game/Data/GameplayActions/DA_ParadoxChronoSpawn.DA_ParadoxChronoSpawn"));
	if (ChronoSpawnDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& Definition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		Definition.InteractionTag =
			ParadoxGameplayTags::Interaction_ChronoSpawn_Spawn;
		Definition.GameplayActionDefinition =
			ChronoSpawnDefinitionFinder.Object;
	}

	PuzzleReceiverComponent = CreateDefaultSubobject<UPuzzleReceiverComponent>(
		TEXT("PuzzleReceiverComponent"));
	PuzzleReceiverComponent->ActivationMode =
		EPuzzleReceiverActivationMode::Automatic;

	WorldStateParticipant = CreateDefaultSubobject<UWorldStateParticipantComponent>(
		TEXT("WorldStateParticipant"));
	WorldStateParticipant->bCaptureExistence = false;
	WorldStateParticipant->bCaptureActorTransform = true;
	WorldStateParticipant->bCaptureAttachment = false;
	WorldStateParticipant->ExistencePolicy =
		EWorldStateExistencePolicy::ExistingOnly;
	FWorldStatePropertySelection& EnabledSelection =
		WorldStateParticipant->CapturedProperties.AddDefaulted_GetRef();
	EnabledSelection.CaptureSourceId =
		FWorldStateCaptureSourceId::OwnerActor();
	EnabledSelection.PropertyName = GET_MEMBER_NAME_CHECKED(
		AParadoxChronoSpawn,
		bChronoSpawnEnabled);
}

void AParadoxChronoSpawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	bChronoSpawnActive = bChronoSpawnEnabled;
	if (SelectableComponent)
	{
		SelectableComponent->SetSelectionAvailability(
			bChronoSpawnEnabled,
			bChronoSpawnEnabled);
	}
	ChronoSpawnState = bChronoSpawnEnabled
		? EParadoxChronoSpawnState::Available
		: EParadoxChronoSpawnState::Disabled;
	ReceiveVisualStateChanged(ChronoSpawnState);
}

void AParadoxChronoSpawn::BeginPlay()
{
	Super::BeginPlay();
	if (PuzzleReceiverComponent)
	{
		if (PuzzleReceiverComponent->ActivationMode
			!= EPuzzleReceiverActivationMode::Automatic)
		{
			PARADOX_LOG_WARNING(
				TEXT("Chrono Spawn '%s' forced Receiver '%s' back to Automatic activation at runtime."),
				*GetNameSafe(this),
				*GetNameSafe(PuzzleReceiverComponent));
			PuzzleReceiverComponent->ActivationMode =
				EPuzzleReceiverActivationMode::Automatic;
		}
		PuzzleReceiverComponent->OnReceiverStateChangedNative.AddUObject(
			this,
			&ThisClass::HandleReceiverStateChanged);
	}
	if (UWorld* World = GetWorld())
	{
		if (UPuzzleGraphSubsystem* Graph =
			World->GetSubsystem<UPuzzleGraphSubsystem>())
		{
			GraphTopologyChangedHandle =
				Graph->OnPuzzleGraphTopologyChangedNative.AddUObject(
					this,
					&ThisClass::HandleGraphTopologyChanged);
		}
		if (UWorldStateSubsystem* WorldState =
			World->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldStateRestoreCompletedHandle =
				WorldState->OnRestoreCompletedNative().AddUObject(
					this,
					&ThisClass::HandleWorldStateRestoreCompleted);
			WorldStateRestoreFailedHandle =
				WorldState->OnRestoreFailedNative().AddUObject(
					this,
					&ThisClass::HandleWorldStateRestoreFailed);
		}
	}
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreRestore.AddUniqueDynamic(
			this,
			&ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.AddUniqueDynamic(
			this,
			&ThisClass::HandleWorldStatePropertiesRestored);
	}
	RefreshActivationState();
	NotifyStateInitialized();
}

void AParadoxChronoSpawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PuzzleReceiverComponent)
	{
		PuzzleReceiverComponent->OnReceiverStateChangedNative.RemoveAll(this);
	}
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreRestore.RemoveDynamic(
			this,
			&ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.RemoveDynamic(
			this,
			&ThisClass::HandleWorldStatePropertiesRestored);
	}
	if (UWorld* World = GetWorld())
	{
		if (UPuzzleGraphSubsystem* Graph =
			World->GetSubsystem<UPuzzleGraphSubsystem>())
		{
			Graph->OnPuzzleGraphTopologyChangedNative.Remove(
				GraphTopologyChangedHandle);
		}
		if (UWorldStateSubsystem* WorldState =
			World->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldState->OnRestoreCompletedNative().Remove(
				WorldStateRestoreCompletedHandle);
			WorldState->OnRestoreFailedNative().Remove(
				WorldStateRestoreFailedHandle);
		}
	}
	GraphTopologyChangedHandle.Reset();
	WorldStateRestoreCompletedHandle.Reset();
	WorldStateRestoreFailedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult AParadoxChronoSpawn::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	auto AddError = [&Context, &Result](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (!SceneRoot || GetRootComponent() != SceneRoot)
	{
		AddError(LOCTEXT("MissingSceneRoot", "Chrono Spawn requires its native Scene Root."));
	}
	if (!SelectionMesh || SelectionMesh->GetAttachParent() != SceneRoot)
	{
		AddError(LOCTEXT("MissingSelectionMesh", "Chrono Spawn requires Selection Mesh attached to Scene Root."));
	}
	if (!SelectableComponent)
	{
		AddError(LOCTEXT("MissingSelectableComponent", "Chrono Spawn requires its native Selectable Component."));
	}
	else
	{
		if (!SelectableComponent->bShowPuzzleConnectionsWhenSelected)
		{
			AddError(LOCTEXT("PuzzleOverlayDisabled", "Chrono Spawn Selectable must show Puzzle connections when selected."));
		}
	}
	if (!InteractionComponent)
	{
		AddError(LOCTEXT("MissingInteractionComponent", "Chrono Spawn requires its native Paradox Interaction Component."));
	}
	else if (InteractionComponent->InteractionDefinitions.Num() != 1)
	{
		AddError(LOCTEXT("InvalidInteractionCatalog", "Chrono Spawn requires exactly one non-spatial Spawn interaction."));
	}
	else
	{
		const FParadoxInteractionDefinition& Definition =
			InteractionComponent->InteractionDefinitions[0];
		const UParadoxChronoSpawnActionDefinition* ActionDefinition =
			Cast<UParadoxChronoSpawnActionDefinition>(
				Definition.GameplayActionDefinition.LoadSynchronous());
		if (Definition.InteractionTag
				!= ParadoxGameplayTags::Interaction_ChronoSpawn_Spawn
			|| !ActionDefinition
			|| ActionDefinition->RequiresSmartObjectSlot())
		{
			AddError(LOCTEXT("InvalidSpawnInteraction", "Chrono Spawn requires the Chrono Spawn Action Definition configured for non-spatial execution."));
		}
	}
	if (!PuzzleReceiverComponent)
	{
		AddError(LOCTEXT("MissingPuzzleReceiver", "Chrono Spawn requires its native Puzzle Receiver Component."));
	}
	else if (PuzzleReceiverComponent->ActivationMode
		!= EPuzzleReceiverActivationMode::Automatic)
	{
		AddError(LOCTEXT("ReceiverNotAutomatic", "Chrono Spawn Puzzle Receiver must use Automatic activation."));
	}
	if (!WorldStateParticipant)
	{
		AddError(LOCTEXT("MissingWorldStateParticipant", "Chrono Spawn requires its native World State participant."));
	}
	return Result;
}
#endif

bool AParadoxChronoSpawn::IsAvailableForSelection() const
{
	return bChronoSpawnEnabled && !bWorldStateRestoreInProgress;
}

bool AParadoxChronoSpawn::CanAssignToNewTimeline() const
{
	return bChronoSpawnEnabled
		&& bChronoSpawnActive
		&& !bAssignedToTimeline;
}

void AParadoxChronoSpawn::SetChronoSpawnEnabled(const bool bEnabled)
{
	if (bChronoSpawnEnabled == bEnabled)
	{
		return;
	}

	bChronoSpawnEnabled = bEnabled;
	RefreshActivationState();
}

void AParadoxChronoSpawn::CommitChronoSpawnState(
	const EParadoxChronoSpawnState NewState)
{
	if (ChronoSpawnState == NewState)
	{
		return;
	}

	const EParadoxChronoSpawnState PreviousState = ChronoSpawnState;
	ChronoSpawnState = NewState;
	ReceiveVisualStateChanged(ChronoSpawnState);
	OnChronoSpawnStateChanged.Broadcast(
		this,
		PreviousState,
		ChronoSpawnState);
}

void AParadoxChronoSpawn::SetRuntimeState(const EParadoxChronoSpawnState NewState)
{
	if (NewState == EParadoxChronoSpawnState::Occupied)
	{
		SetAssignedToTimeline(true);
		return;
	}
	if (NewState == EParadoxChronoSpawnState::Available)
	{
		SetAssignedToTimeline(false);
		return;
	}
	CommitChronoSpawnState(NewState);
}

void AParadoxChronoSpawn::SetAssignedToTimeline(const bool bAssigned)
{
	if (bAssignedToTimeline == bAssigned)
	{
		return;
	}
	bAssignedToTimeline = bAssigned;
	RefreshPresentationState();
	if (InteractionComponent)
	{
		InteractionComponent->NotifyInteractionAffordanceChanged();
	}
}

void AParadoxChronoSpawn::NotifyStateInitialized()
{
	ReceiveStateInitialized(ChronoSpawnState);
}

void AParadoxChronoSpawn::RefreshActivationState()
{
	bool bHasIncomingPuzzleLink = false;
	if (UWorld* World = GetWorld())
	{
		if (UPuzzleGraphSubsystem* Graph =
			World->GetSubsystem<UPuzzleGraphSubsystem>())
		{
			bHasIncomingPuzzleLink =
				!Graph->QueryLinksForReceiverComponent(
					PuzzleReceiverComponent).IsEmpty();
		}
	}

	const bool bNewActive = bChronoSpawnEnabled
		&& !bWorldStateRestoreInProgress
		&& (!bHasIncomingPuzzleLink
			|| (PuzzleReceiverComponent
				&& PuzzleReceiverComponent->IsReceiverActive()));
	const bool bActivationChanged = bChronoSpawnActive != bNewActive;
	bChronoSpawnActive = bNewActive;
	if (SelectableComponent)
	{
		SelectableComponent->SetSelectionAvailability(
			bChronoSpawnEnabled && !bWorldStateRestoreInProgress,
			bChronoSpawnEnabled && !bWorldStateRestoreInProgress);
	}
	RefreshPresentationState();
	if (InteractionComponent)
	{
		InteractionComponent->NotifyInteractionAffordanceChanged();
	}
	if (bActivationChanged)
	{
		OnChronoSpawnActivationChanged.Broadcast(this, bChronoSpawnActive);
	}
}

void AParadoxChronoSpawn::RefreshPresentationState()
{
	EParadoxChronoSpawnState NewState = EParadoxChronoSpawnState::Available;
	if (!bChronoSpawnEnabled)
	{
		NewState = EParadoxChronoSpawnState::Disabled;
	}
	else if (!bChronoSpawnActive)
	{
		NewState = EParadoxChronoSpawnState::Inactive;
	}
	else if (bAssignedToTimeline)
	{
		NewState = EParadoxChronoSpawnState::Occupied;
	}
	CommitChronoSpawnState(NewState);
}

void AParadoxChronoSpawn::HandleReceiverStateChanged(
	UPuzzleReceiverComponent* Receiver,
	const bool bIsActive)
{
	(void)bIsActive;
	if (Receiver == PuzzleReceiverComponent)
	{
		RefreshActivationState();
	}
}

void AParadoxChronoSpawn::HandleGraphTopologyChanged(
	const int64 GraphTopologyRevision,
	APuzzleController* AffectedController,
	const EPuzzleGraphTopologyChangeKind ChangeKind)
{
	(void)GraphTopologyRevision;
	(void)AffectedController;
	(void)ChangeKind;
	RefreshActivationState();
}

void AParadoxChronoSpawn::HandleWorldStatePreRestore(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	bWorldStateRestoreInProgress = true;
	RefreshActivationState();
}

void AParadoxChronoSpawn::HandleWorldStatePropertiesRestored(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	// The participant restores bChronoSpawnEnabled directly. Reconcile its side effects while
	// activation and selection remain suppressed until the complete World State session succeeds.
	RefreshActivationState();
}

void AParadoxChronoSpawn::HandleWorldStateRestoreCompleted(
	const FWorldStateRestoreResult& Result)
{
	(void)Result;
	bWorldStateRestoreInProgress = false;
	RefreshActivationState();
}

void AParadoxChronoSpawn::HandleWorldStateRestoreFailed(
	const FWorldStateRestoreResult& Result)
{
	(void)Result;
	bWorldStateRestoreInProgress = true;
	PARADOX_LOG_ERROR(
		TEXT("Chrono Spawn '%s' remains unavailable because World State restore failed."),
		*GetNameSafe(this));
	RefreshActivationState();
}

void AParadoxChronoSpawn::ReceiveVisualStateChanged_Implementation(
	const EParadoxChronoSpawnState NewState)
{
	(void)NewState;
}

void AParadoxChronoSpawn::ReceiveStateInitialized_Implementation(
	const EParadoxChronoSpawnState InitialState)
{
	(void)InitialState;
}

#undef LOCTEXT_NAMESPACE
