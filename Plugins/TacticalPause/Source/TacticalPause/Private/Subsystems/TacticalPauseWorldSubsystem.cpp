#include "Subsystems/TacticalPauseWorldSubsystem.h"

#include "Algo/StableSort.h"
#include "Engine/World.h"
#include "Settings/TacticalPauseSettings.h"
#include "Subsystems/TacticalPauseTemporalDriver.h"
#include "TacticalPause.h"

namespace
{
	constexpr float DilationTolerance = KINDA_SMALL_NUMBER;

	bool NearlyEqual(float Left, float Right)
	{
		return FMath::IsNearlyEqual(Left, Right, DilationTolerance);
	}

	FString RequestName(ETacticalPauseRequestType RequestType)
	{
		const UEnum* Enum = StaticEnum<ETacticalPauseRequestType>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(RequestType)) : TEXT("Unknown");
	}

	FString ResultName(ETacticalPauseRequestResult Result)
	{
		const UEnum* Enum = StaticEnum<ETacticalPauseRequestResult>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Result)) : TEXT("Unknown");
	}
}

UTacticalPauseWorldSubsystem::UTacticalPauseWorldSubsystem() = default;
UTacticalPauseWorldSubsystem::~UTacticalPauseWorldSubsystem()
{
	delete TemporalDriver;
	TemporalDriver = nullptr;
}

void UTacticalPauseWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bShuttingDown = false;
	bTemporalStateRestored = false;

	// The private driver is the sole location that invokes Unreal temporal mutation APIs.
	if (UWorld* World = GetWorld())
	{
		TemporalDriver = CreateTacticalPauseTemporalDriver(*World).Release();
	}
	BuildValidatedPresets();

	if (TemporalDriver && TemporalDriver->IsAvailable())
	{
		PlaybackState = TemporalDriver->IsPaused() ? ETacticalPlaybackState::Paused : ETacticalPlaybackState::Playing;
		AppliedPlaybackMultiplier = TemporalDriver->IsPaused() ? 0.0f : TemporalDriver->GetGlobalTimeDilation();
		TACTICALPAUSE_LOG_INFO("Initialized Tactical Pause for World %s. State=%s Selected=%.3f Applied=%.3f.",
			*GetNameSafe(GetWorld()),
			PlaybackState == ETacticalPlaybackState::Paused ? TEXT("Paused") : TEXT("Playing"),
			SelectedPlaybackMultiplier,
			AppliedPlaybackMultiplier);
	}
	else
	{
		TACTICALPAUSE_LOG_WARNING("Tactical Pause initialized without an available temporal driver for World %s.", *GetNameSafe(GetWorld()));
	}
}

void UTacticalPauseWorldSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	bShuttingDown = true;
	RestoreTemporalState();
	Super::OnWorldEndPlay(InWorld);
}

void UTacticalPauseWorldSubsystem::Deinitialize()
{
	bShuttingDown = true;
	RestoreTemporalState();
	delete TemporalDriver;
	TemporalDriver = nullptr;
	AvailablePresets.Reset();
	PresetLookup.Reset();
	Super::Deinitialize();
}

