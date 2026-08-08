#pragma once

#include "CoreMinimal.h"
#include "ParadoxSelectionTypes.generated.h"

/** Derived presentation state for one selectable Actor. */
UENUM(BlueprintType)
enum class EParadoxSelectionPresentationState : uint8
{
	None,
	Hovered,
	Selected
};

namespace UE::Paradox::Selection
{
	inline constexpr int32 HoverStencilValue = 230;
	inline constexpr int32 SelectedStencilValue = 240;
}
