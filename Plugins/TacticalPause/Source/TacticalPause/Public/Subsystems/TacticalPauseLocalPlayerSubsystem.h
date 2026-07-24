#pragma once

#include "Subsystems/LocalPlayerSubsystem.h"
#include "TacticalPauseLocalPlayerSubsystem.generated.h"

class APlayerController;
class UTacticalPauseControlsWidget;

/** Local-player owner for opt-in automatic Tactical Pause widget creation and teardown. */
UCLASS()
class TACTICALPAUSE_API UTacticalPauseLocalPlayerSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** Returns the opt-in automatic widget, or null when automatic UI is disabled/unavailable. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|Widget")
	UTacticalPauseControlsWidget* GetControlsWidget() const { return ControlsWidget; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

private:
	/** Creates controls only for an eligible local controller in a supported gameplay world. */
	void CreateControlsWidget(APlayerController* PlayerController);
	/** Removes the owned widget before controller replacement or subsystem teardown. */
	void RemoveControlsWidget();
	/** Applies the configured primary-player/every-player creation policy. */
	bool IsEligibleLocalPlayer() const;

	/** GC-tracked widget whose lifetime is strictly local-player-owned. */
	UPROPERTY(Transient)
	TObjectPtr<UTacticalPauseControlsWidget> ControlsWidget = nullptr;
};
