// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::GridWorld::Private
{
	/**
	 * Computes one direct-velocity request without acceleration or overshoot on the final segment.
	 * @param CurrentLocation Current Pawn feet position.
	 * @param TargetLocation Active segment destination.
	 * @param DeltaTime Current frame duration in seconds.
	 * @param MaxSpeed Navigation movement speed cap in centimetres per second.
	 * @param bIsFinalSegment Limits displacement to TargetLocation / DeltaTime when true.
	 * @return World-space requested velocity.
	 */
	inline FVector CalculateDirectMoveVelocity(
		const FVector& CurrentLocation,
		const FVector& TargetLocation,
		float DeltaTime,
		float MaxSpeed,
		bool bIsFinalSegment)
	{
		const FVector TargetOffset = TargetLocation - CurrentLocation;
		const float ClampedMaxSpeed = FMath::Max(0.0f, MaxSpeed);
		if (bIsFinalSegment)
		{
			return DeltaTime > UE_SMALL_NUMBER
				? (TargetOffset / DeltaTime).GetClampedToMaxSize(ClampedMaxSpeed)
				: FVector::ZeroVector;
		}

		return TargetOffset.GetSafeNormal() * ClampedMaxSpeed;
	}
}
