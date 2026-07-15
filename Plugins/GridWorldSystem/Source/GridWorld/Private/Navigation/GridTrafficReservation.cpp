// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridTrafficReservation.h"

namespace UE::GridWorld::Traffic
{
	/** Returns true when two upright agent height intervals overlap. */
	bool HeightIntervalsOverlap(float FirstMinimum, float FirstHeight, float SecondMinimum, float SecondHeight)
	{
		const float SafeFirstHeight = FMath::Max(0.0f, FirstHeight);
		const float SafeSecondHeight = FMath::Max(0.0f, SecondHeight);
		return FirstMinimum < SecondMinimum + SafeSecondHeight
			&& SecondMinimum < FirstMinimum + SafeFirstHeight;
	}

	/** Uses the larger authored separation so neither owner can weaken the other's clearance. */
	float RequiredHorizontalDistance(
		float FirstRadius,
		float FirstSeparation,
		float SecondRadius,
		float SecondSeparation)
	{
		return FMath::Max(0.0f, FirstRadius)
			+ FMath::Max(0.0f, SecondRadius)
			+ FMath::Max(FMath::Max(0.0f, FirstSeparation), FMath::Max(0.0f, SecondSeparation));
	}

	/** Flattens a world position for horizontal capsule-footprint comparisons. */
	FVector Flatten(const FVector& Point)
	{
		return FVector(Point.X, Point.Y, 0.0);
	}
}

bool FGridTrafficReservationSnapshot::ConflictsWithCell(
	const FVector& CandidateCenter,
	float CandidateRadius,
	float CandidateHeight,
	float CandidateSeparation,
	const FGuid& IgnoredOwnerId,
	FGuid* OutBlockingOwnerId,
	FGridCellId* OutBlockingCellId) const
{
	if (OutBlockingOwnerId != nullptr)
	{
		OutBlockingOwnerId->Invalidate();
	}
	if (OutBlockingCellId != nullptr)
	{
		*OutBlockingCellId = FGridCellId();
	}

	const FVector FlatCandidate = UE::GridWorld::Traffic::Flatten(CandidateCenter);
	for (const FGridTrafficReservedCell& Reserved : Cells)
	{
		if (!Reserved.OwnerId.IsValid() || Reserved.OwnerId == IgnoredOwnerId)
		{
			continue;
		}
		if (!UE::GridWorld::Traffic::HeightIntervalsOverlap(
			CandidateCenter.Z,
			CandidateHeight,
			Reserved.WorldCenter.Z,
			Reserved.AgentHeight))
		{
			continue;
		}

		const float RequiredDistance = UE::GridWorld::Traffic::RequiredHorizontalDistance(
			CandidateRadius,
			CandidateSeparation,
			Reserved.AgentRadius,
			Reserved.AdditionalSeparation);
		if (FVector::DistSquared(FlatCandidate, UE::GridWorld::Traffic::Flatten(Reserved.WorldCenter))
			>= FMath::Square(RequiredDistance))
		{
			continue;
		}

		if (OutBlockingOwnerId != nullptr)
		{
			*OutBlockingOwnerId = Reserved.OwnerId;
		}
		if (OutBlockingCellId != nullptr)
		{
			*OutBlockingCellId = Reserved.CellId;
		}
		return true;
	}

	for (const FGridTrafficReservedSegment& Reserved : Segments)
	{
		if (!Reserved.OwnerId.IsValid() || Reserved.OwnerId == IgnoredOwnerId)
		{
			continue;
		}
		const float ReservedMinimumZ = FMath::Min(Reserved.Start.Z, Reserved.End.Z);
		const float ReservedMaximumZ = FMath::Max(Reserved.Start.Z, Reserved.End.Z) + FMath::Max(0.0f, Reserved.AgentHeight);
		if (CandidateCenter.Z >= ReservedMaximumZ
			|| CandidateCenter.Z + FMath::Max(0.0f, CandidateHeight) <= ReservedMinimumZ)
		{
			continue;
		}

		const float RequiredDistance = UE::GridWorld::Traffic::RequiredHorizontalDistance(
			CandidateRadius,
			CandidateSeparation,
			Reserved.AgentRadius,
			Reserved.AdditionalSeparation);
		if (FMath::PointDistToSegmentSquared(
			FlatCandidate,
			UE::GridWorld::Traffic::Flatten(Reserved.Start),
			UE::GridWorld::Traffic::Flatten(Reserved.End)) >= FMath::Square(RequiredDistance))
		{
			continue;
		}

		if (OutBlockingOwnerId != nullptr)
		{
			*OutBlockingOwnerId = Reserved.OwnerId;
		}
		if (OutBlockingCellId != nullptr)
		{
			*OutBlockingCellId = Reserved.ToCell;
		}
		return true;
	}
	return false;
}