bool UTacticalPauseWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::RequestPause()
{
	return RequestPauseInternal(ETacticalPauseRequestType::Pause);
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::RequestPlay()
{
	return RequestPlayInternal(ETacticalPauseRequestType::Play);
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::TogglePause()
{
	const ETacticalPauseRequestResult Validation = ValidateMutableRequest(ETacticalPauseRequestType::TogglePause);
	if (Validation != ETacticalPauseRequestResult::Succeeded)
	{
		return Validation;
	}
	return IsPaused()
		? RequestPlayInternal(ETacticalPauseRequestType::TogglePause)
		: RequestPauseInternal(ETacticalPauseRequestType::TogglePause);
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::RequestPauseInternal(ETacticalPauseRequestType RequestType)
{
	const ETacticalPauseRequestResult Validation = ValidateMutableRequest(RequestType);
	if (Validation != ETacticalPauseRequestResult::Succeeded)
	{
		return Validation;
	}

	SynchronizeObservedTemporalState(true);
	if (TemporalDriver->IsPaused())
	{
		return ETacticalPauseRequestResult::AlreadyInRequestedState;
	}

	const float PreviousSelected = SelectedPlaybackMultiplier;
	const float PreviousApplied = GetAppliedPlaybackSpeed();
	SetPlaybackState(ETacticalPlaybackState::TransitioningToPause);
	bPauseReleaseAllowed = false;
	bPauseReleaseDelegateInvoked = false;

	// Unreal retains this delegate as our pause contribution and consults it on release.
	const FCanUnpause CanUnpauseDelegate = FCanUnpause::CreateUObject(this, &UTacticalPauseWorldSubsystem::CanReleaseOwnedPause);
	if (!TemporalDriver->AcquirePause(CanUnpauseDelegate) || !TemporalDriver->IsPaused())
	{
		SetPlaybackState(ETacticalPlaybackState::Playing);
		return FailRequest(RequestType, ETacticalPauseRequestResult::ApplyFailed);
	}

	bPauseOwnedByPlugin = true;
	AppliedPlaybackMultiplier = 0.0f;
	BroadcastSpeedChanges(PreviousSelected, PreviousApplied, NAME_None, false);
	const FTacticalPauseStateChange FinalChange{PlaybackState, ETacticalPlaybackState::Paused};
	SetPlaybackState(ETacticalPlaybackState::Paused);
	BroadcastPaused(FinalChange);
	TACTICALPAUSE_LOG_INFO("Pause applied for World %s. SelectedMultiplier=%.3f.", *GetNameSafe(GetWorld()), SelectedPlaybackMultiplier);
	return ETacticalPauseRequestResult::Succeeded;
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::RequestPlayInternal(ETacticalPauseRequestType RequestType)
{
	const ETacticalPauseRequestResult Validation = ValidateMutableRequest(RequestType);
	if (Validation != ETacticalPauseRequestResult::Succeeded)
	{
		return Validation;
	}

	const bool bDilationConflict = SynchronizeObservedTemporalState(true);
	if (!TemporalDriver->IsPaused())
	{
		return ETacticalPauseRequestResult::AlreadyInRequestedState;
	}
	if (!bPauseOwnedByPlugin)
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::ExternalStateConflict);
	}
	if (bDilationConflict)
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::ExternalStateConflict, SelectedPlaybackMultiplier);
	}
	if (!CanSetPlaybackSpeed(SelectedPlaybackMultiplier))
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::InvalidPlaybackSpeed, SelectedPlaybackMultiplier);
	}

	// Capture the complete dilation transaction before applying speed ahead of unpause.
	const float PreviousSelected = SelectedPlaybackMultiplier;
	const float PreviousApplied = 0.0f;
	const float PreviousWorldDilation = TemporalDriver->GetGlobalTimeDilation();
	const bool bPreviousDilationOwned = bDilationOwnedByPlugin;
	const bool bPreviousHasSnapshot = bHasDilationSnapshot;
	const float PreviousSnapshot = PreviousGlobalTimeDilation;
	const float PreviousLastApplied = LastPluginAppliedDilation;

	SetPlaybackState(ETacticalPlaybackState::TransitioningToPlay);
	if (!ApplyGlobalDilation(SelectedPlaybackMultiplier))
	{
		SetPlaybackState(ETacticalPlaybackState::Paused);
		return FailRequest(RequestType, ETacticalPauseRequestResult::ApplyFailed, SelectedPlaybackMultiplier);
	}

	bPauseReleaseAllowed = true;
	bPauseReleaseDelegateInvoked = false;
	const bool bReleaseReportedSuccess = TemporalDriver->ReleasePause();
	bPauseReleaseAllowed = false;
	if (bPauseReleaseDelegateInvoked)
	{
		bPauseOwnedByPlugin = false;
	}

	if (!bReleaseReportedSuccess || TemporalDriver->IsPaused())
	{
		// A stacked external owner may keep the world paused; restore our pre-request state.
		const float RollbackResult = TemporalDriver->SetGlobalTimeDilation(PreviousWorldDilation);
		if (!NearlyEqual(RollbackResult, PreviousWorldDilation))
		{
			TACTICALPAUSE_LOG_ERROR("Failed to roll back time dilation for World %s after Play could not resume. Requested=%.3f Applied=%.3f.",
				*GetNameSafe(GetWorld()), PreviousWorldDilation, RollbackResult);
		}
		bDilationOwnedByPlugin = bPreviousDilationOwned;
		bHasDilationSnapshot = bPreviousHasSnapshot;
		PreviousGlobalTimeDilation = PreviousSnapshot;
		LastPluginAppliedDilation = PreviousLastApplied;
		AppliedPlaybackMultiplier = 0.0f;
		SetPlaybackState(ETacticalPlaybackState::Paused);
		return FailRequest(RequestType,
			TemporalDriver->IsPaused() && !bPauseOwnedByPlugin
				? ETacticalPauseRequestResult::ExternalStateConflict
				: ETacticalPauseRequestResult::ApplyFailed,
			SelectedPlaybackMultiplier);
	}

	AppliedPlaybackMultiplier = TemporalDriver->GetGlobalTimeDilation();
	BroadcastSpeedChanges(PreviousSelected, PreviousApplied, GetSelectedPresetId(), true);
	const FTacticalPauseStateChange FinalChange{PlaybackState, ETacticalPlaybackState::Playing};
	SetPlaybackState(ETacticalPlaybackState::Playing);
	BroadcastResumed(FinalChange);
	TACTICALPAUSE_LOG_INFO("Simulation resumed for World %s at x%.3f.", *GetNameSafe(GetWorld()), AppliedPlaybackMultiplier);
	return ETacticalPauseRequestResult::Succeeded;
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::SetPlaybackSpeed(float InMultiplier)
{
	return SetPlaybackSpeedInternal(InMultiplier, NAME_None, ETacticalPauseRequestType::SetPlaybackSpeed);
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::SetPlaybackPreset(FName InPresetId)
{
	const ETacticalPauseRequestResult Validation = ValidateMutableRequest(ETacticalPauseRequestType::SetPlaybackPreset, 0.0f, InPresetId);
	if (Validation != ETacticalPauseRequestResult::Succeeded)
	{
		return Validation;
	}
	const int32* PresetIndex = PresetLookup.Find(InPresetId);
	if (!PresetIndex || !AvailablePresets.IsValidIndex(*PresetIndex))
	{
		return FailRequest(ETacticalPauseRequestType::SetPlaybackPreset, ETacticalPauseRequestResult::UnknownPreset, 0.0f, InPresetId);
	}
	return SetPlaybackSpeedInternal(AvailablePresets[*PresetIndex].Multiplier, InPresetId, ETacticalPauseRequestType::SetPlaybackPreset);
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::SetPlaybackSpeedInternal(float InMultiplier, FName PresetId, ETacticalPauseRequestType RequestType)
{
	const ETacticalPauseRequestResult Validation = ValidateMutableRequest(RequestType, InMultiplier, PresetId);
	if (Validation != ETacticalPauseRequestResult::Succeeded)
	{
		return Validation;
	}
	if (!CanSetPlaybackSpeed(InMultiplier))
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::InvalidPlaybackSpeed, InMultiplier, PresetId);
	}

	const bool bDilationConflict = SynchronizeObservedTemporalState(true);
	const bool bPaused = TemporalDriver->IsPaused();
	const UTacticalPauseSettings* Settings = GetDefault<UTacticalPauseSettings>();
	if (bPaused && Settings && !Settings->bAllowSpeedSelectionWhilePaused)
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::SpeedSelectionWhilePausedDisabled, InMultiplier, PresetId);
	}
	if (!bPaused && bDilationConflict)
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::ExternalStateConflict, InMultiplier, PresetId);
	}

	const float PreviousSelected = SelectedPlaybackMultiplier;
	const float PreviousApplied = GetAppliedPlaybackSpeed();
	if (bPaused)
	{
		if (NearlyEqual(PreviousSelected, InMultiplier))
		{
			return ETacticalPauseRequestResult::AlreadyInRequestedState;
		}
		SelectedPlaybackMultiplier = InMultiplier;
		BroadcastSpeedChanges(PreviousSelected, PreviousApplied, PresetId, false);
		TACTICALPAUSE_LOG_INFO("Selected resume speed x%.3f for paused World %s. Preset=%s.", InMultiplier, *GetNameSafe(GetWorld()), *PresetId.ToString());
		return ETacticalPauseRequestResult::Succeeded;
	}

	if (NearlyEqual(PreviousSelected, InMultiplier) && NearlyEqual(PreviousApplied, InMultiplier))
	{
		return ETacticalPauseRequestResult::AlreadyInRequestedState;
	}
	if (!ApplyGlobalDilation(InMultiplier))
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::ApplyFailed, InMultiplier, PresetId);
	}

	SelectedPlaybackMultiplier = InMultiplier;
	AppliedPlaybackMultiplier = TemporalDriver->GetGlobalTimeDilation();
	BroadcastSpeedChanges(PreviousSelected, PreviousApplied, PresetId, true);
	TACTICALPAUSE_LOG_INFO("Playback speed changed for World %s. Multiplier=%.3f Preset=%s.", *GetNameSafe(GetWorld()), InMultiplier, *PresetId.ToString());
	return ETacticalPauseRequestResult::Succeeded;
}

