#include "Inventory/ParadoxInventoryComponent.h"

#include "Characters/ParadoxCharacter.h"
#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Inventory/ParadoxItemSlotActor.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Inventory/ParadoxPickupablePassiveEffect.h"
#include "Paradox.h"
#include "Subsystems/WorldStateSubsystem.h"

namespace UE::Paradox::Inventory::Private
{
	struct FOperationGuard
	{
		explicit FOperationGuard(bool& InFlag) : Flag(InFlag) { Flag = true; }
		~FOperationGuard() { Flag = false; }
		bool& Flag;
	};
}

UParadoxInventoryComponent::UParadoxInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UParadoxInventoryComponent::HasItem() const
{
	return IsValid(EquippedItem.Get());
}

bool UParadoxInventoryComponent::CanUnequip() const
{
	return IsValid(EquippedItem.Get()) && !bOperationInProgress && !bResetInProgress;
}

FParadoxItemSlotOperationResult UParadoxInventoryComponent::TransferEquippedItemToSlot(
	AParadoxItemSlotActor& Slot,
	AParadoxInsertablePickupableActor& Item)
{
	AParadoxCharacter* Character = GetParadoxCharacter();
	if (!Character || EquippedItem.Get() != &Item || Item.GetCurrentHolder() != Character)
	{
		return Slot.MakeResult(
			EParadoxItemSlotOperationStatus::OwnershipConflict,
			TEXT("Inventory ownership changed before the Insert transaction committed."));
	}
	if (bResetInProgress || Slot.bResetInProgress)
	{
		return Slot.MakeResult(
			EParadoxItemSlotOperationStatus::ResetInProgress,
			TEXT("Insert was rejected because World State restore started."));
	}
	if (bOperationInProgress)
	{
		return Slot.MakeResult(
			EParadoxItemSlotOperationStatus::OperationInProgress,
			TEXT("A reentrant inventory transition was rejected."));
	}
	if (Slot.InsertedItem || Item.GetCurrentItemSlot() || !Slot.InsertAnchor)
	{
		return Slot.MakeResult(
			EParadoxItemSlotOperationStatus::OwnershipConflict,
			TEXT("Item Slot ownership changed before the Insert transaction committed."));
	}

	UE::Paradox::Inventory::Private::FOperationGuard Guard(bOperationInProgress);
	RemoveAppliedPassiveEffects(&Item);
	UnbindEquippedItem(&Item);
	EquippedItem = nullptr;
	Slot.SetInsertedItemCommitted(&Item);
	Item.SetInsertedStateNative(Slot, *Slot.InsertAnchor);

	Item.ReceiveInsertedIntoSlot(&Slot);
	BroadcastTransition(&Item, nullptr);
	Slot.FinalizeOccupancyTransition(nullptr, &Item);
	LogDebugState(TEXT("TransferToItemSlot"));
	return Slot.MakeResult(
		EParadoxItemSlotOperationStatus::Succeeded,
		TEXT("The equipped item was inserted atomically."));
}

FParadoxItemSlotOperationResult UParadoxInventoryComponent::TransferInsertedItemFromSlot(
	AParadoxItemSlotActor& Slot,
	AParadoxInsertablePickupableActor& Item)
{
	AParadoxCharacter* Character = GetParadoxCharacter();
	if (!Character || EquippedItem || Slot.InsertedItem.Get() != &Item
		|| Item.GetCurrentItemSlot() != &Slot || Item.GetCurrentHolder())
	{
		return Slot.MakeResult(
			EParadoxItemSlotOperationStatus::OwnershipConflict,
			TEXT("Item Slot or inventory ownership changed before Pickup committed."));
	}
	if (bResetInProgress || Slot.bResetInProgress)
	{
		return Slot.MakeResult(
			EParadoxItemSlotOperationStatus::ResetInProgress,
			TEXT("Pickup was rejected because World State restore started."));
	}
	if (bOperationInProgress)
	{
		return Slot.MakeResult(
			EParadoxItemSlotOperationStatus::OperationInProgress,
			TEXT("A reentrant inventory transition was rejected."));
	}

	UE::Paradox::Inventory::Private::FOperationGuard Guard(bOperationInProgress);
	Slot.ClearInsertedItemCommitted(&Item);
	Item.ClearInsertedStateNative(true);
	EquippedItem = &Item;
	BindEquippedItem(Item);
	Item.SetHeldStateNative(*Character, false);
	ApplyPassiveEffects(Item);

	Item.ReceiveRemovedFromSlot(&Slot);
	Item.ReceivePickedUp(Character);
	BroadcastTransition(nullptr, &Item);
	Slot.FinalizeOccupancyTransition(&Item, nullptr);
	LogDebugState(TEXT("TransferFromItemSlot"));
	return Slot.MakeResult(
		EParadoxItemSlotOperationStatus::Succeeded,
		TEXT("The inserted item was picked up atomically."));
}

void UParadoxInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetParadoxCharacter())
	{
		PARADOX_LOG_ERROR(
			TEXT("Inventory component '%s' requires an AParadoxCharacter owner, got '%s'."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
	}
	if (UWorldStateSubsystem* WorldState = GetWorld() ? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr)
	{
		WorldState->OnRestoreStartedNative().AddUObject(this, &ThisClass::HandleWorldStateRestoreStarted);
		WorldState->OnRestoreCompletedNative().AddUObject(this, &ThisClass::HandleWorldStateRestoreFinished);
		WorldState->OnRestoreFailedNative().AddUObject(this, &ThisClass::HandleWorldStateRestoreFinished);
	}
}

void UParadoxInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorldStateSubsystem* WorldState = GetWorld() ? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr)
	{
		WorldState->OnRestoreStartedNative().RemoveAll(this);
		WorldState->OnRestoreCompletedNative().RemoveAll(this);
		WorldState->OnRestoreFailedNative().RemoveAll(this);
	}
	ClearInventoryForReset();
	Super::EndPlay(EndPlayReason);
}

bool UParadoxInventoryComponent::CanEquip(AParadoxPickupableActor* Item) const
{
	return GetParadoxCharacter()
		&& !bOperationInProgress
		&& !bResetInProgress
		&& !IsValid(EquippedItem)
		&& IsValid(Item)
		&& Item->IsAvailableInWorld();
}

FParadoxInventoryOperationResult UParadoxInventoryComponent::TryEquip(
	AParadoxPickupableActor* Item)
{
	AParadoxCharacter* Character = GetParadoxCharacter();
	if (!Character)
	{
		return MakeResult(EParadoxInventoryOperationStatus::InvalidOwner, TEXT("Inventory owner is not a valid Paradox Character."));
	}
	if (bResetInProgress)
	{
		return MakeResult(EParadoxInventoryOperationStatus::ResetInProgress, TEXT("Inventory transitions are disabled during World State restore."));
	}
	if (bOperationInProgress)
	{
		return MakeResult(EParadoxInventoryOperationStatus::OperationInProgress, TEXT("A reentrant inventory transition was rejected."));
	}
	if (IsValid(EquippedItem))
	{
		return MakeResult(EParadoxInventoryOperationStatus::SlotOccupied, TEXT("The single inventory slot is already occupied."));
	}
	if (!IsValid(Item))
	{
		return MakeResult(EParadoxInventoryOperationStatus::InvalidItem, TEXT("A valid pickupable is required."));
	}
	if (!Item->IsAvailableInWorld())
	{
		return MakeResult(
			Item->GetCurrentHolder() ? EParadoxInventoryOperationStatus::OwnershipConflict : EParadoxInventoryOperationStatus::ItemUnavailable,
			TEXT("The pickupable is not available in the world."));
	}

	UE::Paradox::Inventory::Private::FOperationGuard Guard(bOperationInProgress);
	EquippedItem = Item;
	BindEquippedItem(*Item);
	Item->SetHeldStateNative(*Character, false);
	ApplyPassiveEffects(*Item);
	Item->ReceivePickedUp(Character);
	BroadcastTransition(nullptr, Item);
	LogDebugState(TEXT("Equip"));
	return MakeResult(EParadoxInventoryOperationStatus::Succeeded, TEXT("Pickupable equipped."));
}

