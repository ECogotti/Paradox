#pragma once

#include "GridWorldTypes.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "ParadoxCloneReplayExecutionStrategyPrivate.generated.h"

/** Reflected source container used for type-safe Gameplay Actions Property Bag writes. */
USTRUCT()
struct FParadoxCloneReplayGridMoveOverrides
{
	GENERATED_BODY()

	UPROPERTY()
	FGridInjectedPath InjectedPath;
};
