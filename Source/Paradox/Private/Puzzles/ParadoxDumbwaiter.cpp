#include "Puzzles/ParadoxDumbwaiter.h"

#include "Actions/GameplayActionDefinition.h"
#include "Characters/ParadoxCharacter.h"
#include "Components/ArrowComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxDumbwaiter"

namespace UE::Paradox::Dumbwaiter::Private
{
	struct FOperationGuard
	{
		explicit FOperationGuard(bool& InFlag) : Flag(InFlag) { Flag = true; }
		~FOperationGuard() { Flag = false; }
		bool& Flag;
	};
}

AParadoxDumbwaiter::AParadoxDumbwaiter()
{
	PrimaryActorTick.bCanEverTick = false;
	TransferOutCompletionMode = EParadoxTransferPhaseCompletionMode::Explicit;
	TransferInCompletionMode = EParadoxTransferPhaseCompletionMode::Explicit;

	SelectableComponent = CreateDefaultSubobject<UParadoxSelectableComponent>(TEXT("SelectableComponent"));
	SelectableComponent->bShowInteractionCellsWhenSelected = true;
	SelectableComponent->bShowPuzzleConnectionsWhenSelected = true;

	SmartObjectComponent = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObjectComponent"));
	SmartObjectComponent->SetupAttachment(SceneRoot);
	InteractionComponent = CreateDefaultSubobject<UParadoxInteractionComponent>(TEXT("InteractionComponent"));

	WorldStateParticipant = CreateDefaultSubobject<UWorldStateParticipantComponent>(TEXT("WorldStateParticipant"));
	WorldStateParticipant->bCaptureExistence = true;
	// Paired endpoints have a native Static root; restore cargo ownership without attempting
	// to move the endpoint and triggering an invalid static-component transform update.
	WorldStateParticipant->bCaptureActorTransform = false;
	WorldStateParticipant->bCaptureAttachment = false;
	WorldStateParticipant->ExistencePolicy = EWorldStateExistencePolicy::RespawnAndDestroy;
	WorldStateParticipant->RestorePhase = EWorldStateRestorePhase::Late;
	FWorldStatePropertySelection& CargoSelection =
		WorldStateParticipant->CapturedProperties.AddDefaulted_GetRef();
	CargoSelection.CaptureSourceId = FWorldStateCaptureSourceId::OwnerActor();
	CargoSelection.PropertyName = GET_MEMBER_NAME_CHECKED(
		AParadoxDumbwaiter,
		WorldStateStoredPickupable);
	CargoSelection.ReferenceRequirement = EWorldStateReferenceRequirement::Optional;

	static ConstructorHelpers::FObjectFinder<USmartObjectDefinition> SmartObjectDefinitionFinder(
		TEXT("/Game/Data/Inventory/DA_ParadoxItemSlotSmartObject.DA_ParadoxItemSlotSmartObject"));
	if (SmartObjectDefinitionFinder.Succeeded())
	{
		SmartObjectComponent->SetDefinition(SmartObjectDefinitionFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition> InsertDefinitionFinder(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxInsertItem.DA_ParadoxInsertItem"));
	if (InsertDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& Definition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		Definition.InteractionTag = ParadoxGameplayTags::Interaction_ItemSlot_Insert;
		Definition.GameplayActionDefinition = InsertDefinitionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition> PickupDefinitionFinder(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxPickupFromItemSlot.DA_ParadoxPickupFromItemSlot"));
	if (PickupDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& Definition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		Definition.InteractionTag = ParadoxGameplayTags::Interaction_ItemSlot_Pickup;
		Definition.GameplayActionDefinition = PickupDefinitionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinderOptional<UGameplayActionDefinition> SendDefinitionFinder(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxSendDumbwaiter.DA_ParadoxSendDumbwaiter"));
	if (SendDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& Definition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		Definition.InteractionTag = ParadoxGameplayTags::Interaction_Dumbwaiter_Send;
		Definition.GameplayActionDefinition = SendDefinitionFinder.Get();
	}
}

void AParadoxDumbwaiter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PrimaryActorTick.SetTickFunctionEnable(false);
}

#if WITH_EDITOR
void AParadoxDumbwaiter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName ChangedProperty = PropertyChangedEvent.GetPropertyName();
	if ((ChangedProperty == GET_MEMBER_NAME_CHECKED(ThisClass, StoredPickupable)
			|| ChangedProperty == GET_MEMBER_NAME_CHECKED(ThisClass, AcceptedCargoQuery))
		&& IsValid(StoredPickupable.Get())
		&& !MatchesAcceptedCargoQuery(StoredPickupable.Get()))
	{
		PARADOX_LOG_WARNING(
			TEXT("Dumbwaiter '%s' initially stored pickupable '%s' does not match Accepted Cargo Query and will be rejected at runtime."),
			*GetNameSafe(this),
			*GetNameSafe(StoredPickupable.Get()));
	}
}

EDataValidationResult AParadoxDumbwaiter::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	if (TransferOutCompletionMode != EParadoxTransferPhaseCompletionMode::Explicit
		|| TransferInCompletionMode != EParadoxTransferPhaseCompletionMode::Explicit)
	{
		Context.AddError(LOCTEXT(
			"ExplicitCompletionRequired",
			"Dumbwaiters require Explicit Transfer Out and Transfer In completion modes."));
		Result = EDataValidationResult::Invalid;
	}
	if (!IsTemplate())
	{
		const AParadoxDumbwaiter* LinkedDumbwaiter =
			Cast<AParadoxDumbwaiter>(GetLinkedEndpoint());
		if (IsValid(StoredPickupable.Get())
			&& IsValid(LinkedDumbwaiter)
			&& IsValid(LinkedDumbwaiter->StoredPickupable.Get()))
		{
			Context.AddError(LOCTEXT(
				"BothEndpointsInitiallyOccupied",
				"A Dumbwaiter pair may contain only one initially stored pickupable."));
			Result = EDataValidationResult::Invalid;
		}
	}
	const AParadoxInsertablePickupableActor* Cargo = StoredPickupable.Get();
	if (!IsValid(Cargo))
	{
		return Result;
	}
	if (Cargo->GetWorld() != GetWorld())
	{
		Context.AddError(LOCTEXT("InitialCargoDifferentWorld", "Initially Stored Pickupable must belong to the same World."));
		Result = EDataValidationResult::Invalid;
	}
	if (!MatchesAcceptedCargoQuery(Cargo))
	{
		Context.AddError(FText::Format(
			LOCTEXT("InitialCargoRejected", "Initially Stored Pickupable '{0}' does not match Accepted Cargo Query."),
			FText::FromString(GetNameSafe(Cargo))));
		Result = EDataValidationResult::Invalid;
	}
	if (!TransferAnchor)
	{
		Context.AddError(LOCTEXT("MissingCargoAnchor", "An authored cargo requires the inherited Transfer Anchor."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

void AParadoxDumbwaiter::BeginPlay()
{
	Super::BeginPlay();
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreCapture.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStatePreCapture);
		WorldStateParticipant->OnWorldStatePreRestore.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStatePropertiesRestored);
		WorldStateParticipant->OnWorldStateRestored.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStateParticipantRestored);
		WorldStateParticipant->OnWorldStateRestoreFailed.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStateParticipantFailed);
	}
	if (UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr)
	{
		WorldState->OnRestoreStartedNative().AddUObject(
			this, &ThisClass::HandleWorldStateRestoreStarted);
		WorldState->OnRestoreCompletedNative().AddUObject(
			this, &ThisClass::HandleWorldStateRestoreFinished);
		WorldState->OnRestoreFailedNative().AddUObject(
			this, &ThisClass::HandleWorldStateRestoreFinished);
	}

	InitializeAuthoredCargo();
	bInitialized = true;
	NotifyPairOccupancyChanged();
}

void AParadoxDumbwaiter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bInitialized = false;
	if (IsTransferInProgress())
	{
		ResetTransferEndpoint();
	}
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreCapture.RemoveDynamic(
			this, &ThisClass::HandleWorldStatePreCapture);
		WorldStateParticipant->OnWorldStatePreRestore.RemoveDynamic(
			this, &ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.RemoveDynamic(
			this, &ThisClass::HandleWorldStatePropertiesRestored);
		WorldStateParticipant->OnWorldStateRestored.RemoveDynamic(
			this, &ThisClass::HandleWorldStateParticipantRestored);
		WorldStateParticipant->OnWorldStateRestoreFailed.RemoveDynamic(
			this, &ThisClass::HandleWorldStateParticipantFailed);
	}
	if (UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr)
	{
		WorldState->OnRestoreStartedNative().RemoveAll(this);
		WorldState->OnRestoreCompletedNative().RemoveAll(this);
		WorldState->OnRestoreFailedNative().RemoveAll(this);
	}

	if (AParadoxInsertablePickupableActor* Cargo = StoredPickupable.Get())
	{
		UnbindStoredPickupable(Cargo);
		StoredPickupable = nullptr;
		Cargo->ClearDumbwaiterStateNative(true);
		if (EndPlayReason != EEndPlayReason::EndPlayInEditor
			&& EndPlayReason != EEndPlayReason::Quit
			&& IsValid(Cargo))
		{
			Cargo->SetWorldStateNative(Cargo->GetActorTransform(), nullptr, false);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool AParadoxDumbwaiter::IsOccupied() const
{
	return IsValid(StoredPickupable.Get());
}

bool AParadoxDumbwaiter::IsLinkedDumbwaiterOccupied() const
{
	const AParadoxDumbwaiter* LinkedDumbwaiter =
		Cast<AParadoxDumbwaiter>(GetLinkedEndpoint());
	return IsValid(LinkedDumbwaiter) && LinkedDumbwaiter->IsOccupied();
}

bool AParadoxDumbwaiter::CanAcceptCargo(
	AParadoxInsertablePickupableActor* Cargo,
	AParadoxCharacter* Requester) const
{
	return EvaluateAcceptCargo(Cargo, Requester).IsSuccess();
}

FParadoxItemSlotOperationResult AParadoxDumbwaiter::EvaluateAcceptCargo(
	AParadoxInsertablePickupableActor* Cargo,
	AParadoxCharacter* Requester) const
{
	if (!IsValid(Requester))
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::InvalidRequester, TEXT("A valid Paradox Character requester is required."));
	}
	UParadoxInventoryComponent* Inventory = Requester->GetInventoryComponent();
	if (!IsValid(Inventory))
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::MissingInventory, TEXT("The requester does not own a valid inventory component."));
	}
	if (bResetInProgress || Inventory->bResetInProgress)
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::ResetInProgress, TEXT("Dumbwaiter transitions are disabled during World State restore."));
	}
	if (bOperationInProgress || Inventory->bOperationInProgress || IsTransferInProgress())
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::OperationInProgress, TEXT("The Dumbwaiter is already participating in a transition."));
	}
	if (!PuzzleReceiver || !PuzzleReceiver->IsReceiverActive())
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::SlotInactive, TEXT("The Dumbwaiter Puzzle Receiver is inactive."));
	}
	if (IsOccupied())
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::SlotOccupied, TEXT("The Dumbwaiter already contains cargo."));
	}
	if (IsLinkedDumbwaiterOccupied())
	{
		return MakeCargoResult(
			EParadoxItemSlotOperationStatus::SlotOccupied,
			TEXT("The linked Dumbwaiter already contains the pair's cargo."));
	}
	if (!IsValid(Cargo))
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::InvalidItem, TEXT("A valid insertable pickupable is required."));
	}
	if (Inventory->GetEquippedItem() != Cargo || Cargo->GetCurrentHolder() != Requester)
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::RequesterDoesNotOwnItem, TEXT("The requester inventory does not authoritatively own this cargo."));
	}
	if (Cargo->GetCurrentItemSlot() || Cargo->GetCurrentDumbwaiter()
		|| Cargo->GetPickupableState() != EParadoxPickupableState::Held)
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::OwnershipConflict, TEXT("The cargo already has incompatible ownership state."));
	}
	if (!TransferAnchor || !TransferAnchor->IsRegistered())
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::InvalidPlacement, TEXT("The Dumbwaiter has no registered Transfer Anchor."));
	}
	if (!MatchesAcceptedCargoQuery(Cargo))
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::IncompatibleTraits, TEXT("The cargo traits do not match the Dumbwaiter query."));
	}
	FString AdditionalDiagnostic;
	if (!CanAcceptCargoAdditional(Cargo, Requester, AdditionalDiagnostic))
	{
		return MakeCargoResult(
			EParadoxItemSlotOperationStatus::AdditionalValidationFailed,
			AdditionalDiagnostic.IsEmpty()
				? TEXT("A derived Dumbwaiter compatibility condition rejected the cargo.")
				: MoveTemp(AdditionalDiagnostic));
	}
	return MakeCargoResult(EParadoxItemSlotOperationStatus::Succeeded, TEXT("The cargo can be inserted."));
}

