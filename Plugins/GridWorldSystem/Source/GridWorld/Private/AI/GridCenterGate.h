// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::GridWorld::Private
{
	/** Terminal classification of one swept, forward-only center-plane gate. */
	enum class EGridCenterGateTraversalResult : uint8
	{
		/** Pawn remains on the approach side. */
		Pending,
		/** Pawn entered tolerance or swept through the permitted plane extent. */
		Passed,
		/** Pawn crossed the forward plane outside its lateral/height extent. */
		Missed
	};

	/** Pure local-grid test for a one-way center gate. */
	struct FGridCenterGate
	{
		/** Local-grid forward direction required to cross the gate. */
		FVector2D Forward = FVector2D(1.0, 0.0);
		/** Permitted lateral half width. */
		float HalfWidth = 0.0f;
		/** Radial tolerance that always counts as a pass. */
		float CenterTolerance = 0.0f;
		/** Maximum vertical deviation from the center plane. */
		float HeightTolerance = 0.0f;

		/** Evaluates current position and the swept previous-to-current crossing. */
		EGridCenterGateTraversalResult Evaluate(
			const FVector& PreviousLocalOffset,
			const FVector& CurrentLocalOffset,
			bool bHasPreviousLocation) const
		{
			const FVector2D NormalizedForward = Forward.GetSafeNormal();
			if (NormalizedForward.IsNearlyZero())
			{
				return EGridCenterGateTraversalResult::Missed;
			}

			const float SafeCenterTolerance = FMath::Max(0.1f, CenterTolerance);
			const FVector2D CurrentHorizontal(CurrentLocalOffset.X, CurrentLocalOffset.Y);
			if (FMath::Abs(CurrentLocalOffset.Z) <= HeightTolerance
				&& CurrentHorizontal.SizeSquared() <= FMath::Square(SafeCenterTolerance))
			{
				return EGridCenterGateTraversalResult::Passed;
			}

			const FVector2D Tangent(-NormalizedForward.Y, NormalizedForward.X);
			const float CurrentAlong = FVector2D::DotProduct(CurrentHorizontal, NormalizedForward);
			const float CurrentLateral = FVector2D::DotProduct(CurrentHorizontal, Tangent);
			const auto IsInsideGate = [this](float Lateral, float Height)
			{
				return FMath::Abs(Lateral) <= FMath::Max(0.1f, HalfWidth)
					&& FMath::Abs(Height) <= HeightTolerance;
			};

			if (!bHasPreviousLocation)
			{
				if (CurrentAlong >= 0.0f)
				{
					return IsInsideGate(CurrentLateral, CurrentLocalOffset.Z)
						? EGridCenterGateTraversalResult::Passed
						: EGridCenterGateTraversalResult::Missed;
				}
				return EGridCenterGateTraversalResult::Pending;
			}

			const FVector2D PreviousHorizontal(PreviousLocalOffset.X, PreviousLocalOffset.Y);
			const float PreviousAlong = FVector2D::DotProduct(PreviousHorizontal, NormalizedForward);
			const float PreviousLateral = FVector2D::DotProduct(PreviousHorizontal, Tangent);

			// Once the Pawn has been observed on the forward side, this gate is terminal:
			// it must either pass or fail, and can never become a target behind the Pawn.
			if (PreviousAlong > 0.0f)
			{
				return IsInsideGate(PreviousLateral, PreviousLocalOffset.Z)
					? EGridCenterGateTraversalResult::Passed
					: EGridCenterGateTraversalResult::Missed;
			}

			if (CurrentAlong < 0.0f)
			{
				return EGridCenterGateTraversalResult::Pending;
			}

			const float AlongDelta = CurrentAlong - PreviousAlong;
			if (AlongDelta <= UE_KINDA_SMALL_NUMBER)
			{
				return IsInsideGate(CurrentLateral, CurrentLocalOffset.Z)
					? EGridCenterGateTraversalResult::Passed
					: EGridCenterGateTraversalResult::Missed;
			}

			const float CrossingAlpha = FMath::Clamp(-PreviousAlong / AlongDelta, 0.0f, 1.0f);
			const float CrossingLateral = FMath::Lerp(PreviousLateral, CurrentLateral, CrossingAlpha);
			const float CrossingHeight = FMath::Lerp(PreviousLocalOffset.Z, CurrentLocalOffset.Z, CrossingAlpha);
			return IsInsideGate(CrossingLateral, CrossingHeight)
				? EGridCenterGateTraversalResult::Passed
				: EGridCenterGateTraversalResult::Missed;
		}
	};
}
