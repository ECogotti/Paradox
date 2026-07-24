#include "Settings/TacticalPauseSettings.h"

#include "Widgets/TacticalPauseControlsWidget.h"

namespace
{
	FTacticalPlaybackSpeedPreset MakePreset(const TCHAR* Id, const TCHAR* Label, float Multiplier, int32 SortOrder)
	{
		FTacticalPlaybackSpeedPreset Preset;
		Preset.Id = FName(Id);
		Preset.DisplayName = FText::FromString(Label);
		Preset.Multiplier = Multiplier;
		Preset.SortOrder = SortOrder;
		return Preset;
	}
}

UTacticalPauseSettings::UTacticalPauseSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Tactical Pause");

	SpeedPresets = {
		MakePreset(TEXT("Normal"), TEXT("x1"), 1.0f, 0),
		MakePreset(TEXT("Fast"), TEXT("x1.5"), 1.5f, 10),
		MakePreset(TEXT("Faster"), TEXT("x2"), 2.0f, 20),
		MakePreset(TEXT("Fastest"), TEXT("x3"), 3.0f, 30)
	};
	DefaultWidgetClass = TSoftClassPtr<UTacticalPauseControlsWidget>(FSoftObjectPath(
		TEXT("/TacticalPause/UI/WBP_TacticalPauseControls_Default.WBP_TacticalPauseControls_Default_C")));
}

FName UTacticalPauseSettings::GetCategoryName() const
{
	return TEXT("Game");
}
