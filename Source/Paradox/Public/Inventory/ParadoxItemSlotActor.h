#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Inventory/ParadoxItemSlotTypes.h"
#include "Types/WorldStateTypes.h"
#include "ParadoxItemSlotActor.generated.h"

class AParadoxCharacter;
class AParadoxInsertablePickupableActor;
class AParadoxItemSlotActor;
class UArrowComponent;
class UParadoxInteractionComponent;
class UParadoxInventoryComponent;
class UParadoxSelectableComponent;
class UPerceptionKnowledgeSourceComponent;
class USceneComponent;
class USmartObjectComponent;
class UWorldStateParticipantComponent;
struct FWorldStateRestoreLifecycleContext;
struct FWorldStateRestoreResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxInsertedItemChanged,
	AParadoxItemSlotActor*, Slot,
	AParadoxInsertablePickupableActor*, PreviousItem,
	AParadoxInsertablePickupableActor*, NewItem);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxItemSlotActiveStateChanged,
	AParadoxItemSlotActor*, Slot,
	bool, bPreviousActive,
	bool, bNewActive);

/** One selectable world slot that atomically exchanges one insertable with a Character inventory. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxItemSlotActor : public AActor
{
	GENERATED_BODY()

public:
	AParadoxItemSlotActor();

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot")
	bool IsOccupied() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot")
	AParadoxInsertablePickupableActor* GetInsertedItem() const { return InsertedItem.Get(); }

	/** Safe authoritative activity query; native requirements cannot be bypassed by Blueprint. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot")
	bool IsSlotActive() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot")
	bool CanAcceptItem(AParadoxInsertablePickupableActor* Item, AParadoxCharacter* Requester) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot")
	FParadoxItemSlotOperationResult EvaluateAcceptItem(
		AParadoxInsertablePickupableActor* Item,
		AParadoxCharacter* Requester) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot")
	FParadoxItemSlotOperationResult EvaluatePickupInsertedItem(AParadoxCharacter* Requester) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot")
	FParadoxItemSlotOperationResult TryInsertItem(AParadoxCharacter* Requester);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot")
	FParadoxItemSlotOperationResult TryPickupInsertedItem(AParadoxCharacter* Requester);

	/** Re-evaluates cached presentation/perception state after a dynamic native or Blueprint condition changes. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot")
	void NotifySlotActiveStateMayHaveChanged();

	/** Event-driven path used when an inserted item's dynamic puzzle condition changes. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Item Slot")
	void NotifyInsertedItemRelevantStateChanged(AParadoxInsertablePickupableActor* ChangedItem);

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Components")
	UArrowComponent* GetInsertAnchor() const { return InsertAnchor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Components")
	UParadoxSelectableComponent* GetSelectableComponent() const { return SelectableComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Components")
	UParadoxInteractionComponent* GetInteractionComponent() const { return InteractionComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Components")
	USmartObjectComponent* GetSmartObjectComponent() const { return SmartObjectComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Components")
	UWorldStateParticipantComponent* GetWorldStateParticipantComponent() const { return WorldStateParticipant.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Item Slot|Components")
	UPerceptionKnowledgeSourceComponent* GetPerceptionSourceComponent() const { return PerceptionSource.Get(); }

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Item Slot|Events")
	FParadoxInsertedItemChanged OnInsertedItemChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Item Slot|Events")
	FParadoxItemSlotActiveStateChanged OnSlotActiveStateChanged;

	/** Local half of the Paradox.Inventory.Debug gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Debug")
	bool bEnableDebug = false;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Non-Blueprint native gate; Puzzle Slots add receiver activation here. */
	virtual bool EvaluateRequiredSlotActive() const;

	/** Additional local condition; it is always ANDed with native requirements. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Item Slot", meta = (BlueprintProtected = "true"))
	bool EvaluateAdditionalSlotActive() const;
	virtual bool EvaluateAdditionalSlotActive_Implementation() const;

	/** Additional requester/item validation after all shared invariants and the tag query pass. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Item Slot", meta = (BlueprintProtected = "true"))
	bool CanAcceptItemAdditional(
		AParadoxInsertablePickupableActor* Item,
		AParadoxCharacter* Requester,
		FString& OutDiagnostic) const;
	virtual bool CanAcceptItemAdditional_Implementation(
		AParadoxInsertablePickupableActor* Item,
		AParadoxCharacter* Requester,
		FString& OutDiagnostic) const;

	/** Internal lifecycle/scripted release; lock policy intentionally does not apply. */
	FParadoxItemSlotOperationResult ReleaseInsertedItemToWorld(const FTransform& WorldTransform);

	/** Native extension called after final occupancy/activity state is already coherent. */
	virtual void HandleAuthoritativeSlotStateChanged();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Item Slot|Presentation", meta = (DisplayName = "On Inserted Item Changed"))
	void ReceiveInsertedItemChanged(
		AParadoxInsertablePickupableActor* PreviousItem,
		AParadoxInsertablePickupableActor* NewItem);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Item Slot|Presentation", meta = (DisplayName = "On Slot Active State Changed"))
	void ReceiveSlotActiveStateChanged(bool bPreviousActive, bool bNewActive);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Compatibility")
	FGameplayTagQuery AcceptedItemQuery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot|Policy")
	bool bLockInsertedItem = false;

