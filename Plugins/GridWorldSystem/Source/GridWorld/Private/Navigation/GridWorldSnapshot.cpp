// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridWorldSnapshot.h"

#include "Algo/Unique.h"

namespace UE::GridWorld::Private
{
	int32 FloorDivide(int32 Value, int32 Divisor)
	{
		check(Divisor > 0);
		const int32 Quotient = Value / Divisor;
		const int32 Remainder = Value % Divisor;
		return Remainder < 0 ? Quotient - 1 : Quotient;
	}
}

bool FGridWorldSnapshot::Finalize(FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Message)
	{
		if (OutError != nullptr)
		{
			*OutError = Message;
		}
		return false;
	};

	if (!GridId.IsValid())
	{
		return Fail(TEXT("GridId is invalid."));
	}
	if (!GridTransform.IsValid())
	{
		return Fail(TEXT("Grid transform or cell size is invalid."));
	}
	if (ChunkSize <= 0)
	{
		return Fail(TEXT("ChunkSize must be positive."));
	}
	if (Revisions.Topology <= 0 || static_cast<uint64>(Revisions.Topology) > MAX_uint32)
	{
		return Fail(TEXT("Topology generation must fit in a non-zero uint32."));
	}
	if (Cells.Num() >= static_cast<int64>(MAX_uint32))
	{
		return Fail(TEXT("The snapshot contains too many cells for NavNodeRef encoding."));
	}

	if (Regions.IsEmpty())
	{
		FGridRegionData Region;
		Region.GridId = GridId;
		Region.GridTransform = GridTransform;
		Regions.Add(GridId, Region);
	}

	CellIndexById.Reset();
	CellIndexById.Reserve(Cells.Num());
	Chunks.Reset();
	WorldBounds.Init();

	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		FGridCellData& Cell = Cells[CellIndex];
		if (!Cell.Id.GridId.IsValid())
		{
			Cell.Id.GridId = GridId;
		}
		FGridRegionData* Region = Regions.Find(Cell.Id.GridId);
		if (Region == nullptr)
		{
			return Fail(TEXT("A cell references an unknown grid region."));
		}
		if (CellIndexById.Contains(Cell.Id))
		{
			return Fail(TEXT("Duplicate persistent cell identity."));
		}
		if (!Cell.bHasAuthoredWorldCenter)
		{
			Cell.WorldCenter = Region->GridTransform.CellToWorld(Cell.Id.Coord);
		}
		if (Cell.FloorNormal.ContainsNaN() || Cell.FloorNormal.IsNearlyZero())
		{
			return Fail(TEXT("A cell has an invalid floor normal."));
		}
		Cell.FloorNormal = Cell.FloorNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		Cell.TraversalCost = FMath::Max(1, Cell.TraversalCost);
		CellIndexById.Add(Cell.Id, CellIndex);

		const FGridChunkCoord ChunkCoord = CellToChunk(Cell.Id.Coord, ChunkSize, Cell.Id.GridId);
		FGridChunkData& Chunk = Chunks.FindOrAdd(ChunkCoord);
		Chunk.Coord = ChunkCoord;
		Chunk.CellIndices.Add(CellIndex);
		const FVector HalfExtent(Region->GridTransform.CellSize.X * 0.5, Region->GridTransform.CellSize.Y * 0.5, Region->GridTransform.CellSize.Z * 0.5);
		const FBox CellBounds = FBox(-HalfExtent, HalfExtent).TransformBy(
			FTransform(Region->GridTransform.Rotation, Cell.WorldCenter).ToMatrixWithScale());
		Chunk.WorldBounds += CellBounds;
		Region->WorldBounds += CellBounds;
		WorldBounds += CellBounds;
	}

	for (FGridCellData& Cell : Cells)
	{
		Cell.Neighbors.Sort([this](int32 Left, int32 Right)
		{
			if (!Cells.IsValidIndex(Left))
			{
				return false;
			}
			if (!Cells.IsValidIndex(Right))
			{
				return true;
			}
			return Cells[Left].Id.Coord < Cells[Right].Id.Coord;
		});
		Cell.Neighbors.SetNum(Algo::Unique(Cell.Neighbors));
		for (const int32 NeighborIndex : Cell.Neighbors)
		{
			if (!Cells.IsValidIndex(NeighborIndex))
			{
				return Fail(TEXT("A cell references an invalid neighbor index."));
			}
		}
	}

	return true;
}

