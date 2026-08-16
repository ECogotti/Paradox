#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Inventory/ParadoxDropTargetingComponent.h"
#include "Types/GameplayActionTypes.h"
#include "ParadoxInventoryWidget.generated.h"

class AParadoxCharacter;
class AParadoxPickupableActor;
class UParadoxInventoryComponent;
class UParadoxInventoryActionButtonWidget;
class UParadoxPickupableAction;
class UCommonButtonBase;
class UCommonTextBlock;
class UGameplayActionComponent;
class UImage;
class UTexture2D;
class UVerticalBox;
class UWidgetSwitcher;

/** Event-driven one-slot equipment presentation with a complete native fallback layout. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API UParadoxInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UParadoxInventoryWidget(const FObjectInitializer& ObjectInitializer);

	/** Explicit inventory data/action source. The owning Player remains only the widget/input owner. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Widget")
	void SetInventoryCharacter(AParadoxCharacter* Character);

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Widget")
	AParadoxCharacter* GetInventoryCharacter() const { return InventoryCharacter.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Widget")
	UParadoxInventoryComponent* GetInventoryComponent() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Widget")
	AParadoxPickupableActor* GetEquippedPickupable() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Widget")
	bool HasEquippedItem() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Widget")
	TArray<UParadoxPickupableAction*> GetPickupableActions() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Widget")
	bool CanDrop() const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Widget")
	FParadoxDropTargetingResult RequestDrop();

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Widget")
	bool CanExecutePickupableAction(UParadoxPickupableAction* Action) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Widget")
	FGameplayActionSubmissionResult RequestPickupableAction(UParadoxPickupableAction* Action);

	/** Re-queries authoritative state; rebuilding buttons is reserved for catalog changes. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Widget")
	void RefreshInventoryPresentation(bool bRebuildActions = false);

	/** Optional texture shown by the native/Blueprint empty-slot presentation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Inventory|Widget|Presentation")
	TSoftObjectPtr<UTexture2D> EmptySlotTexture;

	/** Replaceable entry class used to build one button per equipped-item action. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Inventory|Widget|Presentation")
	TSubclassOf<UParadoxInventoryActionButtonWidget> ActionButtonWidgetClass;

protected:
	virtual void NativeOnInitialized() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Inventory|Widget", meta = (DisplayName = "On Inventory Item Changed"))
	void ReceiveInventoryItemChanged(
		AParadoxPickupableActor* PreviousItem,
		AParadoxPickupableActor* NewItem);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Inventory|Widget", meta = (DisplayName = "On Inventory Presentation Updated"))
	void ReceiveInventoryPresentationUpdated(
		AParadoxPickupableActor* EquippedItem,
		bool bCanDropItem);

	/** Required switcher whose page 0 is Empty and page 1 is Equipped. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UWidgetSwitcher> EquipmentStateSwitcher = nullptr;

	/** Required empty-slot image. It may be collapsed when no EmptySlotTexture is configured. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UImage> EmptySlotIcon = nullptr;

	/** Required image for the equipped pickupable. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UImage> EquippedItemIcon = nullptr;

	/** Optional Common UI text for the equipped pickupable display name. Icon-only layouts may omit it. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UCommonTextBlock> EquippedItemName = nullptr;

	/** Required Common UI button that starts the authoritative Drop flow. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UCommonButtonBase> DropButton = nullptr;

	/** Required container populated with the equipped pickupable's action entries. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UVerticalBox> SpecialActionsContainer = nullptr;

private:
	void EnsureNativeFallbackTree();
	void BuildNativeFallbackTree();
	void BindInventory();
	void UnbindInventory();
	void BindPresentationSources();
	void UnbindPresentationSources();
	void RebuildActionButtons();
	void RefreshActionButtonAvailability();

	UFUNCTION()
	void HandleEquippedItemChanged(
		AParadoxPickupableActor* PreviousItem,
		AParadoxPickupableActor* NewItem);

	UFUNCTION()
	void HandlePickupableActionsChanged(AParadoxPickupableActor* Pickupable);

	UFUNCTION()
	void HandleGameplayActionEvent(const FGameplayActionEvent& Event);

	UFUNCTION()
	void HandleDropTargetingChanged(bool bIsTargeting);

	UFUNCTION()
	void HandleDropClicked();

	UFUNCTION()
	void HandleSpecialActionRequested(UParadoxPickupableAction* Action);

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxCharacter> InventoryCharacter;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxInventoryComponent> BoundInventory;

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxPickupableActor> BoundPickupable;

	UPROPERTY(Transient)
	TWeakObjectPtr<UGameplayActionComponent> BoundGameplayActions;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxDropTargetingComponent> BoundDropTargeting;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UParadoxInventoryActionButtonWidget>> GeneratedActionButtons;
};
