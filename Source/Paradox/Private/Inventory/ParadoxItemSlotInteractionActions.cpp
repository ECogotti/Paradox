#include "Inventory/ParadoxItemSlotInteractionActions.h"

#include "Characters/ParadoxCharacter.h"
#include "GameplayActionTags.h"
#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxItemSlotActor.h"
#include "Paradox.h"
#include "Puzzles/ParadoxDumbwaiter.h"

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
		AActor*& OutTarget,
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic)
	{
		OutCharacter = Cast<AParadoxCharacter>(Action.GetInteractionRequester());
		OutTarget = Action.GetInteractionTarget();
		if (!OutCharacter || !OutCharacter->GetInventoryComponent()
			|| (!Cast<AParadoxItemSlotActor>(OutTarget)
				&& !Cast<AParadoxDumbwaiter>(OutTarget)))
		{
			OutFailureReason = ParadoxGameplayTags::Result_Failure_ItemSlot_InvalidRequest;
			OutDiagnostic = TEXT("Insert and Pickup require a Paradox Character, inventory, and Item Slot or Dumbwaiter target.");
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
	AActor* Target = nullptr;
	if (!UE::Paradox::ItemSlotInteraction::Private::Resolve(
		*this, Character, Target, OutFailureReason, OutDiagnostic))
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
		if (AParadoxItemSlotActor* Slot = Cast<AParadoxItemSlotActor>(Target))
		{
			Result = Slot->EvaluateAcceptItem(Item, Character);
		}
		else
		{
			Result = CastChecked<AParadoxDumbwaiter>(Target)->EvaluateAcceptCargo(Item, Character);
		}
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
	AParadoxDumbwaiter* Dumbwaiter = Cast<AParadoxDumbwaiter>(GetInteractionTarget());
	const FParadoxItemSlotOperationResult Result = Slot
		? Slot->TryInsertItem(Character)
		: Dumbwaiter
			? Dumbwaiter->TryInsertCargo(Character)
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
	AActor* Target = nullptr;
	if (!UE::Paradox::ItemSlotInteraction::Private::Resolve(
		*this, Character, Target, OutFailureReason, OutDiagnostic))
	{
		return false;
	}
	const AParadoxItemSlotActor* Slot = Cast<AParadoxItemSlotActor>(Target);
	const FParadoxItemSlotOperationResult Result = Slot
		? Slot->EvaluatePickupInsertedItem(Character)
		: CastChecked<AParadoxDumbwaiter>(Target)->EvaluatePickupCargo(Character);
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
	AParadoxDumbwaiter* Dumbwaiter = Cast<AParadoxDumbwaiter>(GetInteractionTarget());
	const FParadoxItemSlotOperationResult Result = Slot
		? Slot->TryPickupInsertedItem(Character)
		: Dumbwaiter
			? Dumbwaiter->TryPickupCargo(Character)
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
