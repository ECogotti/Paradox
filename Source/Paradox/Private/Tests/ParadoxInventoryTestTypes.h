#pragma once

#include "Inventory/ParadoxInventoryWidget.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Inventory/ParadoxPickupablePassiveEffect.h"
#include "ParadoxInventoryTestTypes.generated.h"

class UParadoxPickupableAction;

UCLASS()
class UParadoxInventoryTestPassiveEffect : public UParadoxPickupablePassiveEffect
{
	GENERATED_BODY()

public:
	virtual void Apply_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable) override;
	virtual void Remove_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable) override;

	UPROPERTY()
	int32 ApplyCount = 0;

	UPROPERTY()
	int32 RemoveCount = 0;

	UPROPERTY()
	bool bAttemptReentrantDrop = false;

	UPROPERTY()
	EParadoxInventoryOperationStatus ReentrantStatus =
		EParadoxInventoryOperationStatus::Succeeded;
};

UCLASS()
class AParadoxInventoryTestPickupable : public AParadoxPickupableActor
{
	GENERATED_BODY()

public:
	AParadoxInventoryTestPickupable();

	UParadoxInventoryTestPassiveEffect* GetTestEffect() const
	{
		return TestEffect;
	}

	void ClearTestConfiguration();
	UParadoxPickupableAction* AddEmptyTestAction();

private:
	UPROPERTY()
	TObjectPtr<UParadoxInventoryTestPassiveEffect> TestEffect;
};

UCLASS()
class UParadoxInventoryTestWidget : public UParadoxInventoryWidget
{
	GENERATED_BODY()
};
