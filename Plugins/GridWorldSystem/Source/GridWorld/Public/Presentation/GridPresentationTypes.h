// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "GridPresentationTypes.generated.h"

/** Resolved interaction appearance. Hover and selection contributions are tracked independently. */
UENUM(BlueprintType)
enum class EGridCellInteractionVisualState : uint8
{
	Unselected,
	Hovered,
	Selected
};

/** Generic owner-scoped overlay layer. Consumers assign project meaning to Primary and Secondary. */
UENUM(BlueprintType)
enum class EGridCellOverlayVisualState : uint8
{
	None,
	Primary,
	Secondary
};

/** Independent path-presentation layer reserved for the cell-overlay milestone. */
UENUM(BlueprintType)
enum class EGridCellPathVisualState : uint8
{
	None,
	Preview,
	ActiveRemaining,
	ActiveCurrent,
	ActiveTraversed,
	Destination,
	Invalid,
	Custom
};

/** Read-only visual flags derived from the authoritative published cell. */
UENUM(BlueprintType, meta = (Bitflags))
enum class EGridCellNavigationVisualFlags : uint8
{
	None = 0,
	Traversable = 1 << 0,
	Blocked = 1 << 1,
	HighCost = 1 << 2,
	Occupied = 1 << 3,
	Reserved = 1 << 4
};
ENUM_CLASS_FLAGS(EGridCellNavigationVisualFlags);

/** Layered, non-authoritative presentation state for one persistent cell. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellVisualState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	EGridCellInteractionVisualState InteractionState = EGridCellInteractionVisualState::Unselected;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	EGridCellPathVisualState PathState = EGridCellPathVisualState::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	EGridCellOverlayVisualState OverlayState = EGridCellOverlayVisualState::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation", meta = (Bitmask, BitmaskEnum = "/Script/GridWorld.EGridCellNavigationVisualFlags"))
	int32 NavigationFlags = static_cast<int32>(EGridCellNavigationVisualFlags::None);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	float Emphasis = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	float PathProgress = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	float CustomStyleValue = 0.0f;
};

/** Central material contract shared by the runtime renderer and authored cell materials. */
struct GRIDWORLD_API FGridCellMaterialDataLayout
{
	static constexpr int32 InteractionState = 0;
	static constexpr int32 PathState = 1;
	static constexpr int32 NavigationFlags = 2;
	static constexpr int32 Emphasis = 3;
	static constexpr int32 ResolvedRed = 4;
	static constexpr int32 ResolvedGreen = 5;
	static constexpr int32 ResolvedBlue = 6;
	static constexpr int32 ResolvedAlpha = 7;
	static constexpr int32 PathProgress = 8;
	static constexpr int32 CustomStyleValue = 9;
	static constexpr int32 NumFloats = 10;

	/** Writes all ten custom-data channels in their documented order. */
	static void Write(const FGridCellVisualState& State, const FLinearColor& ResolvedColor, float (&OutData)[NumFloats]);
};

/** Internal resolved path layer consumed by the runtime cell renderer. */
struct GRIDWORLD_API FGridResolvedPathVisualState
{
	EGridCellPathVisualState State = EGridCellPathVisualState::None;
	float PathProgress = 0.0f;

	bool operator==(const FGridResolvedPathVisualState& Other) const
	{
		return State == Other.State && FMath::IsNearlyEqual(PathProgress, Other.PathProgress);
	}

	bool operator!=(const FGridResolvedPathVisualState& Other) const { return !(*this == Other); }
};
