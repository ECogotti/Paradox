// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridWorldBuilder.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GridWorldModule.h"
#include "Navigation/GridNavigationBoundsVolume.h"
#include "Navigation/GridWalkingSurface.h"

namespace UE::GridWorld::Private
{
	constexpr double ClearanceLiftSampleSpacing = 5.0;
	constexpr int32 MaximumClearanceLiftSamples = 16;
}

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

void FGridWorldBuilder::AddNavigationIrrelevantComponentsToQuery(
	UWorld& World,
	FCollisionQueryParams& QueryParams)
{
	for (TActorIterator<AActor> ActorIt(&World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
		{
			continue;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (IsValid(PrimitiveComponent) && !PrimitiveComponent->CanEverAffectNavigation())
			{
				QueryParams.AddIgnoredComponent(PrimitiveComponent);
			}
		}
	}
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
	// GridWorld uses physical collision queries for topology generation, so mirror Unreal's
	// navigation-relevance contract explicitly instead of baking opt-out components as floors
	// or permanent clearance obstacles.
	AddNavigationIrrelevantComponentsToQuery(World, QueryParams);

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const double LocalX = (static_cast<double>(X) + 0.5) * GridTransform.CellSize.X;
			const double LocalY = (static_cast<double>(Y) + 0.5) * GridTransform.CellSize.Y;
			TArray<FHitResult> SurfaceHits;
			GatherSurfaceHits(
				World,
				GridTransform,
				LocalX,
				LocalY,
				TraceTop,
				TraceBottom,
				Volume.LayerHeight,
				Volume.MaxSlopeDegrees,
				Volume.AgentRadius,
				Volume.CollisionProfileName,
				QueryParams,
				SurfaceHits);
			for (const FHitResult& Hit : SurfaceHits)
			{
				const FVector LocalImpactPoint = GridTransform.WorldToLocal(Hit.ImpactPoint);
				const FVector FloorNormal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
				const double UpDot = FVector::DotProduct(FloorNormal, FVector::UpVector);
				const int32 Layer = FMath::RoundToInt(LocalImpactPoint.Z / Volume.LayerHeight);
				if (!HasAgentClearance(
					World,
					Hit.ImpactPoint,
					UpDot,
					Volume.AgentRadius,
					HalfHeight,
					Volume.MaxStepHeight,
					Volume.CollisionProfileName,
					QueryParams))
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

void FGridWorldBuilder::GatherSurfaceHits(
	UWorld& World,
	const FGridTransform& GridTransform,
	double LocalX,
	double LocalY,
	double TraceTop,
	double TraceBottom,
	double LayerHeight,
	double MaxSlopeDegrees,
	double AgentRadius,
	FName CollisionProfileName,
	const FCollisionQueryParams& QueryParams,
	TArray<FHitResult>& OutHits)
{
	OutHits.Reset();
	FCollisionQueryParams FloorQueryParams(QueryParams);
	FloorQueryParams.bFindInitialOverlaps = false;

	double CurrentTop = TraceTop;
	const double SurfaceAdvance = FMath::Max(5.0, AgentRadius * 0.25);
	const int32 MaxTraceIterations = FMath::Max(
		1,
		FMath::CeilToInt((TraceTop - TraceBottom) / SurfaceAdvance) + 1);
	TSet<int32> SeenLayers;
	for (int32 TraceIndex = 0; TraceIndex < MaxTraceIterations && CurrentTop > TraceBottom; ++TraceIndex)
	{
		FHitResult Hit;
		const FVector Start = GridTransform.LocalToWorld(FVector(LocalX, LocalY, CurrentTop));
		const FVector End = GridTransform.LocalToWorld(FVector(LocalX, LocalY, TraceBottom));
		if (!World.LineTraceSingleByProfile(Hit, Start, End, CollisionProfileName, FloorQueryParams))
		{
			break;
		}

		const FVector LocalImpactPoint = GridTransform.WorldToLocal(Hit.ImpactPoint);
		CurrentTop = LocalImpactPoint.Z - SurfaceAdvance;
		if (Hit.bStartPenetrating)
		{
			continue;
		}
		const FVector FloorNormal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const double UpDot = FVector::DotProduct(FloorNormal, FVector::UpVector);
		if (UpDot + UE_KINDA_SMALL_NUMBER < FMath::Cos(FMath::DegreesToRadians(MaxSlopeDegrees)))
		{
			continue;
		}

		const int32 Layer = FMath::RoundToInt(LocalImpactPoint.Z / LayerHeight);
		if (SeenLayers.Contains(Layer))
		{
			continue;
		}
		SeenLayers.Add(Layer);
		OutHits.Add(MoveTemp(Hit));
	}
}

bool FGridWorldBuilder::HasAgentClearance(
	UWorld& World,
	const FVector& FloorLocation,
	double FloorUpDot,
	double AgentRadius,
	double AgentHalfHeight,
	double MaxStepHeight,
	FName CollisionProfileName,
	const FCollisionQueryParams& QueryParams)
{
	const double SafeRadius = FMath::Max(0.0, AgentRadius);
	const double SafeHalfHeight = FMath::Max(AgentHalfHeight, SafeRadius);
	const FCollisionShape AgentShape = FCollisionShape::MakeCapsule(SafeRadius, SafeHalfHeight);
	const double CapsuleCenterHeight = UE::GridWorld::WalkingSurface::CalculateUprightCapsuleCenterHeight(
		SafeHalfHeight,
		SafeRadius,
		FloorUpDot);
	const FVector BaseCapsuleCenter = FloorLocation + FVector::UpVector * CapsuleCenterHeight;
	auto IsBlockedAtLift = [&World, &QueryParams, CollisionProfileName, &AgentShape, &BaseCapsuleCenter](double Lift)
	{
		return World.OverlapBlockingTestByProfile(
			BaseCapsuleCenter + FVector::UpVector * Lift,
			FQuat::Identity,
			CollisionProfileName,
			AgentShape,
			QueryParams);
	};

	if (!IsBlockedAtLift(0.0))
	{
		return true;
	}

	const double SafeMaxStepHeight = FMath::Max(0.0, MaxStepHeight);
	if (SafeMaxStepHeight <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Most low obstacles clear at the maximum legal step, so test that endpoint first.
	if (!IsBlockedAtLift(SafeMaxStepHeight))
	{
		return true;
	}

	// A low ceiling may block the maximum lift even though a smaller legal lift clears the step.
	const int32 LiftSampleCount = FMath::Clamp(
		FMath::CeilToInt(SafeMaxStepHeight / UE::GridWorld::Private::ClearanceLiftSampleSpacing),
		2,
		UE::GridWorld::Private::MaximumClearanceLiftSamples);
	for (int32 LiftSampleIndex = 1; LiftSampleIndex < LiftSampleCount; ++LiftSampleIndex)
	{
		const double Lift = SafeMaxStepHeight * static_cast<double>(LiftSampleIndex) / static_cast<double>(LiftSampleCount);
		if (!IsBlockedAtLift(Lift))
		{
			return true;
		}
	}
	return false;
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
