// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::GridWorld::WalkingSurface
{
	/** @return Normalized floor normal with world-up fallback for invalid compact input. */
	inline FVector GetSafeFloorNormal(const FVector3f& FloorNormal)
	{
		return FVector(FloorNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	}

	/** @return Floor angle in degrees from world-up. */
	inline double CalculateFloorSlopeDegrees(const FVector3f& FloorNormal)
	{
		const FVector Normal = GetSafeFloorNormal(FloorNormal);
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0)));
	}

	/**
	 * Computes a vertical upright-capsule center tangent to a sloped plane.
	 * @return Height above the sampled impact point, including Clearance.
	 */
	inline double CalculateUprightCapsuleCenterHeight(
		double HalfHeight,
		double Radius,
		double FloorUpDot,
		double Clearance = 1.0)
	{
		const double SafeHalfHeight = FMath::Max(HalfHeight, Radius);
		const double SafeUpDot = FMath::Max(FloorUpDot, UE_DOUBLE_KINDA_SMALL_NUMBER);
		return SafeHalfHeight - Radius + Radius / SafeUpDot + Clearance;
	}

	/** @return Natural floor height delta predicted by FloorNormal along HorizontalDelta. */
	inline double CalculateExpectedFloorHeightDelta(const FVector3f& FloorNormal, const FVector& HorizontalDelta)
	{
		const FVector Normal = GetSafeFloorNormal(FloorNormal);
		if (FMath::IsNearlyZero(Normal.Z))
		{
			return 0.0;
		}
		return -(Normal.X * HorizontalDelta.X + Normal.Y * HorizontalDelta.Y) / Normal.Z;
	}

	/** @return Actual Z delta minus the mean natural slope delta from both endpoint normals. */
	inline double CalculateResidualHeightDelta(
		const FVector& FromLocation,
		const FVector3f& FromFloorNormal,
		const FVector& ToLocation,
		const FVector3f& ToFloorNormal)
	{
		const FVector Delta = ToLocation - FromLocation;
		const FVector HorizontalDelta(Delta.X, Delta.Y, 0.0);
		const double ExpectedFrom = CalculateExpectedFloorHeightDelta(FromFloorNormal, HorizontalDelta);
		const double ExpectedTo = CalculateExpectedFloorHeightDelta(ToFloorNormal, HorizontalDelta);
		return Delta.Z - 0.5 * (ExpectedFrom + ExpectedTo);
	}

	/** @return True when only the residual step/drop lies within configured limits. */
	inline bool IsWalkingTransitionAllowed(
		const FVector& FromLocation,
		const FVector3f& FromFloorNormal,
		const FVector& ToLocation,
		const FVector3f& ToFloorNormal,
		double MaxStepHeight,
		double MaxDropHeight)
	{
		const double ResidualHeightDelta = CalculateResidualHeightDelta(
			FromLocation,
			FromFloorNormal,
			ToLocation,
			ToFloorNormal);
		return ResidualHeightDelta <= MaxStepHeight + UE_DOUBLE_KINDA_SMALL_NUMBER
			&& ResidualHeightDelta >= -MaxDropHeight - UE_DOUBLE_KINDA_SMALL_NUMBER;
	}

	/** @return True when a Character Movement floor angle is insufficient for the generated path. */
	inline bool RequiresWalkableFloorWarning(double PathSlopeDegrees, double WalkableFloorAngleDegrees)
	{
		return PathSlopeDegrees > WalkableFloorAngleDegrees + 0.1;
	}
}
