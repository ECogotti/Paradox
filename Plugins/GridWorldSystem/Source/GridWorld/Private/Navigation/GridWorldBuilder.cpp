// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridWorldBuilder.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GridWorldModule.h"
#include "Navigation/GridNavigationBoundsVolume.h"
#include "Navigation/GridWalkingSurface.h"

TSharedPtr<FGridWorldSnapshot, ESPMode::ThreadSafe> FGridWorldBuilder::Build(UWorld& World, uint32 TopologyGeneration, TArray<FString>& OutErrors)
{
	check(IsInGameThread());
	OutErrors.Reset();
	TArray<AGridNavigationBoundsVolume*> Volumes;
	for (TActorIterator<AGridNavigationBoundsVolume> It(&World); It; ++It)
	{
		if (IsValid(*It) && !It->IsActorBeingDestroyed())
		{
			It->EnsureStableGridId();
			Volumes.Add(*It);
		}
	}
	Volumes.Sort([](const AGridNavigationBoundsVolume& Left, const AGridNavigationBoundsVolume& Right)
	{
		return Left.GridId < Right.GridId;
	});

	if (Volumes.IsEmpty())
	{
		OutErrors.Add(TEXT("No GridNavigationBoundsVolume exists in the world."));
		return nullptr;
	}
	if (!ValidateVolumes(Volumes, OutErrors))
	{
		return nullptr;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Snapshot->GridId = Volumes[0]->GridId;
	Snapshot->GridTransform = Volumes[0]->GetGridTransform();
	Snapshot->Revisions.Topology = FMath::Max<uint32>(1, TopologyGeneration);
	Snapshot->Revisions.Traversal = 1;
	Snapshot->Revisions.Occupancy = 1;
	Snapshot->ChunkSize = Volumes[0]->ChunkSize;
	uint32 AgentSettingsHash = 0;

	for (const AGridNavigationBoundsVolume* Volume : Volumes)
	{
		FGridRegionData Region;
		Region.GridId = Volume->GridId;
		Region.GridTransform = Volume->GetGridTransform();
		Region.WorldBounds = Volume->GetGridWorldBounds();
		Region.MovementMode = Volume->MovementMode;
		Region.bAllowCornerCutting = Volume->bAllowCornerCutting;
		Region.PathFollowingStyle = Volume->PathFollowingStyle;
		Region.PathDriveMode = Volume->PathDriveMode;
		Region.bUseAcceleratedFinalApproach = Volume->bUseAcceleratedFinalApproach;
		Region.CellCenterTolerance = Volume->CellCenterTolerance;
		Region.StopSpeedTolerance = Volume->StopSpeedTolerance;
		Region.MaxStepHeight = Volume->MaxStepHeight;
		Region.MaxDropHeight = Volume->MaxDropHeight;
		Snapshot->Regions.Add(Region.GridId, Region);
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->AgentRadius));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->AgentHeight));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->HorizontalCellSize));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->LayerHeight));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->MaxSlopeDegrees));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->MaxStepHeight));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->MaxDropHeight));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->PathFollowingStyle));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->PathDriveMode));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->bUseAcceleratedFinalApproach));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->CellCenterTolerance));
		AgentSettingsHash = HashCombineFast(AgentSettingsHash, GetTypeHash(Volume->StopSpeedTolerance));
		const int32 PreviousCellCount = Snapshot->Cells.Num();
		SampleVolume(World, *Volume, *Snapshot);
		if (Snapshot->Cells.Num() == PreviousCellCount)
		{
			OutErrors.Add(FString::Printf(
				TEXT("%s generated no navigable cells. Check bounds placement, collision profile '%s', floor slope, and agent clearance."),
				*GetNameSafe(Volume),
				*Volume->CollisionProfileName.ToString()));
		}
	}
	Snapshot->AgentSettingsHash = AgentSettingsHash;
	if (!OutErrors.IsEmpty())
	{
		return nullptr;
	}

	BuildAdjacency(*Snapshot);
	FString FinalizeError;
	if (!Snapshot->Finalize(&FinalizeError))
	{
		OutErrors.Add(FinalizeError);
		return nullptr;
	}
	return Snapshot;
}

