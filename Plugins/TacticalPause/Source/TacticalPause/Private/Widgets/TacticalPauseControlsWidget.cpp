#include "Widgets/TacticalPauseControlsWidget.h"

#include "CommonButtonBase.h"
#include "Engine/World.h"
#include "Settings/TacticalPauseSettings.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"
#include "TacticalPause.h"

namespace
{
	constexpr int32 SpeedButtonCount = 4;

	void SetCommonButtonPresentation(UCommonButtonBase* Button, bool bInteractionEnabled, bool bSelected, bool bVisible = true)
	{
		if (!Button)
		{
			return;
		}

		Button->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		Button->SetIsSelectable(true);
		if (bSelected)
		{
			Button->SetIsSelected(true, false);
		}
		else
		{
			Button->ClearSelection();
		}
		Button->SetIsInteractionEnabled(bInteractionEnabled);
	}
}

void UTacticalPauseControlsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindButtonEvents();
	BindToSubsystem();
	RefreshPresentation();
}

void UTacticalPauseControlsWidget::NativeDestruct()
{
	UnbindFromSubsystem();
	UnbindButtonEvents();
	Super::NativeDestruct();
}
UTacticalPauseWorldSubsystem* UTacticalPauseControlsWidget::GetTacticalPauseSubsystem() const
{
	if (BoundSubsystem)
	{
		return BoundSubsystem;
	}
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UTacticalPauseWorldSubsystem>() : nullptr;
}

ETacticalPauseRequestResult UTacticalPauseControlsWidget::RequestPauseFromWidget()
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem();
	const ETacticalPauseRequestResult Result = Subsystem ? Subsystem->RequestPause() : ETacticalPauseRequestResult::InvalidWorld;
	RefreshPresentation();
	return Result;
}

ETacticalPauseRequestResult UTacticalPauseControlsWidget::RequestPlayFromWidget()
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem();
	const ETacticalPauseRequestResult Result = Subsystem ? Subsystem->RequestPlay() : ETacticalPauseRequestResult::InvalidWorld;
	RefreshPresentation();
	return Result;
}

ETacticalPauseRequestResult UTacticalPauseControlsWidget::SelectPlaybackPresetFromWidget(FName PresetId)
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem();
	const ETacticalPauseRequestResult Result = Subsystem ? Subsystem->SetPlaybackPreset(PresetId) : ETacticalPauseRequestResult::InvalidWorld;
	RefreshPresentation();
	return Result;
}

void UTacticalPauseControlsWidget::RefreshPresentation()
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem();
	if (!Subsystem)
	{
		SetCommonButtonPresentation(PlayButton, false, false);
		SetCommonButtonPresentation(PauseButton, false, false);
		for (int32 SlotIndex = 0; SlotIndex < SpeedButtonCount; ++SlotIndex)
		{
			SetCommonButtonPresentation(GetSpeedButton(SlotIndex), false, false);
		}
		BP_OnPresentationUpdated();
		return;
	}

	const ETacticalPlaybackState State = Subsystem->GetPlaybackState();
	const bool bTransitioning = State == ETacticalPlaybackState::TransitioningToPause || State == ETacticalPlaybackState::TransitioningToPlay;
	const bool bPaused = State == ETacticalPlaybackState::Paused || State == ETacticalPlaybackState::TransitioningToPause;
	const bool bPlaying = State == ETacticalPlaybackState::Playing;
	SetCommonButtonPresentation(PlayButton, !bTransitioning && bPaused && Subsystem->CanPlay(), bPlaying);
	SetCommonButtonPresentation(PauseButton, !bTransitioning && !bPaused && Subsystem->CanPause(), State == ETacticalPlaybackState::Paused);

	const UTacticalPauseSettings* Settings = GetDefault<UTacticalPauseSettings>();
	const bool bCanSelectWhilePaused = !bPaused || !Settings || Settings->bAllowSpeedSelectionWhilePaused;
	const FName SelectedPresetId = Subsystem->GetSelectedPresetId();
	const TArray<FTacticalPlaybackSpeedPreset> Presets = Subsystem->GetAvailablePresets();
	for (int32 SlotIndex = 0; SlotIndex < SpeedButtonCount; ++SlotIndex)
	{
		UCommonButtonBase* Button = GetSpeedButton(SlotIndex);
		const bool bHasPreset = Presets.IsValidIndex(SlotIndex);
		const bool bSelected = bHasPreset && Presets[SlotIndex].Id == SelectedPresetId;
		const bool bCanSelect = !bTransitioning
			&& bCanSelectWhilePaused
			&& bHasPreset
			&& !bSelected
			&& Subsystem->CanSetPlaybackSpeed(Presets[SlotIndex].Multiplier);
		SetCommonButtonPresentation(Button, bCanSelect, bSelected, bHasPreset);
	}

	const bool bShouldShow = !Settings
		|| bTransitioning
		|| (bPaused ? Settings->bShowWidgetWhilePaused : Settings->bShowWidgetWhilePlaying);
	SetVisibility(bShouldShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	BP_OnPresentationUpdated();
}