ETacticalPlaybackState UTacticalPauseWorldSubsystem::GetPlaybackState() const
{
	if (PlaybackState == ETacticalPlaybackState::TransitioningToPause || PlaybackState == ETacticalPlaybackState::TransitioningToPlay)
	{
		return PlaybackState;
	}
	if (TemporalDriver && TemporalDriver->IsAvailable())
	{
		return TemporalDriver->IsPaused() ? ETacticalPlaybackState::Paused : ETacticalPlaybackState::Playing;
	}
	return PlaybackState;
}

bool UTacticalPauseWorldSubsystem::IsPaused() const
{
	const ETacticalPlaybackState State = GetPlaybackState();
	return State == ETacticalPlaybackState::Paused || State == ETacticalPlaybackState::TransitioningToPause;
}

bool UTacticalPauseWorldSubsystem::IsPlaying() const
{
	const ETacticalPlaybackState State = GetPlaybackState();
	return State == ETacticalPlaybackState::Playing || State == ETacticalPlaybackState::TransitioningToPlay;
}

float UTacticalPauseWorldSubsystem::GetAppliedPlaybackSpeed() const
{
	if (TemporalDriver && TemporalDriver->IsAvailable())
	{
		return TemporalDriver->IsPaused() ? 0.0f : TemporalDriver->GetGlobalTimeDilation();
	}
	return AppliedPlaybackMultiplier;
}

