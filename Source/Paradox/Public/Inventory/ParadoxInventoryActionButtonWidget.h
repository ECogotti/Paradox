#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "ParadoxInventoryActionButtonWidget.generated.h"

class UCommonButtonBase;
class UCommonTextBlock;
class UImage;
class UParadoxPickupableAction;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxInventoryActionButtonRequested,
	UParadoxPickupableAction*, Action);

/** One reusable, designer-replaceable button for an equipped item's semantic Gameplay Action. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API UParadoxInventoryActionButtonWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Widget")
	void SetPickupableAction(UParadoxPickupableAction* InAction);

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Widget")
	UParadoxPickupableAction* GetPickupableAction() const { return PickupableAction.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Widget")
	void RefreshActionPresentation(bool bActionEnabled);

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Inventory|Widget")
	FParadoxInventoryActionButtonRequested OnActionRequested;

protected:
	virtual void NativeOnInitialized() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Inventory|Widget", meta = (DisplayName = "On Action Presentation Updated"))
	void ReceiveActionPresentationUpdated(
		UParadoxPickupableAction* Action,
		bool bActionEnabled);

	/** Required Common UI button used to request this action. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UCommonButtonBase> ActionButton = nullptr;

	/** Required icon for the action presentation. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UImage> ActionIcon = nullptr;

	/** Required Common UI text used for the action label. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Paradox|Inventory|Widget")
	TObjectPtr<UCommonTextBlock> ActionLabel = nullptr;

private:
	void EnsureNativeFallbackTree();
	void BuildNativeFallbackTree();

	UFUNCTION()
	void HandleActionButtonClicked();

	UPROPERTY(Transient)
	TObjectPtr<UParadoxPickupableAction> PickupableAction = nullptr;

	bool bPresentationEnabled = false;
};