FParadoxItemSlotOperationResult AParadoxDumbwaiter::EvaluatePickupCargo(
	AParadoxCharacter* Requester) const
{
	if (!IsValid(Requester))
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::InvalidRequester, TEXT("A valid Paradox Character requester is required."));
	}
	UParadoxInventoryComponent* Inventory = Requester->GetInventoryComponent();
	if (!IsValid(Inventory))
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::MissingInventory, TEXT("The requester does not own a valid inventory component."));
	}
	if (bResetInProgress || Inventory->bResetInProgress)
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::ResetInProgress, TEXT("Dumbwaiter transitions are disabled during World State restore."));
	}
	if (bOperationInProgress || Inventory->bOperationInProgress || IsTransferInProgress())
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::OperationInProgress, TEXT("The Dumbwaiter is already participating in a transition."));
	}
	if (!PuzzleReceiver || !PuzzleReceiver->IsReceiverActive())
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::SlotInactive, TEXT("The Dumbwaiter Puzzle Receiver is inactive."));
	}
	AParadoxInsertablePickupableActor* Cargo = StoredPickupable.Get();
	if (!IsValid(Cargo))
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::SlotEmpty, TEXT("The Dumbwaiter is empty."));
	}
	if (bLockStoredPickupable)
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::ItemLocked, TEXT("The stored cargo is locked against ordinary Pickup."));
	}
	if (Inventory->HasItem())
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::InventoryOccupied, TEXT("Pickup requires an empty requester inventory."));
	}
	if (!Cargo->IsInserted() || Cargo->GetCurrentDumbwaiter() != this
		|| Cargo->GetCurrentItemSlot() || Cargo->GetCurrentHolder())
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::OwnershipConflict, TEXT("The cargo and Dumbwaiter ownership references disagree."));
	}
	return MakeCargoResult(EParadoxItemSlotOperationStatus::Succeeded, TEXT("The stored cargo can be picked up."));
}