FName UTacticalPauseWorldSubsystem::GetSelectedPresetId() const
{
	for (const FTacticalPlaybackSpeedPreset& Preset : AvailablePresets)
	{
		if (NearlyEqual(Preset.Multiplier, SelectedPlaybackMultiplier))
		{
			return Preset.Id;
		}
	}
	return NAME_None;
}

bool UTacticalPauseWorldSubsystem::CanPause() const
{
	return !bShuttingDown
		&& TemporalDriver
		&& TemporalDriver->IsAvailable()
		&& PlaybackState != ETacticalPlaybackState::TransitioningToPause
		&& PlaybackState != ETacticalPlaybackState::TransitioningToPlay
		&& !TemporalDriver->IsPaused();
}

bool UTacticalPauseWorldSubsystem::CanPlay() const
{
	return !bShuttingDown
		&& TemporalDriver
		&& TemporalDriver->IsAvailable()
		&& PlaybackState != ETacticalPlaybackState::TransitioningToPause
		&& PlaybackState != ETacticalPlaybackState::TransitioningToPlay
		&& TemporalDriver->IsPaused()
		&& bPauseOwnedByPlugin;
}

bool UTacticalPauseWorldSubsystem::CanSetPlaybackSpeed(float InMultiplier) const
{
	if (!FMath::IsFinite(InMultiplier) || InMultiplier <= 0.0f || InMultiplier > MaximumAllowedMultiplier)
	{
		return false;
	}
	return TemporalDriver
		&& TemporalDriver->IsAvailable()
		&& InMultiplier <= TemporalDriver->GetMaximumGlobalTimeDilation();
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::ValidateMutableRequest(ETacticalPauseRequestType RequestType, float RequestedMultiplier, FName PresetId)
{
	if (bShuttingDown)
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::ShuttingDown, RequestedMultiplier, PresetId);
	}
	if (!TemporalDriver || !TemporalDriver->IsAvailable())
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::InvalidWorld, RequestedMultiplier, PresetId);
	}
	if (PlaybackState == ETacticalPlaybackState::TransitioningToPause || PlaybackState == ETacticalPlaybackState::TransitioningToPlay)
	{
		return FailRequest(RequestType, ETacticalPauseRequestResult::TransitionInProgress, RequestedMultiplier, PresetId);
	}
	return ETacticalPauseRequestResult::Succeeded;
}