FParadoxInventoryOperationResult UParadoxInventoryComponent::TrySwap(
	AParadoxPickupableActor* IncomingItem)
{
	AParadoxCharacter* Character = GetParadoxCharacter();
	AParadoxPickupableActor* OutgoingItem = EquippedItem.Get();
	if (!Character)
	{
		return MakeResult(EParadoxInventoryOperationStatus::InvalidOwner, TEXT("Inventory owner is not a valid Paradox Character."));
	}
	if (bResetInProgress)
	{
		return MakeResult(EParadoxInventoryOperationStatus::ResetInProgress, TEXT("Inventory transitions are disabled during World State restore."));
	}
	if (bOperationInProgress)
	{
		return MakeResult(EParadoxInventoryOperationStatus::OperationInProgress, TEXT("A reentrant inventory transition was rejected."));
	}
	if (!IsValid(OutgoingItem))
	{
		return MakeResult(EParadoxInventoryOperationStatus::SlotEmpty, TEXT("Swap requires an equipped pickupable."));
	}
	if (!IsValid(IncomingItem) || IncomingItem == OutgoingItem)
	{
		return MakeResult(EParadoxInventoryOperationStatus::InvalidItem, TEXT("Swap requires a different valid pickupable."));
	}
	if (!IncomingItem->IsAvailableInWorld())
	{
		return MakeResult(EParadoxInventoryOperationStatus::ItemUnavailable, TEXT("The incoming pickupable is not available in the world."));
	}
	if (OutgoingItem->GetCurrentHolder() != Character)
	{
		return MakeResult(EParadoxInventoryOperationStatus::OwnershipConflict, TEXT("The equipped pickupable holder does not match the inventory owner."));
	}

	const FTransform IncomingWorldTransform = IncomingItem->GetActorTransform();
	UE::Paradox::Inventory::Private::FOperationGuard Guard(bOperationInProgress);
	RemoveAppliedPassiveEffects(OutgoingItem);
	UnbindEquippedItem(OutgoingItem);
	OutgoingItem->SetWorldStateNative(IncomingWorldTransform, Character, false);
	EquippedItem = IncomingItem;
	BindEquippedItem(*IncomingItem);
	IncomingItem->SetHeldStateNative(*Character, false);
	ApplyPassiveEffects(*IncomingItem);
	OutgoingItem->ReceiveDropped(Character);
	IncomingItem->ReceivePickedUp(Character);
	BroadcastTransition(OutgoingItem, IncomingItem);
	LogDebugState(TEXT("Swap"));
	return MakeResult(EParadoxInventoryOperationStatus::Succeeded, TEXT("Pickupables swapped atomically."));
}

FParadoxInventoryOperationResult UParadoxInventoryComponent::TryDropAtTransform(
	const FTransform& WorldTransform)
{
	AParadoxCharacter* Character = GetParadoxCharacter();
	AParadoxPickupableActor* Item = EquippedItem.Get();
	if (!Character)
	{
		return MakeResult(EParadoxInventoryOperationStatus::InvalidOwner, TEXT("Inventory owner is not a valid Paradox Character."));
	}
	if (bResetInProgress)
	{
		return MakeResult(EParadoxInventoryOperationStatus::ResetInProgress, TEXT("Inventory transitions are disabled during World State restore."));
	}
	if (bOperationInProgress)
	{
		return MakeResult(EParadoxInventoryOperationStatus::OperationInProgress, TEXT("A reentrant inventory transition was rejected."));
	}
	if (!IsValid(Item))
	{
		return MakeResult(EParadoxInventoryOperationStatus::SlotEmpty, TEXT("Drop requires an equipped pickupable."));
	}
	if (!WorldTransform.IsValid())
	{
		return MakeResult(EParadoxInventoryOperationStatus::InvalidPlacement, TEXT("Drop placement transform is invalid."));
	}
	if (Item->GetCurrentHolder() != Character)
	{
		return MakeResult(EParadoxInventoryOperationStatus::OwnershipConflict, TEXT("The equipped pickupable holder does not match the inventory owner."));
	}

	UE::Paradox::Inventory::Private::FOperationGuard Guard(bOperationInProgress);
	RemoveAppliedPassiveEffects(Item);
	UnbindEquippedItem(Item);
	EquippedItem = nullptr;
	Item->SetWorldStateNative(WorldTransform, Character, true);
	BroadcastTransition(Item, nullptr);
	LogDebugState(TEXT("Drop"));
	return MakeResult(EParadoxInventoryOperationStatus::Succeeded, TEXT("Pickupable dropped into the world."));
}

FParadoxInventoryOperationResult UParadoxInventoryComponent::ClearInventoryForReset()
{
	if (bOperationInProgress)
	{
		return MakeResult(EParadoxInventoryOperationStatus::OperationInProgress, TEXT("Inventory cleanup cannot interrupt an active transition."));
	}
	AParadoxPickupableActor* Item = EquippedItem.Get();
	if (!Item)
	{
		AppliedPassiveEffects.Reset();
		return MakeResult(EParadoxInventoryOperationStatus::Succeeded, TEXT("Inventory was already empty."));
	}

	UE::Paradox::Inventory::Private::FOperationGuard Guard(bOperationInProgress);
	RemoveAppliedPassiveEffects(Item);
	UnbindEquippedItem(Item);
	EquippedItem = nullptr;
	Item->PrepareForWorldStateRestore();
	BroadcastTransition(Item, nullptr);
	LogDebugState(TEXT("ClearForReset"));
	return MakeResult(EParadoxInventoryOperationStatus::Succeeded, TEXT("Inventory cleared for World State restore."));
}

