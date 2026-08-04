#pragma once

#include "Actions/GameplayActionDefinition.h"
#include "ParadoxTimeTravelActionDefinition.generated.h"

/** Recorded terminal command that plays the owning avatar's time-travel effect. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxTimeTravelActionDefinition
	: public UGameplayActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxTimeTravelActionDefinition();
};
