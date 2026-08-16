#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "ParadoxInsertablePickupableActor.generated.h"

class AParadoxItemSlotActor;
class UParadoxInventoryComponent;
class USceneComponent;

/** Standard pickupable extended only with data-driven Item Slot ownership. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxInsertablePickupableActor : public AParadoxPickupableActor
{
	GENERATED_BODY()

public:
	AParadoxInsertablePickupableActor();

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot")
	const FGameplayTagContainer& GetInsertableTraits() const { return InsertableTraits; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot")
	bool IsInserted() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot")
	AParadoxItemSlotActor* GetCurrentItemSlot() const { return CurrentItemSlot.Get(); }

	/** Explicit event-driven refresh for dynamic conditions used by a derived Puzzle Slot. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot")
	void NotifyOwningSlotRelevantStateChanged();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PrepareExternalOwnershipForWorldStateRestore() override;
	virtual bool RestoreExternalOwnershipAfterWorldState() override;

	/** Static semantic traits evaluated by the accepting Slot's Gameplay Tag Query. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Compatibility")
	FGameplayTagContainer InsertableTraits;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Item Slot|Presentation", meta = (DisplayName = "On Inserted Into Slot"))
	void ReceiveInsertedIntoSlot(AParadoxItemSlotActor* NewSlot);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Item Slot|Presentation", meta = (DisplayName = "On Removed From Slot"))
	void ReceiveRemovedFromSlot(AParadoxItemSlotActor* PreviousSlot);

private:
	void SetInsertedStateNative(AParadoxItemSlotActor& NewSlot, USceneComponent& InsertAnchor);
	void ClearInsertedStateNative(bool bDetach);

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxItemSlotActor> CurrentItemSlot;

	friend class AParadoxItemSlotActor;
	friend class UParadoxInventoryComponent;
};
