#include "Inventory/ParadoxInsertablePickupableActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Inventory/ParadoxItemSlotActor.h"
#include "Paradox.h"
#include "Puzzles/ParadoxDumbwaiter.h"

AParadoxInsertablePickupableActor::AParadoxInsertablePickupableActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AParadoxInsertablePickupableActor::IsInserted() const
{
	const AParadoxItemSlotActor* Slot = CurrentItemSlot.Get();
	const AParadoxDumbwaiter* Dumbwaiter = CurrentDumbwaiter.Get();
	return GetPickupableState() == EParadoxPickupableState::Inserted
		&& ((Slot && !Dumbwaiter && Slot->GetInsertedItem() == this)
			|| (Dumbwaiter && !Slot && Dumbwaiter->GetStoredPickupable() == this))
		&& GetCurrentHolder() == nullptr;
}

void AParadoxInsertablePickupableActor::NotifyOwningSlotRelevantStateChanged()
{
	if (AParadoxItemSlotActor* Slot = CurrentItemSlot.Get())
	{
		Slot->NotifyInsertedItemRelevantStateChanged(this);
		return;
	}
	if (AParadoxDumbwaiter* Dumbwaiter = CurrentDumbwaiter.Get())
	{
		Dumbwaiter->NotifyStoredPickupableRelevantStateChanged(this);
	}
}

void AParadoxInsertablePickupableActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AParadoxItemSlotActor* Slot = CurrentItemSlot.Get())
	{
		Slot->HandleInsertedItemInvalidated(this);
	}
	if (AParadoxDumbwaiter* Dumbwaiter = CurrentDumbwaiter.Get())
	{
		Dumbwaiter->HandleStoredPickupableInvalidated(this);
	}
	CurrentItemSlot.Reset();
	CurrentDumbwaiter.Reset();
	Super::EndPlay(EndPlayReason);
}

void AParadoxInsertablePickupableActor::PrepareExternalOwnershipForWorldStateRestore()
{
	CurrentItemSlot.Reset();
	CurrentDumbwaiter.Reset();
}

bool AParadoxInsertablePickupableActor::RestoreExternalOwnershipAfterWorldState()
{
	AParadoxItemSlotActor* Slot = CurrentItemSlot.Get();
	AParadoxDumbwaiter* Dumbwaiter = CurrentDumbwaiter.Get();
	if (IsValid(Slot) && !Dumbwaiter && Slot->GetInsertedItem() == this && Slot->GetInsertAnchor())
	{
		SetInsertedStateNative(*Slot, *Slot->GetInsertAnchor());
		return true;
	}
	if (IsValid(Dumbwaiter) && !Slot
		&& Dumbwaiter->GetStoredPickupable() == this
		&& Dumbwaiter->TransferAnchor)
	{
		SetDumbwaiterStateNative(*Dumbwaiter, *Dumbwaiter->TransferAnchor);
		return true;
	}
	return false;
}

bool AParadoxInsertablePickupableActor::ShouldUseAuthoredCollisionForCurrentState() const
{
	return !bDumbwaiterTransferPresenceSuspended
		&& (Super::ShouldUseAuthoredCollisionForCurrentState()
		|| (GetPickupableState() == EParadoxPickupableState::Inserted
			&& bUseAuthoredInsertedCollision));
}

bool AParadoxInsertablePickupableActor::ShouldUseAuthoredNavigationForCurrentState() const
{
	return !bDumbwaiterTransferPresenceSuspended
		&& (Super::ShouldUseAuthoredNavigationForCurrentState()
		|| (GetPickupableState() == EParadoxPickupableState::Inserted
			&& bUseAuthoredInsertedNavigationInfluence));
}

bool AParadoxInsertablePickupableActor::ShouldPreserveAuthoredCollisionConfiguration() const
{
	return Super::ShouldPreserveAuthoredCollisionConfiguration()
		|| bUseAuthoredInsertedCollision;
}

bool AParadoxInsertablePickupableActor::ShouldPreserveAuthoredNavigationConfiguration() const
{
	return Super::ShouldPreserveAuthoredNavigationConfiguration()
		|| bUseAuthoredInsertedNavigationInfluence;
}

void AParadoxInsertablePickupableActor::SetInsertedStateNative(
	AParadoxItemSlotActor& NewSlot,
	USceneComponent& InsertAnchor)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CurrentDumbwaiter.Reset();
	CurrentItemSlot = &NewSlot;
	SetExternallyOwnedStateNative(EParadoxPickupableState::Inserted, false);
	SetActorTransform(InsertAnchor.GetComponentTransform(), false, nullptr, ETeleportType::TeleportPhysics);
	if (!AttachToComponent(&InsertAnchor, FAttachmentTransformRules::SnapToTargetNotIncludingScale))
	{
		PARADOX_LOG_ERROR(
			TEXT("Insertable pickupable '%s' could not attach to slot anchor '%s'; ownership remains coherent at the anchor transform."),
			*GetNameSafe(this),
			*GetNameSafe(&InsertAnchor));
	}
	RefreshPresenceAfterPlacement();
}

void AParadoxInsertablePickupableActor::SetDumbwaiterStateNative(
	AParadoxDumbwaiter& NewDumbwaiter,
	USceneComponent& CargoAnchor)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CurrentItemSlot.Reset();
	CurrentDumbwaiter = &NewDumbwaiter;
	SetExternallyOwnedStateNative(EParadoxPickupableState::Inserted, false);
	SetActorTransform(CargoAnchor.GetComponentTransform(), false, nullptr, ETeleportType::TeleportPhysics);
	if (!AttachToComponent(&CargoAnchor, FAttachmentTransformRules::SnapToTargetNotIncludingScale))
	{
		PARADOX_LOG_ERROR(
			TEXT("Insertable pickupable '%s' could not attach to dumbwaiter anchor '%s'; ownership remains coherent at the anchor transform."),
			*GetNameSafe(this),
			*GetNameSafe(&CargoAnchor));
	}
	RefreshPresenceAfterPlacement();
}

void AParadoxInsertablePickupableActor::SetDumbwaiterTransferPresenceSuspendedNative(
	const bool bSuspended)
{
	if (bDumbwaiterTransferPresenceSuspended == bSuspended)
	{
		return;
	}
	bDumbwaiterTransferPresenceSuspended = bSuspended;
	RefreshPresenceAfterPlacement();
}

void AParadoxInsertablePickupableActor::ClearInsertedStateNative(const bool bDetach)
{
	if (bDetach)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	CurrentItemSlot.Reset();
}

void AParadoxInsertablePickupableActor::ClearDumbwaiterStateNative(const bool bDetach)
{
	if (bDetach)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	CurrentDumbwaiter.Reset();
}
