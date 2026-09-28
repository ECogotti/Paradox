#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "ParadoxInsertablePickupableActor.generated.h"

class AParadoxItemSlotActor;
class AParadoxDumbwaiter;
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

	/** Returns the paired-transfer cargo owner, mutually exclusive with CurrentItemSlot. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter")
	AParadoxDumbwaiter* GetCurrentDumbwaiter() const { return CurrentDumbwaiter.Get(); }

	/** Explicit event-driven refresh for dynamic conditions used by a derived Puzzle Slot. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot")
	void NotifyOwningSlotRelevantStateChanged();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PrepareExternalOwnershipForWorldStateRestore() override;
	virtual bool RestoreExternalOwnershipAfterWorldState() override;
	virtual bool ShouldUseAuthoredCollisionForCurrentState() const override;
	virtual bool ShouldUseAuthoredNavigationForCurrentState() const override;
	virtual bool ShouldPreserveAuthoredCollisionConfiguration() const override;
	virtual bool ShouldPreserveAuthoredNavigationConfiguration() const override;

	/** Static semantic traits evaluated by the accepting Slot's Gameplay Tag Query. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Compatibility")
	FGameplayTagContainer InsertableTraits;

	/**
	 * Restores the pickupable's authored primitive collision while it is inserted.
	 * Disabled by default; Held and restore-pending states always remain collisionless.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Item Slot|Inserted Presence",
		meta = (DisplayName = "Enable Authored Collision While Inserted"))
	bool bUseAuthoredInsertedCollision = false;

	/**
	 * Enables navigation relevance, occupancy publication, and GridWorld cell blocking while inserted.
	 * The modifier continues to mirror the inherited OccupancyComponent bounds.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Item Slot|Inserted Presence",
		meta = (DisplayName = "Enable Navigation Blocking While Inserted"))
	bool bUseAuthoredInsertedNavigationInfluence = false;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Item Slot|Presentation", meta = (DisplayName = "On Inserted Into Slot"))
	void ReceiveInsertedIntoSlot(AParadoxItemSlotActor* NewSlot);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Item Slot|Presentation", meta = (DisplayName = "On Removed From Slot"))
	void ReceiveRemovedFromSlot(AParadoxItemSlotActor* PreviousSlot);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Dumbwaiter|Presentation", meta = (DisplayName = "On Stored In Dumbwaiter"))
	void ReceiveStoredInDumbwaiter(AParadoxDumbwaiter* NewDumbwaiter);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Dumbwaiter|Presentation", meta = (DisplayName = "On Removed From Dumbwaiter"))
	void ReceiveRemovedFromDumbwaiter(AParadoxDumbwaiter* PreviousDumbwaiter);

private:
	void SetInsertedStateNative(AParadoxItemSlotActor& NewSlot, USceneComponent& InsertAnchor);
	void SetDumbwaiterStateNative(AParadoxDumbwaiter& NewDumbwaiter, USceneComponent& CargoAnchor);
	void SetDumbwaiterTransferPresenceSuspendedNative(bool bSuspended);
	void ClearInsertedStateNative(bool bDetach);
	void ClearDumbwaiterStateNative(bool bDetach);

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxItemSlotActor> CurrentItemSlot;

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxDumbwaiter> CurrentDumbwaiter;

	/** Suppresses collision and navigation influence while a Dumbwaiter cart transition is active. */
	UPROPERTY(Transient)
	bool bDumbwaiterTransferPresenceSuspended = false;

	friend class AParadoxItemSlotActor;
	friend class AParadoxDumbwaiter;
	friend class UParadoxInventoryComponent;
};
