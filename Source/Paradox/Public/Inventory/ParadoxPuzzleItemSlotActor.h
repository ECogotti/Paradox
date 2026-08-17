#pragma once

#include "CoreMinimal.h"
#include "Inventory/ParadoxItemSlotActor.h"
#include "ParadoxPuzzleItemSlotActor.generated.h"

class UPuzzleEmitterComponent;
class UPuzzleReceiverComponent;

/** Puzzle capabilities driven by the item inserted into a Puzzle Item Slot. */
UENUM(BlueprintType)
enum class EParadoxPuzzleItemSlotRole : uint8
{
	Emitter UMETA(DisplayName = "Emitter"),
	Receiver UMETA(DisplayName = "Receiver"),
	EmitterAndReceiver UMETA(DisplayName = "Emitter and Receiver")
};

/** Item Slot that receives optional Puzzle power and publishes its evaluated occupied result. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxPuzzleItemSlotActor : public AParadoxItemSlotActor
{
	GENERATED_BODY()

public:
	AParadoxPuzzleItemSlotActor();

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Puzzle")
	UPuzzleEmitterComponent* GetPuzzleEmitterComponent() const { return PuzzleEmitter.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Puzzle")
	UPuzzleReceiverComponent* GetPuzzleReceiverComponent() const { return PuzzleReceiver.Get(); }

	/** True when this Slot's configured role publishes an Emitter signal. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Puzzle")
	bool UsesPuzzleEmitter() const;

	/** True when this Slot's configured role gates Receiver activation through the inserted item. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Puzzle")
	bool UsesPuzzleReceiver() const;

	/** True when an item is inserted and matches any exact Right Item Tag; an empty list accepts any inserted item. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Puzzle")
	bool IsRightItemInserted() const;

	/** Item-owned permission used as the Receiver's explicit Manual activation requirement. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Puzzle")
	bool IsReceiverActivationPermittedByInsertedItem() const;

	/** Explicit refresh path for item conditions used by an overridden puzzle evaluation. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot|Puzzle")
	void NotifyPuzzleRelevantItemStateChanged();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot|Puzzle")
	void RefreshPuzzleOutput();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool EvaluateRequiredSlotActive() const override;
	virtual void HandleAuthoritativeSlotStateChanged() override;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Item Slot|Puzzle", meta = (BlueprintProtected = "true"))
	bool EvaluatePuzzleOutput() const;
	virtual bool EvaluatePuzzleOutput_Implementation() const;

	/** Exact persistent Puzzle channel published by this Slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Puzzle")
	FGameplayTag OutputSignalTag;

	/** Selects which Puzzle capability is driven by the inserted-item correctness condition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Puzzle")
	EParadoxPuzzleItemSlotRole PuzzleRole = EParadoxPuzzleItemSlotRole::Emitter;

	/**
	 * Exact item-trait alternatives that satisfy this Slot after insertion.
	 * Empty preserves the legacy behavior where every allowed inserted item is considered correct.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Puzzle")
	FGameplayTagContainer RightItemTags;

	/** When enabled, effective Receiver state is a mandatory native Slot-activity gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Puzzle")
	bool bRequirePuzzleReceiverForActivation = false;

	/** Applies the role's authoritative Receiver activation policy after authored settings change. */
	void ConfigurePuzzleRole();

	/** Reconciles the inserted-item permission with the Receiver's Manual activation latch. */
	void RefreshReceiverActivationPermission();

private:
	void QueueReceiverActivationPermissionRefresh();
	void HandleQueuedReceiverActivationPermissionRefresh();
	void HandleReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bReceiverActive);
	void HandleReceiverActivationPrerequisitesChanged(
		UPuzzleReceiverComponent* Receiver,
		bool bPrerequisitesSatisfied);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPuzzleEmitterComponent> PuzzleEmitter;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPuzzleReceiverComponent> PuzzleReceiver;

	FTimerHandle ReceiverPermissionRefreshTimer;
	bool bReceiverPermissionRefreshQueued = false;
};