ETacticalPauseRequestResult UTacticalPauseWorldSubsystem::FailRequest(ETacticalPauseRequestType RequestType, ETacticalPauseRequestResult Result, float RequestedMultiplier, FName PresetId)
{
	FTacticalPauseRequestFailure Failure;
	Failure.RequestType = RequestType;
	Failure.Result = Result;
	Failure.RequestedMultiplier = RequestedMultiplier;
	Failure.PresetId = PresetId;
	OnRequestFailed.Broadcast(Failure);
	RequestFailedNative.Broadcast(Failure);
	TACTICALPAUSE_LOG_WARNING("Request %s failed for World %s. Result=%s Multiplier=%.3f Preset=%s State=%d.",
		*RequestName(RequestType), *GetNameSafe(GetWorld()), *ResultName(Result), RequestedMultiplier, *PresetId.ToString(), static_cast<int32>(GetPlaybackState()));
	return Result;
}

void UTacticalPauseWorldSubsystem::BuildValidatedPresets()
{
	AvailablePresets.Reset();
	PresetLookup.Reset();
	const UTacticalPauseSettings* Settings = GetDefault<UTacticalPauseSettings>();
	MaximumAllowedMultiplier = Settings && FMath::IsFinite(Settings->MaximumAllowedMultiplier) && Settings->MaximumAllowedMultiplier > 0.0f
		? Settings->MaximumAllowedMultiplier
		: 3.0f;
	if (!Settings || !FMath::IsFinite(Settings->MaximumAllowedMultiplier) || Settings->MaximumAllowedMultiplier <= 0.0f)
	{
		TACTICALPAUSE_LOG_ERROR("Invalid MaximumAllowedMultiplier in Tactical Pause settings. Using safe fallback x3.");
	}

	if (Settings)
	{
		TSet<FName> SeenIds;
		for (const FTacticalPlaybackSpeedPreset& Preset : Settings->SpeedPresets)
		{
			if (Preset.Id.IsNone() || !FMath::IsFinite(Preset.Multiplier) || Preset.Multiplier <= 0.0f || Preset.Multiplier > MaximumAllowedMultiplier)
			{
				TACTICALPAUSE_LOG_WARNING("Ignoring invalid Tactical Pause preset '%s' with multiplier %.3f.", *Preset.Id.ToString(), Preset.Multiplier);
				continue;
			}
			if (SeenIds.Contains(Preset.Id))
			{
				TACTICALPAUSE_LOG_WARNING("Ignoring duplicate Tactical Pause preset ID '%s'; the first valid entry wins.", *Preset.Id.ToString());
				continue;
			}
			SeenIds.Add(Preset.Id);
			AvailablePresets.Add(Preset);
		}
	}

	// Stable sorting makes equal SortOrder entries deterministic from configuration order.
	Algo::StableSort(AvailablePresets, [](const FTacticalPlaybackSpeedPreset& Left, const FTacticalPlaybackSpeedPreset& Right)
	{
		return Left.SortOrder < Right.SortOrder;
	});
	if (AvailablePresets.IsEmpty())
	{
		FTacticalPlaybackSpeedPreset Fallback;
		Fallback.Id = TEXT("Normal");
		Fallback.DisplayName = FText::FromString(TEXT("x1"));
		Fallback.Multiplier = 1.0f;
		AvailablePresets.Add(Fallback);
		TACTICALPAUSE_LOG_ERROR("No valid Tactical Pause speed presets were configured. Using safe fallback Normal/x1.");
	}

	for (int32 Index = 0; Index < AvailablePresets.Num(); ++Index)
	{
		PresetLookup.Add(AvailablePresets[Index].Id, Index);
	}
	const int32* DefaultIndex = Settings ? PresetLookup.Find(Settings->DefaultPresetId) : nullptr;
	if (!DefaultIndex)
	{
		TACTICALPAUSE_LOG_WARNING("Configured Tactical Pause default preset '%s' is unavailable; using '%s'.",
			Settings ? *Settings->DefaultPresetId.ToString() : TEXT("None"), *AvailablePresets[0].Id.ToString());
	}
	SelectedPlaybackMultiplier = AvailablePresets[DefaultIndex ? *DefaultIndex : 0].Multiplier;
}

