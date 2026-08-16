#pragma once

#include "CoreMinimal.h"
#include "Inventory/ParadoxItemSlotActor.h"
#include "ParadoxPuzzleItemSlotActor.generated.h"

class UPuzzleEmitterComponent;
class UPuzzleReceiverComponent;

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

	/** Explicit refresh path for item conditions used by an overridden puzzle evaluation. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot|Puzzle")
	void NotifyPuzzleRelevantItemStateChanged();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot|Puzzle")
	void RefreshPuzzleOutput();

protected:
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

	/** When enabled, effective Receiver state is a mandatory native Slot-activity gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Puzzle")
	bool bRequirePuzzleReceiverForActivation = false;

private:
	void HandleReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bReceiverActive);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPuzzleEmitterComponent> PuzzleEmitter;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPuzzleReceiverComponent> PuzzleReceiver;
};
