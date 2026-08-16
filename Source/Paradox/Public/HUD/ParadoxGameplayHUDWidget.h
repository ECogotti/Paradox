#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "HUD/ParadoxGameplayHUDTypes.h"
#include "ParadoxGameplayHUDWidget.generated.h"

class AParadoxPlayerController;
class UPanelWidget;
class UParadoxGameplayHUDComponent;
class UParadoxInventoryWidget;
class UWidgetSwitcher;
class UTacticalPauseControlsWidget;

/**
 * Native, Blueprint-replaceable presentation boundary for the persistent Gameplay HUD.
 * Page zero of HUDModeSwitcher is Normal; page one is designer-owned Collapsed content.
 */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API UParadoxGameplayHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	UParadoxGameplayHUDComponent* GetHUDComponent() const { return HUDComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	AParadoxPlayerController* GetParadoxPlayerController() const { return ParadoxPlayerController.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	EParadoxGameplayHUDMode GetHUDMode() const { return CurrentMode; }

	/** Stable indices used by both the native fallback and designer-authored Widget Blueprints. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	static int32 GetNormalModePageIndex() { return 0; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	static int32 GetCollapsedModePageIndex() { return 1; }

	/** Read-only access to the designer-owned collapsed page container. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	UPanelWidget* GetCollapsedModeContainer() const { return CollapsedModeContainer.Get(); }

	/** Internal coordinator boundary. Blueprint should request changes through the HUD component. */
	void AssignHUDContext(
		UParadoxGameplayHUDComponent* InHUDComponent,
		AParadoxPlayerController* InPlayerController);
	void ClearHUDContext();
	bool ApplyHUDMode(EParadoxGameplayHUDMode NewMode);
	void SetSectionVisibility(EParadoxGameplayHUDSection Section, ESlateVisibility NewVisibility);

protected:
	virtual void NativeOnInitialized() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Presentation hook invoked after the native switcher has already changed page. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Gameplay HUD", meta = (BlueprintProtected = "true"))
	void OnModeChanged(EParadoxGameplayHUDMode PreviousMode, EParadoxGameplayHUDMode NewMode);
	virtual void OnModeChanged_Implementation(
		EParadoxGameplayHUDMode PreviousMode,
		EParadoxGameplayHUDMode NewMode);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Gameplay HUD", meta = (BlueprintProtected = "true", DisplayName = "On HUD Context Assigned"))
	void ReceiveHUDContextAssigned();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Gameplay HUD", meta = (BlueprintProtected = "true", DisplayName = "On HUD Context Cleared"))
	void ReceiveHUDContextCleared();

	/** Required for authored layouts; the native fallback creates exactly Normal and Collapsed pages. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Paradox|Gameplay HUD")
	TObjectPtr<UWidgetSwitcher> HUDModeSwitcher = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Paradox|Gameplay HUD")
	TObjectPtr<UPanelWidget> TacticalPauseContainer = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Paradox|Gameplay HUD")
	TObjectPtr<UPanelWidget> EquipmentContainer = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Paradox|Gameplay HUD")
	TObjectPtr<UPanelWidget> StatusContainer = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional), Category = "Paradox|Gameplay HUD")
	TObjectPtr<UPanelWidget> CollapsedModeContainer = nullptr;

private:
	void EnsureNativeFallbackTree();
	void BuildNativeFallbackTree();
	UTacticalPauseControlsWidget* FindEmbeddedTacticalPauseWidget() const;
	UParadoxInventoryWidget* FindEmbeddedEquipmentWidget() const;
	UPanelWidget* GetSectionContainer(EParadoxGameplayHUDSection Section) const;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxGameplayHUDComponent> HUDComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxPlayerController> ParadoxPlayerController;

	EParadoxGameplayHUDMode CurrentMode = EParadoxGameplayHUDMode::Normal;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxGameplayHUDTestAccessor;
#endif
	friend class UParadoxGameplayHUDComponent;
};
