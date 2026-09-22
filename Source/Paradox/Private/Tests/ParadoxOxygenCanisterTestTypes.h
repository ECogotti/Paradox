#pragma once

#include "Inventory/ParadoxOxygenCanister.h"
#include "Inventory/ParadoxPickupablePassiveEffect.h"
#include "ParadoxOxygenCanisterTestTypes.generated.h"

class UParadoxInventoryComponent;

UCLASS()
class UParadoxOxygenCanisterTestPassiveEffect : public UParadoxPickupablePassiveEffect
{
	GENERATED_BODY()

public:
	virtual void Apply_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable) override;
	virtual void Remove_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable) override;

	int32 ApplyCount = 0;
	int32 RemoveCount = 0;
};

UCLASS()
class AParadoxOxygenCanisterTestActor : public AParadoxOxygenCanister
{
	GENERATED_BODY()

public:
	AParadoxOxygenCanisterTestActor();

	void SetRestoreSecondsForTest(float NewValue)
	{
		OxygenRestoreSeconds = NewValue;
	}

	UParadoxOxygenCanisterTestPassiveEffect* GetTestPassiveEffect() const
	{
		return TestPassiveEffect;
	}

	int32 CommittedCount = 0;
	int32 FailedCount = 0;

protected:
	virtual void HandleUseCommitted(
		AParadoxCharacter* Character,
		const FParadoxPickupableUseResult& Result) override;

	virtual void HandleUseFailed(
		AParadoxCharacter* Character,
		const FParadoxPickupableUseResult& Result) override;

private:
	UPROPERTY()
	TObjectPtr<UParadoxOxygenCanisterTestPassiveEffect> TestPassiveEffect;
};

/** Attempts nested inventory work from Oxygen's synchronous change notification. */
UCLASS()
class UParadoxOxygenCanisterReentrantObserver : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleOxygenChanged(
		float OldRemainingSeconds,
		float NewRemainingSeconds,
		float DurationSeconds,
		float NormalizedOxygen);

	UFUNCTION()
	void HandleEquippedItemChanged(
		AParadoxPickupableActor* PreviousItem,
		AParadoxPickupableActor* NewItem);

	UPROPERTY()
	TObjectPtr<UParadoxInventoryComponent> Inventory;

	UPROPERTY()
	TObjectPtr<AParadoxOxygenCanister> Canister;

	int32 OxygenChangedCount = 0;
	int32 InventoryTransitionCount = 0;
	EParadoxInventoryOperationStatus ReentrantDropStatus =
		EParadoxInventoryOperationStatus::Succeeded;
	FParadoxPickupableUseResult ReentrantUseResult;
};