FParadoxItemSlotOperationResult AParadoxDumbwaiter::TryInsertCargo(AParadoxCharacter* Requester)
{
	UParadoxInventoryComponent* Inventory = IsValid(Requester)
		? Requester->GetInventoryComponent()
		: nullptr;
	AParadoxInsertablePickupableActor* Cargo = Inventory
		? Cast<AParadoxInsertablePickupableActor>(Inventory->GetEquippedItem())
		: nullptr;
	if (Inventory && Inventory->HasItem() && !Cargo)
	{
		return MakeCargoResult(EParadoxItemSlotOperationStatus::NotInsertable, TEXT("The equipped pickupable cannot be stored in a Dumbwaiter."));
	}
	const FParadoxItemSlotOperationResult Validation = EvaluateAcceptCargo(Cargo, Requester);
	if (!Validation.IsSuccess())
	{
		return Validation;
	}
	UE::Paradox::Dumbwaiter::Private::FOperationGuard Guard(bOperationInProgress);
	return Inventory->TransferEquippedItemToDumbwaiter(*this, *Cargo);
}

FParadoxItemSlotOperationResult AParadoxDumbwaiter::TryPickupCargo(AParadoxCharacter* Requester)
{
	const FParadoxItemSlotOperationResult Validation = EvaluatePickupCargo(Requester);
	if (!Validation.IsSuccess())
	{
		return Validation;
	}
	UParadoxInventoryComponent* Inventory = Requester->GetInventoryComponent();
	AParadoxInsertablePickupableActor* Cargo = StoredPickupable.Get();
	UE::Paradox::Dumbwaiter::Private::FOperationGuard Guard(bOperationInProgress);
	return Inventory->TransferDumbwaiterItemToInventory(*this, *Cargo);
}

