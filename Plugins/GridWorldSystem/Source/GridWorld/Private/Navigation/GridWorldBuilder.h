// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Navigation/GridWorldSnapshot.h"

class AGridNavigationBoundsVolume;
class UWorld;

/** Synchronous Game-Thread collision sampler and immutable topology builder. */
class FGridWorldBuilder
{
public:
	/** Builds every valid bounds region. @param OutErrors Receives validation/generation failures. @return New mutable snapshot or nullptr. */
	static TSharedPtr<FGridWorldSnapshot, ESPMode::ThreadSafe> Build(
		UWorld& World,
		uint32 TopologyGeneration,
		TArray<FString>& OutErrors);

private:
	/** Rejects duplicate IDs, unsupported transforms/shapes, and ambiguous overlaps. */
	static bool ValidateVolumes(const TArray<AGridNavigationBoundsVolume*>& Volumes, TArray<FString>& OutErrors);
	/** Samples floors and upright capsule clearance for one volume. */
	static void SampleVolume(UWorld& World, const AGridNavigationBoundsVolume& Volume, FGridWorldSnapshot& Snapshot);
	/** Publishes deterministic slope-aware ordinary adjacency after all cells are sampled. */
	static void BuildAdjacency(FGridWorldSnapshot& Snapshot);
};
