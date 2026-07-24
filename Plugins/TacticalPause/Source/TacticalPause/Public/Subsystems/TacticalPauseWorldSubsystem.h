#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Types/TacticalPauseTypes.h"
#include "TacticalPauseWorldSubsystem.generated.h"

class ITacticalPauseTemporalDriver;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTacticalPauseStateChangedEvent, const FTacticalPauseStateChange&, Change);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTacticalPauseSpeedChangedEvent, const FTacticalPauseSpeedChange&, Change);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTacticalPauseRequestFailedEvent, const FTacticalPauseRequestFailure&, Failure);

DECLARE_MULTICAST_DELEGATE_OneParam(FTacticalPauseStateChangedNativeEvent, const FTacticalPauseStateChange&);
DECLARE_MULTICAST_DELEGATE_OneParam(FTacticalPauseSpeedChangedNativeEvent, const FTacticalPauseSpeedChange&);
DECLARE_MULTICAST_DELEGATE_OneParam(FTacticalPauseRequestFailedNativeEvent, const FTacticalPauseRequestFailure&);

/** Per-world authority for gameplay pause, playback speed, ownership, and restoration. */
UCLASS()
class TACTICALPAUSE_API UTacticalPauseWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UTacticalPauseWorldSubsystem();
	virtual ~UTacticalPauseWorldSubsystem() override;

	/** Broadcast for every authoritative state step, including transition states. */
	UPROPERTY(BlueprintAssignable, Category = "Tactical Pause|Events")
	FTacticalPauseStateChangedEvent OnPlaybackStateChanged;

	/** Broadcast only after Unreal reports the world as paused. */
	UPROPERTY(BlueprintAssignable, Category = "Tactical Pause|Events")
	FTacticalPauseStateChangedEvent OnPaused;

	/** Broadcast only after Unreal reports the world as playing. */
	UPROPERTY(BlueprintAssignable, Category = "Tactical Pause|Events")
	FTacticalPauseStateChangedEvent OnResumed;

	/** Broadcast when the multiplier selected for play or resume changes. */
	UPROPERTY(BlueprintAssignable, Category = "Tactical Pause|Events")
	FTacticalPauseSpeedChangedEvent OnSelectedPlaybackSpeedChanged;

	/** Broadcast when the effective world multiplier changes. */
	UPROPERTY(BlueprintAssignable, Category = "Tactical Pause|Events")
	FTacticalPauseSpeedChangedEvent OnAppliedPlaybackSpeedChanged;

	/** Broadcast with structured context whenever a mutation fails. */
	UPROPERTY(BlueprintAssignable, Category = "Tactical Pause|Events")
	FTacticalPauseRequestFailedEvent OnRequestFailed;

	/** Pauses gameplay without taking ownership of an already external pause. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Commands")
	ETacticalPauseRequestResult RequestPause();

	/** Resumes gameplay at the selected speed only when this plugin owns pause. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Commands")
	ETacticalPauseRequestResult RequestPlay();

	/** Routes to Play or Pause from the currently observed world state. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Commands")
	ETacticalPauseRequestResult TogglePause();

	/** Selects and, while playing, applies a validated positive multiplier. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Commands")
	ETacticalPauseRequestResult SetPlaybackSpeed(float InMultiplier);

	/** Selects and applies the validated preset identified by InPresetId. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause|Commands")
	ETacticalPauseRequestResult SetPlaybackPreset(FName InPresetId);

	/** Returns the live stable state or the authoritative in-progress transition. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|State")
	ETacticalPlaybackState GetPlaybackState() const;

	/** True when the world is paused or is transitioning toward pause. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|State")
	bool IsPaused() const;

	/** True when the world is playing or is transitioning toward play. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|State")
	bool IsPlaying() const;

	/** Multiplier selected for current play or the next resume. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|State")
	float GetSelectedPlaybackSpeed() const { return SelectedPlaybackMultiplier; }

	/** Live effective multiplier, reported as zero while gameplay is paused. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|State")
	float GetAppliedPlaybackSpeed() const;

	/** Matching validated preset ID, or None for a custom selected multiplier. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|State")
	FName GetSelectedPresetId() const;

	/** Copy of the validated presets in deterministic display order. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|State")
	TArray<FTacticalPlaybackSpeedPreset> GetAvailablePresets() const { return AvailablePresets; }

	/** True when a new plugin-owned pause can currently be acquired. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|Validation")
	bool CanPause() const;

	/** True when the plugin can safely release its pause and resume. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|Validation")
	bool CanPlay() const;

	/** Validates multiplier bounds and temporal-driver availability without mutating state. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause|Validation")
	bool CanSetPlaybackSpeed(float InMultiplier) const;

	/** Native equivalents of the Blueprint-assignable event surface. */
	FTacticalPauseStateChangedNativeEvent& OnPlaybackStateChangedNative() { return PlaybackStateChangedNative; }
	FTacticalPauseStateChangedNativeEvent& OnPausedNative() { return PausedNative; }
	FTacticalPauseStateChangedNativeEvent& OnResumedNative() { return ResumedNative; }
	FTacticalPauseSpeedChangedNativeEvent& OnSelectedPlaybackSpeedChangedNative() { return SelectedPlaybackSpeedChangedNative; }
	FTacticalPauseSpeedChangedNativeEvent& OnAppliedPlaybackSpeedChangedNative() { return AppliedPlaybackSpeedChangedNative; }
	FTacticalPauseRequestFailedNativeEvent& OnRequestFailedNative() { return RequestFailedNative; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Shared implementations preserve the original public request type for diagnostics. */
	ETacticalPauseRequestResult RequestPauseInternal(ETacticalPauseRequestType RequestType);
	ETacticalPauseRequestResult RequestPlayInternal(ETacticalPauseRequestType RequestType);
	ETacticalPauseRequestResult SetPlaybackSpeedInternal(float InMultiplier, FName PresetId, ETacticalPauseRequestType RequestType);
	ETacticalPauseRequestResult ValidateMutableRequest(ETacticalPauseRequestType RequestType, float RequestedMultiplier = 0.0f, FName PresetId = NAME_None);
	ETacticalPauseRequestResult FailRequest(ETacticalPauseRequestType RequestType, ETacticalPauseRequestResult Result, float RequestedMultiplier = 0.0f, FName PresetId = NAME_None);

	/** Rebuilds deterministic preset state from developer settings with safe fallbacks. */
	void BuildValidatedPresets();
	/** Reconciles cached state with live Unreal state and reports dilation conflicts. */
	bool SynchronizeObservedTemporalState(bool bBroadcastChanges);
	/** Relinquishes dilation ownership when a newer external write is observed. */
	bool DetectExternalDilationConflict(float LiveDilation, bool bBroadcastChanges);
	/** Snapshots the external baseline on first acquisition and applies a multiplier. */
	bool ApplyGlobalDilation(float InMultiplier);
	/** Idempotently restores only pause and dilation still owned by this subsystem. */
	void RestoreTemporalState();
	/** FCanUnpause callback that allows only an explicit plugin release window. */
	bool CanReleaseOwnedPause();

	void SetPlaybackState(ETacticalPlaybackState NewState);
	void BroadcastPaused(const FTacticalPauseStateChange& Change);
	void BroadcastResumed(const FTacticalPauseStateChange& Change);
	void BroadcastSpeedChanges(float PreviousSelected, float PreviousApplied, FName PresetId, bool bAppliedToWorld);
	void UpdateAppliedPlaybackMultiplier(float NewAppliedMultiplier, FName PresetId, bool bAppliedToWorld, float PreviousSelected);

	/** Non-UObject implementation boundary, created and deleted with subsystem lifetime. */
	ITacticalPauseTemporalDriver* TemporalDriver = nullptr;
	TArray<FTacticalPlaybackSpeedPreset> AvailablePresets;
	TMap<FName, int32> PresetLookup;

	ETacticalPlaybackState PlaybackState = ETacticalPlaybackState::Playing;
	float SelectedPlaybackMultiplier = 1.0f;
	float AppliedPlaybackMultiplier = 1.0f;
	float MaximumAllowedMultiplier = 3.0f;

	bool bShuttingDown = false;
	bool bTemporalStateRestored = false;
	/** True only after this subsystem successfully contributed Unreal pause ownership. */
	bool bPauseOwnedByPlugin = false;
	/** Temporary gate read by the FCanUnpause delegate during an intentional release. */
	bool bPauseReleaseAllowed = false;
	/** Distinguishes a consumed plugin delegate from an unrelated failed release. */
	bool bPauseReleaseDelegateInvoked = false;
	/** True while the live dilation still matches the plugin's last successful write. */
	bool bDilationOwnedByPlugin = false;
	/** Whether PreviousGlobalTimeDilation contains a restorable external baseline. */
	bool bHasDilationSnapshot = false;
	float PreviousGlobalTimeDilation = 1.0f;
	float LastPluginAppliedDilation = 1.0f;

	FTacticalPauseStateChangedNativeEvent PlaybackStateChangedNative;
	FTacticalPauseStateChangedNativeEvent PausedNative;
	FTacticalPauseStateChangedNativeEvent ResumedNative;
	FTacticalPauseSpeedChangedNativeEvent SelectedPlaybackSpeedChangedNative;
	FTacticalPauseSpeedChangedNativeEvent AppliedPlaybackSpeedChangedNative;
	FTacticalPauseRequestFailedNativeEvent RequestFailedNative;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FTacticalPauseTestAccessor;
#endif
};
