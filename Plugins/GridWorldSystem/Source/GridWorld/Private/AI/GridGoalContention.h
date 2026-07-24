// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Navigation/GridWorldSnapshot.h"

namespace UE::GridWorld::Private
{
	struct FGridGoalCandidate
	{
		/** Dense candidate cell index. */
		int32 CellIndex = INDEX_NONE;
		/** Ordinary graph distance from the requested goal. */
		int32 SearchDistance = 0;
	};

	/** Gathers deterministic ordinary-adjacency candidates around one desired cell. @param OutCandidates Receives distance-ordered candidates. */
	void GatherGridGoalCandidates(
		const FGridWorldSnapshot& Snapshot,
		int32 DesiredCellIndex,
		bool bIncludeDesiredCell,
		int32 MaxSearchRadius,
		const TSet<FGridCellId>& RejectedCells,
		TArray<FGridGoalCandidate>& OutCandidates);

	/** Tests capsule-like horizontal and vertical separation from runtime occupancy owned by other agents. @return True when CandidateCell is available. */
	bool HasGridGoalOccupancySeparation(
		const FGridWorldSnapshot& Snapshot,
		const FGridCellData& CandidateCell,
		const FGuid& OwnOccupantId,
		float AgentRadius,
		float AgentHeight,
		float AdditionalSeparation);

	/**
	 * Copies a complete requested path without its occupied final cell.
	 * @return False when the sequence cannot prove an immediate predecessor for RequestedGoalCell.
	 */
	bool BuildStopBeforeOccupiedCells(
		TConstArrayView<FGridCellId> FullPath,
		const FGridCellId& RequestedGoalCell,
		TArray<FGridCellId>& OutAdjustedPath,
		FGridCellId& OutEffectiveGoalCell);
}
