#include "Tests/ParadoxInventoryTestTypes.h"

#include "Characters/ParadoxCharacter.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxPickupableAction.h"

void UParadoxInventoryTestPassiveEffect::Apply_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable)
{
	++ApplyCount;
	if (bAttemptReentrantDrop && Character && Character->GetInventoryComponent())
	{
		ReentrantStatus = Character->GetInventoryComponent()
			->TryDropAtTransform(Pickupable ? Pickupable->GetActorTransform() : FTransform::Identity)
			.Status;
	}
}

void UParadoxInventoryTestPassiveEffect::Remove_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable)
{
	(void)Character;
	(void)Pickupable;
	++RemoveCount;
}

AParadoxInventoryTestPickupable::AParadoxInventoryTestPickupable()
{
	TestEffect = CreateDefaultSubobject<UParadoxInventoryTestPassiveEffect>(TEXT("TestPassiveEffect"));
	PassiveEffects.Add(TestEffect);
}

void AParadoxInventoryTestPickupable::ClearTestConfiguration()
{
	PassiveEffects.Reset();
	PickupableActions.Reset();
}

UParadoxPickupableAction* AParadoxInventoryTestPickupable::AddEmptyTestAction()
{
	UParadoxPickupableAction* Action = NewObject<UParadoxPickupableAction>(this);
	PickupableActions.Add(Action);
	return Action;
}