FParadoxTransferOperationResult AParadoxDumbwaiter::EvaluateSendCargo(
	AActor* Requester,
	UGameplayActionInstance* RequestingAction) const
{
	return EvaluateTransfer(StoredPickupable.Get(), Requester, RequestingAction);
}

FParadoxTransferOperationResult AParadoxDumbwaiter::TrySendCargo(
	AActor* Requester,
	UGameplayActionInstance* RequestingAction)
{
	return RequestTransfer(StoredPickupable.Get(), Requester, RequestingAction);
}

void AParadoxDumbwaiter::NotifyStoredPickupableRelevantStateChanged(
	AParadoxInsertablePickupableActor* ChangedCargo)
{
	if (ChangedCargo != StoredPickupable.Get())
	{
		PARADOX_LOG_WARNING(
			TEXT("Dumbwaiter '%s' ignored a relevant-state notification from non-owned cargo '%s'."),
			*GetNameSafe(this),
			*GetNameSafe(ChangedCargo));
		return;
	}
	RefreshPairInteractionAffordances();
}

bool AParadoxDumbwaiter::CanTransferSubject_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	FString& OutDiagnostic) const
{
	(void)Requester;
	const AParadoxInsertablePickupableActor* Cargo =
		Cast<AParadoxInsertablePickupableActor>(TransferSubject);
	if (!IsValid(Cargo))
	{
		OutDiagnostic = TEXT("Dumbwaiters transfer insertable pickupables only.");
		return false;
	}
	if (StoredPickupable.Get() != Cargo || Cargo->GetCurrentDumbwaiter() != this
		|| Cargo->GetCurrentItemSlot() || Cargo->GetCurrentHolder() || !Cargo->IsInserted())
	{
		OutDiagnostic = TEXT("The source Dumbwaiter does not authoritatively own the requested cargo.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxDumbwaiter::CanSourceStartTransfer_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	AParadoxPairedTransferEndpoint* Destination,
	FString& OutDiagnostic) const
{
	(void)Requester;
	if (Destination != GetLinkedEndpoint() || !Cast<AParadoxDumbwaiter>(Destination))
	{
		OutDiagnostic = TEXT("The configured destination is not the linked Dumbwaiter.");
		return false;
	}
	if (StoredPickupable.Get() != TransferSubject)
	{
		OutDiagnostic = TEXT("The source cargo changed before transfer acquisition.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxDumbwaiter::CanDestinationReceiveTransfer_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	AParadoxPairedTransferEndpoint* Source,
	FString& OutDiagnostic) const
{
	const AParadoxInsertablePickupableActor* Cargo =
		Cast<AParadoxInsertablePickupableActor>(TransferSubject);
	if (!IsValid(Cargo) || !Cast<AParadoxDumbwaiter>(Source))
	{
		OutDiagnostic = TEXT("The Dumbwaiter destination requires valid insertable cargo and a Dumbwaiter source.");
		return false;
	}
	if (IsOccupied())
	{
		OutDiagnostic = TEXT("The destination Dumbwaiter is occupied.");
		return false;
	}
	if (bOperationInProgress || bResetInProgress)
	{
		OutDiagnostic = TEXT("The destination Dumbwaiter is participating in another cargo transition.");
		return false;
	}
	if (!TransferAnchor || !TransferAnchor->IsRegistered())
	{
		OutDiagnostic = TEXT("The destination Dumbwaiter has no registered Transfer Anchor.");
		return false;
	}
	if (!MatchesAcceptedCargoQuery(Cargo))
	{
		OutDiagnostic = TEXT("The cargo traits do not match the destination Dumbwaiter query.");
		return false;
	}
	if (!CanAcceptCargoAdditional(const_cast<AParadoxInsertablePickupableActor*>(Cargo), Requester, OutDiagnostic))
	{
		if (OutDiagnostic.IsEmpty())
		{
			OutDiagnostic = TEXT("A derived destination compatibility condition rejected the cargo.");
		}
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxDumbwaiter::PrepareSubjectForTransfer_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	if (!CanTransferSubject_Implementation(Context.Subject, Context.Requester, OutDiagnostic))
	{
		return false;
	}
	AParadoxInsertablePickupableActor* Cargo =
		CastChecked<AParadoxInsertablePickupableActor>(Context.Subject);
	Cargo->SetDumbwaiterTransferPresenceSuspendedNative(true);
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxDumbwaiter::PerformTransferCommit_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	AParadoxDumbwaiter* Source = Cast<AParadoxDumbwaiter>(Context.Source);
	AParadoxDumbwaiter* Destination = Cast<AParadoxDumbwaiter>(Context.Destination);
	AParadoxInsertablePickupableActor* Cargo =
		Cast<AParadoxInsertablePickupableActor>(Context.Subject);
	if (!Source || !Destination || !Cargo
		|| Source != this
		|| Source->StoredPickupable.Get() != Cargo
		|| Cargo->GetCurrentDumbwaiter() != Source
		|| Destination->IsOccupied()
		|| !Destination->TransferAnchor)
	{
		OutDiagnostic = TEXT("Dumbwaiter ownership changed before the paired commit.");
		return false;
	}

	Source->ClearStoredPickupableCommitted(Cargo);
	Destination->SetStoredPickupableCommitted(Cargo);
	Cargo->SetDumbwaiterStateNative(*Destination, *Destination->TransferAnchor);
	Cargo->ReceiveRemovedFromDumbwaiter(Source);
	Cargo->ReceiveStoredInDumbwaiter(Destination);
	Source->FinalizeCargoTransition(Cargo, nullptr, false);
	Destination->FinalizeCargoTransition(nullptr, Cargo, false);
	Source->NotifyPairOccupancyChanged();
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxDumbwaiter::FinalizeTransferredSubject_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	const AParadoxDumbwaiter* Destination = Cast<AParadoxDumbwaiter>(Context.Destination);
	AParadoxInsertablePickupableActor* Cargo =
		Cast<AParadoxInsertablePickupableActor>(Context.Subject);
	if (!Destination || !Cargo || Destination->StoredPickupable.Get() != Cargo
		|| Cargo->GetCurrentDumbwaiter() != Destination)
	{
		OutDiagnostic = TEXT("Transferred cargo ownership is not coherent at finalization.");
		return false;
	}
	Cargo->SetDumbwaiterTransferPresenceSuspendedNative(false);
	OutDiagnostic.Reset();
	return true;
}

void AParadoxDumbwaiter::HandleTransferCancelled_Implementation(
	const FParadoxTransferOperationContext& Context,
	const EParadoxTransferCancellationReason Reason,
	const bool bWasCommitted)
{
	(void)Reason;
	(void)bWasCommitted;
	if (AParadoxInsertablePickupableActor* Cargo =
		Cast<AParadoxInsertablePickupableActor>(Context.Subject);
		IsValid(Cargo))
	{
		Cargo->SetDumbwaiterTransferPresenceSuspendedNative(false);
	}
	RefreshPairInteractionAffordances();
}

bool AParadoxDumbwaiter::IsLinkedEndpointCompatible(
	const AParadoxPairedTransferEndpoint* Candidate,
	FString& OutDiagnostic) const
{
	if (!Cast<AParadoxDumbwaiter>(Candidate))
	{
		OutDiagnostic = TEXT("A Dumbwaiter can pair only with another Dumbwaiter.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxDumbwaiter::CanAcceptCargoAdditional_Implementation(
	AParadoxInsertablePickupableActor* Cargo,
	AActor* Requester,
	FString& OutDiagnostic) const
{
	(void)Cargo;
	(void)Requester;
	OutDiagnostic.Reset();
	return true;
}

FParadoxItemSlotOperationResult AParadoxDumbwaiter::MakeCargoResult(
	const EParadoxItemSlotOperationStatus Status,
	FString Diagnostic) const
{
	FParadoxItemSlotOperationResult Result;
	Result.Status = Status;
	Result.DiagnosticMessage = MoveTemp(Diagnostic);
	return Result;
}

bool AParadoxDumbwaiter::MatchesAcceptedCargoQuery(
	const AParadoxInsertablePickupableActor* Cargo) const
{
	return IsValid(Cargo)
		&& (AcceptedCargoQuery.IsEmpty()
			|| AcceptedCargoQuery.Matches(Cargo->GetInsertableTraits()));
}

void AParadoxDumbwaiter::InitializeAuthoredCargo()
{
	AParadoxInsertablePickupableActor* Cargo = StoredPickupable.Get();
	if (!IsValid(Cargo))
	{
		StoredPickupable = nullptr;
		WorldStateStoredPickupable.Reset();
		return;
	}
	if (Cargo->GetCurrentHolder() || Cargo->GetCurrentItemSlot() || Cargo->GetCurrentDumbwaiter()
		|| !MatchesAcceptedCargoQuery(Cargo) || !TransferAnchor)
	{
		PARADOX_LOG_ERROR(
			TEXT("Dumbwaiter '%s' rejected invalid authored baseline cargo '%s'."),
			*GetNameSafe(this),
			*GetNameSafe(Cargo));
		StoredPickupable = nullptr;
		WorldStateStoredPickupable.Reset();
		return;
	}
	BindStoredPickupable(*Cargo);
	Cargo->SetDumbwaiterStateNative(*this, *TransferAnchor);
	Cargo->ReceiveStoredInDumbwaiter(this);
	WorldStateStoredPickupable = Cargo;
}

void AParadoxDumbwaiter::SetStoredPickupableCommitted(
	AParadoxInsertablePickupableActor* NewCargo)
{
	StoredPickupable = NewCargo;
	if (NewCargo)
	{
		BindStoredPickupable(*NewCargo);
	}
}

void AParadoxDumbwaiter::ClearStoredPickupableCommitted(
	AParadoxInsertablePickupableActor* ExpectedCargo)
{
	if (StoredPickupable.Get() != ExpectedCargo)
	{
		return;
	}
	UnbindStoredPickupable(ExpectedCargo);
	StoredPickupable = nullptr;
}

void AParadoxDumbwaiter::FinalizeCargoTransition(
	AParadoxInsertablePickupableActor* PreviousCargo,
	AParadoxInsertablePickupableActor* NewCargo,
	const bool bNotifyPair)
{
	RefreshInteractionAffordances();
	OnStoredPickupableChanged.Broadcast(this, PreviousCargo, NewCargo);
	ReceiveStoredPickupableChanged(PreviousCargo, NewCargo);
	if (bNotifyPair)
	{
		NotifyPairOccupancyChanged();
	}
}

void AParadoxDumbwaiter::NotifyPairOccupancyChanged()
{
	DispatchPairOccupancyChanged();
	if (AParadoxDumbwaiter* LinkedDumbwaiter =
		Cast<AParadoxDumbwaiter>(GetLinkedEndpoint()))
	{
		LinkedDumbwaiter->DispatchPairOccupancyChanged();
	}
}

void AParadoxDumbwaiter::DispatchPairOccupancyChanged()
{
	RefreshInteractionAffordances();
	const bool bSelfOccupied = IsOccupied();
	const bool bLinkedOccupied = IsLinkedDumbwaiterOccupied();
	OnPairOccupancyChanged.Broadcast(this, bSelfOccupied, bLinkedOccupied);
	ReceivePairOccupancyChanged(bSelfOccupied, bLinkedOccupied);
}

void AParadoxDumbwaiter::RefreshPairInteractionAffordances()
{
	RefreshInteractionAffordances();
	if (AParadoxDumbwaiter* LinkedDumbwaiter =
		Cast<AParadoxDumbwaiter>(GetLinkedEndpoint()))
	{
		LinkedDumbwaiter->RefreshInteractionAffordances();
	}
}

void AParadoxDumbwaiter::BindStoredPickupable(AParadoxInsertablePickupableActor& Cargo)
{
	Cargo.OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleStoredPickupableDestroyed);
}

void AParadoxDumbwaiter::UnbindStoredPickupable(AParadoxInsertablePickupableActor* Cargo)
{
	if (Cargo)
	{
		Cargo->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleStoredPickupableDestroyed);
	}
}

void AParadoxDumbwaiter::PrepareForWorldStateRestore()
{
	if (bResetInProgress)
	{
		return;
	}
	bResetInProgress = true;
	if (AParadoxInsertablePickupableActor* Cargo = StoredPickupable.Get())
	{
		UnbindStoredPickupable(Cargo);
		StoredPickupable = nullptr;
		Cargo->ClearDumbwaiterStateNative(true);
		Cargo->PrepareForWorldStateRestore();
	}
	RefreshInteractionAffordances();
}

void AParadoxDumbwaiter::RestoreCapturedRelationship()
{
	AParadoxInsertablePickupableActor* Cargo = WorldStateStoredPickupable.Get();
	if (!IsValid(Cargo))
	{
		StoredPickupable = nullptr;
		return;
	}
	if (Cargo->GetCurrentHolder() || Cargo->GetCurrentItemSlot()
		|| (Cargo->GetCurrentDumbwaiter() && Cargo->GetCurrentDumbwaiter() != this)
		|| !MatchesAcceptedCargoQuery(Cargo) || !TransferAnchor)
	{
		PARADOX_LOG_ERROR(
			TEXT("Dumbwaiter '%s' could not restore captured cargo '%s' because another owner is authoritative."),
			*GetNameSafe(this),
			*GetNameSafe(Cargo));
		StoredPickupable = nullptr;
		return;
	}
	SetStoredPickupableCommitted(Cargo);
	Cargo->SetDumbwaiterStateNative(*this, *TransferAnchor);
}

void AParadoxDumbwaiter::FinishWorldStateRestore(const bool bSucceeded)
{
	bResetInProgress = false;
	NotifyPairOccupancyChanged();
	if (!bSucceeded)
	{
		PARADOX_LOG_ERROR(TEXT("Dumbwaiter '%s' World State restore failed."), *GetNameSafe(this));
	}
}

void AParadoxDumbwaiter::RefreshInteractionAffordances()
{
	if (InteractionComponent)
	{
		InteractionComponent->NotifyInteractionAffordanceChanged();
	}
}

void AParadoxDumbwaiter::HandleWorldStateRestoreStarted(
	const FWorldStateRestoreLifecycleContext& Context)
{
	(void)Context;
	PrepareForWorldStateRestore();
}

void AParadoxDumbwaiter::HandleWorldStateRestoreFinished(
	const FWorldStateRestoreResult& Result)
{
	FinishWorldStateRestore(Result.IsSuccess());
}

void AParadoxDumbwaiter::HandleWorldStatePreCapture(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	WorldStateStoredPickupable = StoredPickupable.Get();
}

void AParadoxDumbwaiter::HandleWorldStatePreRestore(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	PrepareForWorldStateRestore();
}

void AParadoxDumbwaiter::HandleWorldStatePropertiesRestored(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	RestoreCapturedRelationship();
}

void AParadoxDumbwaiter::HandleWorldStateParticipantRestored(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	RefreshInteractionAffordances();
}

void AParadoxDumbwaiter::HandleWorldStateParticipantFailed(
	const FWorldStateParticipantResult& Result)
{
	PARADOX_LOG_ERROR(
		TEXT("Dumbwaiter '%s' World State participant restore failed (issues=%d)."),
		*GetNameSafe(this),
		Result.Issues.Num());
}

void AParadoxDumbwaiter::HandleStoredPickupableDestroyed(AActor* DestroyedActor)
{
	HandleStoredPickupableInvalidated(
		Cast<AParadoxInsertablePickupableActor>(DestroyedActor));
}

void AParadoxDumbwaiter::HandleStoredPickupableInvalidated(
	AParadoxInsertablePickupableActor* Cargo)
{
	if (!Cargo || StoredPickupable.Get() != Cargo)
	{
		return;
	}
	UnbindStoredPickupable(Cargo);
	StoredPickupable = nullptr;
	if (bInitialized && !bResetInProgress)
	{
		FinalizeCargoTransition(Cargo, nullptr);
	}
}

#undef LOCTEXT_NAMESPACE
