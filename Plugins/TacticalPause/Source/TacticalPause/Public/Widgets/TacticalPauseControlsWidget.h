#pragma once

#include "CommonActivatableWidget.h"
#include "Types/TacticalPauseTypes.h"
#include "TacticalPauseControlsWidget.generated.h"

class UCommonButtonBase;
class UTacticalPauseWorldSubsystem;

/**
 * Common UI control surface whose layout and style are supplied entirely by its Widget Blueprint.
 * The Blueprint must bind the six named Common UI buttons declared below; this class owns only
 * command routing, selection state, activation lifecycle, and subsystem observation.
 */
UCLASS(Blueprintable)
class TACTICALPAUSE_API UTacticalPauseControlsWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Resolves the authoritative world subsystem used by every widget command. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|Widget")
	UTacticalPauseWorldSubsystem* GetTacticalPauseSubsystem() const;

	/** Forwards Pause to the subsystem and refreshes presentation from authoritative state. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Widget")
	ETacticalPauseRequestResult RequestPauseFromWidget();

	/** Forwards Play to the subsystem and refreshes presentation from authoritative state. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Widget")
	ETacticalPauseRequestResult RequestPlayFromWidget();

	/** Forwards a preset selection without manipulating world time directly. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Widget")
	ETacticalPauseRequestResult SelectPlaybackPresetFromWidget(FName PresetId);

	/** Updates Common UI interaction/selection state and configured widget visibility. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Widget")
	void RefreshPresentation();

	/** Presentation-only hook fired after Common UI button state has refreshed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical Pause|Widget", meta = (DisplayName = "On Tactical Pause Presentation Updated"))
	void BP_OnPresentationUpdated();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

	/** Required Common UI button named PlayButton in the Widget Blueprint hierarchy. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Tactical Pause|Widget")
	TObjectPtr<UCommonButtonBase> PlayButton = nullptr;

	/** Required Common UI button named PauseButton in the Widget Blueprint hierarchy. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Tactical Pause|Widget")
	TObjectPtr<UCommonButtonBase> PauseButton = nullptr;

	/** Required Common UI button representing validated preset index 0. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Tactical Pause|Widget")
	TObjectPtr<UCommonButtonBase> SpeedButton1 = nullptr;

	/** Required Common UI button representing validated preset index 1. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Tactical Pause|Widget")
	TObjectPtr<UCommonButtonBase> SpeedButton2 = nullptr;

	/** Required Common UI button representing validated preset index 2. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Tactical Pause|Widget")
	TObjectPtr<UCommonButtonBase> SpeedButton3 = nullptr;

	/** Required Common UI button representing validated preset index 3. */
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidget), Category = "Tactical Pause|Widget")
	TObjectPtr<UCommonButtonBase> SpeedButton4 = nullptr;

private:
	/** Binds Common UI native click events once the Blueprint widget tree is constructed. */
	void BindButtonEvents();
	/** Removes every Common UI click binding before destruction. */
	void UnbindButtonEvents();
	/** Subscribes while this activatable widget is active; no Tick or polling is used. */
	void BindToSubsystem();
	/** Symmetrically removes every subsystem observer when deactivated or destroyed. */
	void UnbindFromSubsystem();
	/** Returns one of the four required preset buttons without allocating a temporary array. */
	UCommonButtonBase* GetSpeedButton(int32 SlotIndex) const;
	/** Routes a configured button slot to the corresponding validated preset. */
	ETacticalPauseRequestResult SelectPlaybackPresetSlot(int32 SlotIndex);

	void HandleStateChanged(const FTacticalPauseStateChange& Change);
	void HandleSpeedChanged(const FTacticalPauseSpeedChange& Change);
	void HandleRequestFailed(const FTacticalPauseRequestFailure& Failure);
	void HandlePlayClicked();
	void HandlePauseClicked();
	void HandleSpeedButton1Clicked();
	void HandleSpeedButton2Clicked();
	void HandleSpeedButton3Clicked();
	void HandleSpeedButton4Clicked();

	/** GC-tracked authority observed only while the Common UI widget is active. */
	UPROPERTY(Transient)
	TObjectPtr<UTacticalPauseWorldSubsystem> BoundSubsystem = nullptr;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FTacticalPauseTestAccessor;
#endif
};
