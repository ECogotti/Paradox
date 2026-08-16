#include "Tests/ParadoxItemSlotTestTypes.h"

#include "Characters/ParadoxCharacter.h"

void UParadoxItemSlotTestPassiveEffect::Apply_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable)
{
	(void)Character;
	(void)Pickupable;
	++ApplyCount;
}

void UParadoxItemSlotTestPassiveEffect::Remove_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable)
{
	(void)Pickupable;
	++RemoveCount;
	if (bAttemptReentrantInsert && ReentrantSlot)
	{
		ReentrantStatus = ReentrantSlot->TryInsertItem(Character).Status;
	}
}

AParadoxItemSlotTestInsertable::AParadoxItemSlotTestInsertable()
{
	TestEffect = CreateDefaultSubobject<UParadoxItemSlotTestPassiveEffect>(TEXT("TestPassiveEffect"));
	PassiveEffects.Add(TestEffect);
}

bool AParadoxItemSlotTestActor::EvaluateRequiredSlotActive() const
{
	return Super::EvaluateRequiredSlotActive() && bTestRequiredActive;
}

bool AParadoxItemSlotTestActor::EvaluateAdditionalSlotActive_Implementation() const
{
	return bTestAdditionalActive;
}

bool AParadoxItemSlotTestActor::CanAcceptItemAdditional_Implementation(
	AParadoxInsertablePickupableActor* Item,
	AParadoxCharacter* Requester,
	FString& OutDiagnostic) const
{
	(void)Item;
	(void)Requester;
	if (!bTestAdditionalAcceptance)
	{
		OutDiagnostic = TEXT("Rejected by the test's dynamic compatibility hook.");
	}
	return bTestAdditionalAcceptance;
}

bool AParadoxPuzzleItemSlotTestActor::EvaluateAdditionalSlotActive_Implementation() const
{
	return bTestAdditionalActive;
}
