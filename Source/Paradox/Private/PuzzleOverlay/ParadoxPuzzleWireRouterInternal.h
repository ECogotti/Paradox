#pragma once

#include "PuzzleOverlay/ParadoxPuzzleCircuitTypes.h"
#include "Tasks/Task.h"

namespace UE::Paradox::PuzzleOverlay::Private
{
	/** Cheap cooperative checkpoint. Correctness still relies on generation validation at apply time. */
	FORCEINLINE bool IsRoutingCancellationRequested()
	{
		return UE::Tasks::FCancellationTokenScope::IsCurrentWorkCanceled();
	}

	FORCEINLINE FParadoxPuzzleRoutingResult MakeCancelledRoutingResult(
		const FParadoxPuzzleRoutingSnapshot& Snapshot)
	{
		FParadoxPuzzleRoutingResult Result;
		Result.RoutingGeneration = Snapshot.RoutingGeneration;
		Result.Diagnostics.Algorithm = Snapshot.Settings.Algorithm;
		Result.bCancelled = true;
		return Result;
	}

	FParadoxPuzzleRoutingResult CalculateLegacyIndependentRoutes(
		const FParadoxPuzzleRoutingSnapshot& Snapshot);

	FParadoxPuzzleRoutingResult CalculateOrderedBundleRoutes(
		const FParadoxPuzzleRoutingSnapshot& Snapshot);

	FParadoxPuzzleRoutingResult CalculateDistributedRepulsiveRoutes(
		const FParadoxPuzzleRoutingSnapshot& Snapshot);
}