bool UTacticalPauseWorldSubsystem::SynchronizeObservedTemporalState(bool bBroadcastChanges)
{
	if (!TemporalDriver || !TemporalDriver->IsAvailable())
	{
		return false;
	}
	const float LiveDilation = TemporalDriver->GetGlobalTimeDilation();
	const bool bConflict = DetectExternalDilationConflict(LiveDilation, bBroadcastChanges);
	const ETacticalPlaybackState ObservedState = TemporalDriver->IsPaused() ? ETacticalPlaybackState::Paused : ETacticalPlaybackState::Playing;
	if (PlaybackState != ETacticalPlaybackState::TransitioningToPause
		&& PlaybackState != ETacticalPlaybackState::TransitioningToPlay
		&& PlaybackState != ObservedState)
	{
		const FTacticalPauseStateChange Change{PlaybackState, ObservedState};
		SetPlaybackState(ObservedState);
		if (bBroadcastChanges)
		{
			ObservedState == ETacticalPlaybackState::Paused ? BroadcastPaused(Change) : BroadcastResumed(Change);
		}
	}
	const float ObservedApplied = TemporalDriver->IsPaused() ? 0.0f : LiveDilation;
	if (!NearlyEqual(AppliedPlaybackMultiplier, ObservedApplied))
	{
		UpdateAppliedPlaybackMultiplier(ObservedApplied, NAME_None, false, SelectedPlaybackMultiplier);
	}
	return bConflict;
}

