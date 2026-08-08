#pragma once

#include "Components/WidgetInteractionComponent.h"
#include "CoreMinimal.h"
#include "ParadoxWidgetInteractionComponent.generated.h"

/** Widget Interaction adapter that refreshes hover from the controller's shared custom hit. */
UCLASS(ClassGroup = (Paradox), NotBlueprintable)
class PARADOX_API UParadoxWidgetInteractionComponent final : public UWidgetInteractionComponent
{
	GENERATED_BODY()

public:
	/** Re-evaluates the Slate hover path without performing a second world trace. */
	void RefreshFromCustomHitResult() { SimulatePointerMovement(); }
};
