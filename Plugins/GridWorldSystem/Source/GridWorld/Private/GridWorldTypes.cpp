// Copyright Epic Games, Inc. All Rights Reserved.

#include "GridWorldTypes.h"

bool FGridTransform::IsValid() const
{
	return !Origin.ContainsNaN()
		&& !Rotation.ContainsNaN()
		&& !CellSize.ContainsNaN()
		&& CellSize.X > UE_SMALL_NUMBER
		&& CellSize.Y > UE_SMALL_NUMBER
		&& CellSize.Z > UE_SMALL_NUMBER;
}

FVector FGridTransform::CellToWorld(const FGridCellCoord& Coord) const
{
	const FVector LocalCenter(
		(static_cast<double>(Coord.X) + 0.5) * CellSize.X,
		(static_cast<double>(Coord.Y) + 0.5) * CellSize.Y,
		static_cast<double>(Coord.Layer) * CellSize.Z);
	return LocalToWorld(LocalCenter);
}

FGridCellCoord FGridTransform::WorldToCell(const FVector& WorldLocation) const
{
	if (!IsValid() || WorldLocation.ContainsNaN())
	{
		return FGridCellCoord();
	}

	const FVector Local = WorldToLocal(WorldLocation);
	return FGridCellCoord(
		FMath::FloorToInt(Local.X / CellSize.X),
		FMath::FloorToInt(Local.Y / CellSize.Y),
		FMath::RoundToInt(Local.Z / CellSize.Z));
}

FVector FGridTransform::WorldToLocal(const FVector& WorldLocation) const
{
	return Rotation.UnrotateVector(WorldLocation - Origin);
}

FVector FGridTransform::LocalToWorld(const FVector& LocalLocation) const
{
	return Origin + Rotation.RotateVector(LocalLocation);
}
