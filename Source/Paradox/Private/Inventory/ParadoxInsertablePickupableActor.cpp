#include "Inventory/ParadoxInsertablePickupableActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Inventory/ParadoxItemSlotActor.h"
#include "Paradox.h"

AParadoxInsertablePickupableActor::AParadoxInsertablePickupableActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

bool AParadoxInsertablePickupableActor::IsInserted() const
{
	const AParadoxItemSlotActor* Slot = CurrentItemSlot.Get();
	return GetPickupableState() == EParadoxPickupableState::Inserted
		&& Slot
		&& Slot->GetInsertedItem() == this
		&& GetCurrentHolder() == nullptr;
}

void AParadoxInsertablePickupableActor::NotifyOwningSlotRelevantStateChanged()
{
	if (AParadoxItemSlotActor* Slot = CurrentItemSlot.Get())
	{
		Slot->NotifyInsertedItemRelevantStateChanged(this);
	}
}

void AParadoxInsertablePickupableActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AParadoxItemSlotActor* Slot = CurrentItemSlot.Get())
	{
		Slot->HandleInsertedItemInvalidated(this);
	}
	CurrentItemSlot.Reset();
	Super::EndPlay(EndPlayReason);
}

void AParadoxInsertablePickupableActor::PrepareExternalOwnershipForWorldStateRestore()
{
	CurrentItemSlot.Reset();
}

bool AParadoxInsertablePickupableActor::RestoreExternalOwnershipAfterWorldState()
{
	AParadoxItemSlotActor* Slot = CurrentItemSlot.Get();
	if (!IsValid(Slot) || Slot->GetInsertedItem() != this || !Slot->GetInsertAnchor())
	{
		return false;
	}
	SetInsertedStateNative(*Slot, *Slot->GetInsertAnchor());
	return true;
}

bool AParadoxInsertablePickupableActor::ShouldUseAuthoredCollisionForCurrentState() const
{
	return Super::ShouldUseAuthoredCollisionForCurrentState()
		|| (GetPickupableState() == EParadoxPickupableState::Inserted
			&& bUseAuthoredInsertedCollision);
}

bool AParadoxInsertablePickupableActor::ShouldUseAuthoredNavigationForCurrentState() const
{
	return Super::ShouldUseAuthoredNavigationForCurrentState()
		|| (GetPickupableState() == EParadoxPickupableState::Inserted
			&& bUseAuthoredInsertedNavigationInfluence);
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

void AParadoxInsertablePickupableActor::ClearInsertedStateNative(const bool bDetach)
{
	if (bDetach)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	CurrentItemSlot.Reset();
}