FParadoxInventoryOperationResult UParadoxInventoryComponent::MakeResult(
	const EParadoxInventoryOperationStatus Status,
	FString Diagnostic) const
{
	FParadoxInventoryOperationResult Result;
	Result.Status = Status;
	Result.DiagnosticMessage = MoveTemp(Diagnostic);
	return Result;
}

AParadoxCharacter* UParadoxInventoryComponent::GetParadoxCharacter() const
{
	return Cast<AParadoxCharacter>(GetOwner());
}

void UParadoxInventoryComponent::ApplyPassiveEffects(AParadoxPickupableActor& Item)
{
	AppliedPassiveEffects.Reset();
	AParadoxCharacter* Character = GetParadoxCharacter();
	for (UParadoxPickupablePassiveEffect* Effect : Item.GetPassiveEffects())
	{
		if (!IsValid(Effect))
		{
			PARADOX_LOG_WARNING(TEXT("Pickupable '%s' contains a null passive effect."), *GetNameSafe(&Item));
			continue;
		}
		if (AppliedPassiveEffects.Contains(Effect))
		{
			PARADOX_LOG_WARNING(TEXT("Pickupable '%s' contains duplicate passive effect '%s'; it was applied once."), *GetNameSafe(&Item), *GetNameSafe(Effect));
			continue;
		}
		Effect->Apply(Character, &Item);
		AppliedPassiveEffects.Add(Effect);
	}
}

void UParadoxInventoryComponent::RemoveAppliedPassiveEffects(AParadoxPickupableActor* Item)
{
	AParadoxCharacter* Character = GetParadoxCharacter();
	for (int32 Index = AppliedPassiveEffects.Num() - 1; Index >= 0; --Index)
	{
		if (UParadoxPickupablePassiveEffect* Effect = AppliedPassiveEffects[Index])
		{
			Effect->Remove(Character, Item);
		}
	}
	AppliedPassiveEffects.Reset();
}

void UParadoxInventoryComponent::BindEquippedItem(AParadoxPickupableActor& Item)
{
	Item.OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleEquippedItemDestroyed);
}

void UParadoxInventoryComponent::UnbindEquippedItem(AParadoxPickupableActor* Item)
{
	if (Item)
	{
		Item->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleEquippedItemDestroyed);
	}
}

void UParadoxInventoryComponent::BroadcastTransition(
	AParadoxPickupableActor* PreviousItem,
	AParadoxPickupableActor* NewItem)
{
	OnEquippedItemChanged.Broadcast(PreviousItem, NewItem);
}

void UParadoxInventoryComponent::LogDebugState(const TCHAR* EventName) const
{
	if (!bEnableDebug || !IsParadoxInventoryDebugEnabled())
	{
		return;
	}
	PARADOX_LOG_INFO(
		TEXT("Inventory event=%s owner=%s item=%s passive_effects=%d reset=%d"),
		EventName,
		*GetNameSafe(GetOwner()),
		*GetNameSafe(EquippedItem.Get()),
		AppliedPassiveEffects.Num(),
		bResetInProgress ? 1 : 0);
}

void UParadoxInventoryComponent::HandleWorldStateRestoreStarted(
	const FWorldStateRestoreLifecycleContext& Context)
{
	(void)Context;
	bResetInProgress = true;
	const FParadoxInventoryOperationResult Result = ClearInventoryForReset();
	if (!Result.IsSuccess())
	{
		PARADOX_LOG_ERROR(
			TEXT("Inventory '%s' could not clear for World State restore: %s"),
			*GetNameSafe(this),
			*Result.DiagnosticMessage);
	}
}

void UParadoxInventoryComponent::HandleWorldStateRestoreFinished(
	const FWorldStateRestoreResult& Result)
{
	(void)Result;
	bResetInProgress = false;
	LogDebugState(TEXT("WorldStateRestoreFinished"));
}

void UParadoxInventoryComponent::HandleEquippedItemDestroyed(AActor* DestroyedActor)
{
	AParadoxPickupableActor* DestroyedItem = Cast<AParadoxPickupableActor>(DestroyedActor);
	if (!DestroyedItem || DestroyedItem != EquippedItem)
	{
		return;
	}
	if (bOperationInProgress)
	{
		PARADOX_LOG_ERROR(TEXT("Equipped pickupable '%s' was destroyed during an inventory transition."), *GetNameSafe(DestroyedItem));
	}
	RemoveAppliedPassiveEffects(DestroyedItem);
	UnbindEquippedItem(DestroyedItem);
	EquippedItem = nullptr;
	BroadcastTransition(DestroyedItem, nullptr);
	LogDebugState(TEXT("EquippedItemDestroyed"));
}