int32 FGridWorldSnapshot::FindCellIndex(const FGridCellCoord& Coord) const
{
	return FindCellIndex(GridId, Coord);
}

int32 FGridWorldSnapshot::FindCellIndex(const FGuid& InGridId, const FGridCellCoord& Coord) const
{
	FGridCellId CellId;
	CellId.GridId = InGridId;
	CellId.Coord = Coord;
	const int32* Index = CellIndexById.Find(CellId);
	return Index != nullptr ? *Index : INDEX_NONE;
}

const FGridCellData* FGridWorldSnapshot::FindCell(const FGridCellCoord& Coord) const
{
	const int32 Index = FindCellIndex(Coord);
	return Cells.IsValidIndex(Index) ? &Cells[Index] : nullptr;
}

const FGridCellData* FGridWorldSnapshot::FindCell(const FGridCellId& CellId) const
{
	const int32* Index = CellIndexById.Find(CellId);
	return Index != nullptr && Cells.IsValidIndex(*Index) ? &Cells[*Index] : nullptr;
}

NavNodeRef FGridWorldSnapshot::MakeNodeRef(int32 CellIndex) const
{
	return Cells.IsValidIndex(CellIndex)
		? EncodeNodeRef(static_cast<uint32>(Revisions.Topology), static_cast<uint32>(CellIndex))
		: INVALID_NAVNODEREF;
}

int32 FGridWorldSnapshot::ResolveNodeRef(NavNodeRef NodeRef) const
{
	uint32 Generation = 0;
	uint32 DenseIndex = 0;
	return DecodeNodeRef(NodeRef, Generation, DenseIndex)
		&& Generation == static_cast<uint32>(Revisions.Topology)
		&& Cells.IsValidIndex(static_cast<int32>(DenseIndex))
		? static_cast<int32>(DenseIndex)
		: INDEX_NONE;
}

NavNodeRef FGridWorldSnapshot::EncodeNodeRef(uint32 TopologyGeneration, uint32 DenseIndex)
{
	if (TopologyGeneration == 0 || DenseIndex == MAX_uint32)
	{
		return INVALID_NAVNODEREF;
	}
	return (static_cast<uint64>(TopologyGeneration) << 32) | static_cast<uint64>(DenseIndex + 1);
}

bool FGridWorldSnapshot::DecodeNodeRef(NavNodeRef NodeRef, uint32& OutTopologyGeneration, uint32& OutDenseIndex)
{
	OutTopologyGeneration = static_cast<uint32>(NodeRef >> 32);
	const uint32 EncodedIndex = static_cast<uint32>(NodeRef & MAX_uint32);
	if (OutTopologyGeneration == 0 || EncodedIndex == 0)
	{
		OutDenseIndex = 0;
		return false;
	}
	OutDenseIndex = EncodedIndex - 1;
	return true;
}

FGridChunkCoord FGridWorldSnapshot::CellToChunk(const FGridCellCoord& Coord, int32 InChunkSize, const FGuid& InGridId)
{
	const int32 SafeChunkSize = FMath::Max(1, InChunkSize);
	return FGridChunkCoord{
		InGridId,
		UE::GridWorld::Private::FloorDivide(Coord.X, SafeChunkSize),
		UE::GridWorld::Private::FloorDivide(Coord.Y, SafeChunkSize),
		Coord.Layer};
}
