#pragma once

#include "Actions/GameplayActionDefinition.h"
#include "ParadoxInvestigationWaitActionDefinition.generated.h"

/** Native inspection wait Definition so investigation never uses a direct timer as gameplay work. */
UCLASS()
class PARADOX_API UParadoxInvestigationWaitActionDefinition
	: public UGameplayActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxInvestigationWaitActionDefinition();
};

