#pragma once

#include "CoreMinimal.h"
#include "PuzzleOverlay/ParadoxPuzzleCircuitTypes.h"

/** Deterministic, UObject-free orthogonal router for one selected puzzle subgraph. */
class PARADOX_API FParadoxPuzzleWireRouter
{
public:
	static FParadoxPuzzleRoutingResult CalculateRoutes(
		const FParadoxPuzzleRoutingSnapshot& Snapshot);
};

