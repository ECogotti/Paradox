// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Navigation/GridWorldSnapshot.h"

namespace UE::GridWorld::Debug
{
	/**
	 * Projects the four logical X/Y cell corners onto the cell's tangent floor plane.
	 * @param Region Coordinate frame and cell dimensions.
	 * @param Cell Generated center and floor normal.
	 * @param OutVertices Receives four clockwise world-space vertices.
	 * @param SurfaceOffset Normal offset used to prevent z-fighting.
	 * @return False when the tangent plane cannot be projected along the sampling axis.
	 */
	inline bool BuildProjectedCellQuad(
		const FGridRegionData& Region,
		const FGridCellData& Cell,
		FVector (&OutVertices)[4],
		double SurfaceOffset = 2.0)
	{
		const FVector FloorNormal = FVector(Cell.FloorNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector SamplingAxis = Region.GridTransform.Rotation.RotateVector(FVector::UpVector).GetSafeNormal();
		const double ProjectionDenominator = FVector::DotProduct(FloorNormal, SamplingAxis);
		if (FloorNormal.ContainsNaN()
			|| SamplingAxis.ContainsNaN()
			|| FMath::IsNearlyZero(ProjectionDenominator))
		{
			return false;
		}

		const FVector LocalCenter = Region.GridTransform.WorldToLocal(Cell.WorldCenter);
		const FVector2D HalfSize(
			FMath::Abs(Region.GridTransform.CellSize.X) * 0.5,
			FMath::Abs(Region.GridTransform.CellSize.Y) * 0.5);
		static const FVector2D CornerSigns[4] = {
			FVector2D(-1.0, -1.0),
			FVector2D(1.0, -1.0),
			FVector2D(1.0, 1.0),
			FVector2D(-1.0, 1.0)};

		for (int32 CornerIndex = 0; CornerIndex < UE_ARRAY_COUNT(CornerSigns); ++CornerIndex)
		{
			const FVector LocalCorner(
				LocalCenter.X + CornerSigns[CornerIndex].X * HalfSize.X,
				LocalCenter.Y + CornerSigns[CornerIndex].Y * HalfSize.Y,
				LocalCenter.Z);
			const FVector UnprojectedCorner = Region.GridTransform.LocalToWorld(LocalCorner);
			const double ProjectionDistance = FVector::DotProduct(
				FloorNormal,
				Cell.WorldCenter - UnprojectedCorner) / ProjectionDenominator;
			OutVertices[CornerIndex] = UnprojectedCorner
				+ SamplingAxis * ProjectionDistance
				+ FloorNormal * SurfaceOffset;
		}
		return true;
	}
}
