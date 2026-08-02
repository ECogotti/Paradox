#pragma once

#include "Actions/GameplayActionDefinition.h"
#include "ParadoxSetCrouchedActionDefinition.generated.h"

namespace ParadoxSetCrouchedActionParameters
{
	PARADOX_API extern const FName DesiredCrouched;
}

/** Ready-to-author Definition for the deterministic Paradox stance command. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxSetCrouchedActionDefinition
	: public UGameplayActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxSetCrouchedActionDefinition();
};