bool FGridTrafficReservationSnapshot::ConflictsWithSegment(
	const FVector& Start,
	const FVector& End,
	float CandidateRadius,
	float CandidateHeight,
	float CandidateSeparation,
	const FGuid& IgnoredOwnerId,
	FGuid* OutBlockingOwnerId) const
{
	if (OutBlockingOwnerId != nullptr)
	{
		OutBlockingOwnerId->Invalidate();
	}

	const float CandidateMinimumZ = FMath::Min(Start.Z, End.Z);
	const float CandidateMaximumZ = FMath::Max(Start.Z, End.Z) + FMath::Max(0.0f, CandidateHeight);
	const FVector FlatStart = UE::GridWorld::Traffic::Flatten(Start);
	const FVector FlatEnd = UE::GridWorld::Traffic::Flatten(End);

	for (const FGridTrafficReservedCell& Reserved : Cells)
	{
		if (!Reserved.OwnerId.IsValid() || Reserved.OwnerId == IgnoredOwnerId)
		{
			continue;
		}
		if (CandidateMinimumZ >= Reserved.WorldCenter.Z + FMath::Max(0.0f, Reserved.AgentHeight)
			|| CandidateMaximumZ <= Reserved.WorldCenter.Z)
		{
			continue;
		}

		const float RequiredDistance = UE::GridWorld::Traffic::RequiredHorizontalDistance(
			CandidateRadius,
			CandidateSeparation,
			Reserved.AgentRadius,
			Reserved.AdditionalSeparation);
		if (FMath::PointDistToSegmentSquared(
			UE::GridWorld::Traffic::Flatten(Reserved.WorldCenter),
			FlatStart,
			FlatEnd) >= FMath::Square(RequiredDistance))
		{
			continue;
		}

		if (OutBlockingOwnerId != nullptr)
		{
			*OutBlockingOwnerId = Reserved.OwnerId;
		}
		return true;
	}

	for (const FGridTrafficReservedSegment& Reserved : Segments)
	{
		if (!Reserved.OwnerId.IsValid() || Reserved.OwnerId == IgnoredOwnerId)
		{
			continue;
		}
		const float ReservedMinimumZ = FMath::Min(Reserved.Start.Z, Reserved.End.Z);
		const float ReservedMaximumZ = FMath::Max(Reserved.Start.Z, Reserved.End.Z) + FMath::Max(0.0f, Reserved.AgentHeight);
		if (CandidateMinimumZ >= ReservedMaximumZ || CandidateMaximumZ <= ReservedMinimumZ)
		{
			continue;
		}

		FVector CandidateClosest = FVector::ZeroVector;
		FVector ReservedClosest = FVector::ZeroVector;
		FMath::SegmentDistToSegmentSafe(
			FlatStart,
			FlatEnd,
			UE::GridWorld::Traffic::Flatten(Reserved.Start),
			UE::GridWorld::Traffic::Flatten(Reserved.End),
			CandidateClosest,
			ReservedClosest);
		const float RequiredDistance = UE::GridWorld::Traffic::RequiredHorizontalDistance(
			CandidateRadius,
			CandidateSeparation,
			Reserved.AgentRadius,
			Reserved.AdditionalSeparation);
		if (FVector::DistSquared(CandidateClosest, ReservedClosest) >= FMath::Square(RequiredDistance))
		{
			continue;
		}

		if (OutBlockingOwnerId != nullptr)
		{
			*OutBlockingOwnerId = Reserved.OwnerId;
		}
		return true;
	}
	return false;
}
