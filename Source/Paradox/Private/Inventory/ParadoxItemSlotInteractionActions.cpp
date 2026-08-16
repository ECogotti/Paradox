#include "Inventory/ParadoxItemSlotInteractionActions.h"

#include "Characters/ParadoxCharacter.h"
#include "GameplayActionTags.h"
#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxItemSlotActor.h"
#include "Paradox.h"

namespace UE::Paradox::ItemSlotInteraction::Private
{
	FGameplayTag FailureTagForStatus(const EParadoxItemSlotOperationStatus Status)
	{
		switch (Status)
		{
		case EParadoxItemSlotOperationStatus::SlotInactive:
			return ParadoxGameplayTags::Result_Failure_ItemSlot_Inactive;
		case EParadoxItemSlotOperationStatus::SlotOccupied:
			return ParadoxGameplayTags::Result_Failure_ItemSlot_Occupied;
		case EParadoxItemSlotOperationStatus::SlotEmpty:
			return ParadoxGameplayTags::Result_Failure_ItemSlot_Empty;
		case EParadoxItemSlotOperationStatus::ItemLocked:
			return ParadoxGameplayTags::Result_Failure_ItemSlot_Locked;
		case EParadoxItemSlotOperationStatus::IncompatibleTraits:
		case EParadoxItemSlotOperationStatus::NotInsertable:
		case EParadoxItemSlotOperationStatus::AdditionalValidationFailed:
			return ParadoxGameplayTags::Result_Failure_ItemSlot_Incompatible;
		case EParadoxItemSlotOperationStatus::OwnershipConflict:
		case EParadoxItemSlotOperationStatus::RequesterDoesNotOwnItem:
			return ParadoxGameplayTags::Result_Failure_ItemSlot_OwnershipConflict;
		case EParadoxItemSlotOperationStatus::InventoryOccupied:
			return ParadoxGameplayTags::Result_Failure_Inventory_SlotOccupied;
		default:
			return ParadoxGameplayTags::Result_Failure_ItemSlot_InvalidRequest;
		}
	}

	bool Resolve(
		const UParadoxInteractionActionBase& Action,
		AParadoxCharacter*& OutCharacter,
		AParadoxItemSlotActor*& OutSlot,
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic)
	{
		OutCharacter = Cast<AParadoxCharacter>(Action.GetInteractionRequester());
		OutSlot = Cast<AParadoxItemSlotActor>(Action.GetInteractionTarget());
		if (!OutCharacter || !OutCharacter->GetInventoryComponent() || !OutSlot)
		{
			OutFailureReason = ParadoxGameplayTags::Result_Failure_ItemSlot_InvalidRequest;
			OutDiagnostic = TEXT("Item Slot interactions require a Paradox Character, inventory and Item Slot target.");
			return false;
		}
		return true;
	}
}

bool UParadoxInsertItemInteractionAction::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	AParadoxCharacter* Character = nullptr;
	AParadoxItemSlotActor* Slot = nullptr;
	if (!UE::Paradox::ItemSlotInteraction::Private::Resolve(
		*this, Character, Slot, OutFailureReason, OutDiagnostic))
	{
		return false;
	}
	UParadoxInventoryComponent* Inventory = Character->GetInventoryComponent();
	AParadoxInsertablePickupableActor* Item =
		Cast<AParadoxInsertablePickupableActor>(Inventory->GetEquippedItem());
	FParadoxItemSlotOperationResult Result;
	if (Inventory->HasItem() && !Item)
	{
		Result.Status = EParadoxItemSlotOperationStatus::NotInsertable;
		Result.DiagnosticMessage = TEXT("The equipped pickupable is not insertable.");
	}
	else
	{
		Result = Slot->EvaluateAcceptItem(Item, Character);
	}
	if (Result.IsSuccess())
	{
		return true;
	}
	OutFailureReason =
		UE::Paradox::ItemSlotInteraction::Private::FailureTagForStatus(Result.Status);
	OutDiagnostic = Result.DiagnosticMessage;
	return false;
}

void UParadoxInsertItemInteractionAction::ExecuteInteraction_Implementation()
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	AParadoxItemSlotActor* Slot = Cast<AParadoxItemSlotActor>(GetInteractionTarget());
	const FParadoxItemSlotOperationResult Result = Slot
		? Slot->TryInsertItem(Character)
		: FParadoxItemSlotOperationResult();
	if (Result.IsSuccess())
	{
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, Result.DiagnosticMessage);
		return;
	}
	CompleteInteractionFailure(
		UE::Paradox::ItemSlotInteraction::Private::FailureTagForStatus(Result.Status),
		Result.DiagnosticMessage);
}

bool UParadoxPickupFromItemSlotInteractionAction::ValidatePickupSource(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	AParadoxCharacter* Character = nullptr;
	AParadoxItemSlotActor* Slot = nullptr;
	if (!UE::Paradox::ItemSlotInteraction::Private::Resolve(
		*this, Character, Slot, OutFailureReason, OutDiagnostic))
	{
		return false;
	}
	const FParadoxItemSlotOperationResult Result =
		Slot->EvaluatePickupInsertedItem(Character);
	if (Result.IsSuccess())
	{
		return true;
	}
	OutFailureReason =
		UE::Paradox::ItemSlotInteraction::Private::FailureTagForStatus(Result.Status);
	OutDiagnostic = Result.DiagnosticMessage;
	return false;
}

bool UParadoxPickupFromItemSlotInteractionAction::IsPickupSourceAcquired() const
{
	// The semantic request identifies the Slot, not a mutable requester-relative item.
	// A concurrent empty Slot is therefore revalidated as a failure instead of guessed as success.
	return false;
}

bool UParadoxPickupFromItemSlotInteractionAction::CommitPickupSource(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic)
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	AParadoxItemSlotActor* Slot = Cast<AParadoxItemSlotActor>(GetInteractionTarget());
	const FParadoxItemSlotOperationResult Result = Slot
		? Slot->TryPickupInsertedItem(Character)
		: FParadoxItemSlotOperationResult();
	if (Result.IsSuccess())
	{
		OutDiagnostic = Result.DiagnosticMessage;
		return true;
	}
	OutFailureReason =
		UE::Paradox::ItemSlotInteraction::Private::FailureTagForStatus(Result.Status);
	OutDiagnostic = Result.DiagnosticMessage;
	return false;
}