bool UTacticalPauseWorldSubsystem::DetectExternalDilationConflict(float LiveDilation, bool bBroadcastChanges)
{
	if (!bDilationOwnedByPlugin || NearlyEqual(LiveDilation, LastPluginAppliedDilation))
	{
		return false;
	}
	TACTICALPAUSE_LOG_WARNING("External time-dilation change detected for World %s. PluginLast=%.3f Current=%.3f; ownership relinquished.",
		*GetNameSafe(GetWorld()), LastPluginAppliedDilation, LiveDilation);
	// Never restore across a newer external write; the next explicit request may reacquire.
	bDilationOwnedByPlugin = false;
	bHasDilationSnapshot = false;
	if (bBroadcastChanges && !TemporalDriver->IsPaused())
	{
		UpdateAppliedPlaybackMultiplier(LiveDilation, NAME_None, false, SelectedPlaybackMultiplier);
	}
	return true;
}

bool UTacticalPauseWorldSubsystem::ApplyGlobalDilation(float InMultiplier)
{
	if (!TemporalDriver || !TemporalDriver->IsAvailable())
	{
		return false;
	}
	const float CurrentDilation = TemporalDriver->GetGlobalTimeDilation();
	if (NearlyEqual(CurrentDilation, InMultiplier))
	{
		if (bDilationOwnedByPlugin)
		{
			LastPluginAppliedDilation = InMultiplier;
		}
		return true;
	}
	const bool bWasOwned = bDilationOwnedByPlugin;
	const bool bHadSnapshot = bHasDilationSnapshot;
	const float SavedSnapshot = PreviousGlobalTimeDilation;
	if (!bDilationOwnedByPlugin)
	{
		// Snapshot lazily so initialization alone never claims or restores temporal state.
		PreviousGlobalTimeDilation = CurrentDilation;
		bHasDilationSnapshot = true;
	}
	const float AppliedDilation = TemporalDriver->SetGlobalTimeDilation(InMultiplier);
	if (!NearlyEqual(AppliedDilation, InMultiplier))
	{
		const float RollbackDilation = TemporalDriver->SetGlobalTimeDilation(CurrentDilation);
		TACTICALPAUSE_LOG_ERROR("World %s rejected playback multiplier %.3f. Applied=%.3f Rollback=%.3f.",
			*GetNameSafe(GetWorld()), InMultiplier, AppliedDilation, RollbackDilation);
		bDilationOwnedByPlugin = bWasOwned;
		bHasDilationSnapshot = bHadSnapshot;
		PreviousGlobalTimeDilation = SavedSnapshot;
		return false;
	}
	bDilationOwnedByPlugin = true;
	LastPluginAppliedDilation = AppliedDilation;
	return true;
}

void UTacticalPauseWorldSubsystem::RestoreTemporalState()
{
	if (bTemporalStateRestored)
	{
		return;
	}
	bTemporalStateRestored = true;
	if (!TemporalDriver || !TemporalDriver->IsAvailable())
	{
		return;
	}

	if (bDilationOwnedByPlugin && bHasDilationSnapshot)
	{
		const float LiveDilation = TemporalDriver->GetGlobalTimeDilation();
		// Restore only if the live value still proves that this plugin is the latest writer.
		if (NearlyEqual(LiveDilation, LastPluginAppliedDilation))
		{
			const float RestoredDilation = TemporalDriver->SetGlobalTimeDilation(PreviousGlobalTimeDilation);
			if (NearlyEqual(RestoredDilation, PreviousGlobalTimeDilation))
			{
				TACTICALPAUSE_LOG_INFO("Restored time dilation for World %s to %.3f during teardown.", *GetNameSafe(GetWorld()), RestoredDilation);
			}
			else
			{
				TACTICALPAUSE_LOG_ERROR("Failed to restore time dilation for World %s. Requested=%.3f Applied=%.3f.",
					*GetNameSafe(GetWorld()), PreviousGlobalTimeDilation, RestoredDilation);
			}
		}
		else
		{
			TACTICALPAUSE_LOG_WARNING("Skipped time-dilation restoration for World %s because an external system changed it from plugin value %.3f to %.3f.",
				*GetNameSafe(GetWorld()), LastPluginAppliedDilation, LiveDilation);
		}
	}
	bDilationOwnedByPlugin = false;
	bHasDilationSnapshot = false;

	if (bPauseOwnedByPlugin)
	{
		bPauseReleaseAllowed = true;
		bPauseReleaseDelegateInvoked = false;
		const bool bReleaseReportedSuccess = TemporalDriver->ReleasePause();
		bPauseReleaseAllowed = false;
		if (bPauseReleaseDelegateInvoked)
		{
			bPauseOwnedByPlugin = false;
		}
		if (!bReleaseReportedSuccess && bPauseOwnedByPlugin)
		{
			TACTICALPAUSE_LOG_WARNING("Unable to release plugin-owned pause during teardown for World %s.", *GetNameSafe(GetWorld()));
		}
		else if (TemporalDriver->IsPaused())
		{
			TACTICALPAUSE_LOG_INFO("World %s remains paused by an external owner after Tactical Pause teardown.", *GetNameSafe(GetWorld()));
		}
		else
		{
			TACTICALPAUSE_LOG_INFO("Released Tactical Pause ownership for World %s during teardown.", *GetNameSafe(GetWorld()));
		}
	}
}