bool FGridWorldBuilder::ValidateVolumes(const TArray<AGridNavigationBoundsVolume*>& Volumes, TArray<FString>& OutErrors)
{
	TSet<FGuid> SeenGridIds;
	for (int32 VolumeIndex = 0; VolumeIndex < Volumes.Num(); ++VolumeIndex)
	{
		const AGridNavigationBoundsVolume* Volume = Volumes[VolumeIndex];
		FString Error;
		if (!Volume->ValidateGridBounds(Error))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s"), *GetNameSafe(Volume), *Error));
		}
		if (SeenGridIds.Contains(Volume->GridId))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: duplicate GridId %s."), *GetNameSafe(Volume), *Volume->GridId.ToString()));
		}
		SeenGridIds.Add(Volume->GridId);

		for (int32 OtherIndex = 0; OtherIndex < VolumeIndex; ++OtherIndex)
		{
			if (Volume->GetGridWorldBounds().Intersect(Volumes[OtherIndex]->GetGridWorldBounds()))
			{
				OutErrors.Add(FString::Printf(
					TEXT("%s overlaps %s; ambiguous grid bounds are unsupported."),
					*GetNameSafe(Volume),
					*GetNameSafe(Volumes[OtherIndex])));
			}
		}
	}
	return OutErrors.IsEmpty();
}

void FGridWorldBuilder::SampleVolume(UWorld& World, const AGridNavigationBoundsVolume& Volume, FGridWorldSnapshot& Snapshot)
{
	const FGridTransform GridTransform = Volume.GetGridTransform();
	const FBox LocalBounds = Volume.GetLocalGridBounds();
	const int32 MinX = FMath::FloorToInt(LocalBounds.Min.X / GridTransform.CellSize.X);
	const int32 MaxX = FMath::CeilToInt(LocalBounds.Max.X / GridTransform.CellSize.X) - 1;
	const int32 MinY = FMath::FloorToInt(LocalBounds.Min.Y / GridTransform.CellSize.Y);
	const int32 MaxY = FMath::CeilToInt(LocalBounds.Max.Y / GridTransform.CellSize.Y) - 1;
	const double TraceTop = LocalBounds.Max.Z;
	const double TraceBottom = LocalBounds.Min.Z;
	const double HalfHeight = Volume.AgentHeight * 0.5;
	const FCollisionShape AgentShape = FCollisionShape::MakeCapsule(Volume.AgentRadius, HalfHeight);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GridWorldBuild), false, &Volume);
	QueryParams.bFindInitialOverlaps = true;
	// Pawns are navigation consumers, not authored topology. Baking a placed or runtime Pawn
	// into clearance would remove its own start cell every time navigation is rebuilt.
	for (TActorIterator<APawn> PawnIt(&World); PawnIt; ++PawnIt)
	{
		if (IsValid(*PawnIt) && !PawnIt->IsActorBeingDestroyed())
		{
			QueryParams.AddIgnoredActor(*PawnIt);
		}
	}

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const double LocalX = (static_cast<double>(X) + 0.5) * GridTransform.CellSize.X;
			const double LocalY = (static_cast<double>(Y) + 0.5) * GridTransform.CellSize.Y;
			double CurrentTop = TraceTop;
			const int32 MaxSurfaces = FMath::Max(1, FMath::CeilToInt((TraceTop - TraceBottom) / FMath::Max(1.0, Volume.LayerHeight)) + 4);
			TSet<int32> SeenLayers;
			for (int32 SurfaceIndex = 0; SurfaceIndex < MaxSurfaces && CurrentTop > TraceBottom; ++SurfaceIndex)
			{
				FHitResult Hit;
				const FVector Start = GridTransform.LocalToWorld(FVector(LocalX, LocalY, CurrentTop));
				const FVector End = GridTransform.LocalToWorld(FVector(LocalX, LocalY, TraceBottom));
				if (!World.LineTraceSingleByProfile(Hit, Start, End, Volume.CollisionProfileName, QueryParams))
				{
					break;
				}

				const FVector LocalImpactPoint = GridTransform.WorldToLocal(Hit.ImpactPoint);
				CurrentTop = LocalImpactPoint.Z - FMath::Max(5.0, Volume.AgentRadius * 0.25);
				const FVector FloorNormal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
				const double UpDot = FVector::DotProduct(FloorNormal, FVector::UpVector);
				if (UpDot + UE_KINDA_SMALL_NUMBER < FMath::Cos(FMath::DegreesToRadians(Volume.MaxSlopeDegrees)))
				{
					continue;
				}

				const int32 Layer = FMath::RoundToInt(LocalImpactPoint.Z / Volume.LayerHeight);
				if (SeenLayers.Contains(Layer))
				{
					continue;
				}
				SeenLayers.Add(Layer);
				const double CapsuleCenterHeight = UE::GridWorld::WalkingSurface::CalculateUprightCapsuleCenterHeight(
					HalfHeight,
					Volume.AgentRadius,
					UpDot);
				const FVector CapsuleCenter = Hit.ImpactPoint + FVector::UpVector * CapsuleCenterHeight;
				if (World.OverlapBlockingTestByProfile(CapsuleCenter, FQuat::Identity, Volume.CollisionProfileName, AgentShape, QueryParams))
				{
					continue;
				}

				FGridCellData& Cell = Snapshot.Cells.AddDefaulted_GetRef();
				Cell.Id.GridId = Volume.GridId;
				Cell.Id.Coord = FGridCellCoord(X, Y, Layer);
				Cell.WorldCenter = Hit.ImpactPoint;
				Cell.FloorNormal = FVector3f(FloorNormal);
				Cell.bHasAuthoredWorldCenter = true;
			}
		}
	}
}