private:
	FParadoxItemSlotOperationResult MakeResult(
		EParadoxItemSlotOperationStatus Status,
		FString Diagnostic) const;
	void InitializeAuthoredInsertedItem();
	void SetInsertedItemCommitted(AParadoxInsertablePickupableActor* NewItem);
	void ClearInsertedItemCommitted(AParadoxInsertablePickupableActor* ExpectedItem);
	void FinalizeOccupancyTransition(
		AParadoxInsertablePickupableActor* PreviousItem,
		AParadoxInsertablePickupableActor* NewItem);
	void BindInsertedItem(AParadoxInsertablePickupableActor& Item);
	void UnbindInsertedItem(AParadoxInsertablePickupableActor* Item);
	void PrepareForWorldStateRestore();
	void RestoreCapturedRelationship();
	void FinishWorldStateRestore(bool bSucceeded);
	void RefreshPerceptionState();
	void RefreshInteractionAffordances();
	void LogDebugState(const TCHAR* EventName, const FString& Diagnostic = FString()) const;

	void HandleWorldStateRestoreStarted(const FWorldStateRestoreLifecycleContext& Context);
	void HandleWorldStateRestoreFinished(const FWorldStateRestoreResult& Result);

	UFUNCTION()
	void HandleWorldStatePreCapture(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStatePreRestore(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStatePropertiesRestored(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStateParticipantRestored(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStateParticipantFailed(const FWorldStateParticipantResult& Result);
	UFUNCTION()
	void HandleInsertedItemDestroyed(AActor* DestroyedActor);

	void HandleInsertedItemInvalidated(AParadoxInsertablePickupableActor* Item);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UArrowComponent> InsertAnchor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxSelectableComponent> SelectableComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USmartObjectComponent> SmartObjectComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxInteractionComponent> InteractionComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipant;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPerceptionKnowledgeSourceComponent> PerceptionSource;

	/** Editable only on placed instances to support an authored occupied baseline. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Paradox|Item Slot|State", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AParadoxInsertablePickupableActor> InsertedItem;

	/** WorldState-supported mirror of the authoritative hard runtime relationship. */
	UPROPERTY(Transient)
	TSoftObjectPtr<AParadoxInsertablePickupableActor> WorldStateInsertedItem;

	bool bCachedSlotActive = true;
	bool bOperationInProgress = false;
	bool bResetInProgress = false;
	bool bInitialized = false;

	friend class AParadoxInsertablePickupableActor;
	friend class UParadoxInventoryComponent;
};