bool UTacticalPauseWorldSubsystem::CanReleaseOwnedPause()
{
	bPauseReleaseDelegateInvoked = true;
	return bPauseReleaseAllowed;
}

void UTacticalPauseWorldSubsystem::SetPlaybackState(ETacticalPlaybackState NewState)
{
	if (PlaybackState == NewState)
	{
		return;
	}
	FTacticalPauseStateChange Change;
	Change.PreviousState = PlaybackState;
	Change.NewState = NewState;
	PlaybackState = NewState;
	OnPlaybackStateChanged.Broadcast(Change);
	PlaybackStateChangedNative.Broadcast(Change);
}

void UTacticalPauseWorldSubsystem::BroadcastPaused(const FTacticalPauseStateChange& Change)
{
	OnPaused.Broadcast(Change);
	PausedNative.Broadcast(Change);
}

void UTacticalPauseWorldSubsystem::BroadcastResumed(const FTacticalPauseStateChange& Change)
{
	OnResumed.Broadcast(Change);
	ResumedNative.Broadcast(Change);
}

void UTacticalPauseWorldSubsystem::BroadcastSpeedChanges(float PreviousSelected, float PreviousApplied, FName PresetId, bool bAppliedToWorld)
{
	FTacticalPauseSpeedChange Change;
	Change.PreviousSelectedMultiplier = PreviousSelected;
	Change.NewSelectedMultiplier = SelectedPlaybackMultiplier;
	Change.PreviousAppliedMultiplier = PreviousApplied;
	Change.NewAppliedMultiplier = GetAppliedPlaybackSpeed();
	Change.PresetId = PresetId;
	Change.bAppliedToWorld = bAppliedToWorld;
	if (!NearlyEqual(Change.PreviousSelectedMultiplier, Change.NewSelectedMultiplier))
	{
		OnSelectedPlaybackSpeedChanged.Broadcast(Change);
		SelectedPlaybackSpeedChangedNative.Broadcast(Change);
	}
	if (!NearlyEqual(Change.PreviousAppliedMultiplier, Change.NewAppliedMultiplier))
	{
		OnAppliedPlaybackSpeedChanged.Broadcast(Change);
		AppliedPlaybackSpeedChangedNative.Broadcast(Change);
	}
}

void UTacticalPauseWorldSubsystem::UpdateAppliedPlaybackMultiplier(float NewAppliedMultiplier, FName PresetId, bool bAppliedToWorld, float PreviousSelected)
{
	const float PreviousApplied = AppliedPlaybackMultiplier;
	AppliedPlaybackMultiplier = NewAppliedMultiplier;
	BroadcastSpeedChanges(PreviousSelected, PreviousApplied, PresetId, bAppliedToWorld);
}
