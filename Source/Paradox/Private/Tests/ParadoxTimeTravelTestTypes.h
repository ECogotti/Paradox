#pragma once

#include "Controllers/ParadoxPlayerController.h"
#include "ParadoxTimeTravelTestTypes.generated.h"

/** Concrete native controller used only to verify the abstract production controller contract. */
UCLASS()
class AParadoxTimeTravelTestPlayerController : public AParadoxPlayerController
{
	GENERATED_BODY()
};
