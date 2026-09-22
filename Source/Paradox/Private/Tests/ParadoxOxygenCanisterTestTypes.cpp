#include "Tests/ParadoxOxygenCanisterTestTypes.h"

#include "Characters/ParadoxCharacter.h"
#include "Inventory/ParadoxInventoryComponent.h"

void UParadoxOxygenCanisterTestPassiveEffect::Apply_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable)
{
	(void)Character;
	(void)Pickupable;
	++ApplyCount;
}

void UParadoxOxygenCanisterTestPassiveEffect::Remove_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable)
{
	(void)Character;
	(void)Pickupable;
	++RemoveCount;
}

AParadoxOxygenCanisterTestActor::AParadoxOxygenCanisterTestActor()
{
	TestPassiveEffect = CreateDefaultSubobject<UParadoxOxygenCanisterTestPassiveEffect>(
		TEXT("TestPassiveEffect"));
	PassiveEffects.Add(TestPassiveEffect);
}

void AParadoxOxygenCanisterTestActor::HandleUseCommitted(
	AParadoxCharacter* Character,
	const FParadoxPickupableUseResult& Result)
{
	++CommittedCount;
	Super::HandleUseCommitted(Character, Result);
}

void AParadoxOxygenCanisterTestActor::HandleUseFailed(
	AParadoxCharacter* Character,
	const FParadoxPickupableUseResult& Result)
{
	++FailedCount;
	Super::HandleUseFailed(Character, Result);
}

void UParadoxOxygenCanisterReentrantObserver::HandleOxygenChanged(
	float OldRemainingSeconds,
	float NewRemainingSeconds,
	float DurationSeconds,
	float NormalizedOxygen)
{
	(void)OldRemainingSeconds;
	(void)NewRemainingSeconds;
	(void)DurationSeconds;
	(void)NormalizedOxygen;
	++OxygenChangedCount;
	if (Inventory && Canister)
	{
		ReentrantDropStatus = Inventory->TryDropAtTransform(Canister->GetActorTransform()).Status;
		ReentrantUseResult = Inventory->TryUseEquippedItem(Canister);
	}
}

void UParadoxOxygenCanisterReentrantObserver::HandleEquippedItemChanged(
	AParadoxPickupableActor* PreviousItem,
	AParadoxPickupableActor* NewItem)
{
	(void)PreviousItem;
	(void)NewItem;
	++InventoryTransitionCount;
}
