#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Inventory/ParadoxInventoryTypes.h"
#include "ParadoxInventoryComponent.generated.h"

class AParadoxCharacter;
class AParadoxInsertablePickupableActor;
class AParadoxItemSlotActor;
class AParadoxPickupableActor;
class UParadoxPickupablePassiveEffect;
struct FParadoxItemSlotOperationResult;
struct FWorldStateRestoreLifecycleContext;
struct FWorldStateRestoreResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxEquippedItemChanged,
	AParadoxPickupableActor*, PreviousItem,
	AParadoxPickupableActor*, NewItem);

/** Character-owned authority for exactly one equipped pickupable. */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxInventoryComponent();

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory")
	bool HasItem() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory")
	AParadoxPickupableActor* GetEquippedItem() const { return EquippedItem.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory")
	bool CanEquip(AParadoxPickupableActor* Item) const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory")
	bool CanUnequip() const;

	/** Validated native transition used by Pickup Interaction Actions. */
	FParadoxInventoryOperationResult TryEquip(AParadoxPickupableActor* Item);

	/** Validated atomic transition used by Swap Interaction Actions. */
	FParadoxInventoryOperationResult TrySwap(AParadoxPickupableActor* IncomingItem);

	/** Validated native transition used by Drop Actions after exact cell revalidation. */
	FParadoxInventoryOperationResult TryDropAtTransform(const FTransform& WorldTransform);

	/** Idempotent cleanup used by World State restore and owner teardown. */
	FParadoxInventoryOperationResult ClearInventoryForReset();

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Inventory")
	FParadoxEquippedItemChanged OnEquippedItemChanged;

	/** Local half of the Paradox.Inventory.Debug gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Debug")
	bool bEnableDebug = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FParadoxItemSlotOperationResult TransferEquippedItemToSlot(
		AParadoxItemSlotActor& Slot,
		AParadoxInsertablePickupableActor& Item);
	FParadoxItemSlotOperationResult TransferInsertedItemFromSlot(
		AParadoxItemSlotActor& Slot,
		AParadoxInsertablePickupableActor& Item);
	FParadoxInventoryOperationResult MakeResult(
		EParadoxInventoryOperationStatus Status,
		FString Diagnostic) const;
	AParadoxCharacter* GetParadoxCharacter() const;
	void ApplyPassiveEffects(AParadoxPickupableActor& Item);
	void RemoveAppliedPassiveEffects(AParadoxPickupableActor* Item);
	void BindEquippedItem(AParadoxPickupableActor& Item);
	void UnbindEquippedItem(AParadoxPickupableActor* Item);
	void BroadcastTransition(AParadoxPickupableActor* PreviousItem, AParadoxPickupableActor* NewItem);
	void LogDebugState(const TCHAR* EventName) const;
	void HandleWorldStateRestoreStarted(const FWorldStateRestoreLifecycleContext& Context);
	void HandleWorldStateRestoreFinished(const FWorldStateRestoreResult& Result);

	UFUNCTION()
	void HandleEquippedItemDestroyed(AActor* DestroyedActor);

	UPROPERTY(Transient)
	TObjectPtr<AParadoxPickupableActor> EquippedItem;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UParadoxPickupablePassiveEffect>> AppliedPassiveEffects;

	bool bOperationInProgress = false;
	bool bResetInProgress = false;

	friend class AParadoxItemSlotActor;
};
