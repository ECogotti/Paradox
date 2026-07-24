#pragma once

#include "Engine/DeveloperSettings.h"
#include "Types/TacticalPauseTypes.h"
#include "TacticalPauseSettings.generated.h"

class UTacticalPauseControlsWidget;

/** Project-wide defaults used by each Tactical Pause world and local-player subsystem. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Tactical Pause"))
class TACTICALPAUSE_API UTacticalPauseSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTacticalPauseSettings();
	virtual FName GetCategoryName() const override;

	/** Playback choices validated and exposed by each world subsystem. */
	UPROPERTY(Config, EditAnywhere, Category = "Playback", meta = (TitleProperty = "DisplayName"))
	TArray<FTacticalPlaybackSpeedPreset> SpeedPresets;

	/** Preset selected when a world subsystem initializes. */
	UPROPERTY(Config, EditAnywhere, Category = "Playback")
	FName DefaultPresetId = TEXT("Normal");

	/** Plugin-level speed ceiling; Unreal's world limit is also enforced. */
	UPROPERTY(Config, EditAnywhere, Category = "Playback", meta = (ClampMin = "0.0001", UIMin = "1.0"))
	float MaximumAllowedMultiplier = 3.0f;

	/** Allows a paused player to select the multiplier used by the next Play request. */
	UPROPERTY(Config, EditAnywhere, Category = "Playback")
	bool bAllowSpeedSelectionWhilePaused = true;

	/** Opts into local-player-owned automatic controls. Disabled by default in favor of explicit project UI ownership. */
	UPROPERTY(Config, EditAnywhere, Category = "Widget")
	bool bCreateDefaultWidgetAutomatically = false;

	/** Soft Common UI widget class loaded on demand; invalid classes fall back to the native logic shell. */
	UPROPERTY(Config, EditAnywhere, Category = "Widget", meta = (AllowedClasses = "/Script/TacticalPause.TacticalPauseControlsWidget"))
	TSoftClassPtr<UTacticalPauseControlsWidget> DefaultWidgetClass;

	/** Z order passed to AddToPlayerScreen for automatically created controls. */
	UPROPERTY(Config, EditAnywhere, Category = "Widget")
	int32 WidgetZOrder = 100;

	/** Selects which local players own an automatically created widget. */
	UPROPERTY(Config, EditAnywhere, Category = "Widget")
	ETacticalPauseWidgetPlayerPolicy WidgetPlayerPolicy = ETacticalPauseWidgetPlayerPolicy::PrimaryLocalPlayerOnly;

	/** Keeps automatic controls visible while simulation is playing. */
	UPROPERTY(Config, EditAnywhere, Category = "Widget")
	bool bShowWidgetWhilePlaying = true;

	/** Keeps automatic controls visible while simulation is paused. */
	UPROPERTY(Config, EditAnywhere, Category = "Widget")
	bool bShowWidgetWhilePaused = true;
};
