// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"

namespace UE::GridWorld::AStarMath
{
	/**
	 * Calculates an admissible horizontal Manhattan/octile lower bound; Layer is intentionally ignored for ramps.
	 * @return Fixed-point heuristic cost.
	 */
	inline int64 CalculateHeuristic(
		const FGridCellCoord& From,
		const FGridCellCoord& To,
		EGridMovementMode MovementMode,
		int64 OrthogonalCost,
		int64 DiagonalCost)
	{
		const int64 DeltaX = FMath::Abs(static_cast<int64>(From.X) - To.X);
		const int64 DeltaY = FMath::Abs(static_cast<int64>(From.Y) - To.Y);
		if (MovementMode == EGridMovementMode::FourDirections)
		{
			return (DeltaX + DeltaY) * OrthogonalCost;
		}
		const int64 DiagonalSteps = FMath::Min(DeltaX, DeltaY);
		return DiagonalSteps * DiagonalCost
			+ (FMath::Max(DeltaX, DeltaY) - DiagonalSteps) * OrthogonalCost;
	}
}
