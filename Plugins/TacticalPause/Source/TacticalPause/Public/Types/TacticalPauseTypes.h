#pragma once

#include "CoreMinimal.h"
#include "TacticalPauseTypes.generated.h"

/** Authoritative playback state for one gameplay world. */
UENUM(BlueprintType)
enum class ETacticalPlaybackState : uint8
{
	/** The world is running at the currently applied positive multiplier. */
	Playing,
	/** The world is effectively stopped by Unreal's gameplay pause. */
	Paused,
	/** A validated resume operation is currently being applied. */
	TransitioningToPlay,
	/** A validated pause operation is currently being applied. */
	TransitioningToPause
};

/** Public result returned by every temporal mutation request. */
UENUM(BlueprintType)
enum class ETacticalPauseRequestResult : uint8
{
	/** The request changed authoritative temporal state successfully. */
	Succeeded,
	/** The requested state or value was already authoritative. */
	AlreadyInRequestedState,
	/** No supported gameplay world or temporal driver was available. */
	InvalidWorld,
	/** The requested multiplier was non-finite, non-positive, or above a limit. */
	InvalidPlaybackSpeed,
	/** No validated preset has the requested identifier. */
	UnknownPreset,
	/** Project settings prohibit changing the selected speed while paused. */
	SpeedSelectionWhilePausedDisabled,
	/** Another temporal transition is already in progress. */
	TransitionInProgress,
	/** A newer external pause or dilation owner prevents a safe mutation. */
	ExternalStateConflict,
	/** Unreal rejected the requested pause, resume, or time dilation. */
	ApplyFailed,
	/** World teardown has begun and new mutations are rejected. */
	ShuttingDown
};

/** Operation associated with a failed request. */
UENUM(BlueprintType)
enum class ETacticalPauseRequestType : uint8
{
	/** Explicit pause request. */
	Pause,
	/** Explicit resume request. */
	Play,
	/** State-dependent pause or resume request. */
	TogglePause,
	/** Custom multiplier request. */
	SetPlaybackSpeed,
	/** Configured preset request. */
	SetPlaybackPreset
};

/** Determines which local players receive an automatically created widget. */
UENUM(BlueprintType)
enum class ETacticalPauseWidgetPlayerPolicy : uint8
{
	/** Create automatic controls only for the game instance's first local player. */
	PrimaryLocalPlayerOnly,
	/** Create automatic controls independently for every local player. */
	EveryLocalPlayer
};

/** Data-driven playback option exposed to C++, Blueprint, settings, and the default widget. */
USTRUCT(BlueprintType)
struct TACTICALPAUSE_API FTacticalPlaybackSpeedPreset
{
	GENERATED_BODY()

	/** Stable identifier used by C++, Blueprint, and generated UI controls. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	FName Id = NAME_None;

	/** Localizable label displayed by the default control widget. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	FText DisplayName;

	/** Positive global time-dilation value applied while playing. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (ClampMin = "0.0001", UIMin = "0.1"))
	float Multiplier = 1.0f;

	/** Ascending UI order; equal values retain their configuration order. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Playback")
	int32 SortOrder = 0;
};

/** Context broadcast whenever the subsystem changes playback state. */
USTRUCT(BlueprintType)
struct TACTICALPAUSE_API FTacticalPauseStateChange
{
	GENERATED_BODY()

	/** State observed immediately before the reported transition. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	ETacticalPlaybackState PreviousState = ETacticalPlaybackState::Playing;

	/** State reached by the reported transition. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	ETacticalPlaybackState NewState = ETacticalPlaybackState::Playing;
};

/** Context broadcast for selected or applied playback-speed changes. */
USTRUCT(BlueprintType)
struct TACTICALPAUSE_API FTacticalPauseSpeedChange
{
	GENERATED_BODY()

	/** Resume/playing multiplier selected before the operation. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	float PreviousSelectedMultiplier = 1.0f;

	/** Resume/playing multiplier selected after the operation. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	float NewSelectedMultiplier = 1.0f;

	/** Effective world multiplier before the operation; zero represents pause. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	float PreviousAppliedMultiplier = 1.0f;

	/** Effective world multiplier after the operation; zero represents pause. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	float NewAppliedMultiplier = 1.0f;

	/** Preset responsible for the change, or None for a custom/external value. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	FName PresetId = NAME_None;

	/** True when the operation wrote dilation, false for selection-only changes. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	bool bAppliedToWorld = false;
};

/** Diagnostic context for a rejected temporal request. */
USTRUCT(BlueprintType)
struct TACTICALPAUSE_API FTacticalPauseRequestFailure
{
	GENERATED_BODY()

	/** Public operation that was rejected or could not be applied. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	ETacticalPauseRequestType RequestType = ETacticalPauseRequestType::Pause;

	/** Specific observable reason for the failure. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	ETacticalPauseRequestResult Result = ETacticalPauseRequestResult::ApplyFailed;

	/** Requested multiplier when relevant, otherwise zero. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	float RequestedMultiplier = 0.0f;

	/** Requested preset when relevant, otherwise None. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical Pause")
	FName PresetId = NAME_None;
};
