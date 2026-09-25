#pragma once

#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "ParadoxChronoSpawnActionDefinition.generated.h"

/** Recorded temporal materialization command. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxChronoSpawnActionDefinition
	: public UParadoxInteractionActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxChronoSpawnActionDefinition();
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
