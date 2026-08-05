// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Navigation/GridWorldSnapshot.h"

class AGridNavigationBoundsVolume;
class UWorld;
struct FHitResult;
struct FCollisionQueryParams;

/** Synchronous Game-Thread collision sampler and immutable topology builder. */
class FGridWorldBuilder
{
public:
	/** Builds every valid bounds region. @param OutErrors Receives validation/generation failures. @return New mutable snapshot or nullptr. */
	static TSharedPtr<FGridWorldSnapshot, ESPMode::ThreadSafe> Build(
		UWorld& World,
		uint32 TopologyGeneration,
		TArray<FString>& OutErrors);

	/** Adds collision primitives explicitly excluded from navigation to topology sampling ignores. */
	static void AddNavigationIrrelevantComponentsToQuery(
		UWorld& World,
		FCollisionQueryParams& QueryParams);

	/**
	 * Tests the complete upright agent capsule at a sampled floor.
	 * When the base pose is blocked, deterministic lifted poses up to MaxStepHeight distinguish
	 * low climbable geometry from full-height walls and overhead obstruction.
	 */
	static bool HasAgentClearance(
		UWorld& World,
		const FVector& FloorLocation,
		double FloorUpDot,
		double AgentRadius,
		double AgentHalfHeight,
		double MaxStepHeight,
		FName CollisionProfileName,
		const FCollisionQueryParams& QueryParams);

	/**
	 * Collects distinct walkable surface hits in one vertical grid column.
	 * Floor traces deliberately discard initial overlaps so a trace restarted inside a thick
	 * solid cannot publish its interior repeatedly as additional navigation layers.
	 */
	static void GatherSurfaceHits(
		UWorld& World,
		const FGridTransform& GridTransform,
		double LocalX,
		double LocalY,
		double TraceTop,
		double TraceBottom,
		double LayerHeight,
		double MaxSlopeDegrees,
		double AgentRadius,
		FName CollisionProfileName,
		const FCollisionQueryParams& QueryParams,
		TArray<FHitResult>& OutHits);

private:
	/** Rejects duplicate IDs, unsupported transforms/shapes, and ambiguous overlaps. */
	static bool ValidateVolumes(const TArray<AGridNavigationBoundsVolume*>& Volumes, TArray<FString>& OutErrors);
	/** Samples floors and upright capsule clearance for one volume. */
	static void SampleVolume(UWorld& World, const AGridNavigationBoundsVolume& Volume, FGridWorldSnapshot& Snapshot);
	/** Publishes deterministic slope-aware ordinary adjacency after all cells are sampled. */
	static void BuildAdjacency(FGridWorldSnapshot& Snapshot);
};
