#pragma once

#include "Components/SlateWrapperTypes.h"
#include "CoreMinimal.h"
#include "ParadoxGameplayHUDTypes.generated.h"

/** Presentation page selected by the player for the persistent gameplay HUD. */
UENUM(BlueprintType)
enum class EParadoxGameplayHUDMode : uint8
{
	Normal,
	Collapsed
};

/** Controlled override layered on top of the native gameplay-phase visibility policy. */
UENUM(BlueprintType)
enum class EParadoxGameplayHUDVisibilityOverride : uint8
{
	Automatic,
	ForcedVisible,
	ForcedHidden
};

/** Independently addressable content areas on the Normal HUD page. */
UENUM(BlueprintType)
enum class EParadoxGameplayHUDSection : uint8
{
	TacticalPause,
	Equipment,
	Status
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxGameplayHUDModeChanged,
	EParadoxGameplayHUDMode, PreviousMode,
	EParadoxGameplayHUDMode, NewMode);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxGameplayHUDVisibilityChanged,
	bool, bIsVisible);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxGameplayHUDSectionVisibilityChanged,
	EParadoxGameplayHUDSection, Section,
	ESlateVisibility, Visibility);
