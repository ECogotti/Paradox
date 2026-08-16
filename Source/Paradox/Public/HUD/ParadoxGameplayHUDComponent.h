#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "HUD/ParadoxGameplayHUDTypes.h"
#include "TimeLoop/ParadoxTimeLoopTypes.h"
#include "ParadoxGameplayHUDComponent.generated.h"

class AParadoxCharacter;
class AParadoxPlayerController;
class APawn;
class UParadoxGameplayHUDWidget;
class UParadoxInventoryWidget;
class UParadoxTimeLoopComponent;

/** Local Player Controller-owned coordinator for Gameplay HUD lifetime, policy and data binding. */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxGameplayHUDComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxGameplayHUDComponent();

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	UParadoxGameplayHUDWidget* GetGameplayHUDWidget() const { return GameplayHUDWidget.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	EParadoxGameplayHUDMode GetHUDMode() const { return CurrentMode; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	bool IsHUDCollapsed() const { return CurrentMode == EParadoxGameplayHUDMode::Collapsed; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	bool IsHUDVisible() const { return bHUDVisible; }

	/** Changes presentation mode without changing root visibility. Returns false when unchanged. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Gameplay HUD")
	bool SetHUDMode(EParadoxGameplayHUDMode NewMode);

	/** Toggles Normal/Collapsed without bypassing the root visibility policy. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Gameplay HUD")
	bool ToggleHUDMode();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Gameplay HUD")
	void SetVisibilityOverride(EParadoxGameplayHUDVisibilityOverride NewOverride);

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	EParadoxGameplayHUDVisibilityOverride GetVisibilityOverride() const { return VisibilityOverride; }

	UFUNCTION(BlueprintCallable, Category = "Paradox|Gameplay HUD")
	void SetSectionVisibility(EParadoxGameplayHUDSection Section, ESlateVisibility Visibility);

	UFUNCTION(BlueprintPure, Category = "Paradox|Gameplay HUD")
	ESlateVisibility GetSectionVisibility(EParadoxGameplayHUDSection Section) const;

	/** Re-evaluates the native phase/Pawn policy. This never recreates the widget. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Gameplay HUD")
	void RefreshHUDVisibility();

	/** Used by Player Controller input so hidden HUDs cannot change mode unexpectedly. */
	bool CanToggleHUDModeFromInput() const;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Gameplay HUD|Events")
	FParadoxGameplayHUDModeChanged OnHUDModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Gameplay HUD|Events")
	FParadoxGameplayHUDVisibilityChanged OnHUDVisibilityChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Gameplay HUD|Events")
	FParadoxGameplayHUDSectionVisibilityChanged OnHUDSectionVisibilityChanged;

	/** Root layout. Native class is a complete fallback; assign a Blueprint for authored presentation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Gameplay HUD|Classes")
	TSubclassOf<UParadoxGameplayHUDWidget> GameplayHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Gameplay HUD", meta = (ClampMin = "0"))
	int32 HUDZOrder = 50;

	/** Lets screen-space HUD widgets receive pointer input before unhandled clicks reach gameplay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Gameplay HUD|Input")
	bool bConfigureGameAndUIInputMode = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Gameplay HUD")
	EParadoxGameplayHUDVisibilityOverride VisibilityOverride = EParadoxGameplayHUDVisibilityOverride::Automatic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Gameplay HUD")
	EParadoxGameplayHUDMode InitialHUDMode = EParadoxGameplayHUDMode::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Gameplay HUD|Sections")
	ESlateVisibility TacticalPauseSectionVisibility = ESlateVisibility::Visible;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Gameplay HUD|Sections")
	ESlateVisibility EquipmentSectionVisibility = ESlateVisibility::Visible;

	/** Reserved presentation location for future gameplay status widgets such as Oxygen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Gameplay HUD|Sections")
	ESlateVisibility StatusSectionVisibility = ESlateVisibility::Collapsed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Native automatic policy; Blueprint may extend special project states without owning lifecycle. */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Gameplay HUD", meta = (BlueprintProtected = "true"))
	bool ShouldHUDBeVisible() const;
	virtual bool ShouldHUDBeVisible_Implementation() const;

private:
	void CreateGameplayHUD();
	void DestroyGameplayHUD();
	void ResolveEmbeddedSectionWidgets();
	void ApplySectionVisibilities();
	void ApplyScreenSpaceInputMode(AParadoxPlayerController& Controller) const;
	void BindTimeLoop();
	void UnbindTimeLoop();
	UParadoxTimeLoopComponent* ResolveTimeLoopComponent() const;

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandleTimeLoopPhaseChanged(
		EParadoxTimeLoopPhase PreviousPhase,
		EParadoxTimeLoopPhase NewPhase);

	UPROPERTY(Transient)
	TObjectPtr<UParadoxGameplayHUDWidget> GameplayHUDWidget = nullptr;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxInventoryWidget> EquipmentWidget;

	UPROPERTY(Transient)
	TObjectPtr<UParadoxTimeLoopComponent> BoundTimeLoop = nullptr;

	EParadoxGameplayHUDMode CurrentMode = EParadoxGameplayHUDMode::Normal;
	bool bHUDVisible = false;
	bool bEndingPlay = false;
};