void FGridWorldBuilder::BuildAdjacency(FGridWorldSnapshot& Snapshot)
{
	TMap<FGridCellId, int32> CellMap;
	CellMap.Reserve(Snapshot.Cells.Num());
	for (int32 CellIndex = 0; CellIndex < Snapshot.Cells.Num(); ++CellIndex)
	{
		CellMap.Add(Snapshot.Cells[CellIndex].Id, CellIndex);
	}

	for (int32 FromIndex = 0; FromIndex < Snapshot.Cells.Num(); ++FromIndex)
	{
		FGridCellData& From = Snapshot.Cells[FromIndex];
		const FGridRegionData* Region = Snapshot.Regions.Find(From.Id.GridId);
		if (Region == nullptr)
		{
			continue;
		}
		const int32 DirectionCount = Region->MovementMode == EGridMovementMode::EightDirections ? 8 : 4;
		static constexpr int32 DirectionX[8] = {1, 0, -1, 0, 1, -1, -1, 1};
		static constexpr int32 DirectionY[8] = {0, 1, 0, -1, 1, 1, -1, -1};
		for (int32 DirectionIndex = 0; DirectionIndex < DirectionCount; ++DirectionIndex)
		{
			int32 BestNeighborIndex = INDEX_NONE;
			double BestHeightDifference = TNumericLimits<double>::Max();
			for (int32 CandidateIndex = 0; CandidateIndex < Snapshot.Cells.Num(); ++CandidateIndex)
			{
				const FGridCellData& Candidate = Snapshot.Cells[CandidateIndex];
				if (Candidate.Id.GridId != From.Id.GridId
					|| Candidate.Id.Coord.X != From.Id.Coord.X + DirectionX[DirectionIndex]
					|| Candidate.Id.Coord.Y != From.Id.Coord.Y + DirectionY[DirectionIndex])
				{
					continue;
				}
			const double ResidualHeightDelta = UE::GridWorld::WalkingSurface::CalculateResidualHeightDelta(
				From.WorldCenter,
				From.FloorNormal,
				Candidate.WorldCenter,
				Candidate.FloorNormal);
			if (!UE::GridWorld::WalkingSurface::IsWalkingTransitionAllowed(
				From.WorldCenter,
				From.FloorNormal,
				Candidate.WorldCenter,
				Candidate.FloorNormal,
				Region->MaxStepHeight,
				Region->MaxDropHeight))
			{
				continue;
			}
			const double AbsoluteHeightDelta = FMath::Abs(ResidualHeightDelta);
				if (AbsoluteHeightDelta < BestHeightDifference
					|| (FMath::IsNearlyEqual(AbsoluteHeightDelta, BestHeightDifference)
						&& Candidate.Id.Coord.Layer < Snapshot.Cells[BestNeighborIndex].Id.Coord.Layer))
				{
					BestHeightDifference = AbsoluteHeightDelta;
					BestNeighborIndex = CandidateIndex;
				}
			}
			if (BestNeighborIndex != INDEX_NONE)
			{
				From.Neighbors.Add(BestNeighborIndex);
			}
		}
	}
}