void UTacticalPauseControlsWidget::BindButtonEvents()
{
	UnbindButtonEvents();
	if (!PlayButton || !PauseButton || !SpeedButton1 || !SpeedButton2 || !SpeedButton3 || !SpeedButton4)
	{
		TACTICALPAUSE_LOG_ERROR("Common UI widget %s is missing one or more required BindWidget controls: PlayButton, PauseButton, SpeedButton1-4.", *GetNameSafe(this));
	}

	if (PlayButton)
	{
		PlayButton->OnClicked().AddUObject(this, &UTacticalPauseControlsWidget::HandlePlayClicked);
	}
	if (PauseButton)
	{
		PauseButton->OnClicked().AddUObject(this, &UTacticalPauseControlsWidget::HandlePauseClicked);
	}
	if (SpeedButton1)
	{
		SpeedButton1->OnClicked().AddUObject(this, &UTacticalPauseControlsWidget::HandleSpeedButton1Clicked);
	}
	if (SpeedButton2)
	{
		SpeedButton2->OnClicked().AddUObject(this, &UTacticalPauseControlsWidget::HandleSpeedButton2Clicked);
	}
	if (SpeedButton3)
	{
		SpeedButton3->OnClicked().AddUObject(this, &UTacticalPauseControlsWidget::HandleSpeedButton3Clicked);
	}
	if (SpeedButton4)
	{
		SpeedButton4->OnClicked().AddUObject(this, &UTacticalPauseControlsWidget::HandleSpeedButton4Clicked);
	}
}

void UTacticalPauseControlsWidget::UnbindButtonEvents()
{
	if (PlayButton)
	{
		PlayButton->OnClicked().RemoveAll(this);
	}
	if (PauseButton)
	{
		PauseButton->OnClicked().RemoveAll(this);
	}
	for (int32 SlotIndex = 0; SlotIndex < SpeedButtonCount; ++SlotIndex)
	{
		if (UCommonButtonBase* Button = GetSpeedButton(SlotIndex))
		{
			Button->OnClicked().RemoveAll(this);
		}
	}
}

void UTacticalPauseControlsWidget::BindToSubsystem()
{
	UnbindFromSubsystem();
	BoundSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UTacticalPauseWorldSubsystem>() : nullptr;
	if (!BoundSubsystem)
	{
		TACTICALPAUSE_LOG_WARNING("Tactical Pause controls widget %s could not bind to a World subsystem.", *GetNameSafe(this));
		return;
	}
	BoundSubsystem->OnPlaybackStateChangedNative().AddUObject(this, &UTacticalPauseControlsWidget::HandleStateChanged);
	BoundSubsystem->OnSelectedPlaybackSpeedChangedNative().AddUObject(this, &UTacticalPauseControlsWidget::HandleSpeedChanged);
	BoundSubsystem->OnAppliedPlaybackSpeedChangedNative().AddUObject(this, &UTacticalPauseControlsWidget::HandleSpeedChanged);
	BoundSubsystem->OnRequestFailedNative().AddUObject(this, &UTacticalPauseControlsWidget::HandleRequestFailed);
}

void UTacticalPauseControlsWidget::UnbindFromSubsystem()
{
	if (BoundSubsystem)
	{
		BoundSubsystem->OnPlaybackStateChangedNative().RemoveAll(this);
		BoundSubsystem->OnSelectedPlaybackSpeedChangedNative().RemoveAll(this);
		BoundSubsystem->OnAppliedPlaybackSpeedChangedNative().RemoveAll(this);
		BoundSubsystem->OnRequestFailedNative().RemoveAll(this);
	}
	BoundSubsystem = nullptr;
}

UCommonButtonBase* UTacticalPauseControlsWidget::GetSpeedButton(int32 SlotIndex) const
{
	switch (SlotIndex)
	{
	case 0: return SpeedButton1;
	case 1: return SpeedButton2;
	case 2: return SpeedButton3;
	case 3: return SpeedButton4;
	default: return nullptr;
	}
}

ETacticalPauseRequestResult UTacticalPauseControlsWidget::SelectPlaybackPresetSlot(int32 SlotIndex)
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem();
	if (!Subsystem)
	{
		return ETacticalPauseRequestResult::InvalidWorld;
	}
	const TArray<FTacticalPlaybackSpeedPreset> Presets = Subsystem->GetAvailablePresets();
	return Presets.IsValidIndex(SlotIndex)
		? SelectPlaybackPresetFromWidget(Presets[SlotIndex].Id)
		: ETacticalPauseRequestResult::UnknownPreset;
}

void UTacticalPauseControlsWidget::HandleStateChanged(const FTacticalPauseStateChange& Change)
{
	RefreshPresentation();
}

void UTacticalPauseControlsWidget::HandleSpeedChanged(const FTacticalPauseSpeedChange& Change)
{
	RefreshPresentation();
}

void UTacticalPauseControlsWidget::HandleRequestFailed(const FTacticalPauseRequestFailure& Failure)
{
	RefreshPresentation();
}

void UTacticalPauseControlsWidget::HandlePlayClicked()
{
	RequestPlayFromWidget();
}

void UTacticalPauseControlsWidget::HandlePauseClicked()
{
	RequestPauseFromWidget();
}

void UTacticalPauseControlsWidget::HandleSpeedButton1Clicked()
{
	SelectPlaybackPresetSlot(0);
}

void UTacticalPauseControlsWidget::HandleSpeedButton2Clicked()
{
	SelectPlaybackPresetSlot(1);
}

void UTacticalPauseControlsWidget::HandleSpeedButton3Clicked()
{
	SelectPlaybackPresetSlot(2);
}

void UTacticalPauseControlsWidget::HandleSpeedButton4Clicked()
{
	SelectPlaybackPresetSlot(3);
}
