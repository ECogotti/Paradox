// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridNavigationData.h"

#include "Debug/GridNavigationRenderingComponent.h"
#include "GridWorldModule.h"
#include "Navigation/GridAStar.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridNavigationQueryFilter.h"
#include "Navigation/GridTrafficReservationManager.h"
#include "Navigation/GridNavigationBoundsVolume.h"
#include "Navigation/GridDirtyAreaPolicy.h"
#include "Navigation/GridNavDataGenerator.h"
#include "Navigation/GridWorldBuilder.h"
#include "Navigation/GridWalkingSurface.h"
#include "Navigation/GridOverlayComposer.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "AI/Navigation/NavigationDirtyArea.h"
#include "AIController.h"
#include "AI/GridWorldPathFollowingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"

namespace UE::GridWorld::Serialization
{
	static constexpr uint32 Magic = 0x47575244; // GWRD
	static constexpr int32 CurrentVersion = 7;

	bool CanConsumeVersion(int32 Version)
	{
		return Version >= 2 && Version <= CurrentVersion;
	}

	bool CanPublishVersion(int32 Version)
	{
		return Version == CurrentVersion;
	}

	void SerializeCoord(FArchive& Ar, FGridCellCoord& Coord)
	{
		Ar << Coord.X << Coord.Y << Coord.Layer;
	}

	void SerializeTransform(FArchive& Ar, FGridTransform& Transform)
	{
		Ar << Transform.Origin << Transform.Rotation << Transform.CellSize;
	}

	void SerializeSnapshot(FArchive& Ar, FGridWorldSnapshot& Snapshot, int32 Version)
	{
		Ar << Snapshot.GridId;
		SerializeTransform(Ar, Snapshot.GridTransform);
		Ar << Snapshot.Revisions.Topology << Snapshot.ChunkSize << Snapshot.AgentSettingsHash;

		int32 RegionCount = Snapshot.Regions.Num();
		Ar << RegionCount;
		if (Ar.IsLoading())
		{
			Snapshot.Regions.Reset();
		}
		if (Ar.IsSaving())
		{
			TArray<FGuid> RegionIds;
			Snapshot.Regions.GenerateKeyArray(RegionIds);
			RegionIds.Sort();
			for (const FGuid& RegionId : RegionIds)
			{
				FGridRegionData Region = Snapshot.Regions[RegionId];
				Ar << Region.GridId;
				SerializeTransform(Ar, Region.GridTransform);
				Ar << Region.WorldBounds << Region.MovementMode << Region.bAllowCornerCutting;
				if (Version >= 3)
				{
					Ar << Region.PathFollowingStyle << Region.CellCenterTolerance << Region.StopSpeedTolerance;
				}
				if (Version >= 4)
				{
					Ar << Region.PathDriveMode << Region.bUseAcceleratedFinalApproach;
				}
				Ar << Region.MaxStepHeight << Region.MaxDropHeight;
			}
		}
		else
		{
			for (int32 RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
			{
				FGridRegionData Region;
				Ar << Region.GridId;
				SerializeTransform(Ar, Region.GridTransform);
				Ar << Region.WorldBounds << Region.MovementMode << Region.bAllowCornerCutting;
				if (Version >= 3)
				{
					Ar << Region.PathFollowingStyle << Region.CellCenterTolerance << Region.StopSpeedTolerance;
				}
				if (Version >= 4)
				{
					Ar << Region.PathDriveMode << Region.bUseAcceleratedFinalApproach;
				}
				Ar << Region.MaxStepHeight << Region.MaxDropHeight;
				Snapshot.Regions.Add(Region.GridId, Region);
			}
		}

		int32 CellCount = Snapshot.Cells.Num();
		Ar << CellCount;
		if (Ar.IsLoading())
		{
			Snapshot.Cells.SetNum(CellCount);
		}
		for (FGridCellData& Cell : Snapshot.Cells)
		{
			Ar << Cell.Id.GridId;
			SerializeCoord(Ar, Cell.Id.Coord);
			Ar << Cell.WorldCenter;
			if (Version >= 5)
			{
				Ar << Cell.FloorNormal.X << Cell.FloorNormal.Y << Cell.FloorNormal.Z;
			}
			Ar << Cell.TraversalCost << Cell.AreaId << Cell.TraversalFlags << Cell.TraversalChannels;
			Ar << Cell.bWalkable << Cell.bAuthoredBlocked << Cell.bAuthoredBlockCanBeRemoved << Cell.bHasAuthoredWorldCenter;
			int32 NeighborCount = Cell.Neighbors.Num();
			Ar << NeighborCount;
			if (Ar.IsLoading())
			{
				Cell.Neighbors.SetNum(NeighborCount);
			}
			for (int32& NeighborIndex : Cell.Neighbors)
			{
				Ar << NeighborIndex;
			}
		}

		int32 ChunkCount = Snapshot.Chunks.Num();
		Ar << ChunkCount;
		if (Ar.IsSaving())
		{
			TArray<FGridChunkCoord> ChunkCoords;
			Snapshot.Chunks.GenerateKeyArray(ChunkCoords);
			ChunkCoords.Sort();
			for (const FGridChunkCoord& ChunkCoord : ChunkCoords)
			{
				FGridChunkCoord Coord = ChunkCoord;
				Ar << Coord.GridId << Coord.X << Coord.Y << Coord.Layer;
				TArray<int32> CellIndices = Snapshot.Chunks[ChunkCoord].CellIndices;
				Ar << CellIndices;
			}
		}
		else
		{
			for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
			{
				FGridChunkCoord Coord;
				TArray<int32> IgnoredCellIndices;
				Ar << Coord.GridId << Coord.X << Coord.Y << Coord.Layer << IgnoredCellIndices;
			}
		}
	}
}

namespace UE::GridWorld::Private
{
	const FGridNavigationQueryFilterImpl* GetGridFilter(const FSharedConstNavQueryFilter& Filter)
	{
		return Filter.IsValid() ? static_cast<const FGridNavigationQueryFilterImpl*>(Filter->GetImplementation()) : nullptr;
	}

	FGridAStarQuery MakeInjectedPathValidationQuery(
		const FGridAStarQuery& Query,
		bool bAllowDynamicAgentConflictsDuringValidation)
	{
		FGridAStarQuery ValidationQuery = Query;
		if (bAllowDynamicAgentConflictsDuringValidation)
		{
			ValidationQuery.DynamicAgentPolicy = EGridDynamicAgentPolicy::Ignore;
			ValidationQuery.TrafficReservations.Reset();
		}
		return ValidationQuery;
	}

	bool PassesFilter(const FGridCellData& Cell, const FSharedConstNavQueryFilter& Filter)
	{
		if (!Cell.bWalkable)
		{
			return false;
		}
		if (!Filter.IsValid())
		{
			return true;
		}
		if ((Cell.TraversalFlags & Filter->GetExcludeFlags()) != 0 || (Cell.TraversalFlags & Filter->GetIncludeFlags()) == 0)
		{
			return false;
		}
		const FGridNavigationQueryFilterImpl* GridFilter = GetGridFilter(Filter);
		if (GridFilter == nullptr)
		{
			return true;
		}
		const uint16 ChannelMask = static_cast<uint16>(1u << FMath::Min<uint8>(GridFilter->GetTraversalChannel(), 15));
		return GridFilter->GetAreaCost(Cell.AreaId) < BIG_NUMBER * 0.5f
			&& (Cell.TraversalChannels & ChannelMask) != 0;
	}

	bool FindProjectedCell(const FGridWorldSnapshot& Snapshot, const FVector& Location, int32& OutCellIndex, const FSharedConstNavQueryFilter& Filter = nullptr)
	{
		OutCellIndex = INDEX_NONE;
		double BestDistanceSquared = TNumericLimits<double>::Max();
		for (int32 CellIndex = 0; CellIndex < Snapshot.Cells.Num(); ++CellIndex)
		{
			const FGridCellData& Cell = Snapshot.Cells[CellIndex];
			const FGridRegionData* Region = Snapshot.FindRegion(Cell.Id.GridId);
			if (!PassesFilter(Cell, Filter) || Region == nullptr)
			{
				continue;
			}
			const FVector LocalDelta = Region->GridTransform.WorldToLocal(Location) - Region->GridTransform.WorldToLocal(Cell.WorldCenter);
			const FVector HalfExtent = Region->GridTransform.CellSize * 0.5;
			if (FMath::Abs(LocalDelta.X) > HalfExtent.X || FMath::Abs(LocalDelta.Y) > HalfExtent.Y || FMath::Abs(LocalDelta.Z) > HalfExtent.Z)
			{
				continue;
			}
			const double DistanceSquared = FVector::DistSquared(Location, Cell.WorldCenter);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				OutCellIndex = CellIndex;
			}
		}
		return OutCellIndex != INDEX_NONE;
	}

	bool AreCorridorNeighbors(const FGridWorldSnapshot& Snapshot, int32 FromIndex, int32 ToIndex, const FGridNavigationQueryFilterImpl* GridFilter)
	{
		if (FromIndex == ToIndex)
		{
			return true;
		}
		if (Snapshot.Cells[FromIndex].Neighbors.Contains(ToIndex))
		{
			return true;
		}
		if (GridFilter != nullptr && !GridFilter->AllowsLinks())
		{
			return false;
		}
		for (const FGridLinkData& Link : Snapshot.Links)
		{
			const uint16 ChannelMask = GridFilter != nullptr
				? static_cast<uint16>(1u << FMath::Min<uint8>(GridFilter->GetTraversalChannel(), 15))
				: 1u;
			if (Link.bEnabled && (Link.TraversalChannels & ChannelMask) != 0 && ((Link.FromCellIndex == FromIndex && Link.ToCellIndex == ToIndex)
				|| (Link.bBidirectional && Link.ToCellIndex == FromIndex && Link.FromCellIndex == ToIndex)))
			{
				return true;
			}
		}
		return false;
	}

	int32 AddPathPointIfDistinct(FGridNavigationPath& Path, const FVector& Location, NavNodeRef NodeRef)
	{
		TArray<FNavPathPoint>& Points = Path.GetPathPoints();
		if (Points.IsEmpty() || !Points.Last().Location.Equals(Location, UE_KINDA_SMALL_NUMBER))
		{
			Points.Emplace(Location, NodeRef);
		}
		return Points.Num() - 1;
	}

	bool IsPathTurn(
		TConstArrayView<FNavPathPoint> Points,
		int32 PointIndex,
		const FRotator& GridRotation)
	{
		if (!Points.IsValidIndex(PointIndex - 1) || !Points.IsValidIndex(PointIndex + 1))
		{
			return false;
		}

		const FQuat WorldToGrid = GridRotation.Quaternion().Inverse();
		const FVector LocalIncoming3D = WorldToGrid.RotateVector(Points[PointIndex].Location - Points[PointIndex - 1].Location);
		const FVector LocalOutgoing3D = WorldToGrid.RotateVector(Points[PointIndex + 1].Location - Points[PointIndex].Location);
		const FVector2D Incoming = FVector2D(LocalIncoming3D.X, LocalIncoming3D.Y).GetSafeNormal();
		const FVector2D Outgoing = FVector2D(LocalOutgoing3D.X, LocalOutgoing3D.Y).GetSafeNormal();
		return Incoming.IsNearlyZero()
			|| Outgoing.IsNearlyZero()
			|| FVector2D::DotProduct(Incoming, Outgoing) < 1.0f - UE_KINDA_SMALL_NUMBER;
	}

	bool LessCellId(const FGridCellId& Left, const FGridCellId& Right)
	{
		return Left.GridId != Right.GridId ? Left.GridId < Right.GridId : Left.Coord < Right.Coord;
	}

	FGridChangeSet MakeStructuralChangeSet(
		const FGridWorldSnapshot* PreviousSnapshot,
		const FGridWorldSnapshot* NewSnapshot)
	{
		FGridChangeSet ChangeSet;
		ChangeSet.PreviousRevisions = PreviousSnapshot != nullptr ? PreviousSnapshot->Revisions : FGridRevisionSet();
		ChangeSet.NewRevisions = NewSnapshot != nullptr ? NewSnapshot->Revisions : FGridRevisionSet();

		TSet<FGridCellId> ChangedCellSet;
		TSet<FGuid> ChangedLinkSet;
		auto AddSnapshotState = [&ChangedCellSet, &ChangedLinkSet](const FGridWorldSnapshot* Snapshot)
		{
			if (Snapshot == nullptr)
			{
				return;
			}
			for (const FGridCellData& Cell : Snapshot->Cells)
			{
				ChangedCellSet.Add(Cell.Id);
			}
			for (const FGridLinkData& Link : Snapshot->Links)
			{
				ChangedLinkSet.Add(Link.LinkId);
			}
		};
		AddSnapshotState(PreviousSnapshot);
		AddSnapshotState(NewSnapshot);
		ChangeSet.ChangedCells = ChangedCellSet.Array();
		ChangeSet.ChangedCells.Sort(LessCellId);
		ChangeSet.ChangedTraversalCells = ChangeSet.ChangedCells;
		ChangeSet.ChangedBlockingOccupancyCells = ChangeSet.ChangedCells;
		ChangeSet.ChangedOccupancyCostCells = ChangeSet.ChangedCells;
		ChangeSet.ChangedOccupancyOwnerCells = ChangeSet.ChangedCells;
		ChangeSet.ChangedLinks = ChangedLinkSet.Array();
		ChangeSet.ChangedLinks.Sort();
		return ChangeSet;
	}

}

AGridNavigationData::AGridNavigationData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RuntimeGeneration = ERuntimeGenerationType::Dynamic;
	DefaultQueryFilter->SetFilterType<FGridNavigationQueryFilterImpl>();

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RuntimeNavigationDataId = FGuid::NewGuid();
		TrafficReservationManager = MakeShared<FGridTrafficReservationManager>();
		FindPathImplementation = FindPath;
		FindHierarchicalPathImplementation = FindPath;
		TestPathImplementation = TestPath;
		TestHierarchicalPathImplementation = TestPath;
		RaycastImplementationWithAdditionalResults = NavigationRaycast;
	}
}

void AGridNavigationData::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TrafficReservationManager.IsValid())
	{
		TrafficReservationManager->Reset();
	}
	TrafficReservationsChanged.Clear();
	ClearActiveDebugPaths();
	{
		FWriteScopeLock Lock(DebugDataLock);
		ActiveAgentAvoidanceDebugData.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AGridNavigationData::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	if (HasAnyFlags(RF_ClassDefaultObject) || (!Ar.IsSaving() && !Ar.IsLoading()))
	{
		return;
	}

	uint32 Magic = UE::GridWorld::Serialization::Magic;
	int32 Version = UE::GridWorld::Serialization::CurrentVersion;
	bool bHasSnapshot = Ar.IsSaving() && BaseTopologySnapshot.IsValid();
	Ar << Magic << Version << bHasSnapshot;
	const bool bCanConsumeVersion = Magic == UE::GridWorld::Serialization::Magic
		&& UE::GridWorld::Serialization::CanConsumeVersion(Version);
	if (Ar.IsLoading() && !bCanConsumeVersion)
	{
		GRIDWORLD_LOG_ERROR("Rejected serialized GridWorld data on '%s': magic 0x%08x, version %d.", *GetNameSafe(this), Magic, Version);
		return;
	}
	if (!bHasSnapshot)
	{
		return;
	}

	if (Ar.IsSaving())
	{
		FGridWorldSnapshot SnapshotCopy = *BaseTopologySnapshot;
		UE::GridWorld::Serialization::SerializeSnapshot(Ar, SnapshotCopy, UE::GridWorld::Serialization::CurrentVersion);
	}
	else
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> LoadedSnapshot = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		UE::GridWorld::Serialization::SerializeSnapshot(Ar, *LoadedSnapshot, Version);
		if (!UE::GridWorld::Serialization::CanPublishVersion(Version))
		{
			GRIDWORLD_LOG_ERROR(
				"Rejected serialized GridWorld data on '%s': version %d is incompatible with the current generation rules and must be rebuilt for version %d.",
				*GetNameSafe(this),
				Version,
				UE::GridWorld::Serialization::CurrentVersion);
			return;
		}
		LoadedSnapshot->Revisions.Traversal = 1;
		LoadedSnapshot->Revisions.Occupancy = 1;
		FString Error;
		if (LoadedSnapshot->Finalize(&Error))
		{
			FWriteScopeLock Lock(SnapshotLock);
			BaseTopologySnapshot = StaticCastSharedRef<const FGridWorldSnapshot>(LoadedSnapshot);
			PublishedSnapshot = BaseTopologySnapshot;
		}
		else
		{
			GRIDWORLD_LOG_ERROR("Rejected serialized GridWorld topology on '%s': %s", *GetNameSafe(this), *Error);
		}
	}
}

FBox AGridNavigationData::GetBounds() const
{
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	return Snapshot.IsValid() ? Snapshot->WorldBounds : FBox(ForceInit);
}

bool AGridNavigationData::SupportsRuntimeGeneration() const
{
	return RuntimeGeneration != ERuntimeGenerationType::Static || RequiresInitialRebuild();
}

bool AGridNavigationData::IsNodeRefValid(NavNodeRef NodeRef) const
{
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	return Snapshot.IsValid() && Snapshot->ResolveNodeRef(NodeRef) != INDEX_NONE;
}

bool AGridNavigationData::PublishSnapshot(TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> NewSnapshot, FString* OutError)
{
	if (!NewSnapshot->Finalize(OutError))
	{
		GRIDWORLD_LOG_ERROR("Rejected invalid GridWorld snapshot for '%s': %s", *GetNameSafe(this), OutError != nullptr ? **OutError : TEXT("validation failed"));
		return false;
	}

	const FGridWorldSnapshotPtr PreviousSnapshot = GetSnapshot();
	const FGridChangeSet ChangeSet = UE::GridWorld::Private::MakeStructuralChangeSet(PreviousSnapshot.Get(), &NewSnapshot.Get());
	{
		FWriteScopeLock Lock(SnapshotLock);
		BaseTopologySnapshot = StaticCastSharedRef<const FGridWorldSnapshot>(NewSnapshot);
		PublishedSnapshot = BaseTopologySnapshot;
		LastChangeSet = ChangeSet;
	}
	MarkGeneratedDataPackageDirty();
	GRIDWORLD_LOG_INFO("Published GridWorld snapshot for '%s': %d cells, topology %lld.", *GetNameSafe(this), NewSnapshot->Cells.Num(), NewSnapshot->Revisions.Topology);
	if (RenderingComp != nullptr)
	{
		RenderingComp->MarkRenderStateDirty();
	}
	if (UWorld* World = GetWorld())
	{
		if (UGridWorldSubsystem* Subsystem = World->GetSubsystem<UGridWorldSubsystem>())
		{
			Subsystem->OnGridWorldChanged.Broadcast(ChangeSet);
		}
	}
	return true;
}

FGridWorldSnapshotPtr AGridNavigationData::GetSnapshot() const
{
	FReadScopeLock Lock(SnapshotLock);
	return PublishedSnapshot;
}

FGridRevisionSet AGridNavigationData::GetPublishedRevisions() const
{
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	return Snapshot.IsValid() ? Snapshot->Revisions : FGridRevisionSet();
}

void AGridNavigationData::ConditionalConstructGenerator()
{
	if (!NavDataGenerator.IsValid() && SupportsRuntimeGeneration())
	{
		NavDataGenerator = MakeShared<FGridNavDataGenerator, ESPMode::ThreadSafe>(*this);
	}
}

bool AGridNavigationData::NeedsRebuild() const
{
	return !GetSnapshot().IsValid();
}

bool AGridNavigationData::BuildFromWorld()
{
	if (!IsInGameThread())
	{
		GRIDWORLD_LOG_ERROR("BuildFromWorld must execute on the Game Thread for collision sampling.");
		return false;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		GRIDWORLD_LOG_ERROR("Cannot build GridWorld navigation for '%s' without a world.", *GetNameSafe(this));
		return false;
	}

	const FGridRevisionSet PreviousRevisions = GetPublishedRevisions();
	const uint32 NextTopology = PreviousRevisions.Topology <= 0
		? 1u
		: static_cast<uint32>(FMath::Min<int64>(PreviousRevisions.Topology + 1, MAX_uint32));
	TSharedPtr<FGridWorldSnapshot, ESPMode::ThreadSafe> NewSnapshot = FGridWorldBuilder::Build(*World, NextTopology, LastValidationErrors);
	if (!NewSnapshot.IsValid())
	{
		for (const FString& Error : LastValidationErrors)
		{
			GRIDWORLD_LOG_ERROR("Grid build validation failed: %s", *Error);
		}
		if (RenderingComp != nullptr)
		{
			RenderingComp->MarkRenderStateDirty();
		}
		return false;
	}
	if (!PublishSnapshot(NewSnapshot.ToSharedRef()))
	{
		return false;
	}
	RefreshRuntimeOverlay(false);
	return true;
}

bool AGridNavigationData::BuildDirtyAreas(const TArray<FNavigationDirtyArea>& DirtyAreas, bool bRespectGeometryAutoRebuild)
{
	if (!IsInGameThread() || DirtyAreas.IsEmpty())
	{
		return false;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	LastDirtyChunks.Reset();
	TSet<FGuid> AutoRebuildGeometryGridIds;
	TArray<TPair<FBox, FVector>> AutoRebuildGeometryBounds;
	if (bRespectGeometryAutoRebuild)
	{
		for (TActorIterator<AGridNavigationBoundsVolume> It(World); It; ++It)
		{
			if (!It->bAutoRebuildOnGeometryChanges || !It->GridId.IsValid())
			{
				continue;
			}
			AutoRebuildGeometryGridIds.Add(It->GridId);
			const double MaxCellExtent = FMath::Max3(It->HorizontalCellSize.X, It->HorizontalCellSize.Y, It->LayerHeight);
			AutoRebuildGeometryBounds.Emplace(It->GetGridWorldBounds(), FVector(MaxCellExtent));
		}
	}

	TArray<const FNavigationDirtyArea*> EffectiveDirtyAreas;
	EffectiveDirtyAreas.Reserve(DirtyAreas.Num());
	for (const FNavigationDirtyArea& DirtyArea : DirtyAreas)
	{
		if (!UE::GridWorld::Private::IsAutomaticGeometryChange(DirtyArea, bRespectGeometryAutoRebuild))
		{
			EffectiveDirtyAreas.Add(&DirtyArea);
			continue;
		}
		const bool bTouchesEnabledGrid = AutoRebuildGeometryBounds.ContainsByPredicate([&DirtyArea](const TPair<FBox, FVector>& BoundsAndExpansion)
		{
			return BoundsAndExpansion.Key.IsValid
				&& BoundsAndExpansion.Key.ExpandBy(BoundsAndExpansion.Value).Intersect(DirtyArea.Bounds);
		});
		if (bTouchesEnabledGrid)
		{
			EffectiveDirtyAreas.Add(&DirtyArea);
		}
	}
	if (EffectiveDirtyAreas.IsEmpty())
	{
		return true;
	}

	FGridWorldSnapshotPtr PreviousTopology;
	{
		FReadScopeLock Lock(SnapshotLock);
		PreviousTopology = BaseTopologySnapshot;
	}
	if (!PreviousTopology.IsValid())
	{
		return BuildFromWorld();
	}

	const uint32 NextTopology = static_cast<uint32>(FMath::Min<int64>(PreviousTopology->Revisions.Topology + 1, MAX_uint32));
	TSharedPtr<FGridWorldSnapshot, ESPMode::ThreadSafe> Candidate = FGridWorldBuilder::Build(*World, NextTopology, LastValidationErrors);
	if (!Candidate.IsValid())
	{
		return false;
	}

	for (const FNavigationDirtyArea* DirtyAreaPtr : EffectiveDirtyAreas)
	{
		check(DirtyAreaPtr != nullptr);
		const FNavigationDirtyArea& DirtyArea = *DirtyAreaPtr;
		auto MarkDirtyCells = [this, &DirtyArea, bRespectGeometryAutoRebuild, &AutoRebuildGeometryGridIds](const FGridWorldSnapshot& Source)
		{
			for (const FGridCellData& Cell : Source.Cells)
			{
				if (!UE::GridWorld::Private::ShouldRebuildRegionForDirtyArea(
					DirtyArea,
					bRespectGeometryAutoRebuild,
					AutoRebuildGeometryGridIds.Contains(Cell.Id.GridId)))
				{
					continue;
				}
				const FGridRegionData* Region = Source.FindRegion(Cell.Id.GridId);
				const FVector Expansion = Region != nullptr ? Region->GridTransform.CellSize : Source.GridTransform.CellSize;
				if (!DirtyArea.Bounds.ExpandBy(Expansion).IsInsideOrOn(Cell.WorldCenter))
				{
					continue;
				}
				const FGridChunkCoord CenterChunk = FGridWorldSnapshot::CellToChunk(Cell.Id.Coord, Source.ChunkSize, Cell.Id.GridId);
				for (int32 HaloY = -1; HaloY <= 1; ++HaloY)
				{
					for (int32 HaloX = -1; HaloX <= 1; ++HaloX)
					{
						FGridChunkCoord HaloChunk = CenterChunk;
						HaloChunk.X += HaloX;
						HaloChunk.Y += HaloY;
						LastDirtyChunks.Add(HaloChunk);
					}
				}
			}
		};
		MarkDirtyCells(*PreviousTopology);
		MarkDirtyCells(*Candidate);
	}
	if (LastDirtyChunks.IsEmpty())
	{
		return true;
	}

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Merged = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
	Merged->GridId = Candidate->GridId;
	Merged->GridTransform = Candidate->GridTransform;
	Merged->Revisions = Candidate->Revisions;
	Merged->ChunkSize = Candidate->ChunkSize;
	Merged->AgentSettingsHash = Candidate->AgentSettingsHash;
	Merged->Regions = Candidate->Regions;

	struct FSelectedCell
	{
		FGridCellData Cell;
		const FGridWorldSnapshot* Source = nullptr;
	};
	TArray<FSelectedCell> SelectedCells;
	auto SelectCells = [this, &SelectedCells](const FGridWorldSnapshot& Source, bool bSelectDirty)
	{
		for (const FGridCellData& Cell : Source.Cells)
		{
			const FGridChunkCoord Chunk = FGridWorldSnapshot::CellToChunk(Cell.Id.Coord, Source.ChunkSize, Cell.Id.GridId);
			if (LastDirtyChunks.Contains(Chunk) == bSelectDirty)
			{
				SelectedCells.Add(FSelectedCell{Cell, &Source});
			}
		}
	};
	SelectCells(*PreviousTopology, false);
	SelectCells(*Candidate, true);

	TMap<FGridCellId, int32> NewIndexById;
	for (const FSelectedCell& Selection : SelectedCells)
	{
		FGridCellData Cell = Selection.Cell;
		Cell.Neighbors.Reset();
		NewIndexById.Add(Cell.Id, Merged->Cells.Add(MoveTemp(Cell)));
	}
	for (int32 NewIndex = 0; NewIndex < SelectedCells.Num(); ++NewIndex)
	{
		const FSelectedCell& Selection = SelectedCells[NewIndex];
		for (const int32 SourceNeighborIndex : Selection.Cell.Neighbors)
		{
			if (!Selection.Source->Cells.IsValidIndex(SourceNeighborIndex))
			{
				continue;
			}
			if (const int32* NewNeighborIndex = NewIndexById.Find(Selection.Source->Cells[SourceNeighborIndex].Id))
			{
				Merged->Cells[NewIndex].Neighbors.Add(*NewNeighborIndex);
			}
		}
	}
	if (!PublishSnapshot(Merged))
	{
		return false;
	}
	RefreshRuntimeOverlay(false);
	return true;
}

void AGridNavigationData::ClearGridWorld()
{
	const FGridWorldSnapshotPtr PreviousSnapshot = GetSnapshot();
	const FGridChangeSet ChangeSet = UE::GridWorld::Private::MakeStructuralChangeSet(PreviousSnapshot.Get(), nullptr);
	if (TrafficReservationManager.IsValid() && TrafficReservationManager->Reset())
	{
		TrafficReservationsChanged.Broadcast();
	}
	{
		FWriteScopeLock Lock(SnapshotLock);
		PublishedSnapshot.Reset();
		BaseTopologySnapshot.Reset();
		LastChangeSet = ChangeSet;
	}
	MarkGeneratedDataPackageDirty();
	MarkDebugRenderStateDirty();
	if (UWorld* World = GetWorld())
	{
		if (UGridWorldSubsystem* Subsystem = World->GetSubsystem<UGridWorldSubsystem>())
		{
			Subsystem->OnGridWorldChanged.Broadcast(ChangeSet);
		}
	}
}

void AGridNavigationData::SetDebugPath(TConstArrayView<FNavPathPoint> PathPoints) const
{
	{
		FWriteScopeLock Lock(DebugDataLock);
		LastDebugPathPoints.Reset(PathPoints.Num());
		for (const FNavPathPoint& PathPoint : PathPoints)
		{
			LastDebugPathPoints.Add(PathPoint.Location);
		}
	}
	MarkDebugRenderStateDirty();
}

void AGridNavigationData::MarkDebugRenderStateDirty() const
{
	if (IsInGameThread() && RenderingComp != nullptr)
	{
		RenderingComp->MarkRenderStateDirty();
	}
}

void AGridNavigationData::MarkGeneratedDataPackageDirty() const
{
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	UPackage* Package = GetPackage();
	if (World != nullptr
		&& !World->IsGameWorld()
		&& Package != nullptr
		&& Package != GetTransientPackage()
		&& !HasAnyFlags(RF_Transient))
	{
		MarkPackageDirty();
	}
#endif
}

void AGridNavigationData::SetDebugReachability(TConstArrayView<FVector> WorldPoints) const
{
	{
		FWriteScopeLock Lock(DebugDataLock);
		LastDebugReachablePoints.Reset(WorldPoints.Num());
		for (const FVector& WorldPoint : WorldPoints)
		{
			LastDebugReachablePoints.Add(WorldPoint);
		}
	}
	MarkDebugRenderStateDirty();
}

void AGridNavigationData::GetDebugQueryData(TArray<TArray<FVector>>& OutPathPointSets, TArray<FVector>& OutReachablePoints) const

{
	TArray<FVector> IgnoredRequiredStopPoints;
	TArray<FGridCenterGateDebugData> IgnoredCenterGates;
	TArray<FGridPathDriveDebugData> IgnoredDriveData;
	GetDebugQueryData(OutPathPointSets, OutReachablePoints, IgnoredRequiredStopPoints, IgnoredCenterGates, IgnoredDriveData);
}

void AGridNavigationData::GetDebugQueryData(
	TArray<TArray<FVector>>& OutPathPointSets,
	TArray<FVector>& OutReachablePoints,
	TArray<FVector>& OutRequiredStopPoints) const

{
	TArray<FGridCenterGateDebugData> IgnoredCenterGates;
	TArray<FGridPathDriveDebugData> IgnoredDriveData;
	GetDebugQueryData(OutPathPointSets, OutReachablePoints, OutRequiredStopPoints, IgnoredCenterGates, IgnoredDriveData);
}

void AGridNavigationData::GetDebugQueryData(
	TArray<TArray<FVector>>& OutPathPointSets,
	TArray<FVector>& OutReachablePoints,
	TArray<FVector>& OutRequiredStopPoints,
	TArray<FGridCenterGateDebugData>& OutCenterGates) const

{
	TArray<FGridPathDriveDebugData> IgnoredDriveData;
	GetDebugQueryData(OutPathPointSets, OutReachablePoints, OutRequiredStopPoints, OutCenterGates, IgnoredDriveData);
}

void AGridNavigationData::GetDebugQueryData(
	TArray<TArray<FVector>>& OutPathPointSets,
	TArray<FVector>& OutReachablePoints,
	TArray<FVector>& OutRequiredStopPoints,
	TArray<FGridCenterGateDebugData>& OutCenterGates,
	TArray<FGridPathDriveDebugData>& OutDriveData) const
{
	TArray<FGridAgentAvoidanceDebugData> IgnoredAgentAvoidanceData;
	GetDebugQueryData(
		OutPathPointSets,
		OutReachablePoints,
		OutRequiredStopPoints,
		OutCenterGates,
		OutDriveData,
		IgnoredAgentAvoidanceData);
}

void AGridNavigationData::GetDebugQueryData(
	TArray<TArray<FVector>>& OutPathPointSets,
	TArray<FVector>& OutReachablePoints,
	TArray<FVector>& OutRequiredStopPoints,
	TArray<FGridCenterGateDebugData>& OutCenterGates,
	TArray<FGridPathDriveDebugData>& OutDriveData,
	TArray<FGridAgentAvoidanceDebugData>& OutAgentAvoidanceData) const
{
	FReadScopeLock Lock(DebugDataLock);
	OutPathPointSets = ActiveDebugPathPointSets;
	if (LastDebugPathPoints.Num() > 1)
	{
		OutPathPointSets.Add(LastDebugPathPoints);
	}
	OutReachablePoints = LastDebugReachablePoints;
	OutRequiredStopPoints = ActiveDebugRequiredStopPoints;
	OutCenterGates = ActiveDebugCenterGates;
	OutDriveData = ActiveDebugDriveData;
	OutAgentAvoidanceData.Reset(ActiveAgentAvoidanceDebugData.Num());
	for (const TPair<TWeakObjectPtr<UObject>, FGridAgentAvoidanceDebugData>& Pair : ActiveAgentAvoidanceDebugData)
	{
		if (Pair.Key.IsValid())
		{
			OutAgentAvoidanceData.Add(Pair.Value);
		}
	}
}

void AGridNavigationData::SetAgentAvoidanceDebug(UObject* Source, const FGridAgentAvoidanceDebugData& DebugData) const
{
	if (!IsInGameThread() || Source == nullptr)
	{
		return;
	}
	bool bChanged = false;
	{
		FWriteScopeLock Lock(DebugDataLock);
		for (auto It = ActiveAgentAvoidanceDebugData.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
				bChanged = true;
			}
		}
		const TWeakObjectPtr<UObject> SourceKey(Source);
		const FGridAgentAvoidanceDebugData* Previous = ActiveAgentAvoidanceDebugData.Find(SourceKey);
		if (Previous == nullptr || !(*Previous == DebugData))
		{
			ActiveAgentAvoidanceDebugData.Add(SourceKey, DebugData);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		MarkDebugRenderStateDirty();
	}
}

void AGridNavigationData::ClearAgentAvoidanceDebug(UObject* Source) const
{
	if (!IsInGameThread() || Source == nullptr)
	{
		return;
	}
	bool bRemoved = false;
	{
		FWriteScopeLock Lock(DebugDataLock);
		bRemoved = ActiveAgentAvoidanceDebugData.Remove(Source) > 0;
	}
	if (bRemoved)
	{
		MarkDebugRenderStateDirty();
	}
}

void AGridNavigationData::RefreshActiveDebugPathPointSets()
{
	FWriteScopeLock Lock(DebugDataLock);
	ActiveDebugPathPointSets.Reset(ActiveDebugPaths.Num());
	ActiveDebugRequiredStopPoints.Reset();
	ActiveDebugCenterGates.Reset();
	ActiveDebugDriveData.Reset();
	for (const TPair<TWeakObjectPtr<AAIController>, FActiveDebugPath>& Pair : ActiveDebugPaths)
	{
		if (Pair.Key.IsValid() && Pair.Value.Points.Num() > 1)
		{
			ActiveDebugPathPointSets.Add(Pair.Value.Points);
		}
		if (Pair.Key.IsValid())
		{
			ActiveDebugRequiredStopPoints.Append(Pair.Value.RequiredStopPoints);
			ActiveDebugCenterGates.Append(Pair.Value.CenterGates);
			if (Pair.Value.bHasDriveData)
			{
				ActiveDebugDriveData.Add(Pair.Value.DriveData);
			}
		}
	}
}

void AGridNavigationData::TrackActiveDebugPath(AAIController& Controller, const FNavPathSharedPtr& Path)
{
	if (!IsInGameThread() || !Path.IsValid())
	{
		return;
	}

	UPathFollowingComponent* PathFollowingComponent = Controller.GetPathFollowingComponent();
	if (PathFollowingComponent == nullptr)
	{
		return;
	}

	const TWeakObjectPtr<AAIController> ControllerKey(&Controller);
	FActiveDebugPath& Entry = ActiveDebugPaths.FindOrAdd(ControllerKey);
	const FNavPathSharedPtr PreviousPath = Entry.Path.Pin();
	if (PreviousPath.IsValid() && PreviousPath.Get() != Path.Get() && Entry.PathObserverHandle.IsValid())
	{
		PreviousPath->RemoveObserver(Entry.PathObserverHandle);
		Entry.PathObserverHandle.Reset();
	}
	if (Entry.PathFollowingComponent.Get() != PathFollowingComponent && Entry.MoveFinishedHandle.IsValid())
	{
		if (UPathFollowingComponent* PreviousPathFollowing = Entry.PathFollowingComponent.Get())
		{
			PreviousPathFollowing->OnRequestFinished.Remove(Entry.MoveFinishedHandle);
		}
		Entry.MoveFinishedHandle.Reset();
	}

	Entry.PathFollowingComponent = PathFollowingComponent;
	Entry.Path = Path;
	Entry.Points.Reset(Path->GetPathPoints().Num());
	for (const FNavPathPoint& PathPoint : Path->GetPathPoints())
	{
		Entry.Points.Add(PathPoint.Location);
	}
	Entry.RequiredStopPoints.Reset();
	Entry.CenterGates.Reset();
	Entry.bHasDriveData = false;
	if (const FGridNavigationPath* GridPath = Path->CastPath<FGridNavigationPath>())
	{
		GridPath->GetRequiredStopLocations(Entry.RequiredStopPoints);
		GridPath->GetCenterGateDebugData(Entry.CenterGates);
		Entry.bHasDriveData = GridPath->GetDriveDebugData(Entry.DriveData);
	}
	if (!Entry.PathObserverHandle.IsValid())
	{
		Entry.PathObserverHandle = Path->AddObserver(FNavigationPath::FPathObserverDelegate::FDelegate::CreateUObject(
			this,
			&AGridNavigationData::OnActiveDebugPathEvent,
			ControllerKey));
	}
	if (!Entry.MoveFinishedHandle.IsValid())
	{
		Entry.MoveFinishedHandle = PathFollowingComponent->OnRequestFinished.AddUObject(
			this,
			&AGridNavigationData::OnActiveDebugMoveFinished,
			ControllerKey);
	}
	Controller.OnEndPlay.AddUniqueDynamic(this, &AGridNavigationData::OnTrackedControllerEndPlay);

	RefreshActiveDebugPathPointSets();
	MarkDebugRenderStateDirty();
}

void AGridNavigationData::RemoveActiveDebugPath(TWeakObjectPtr<AAIController> Controller)
{
	FActiveDebugPath Entry;
	if (!ActiveDebugPaths.RemoveAndCopyValue(Controller, Entry))
	{
		return;
	}
	if (const FNavPathSharedPtr Path = Entry.Path.Pin(); Path.IsValid() && Entry.PathObserverHandle.IsValid())
	{
		Path->RemoveObserver(Entry.PathObserverHandle);
	}
	if (UPathFollowingComponent* PathFollowingComponent = Entry.PathFollowingComponent.Get(); PathFollowingComponent != nullptr && Entry.MoveFinishedHandle.IsValid())
	{
		PathFollowingComponent->OnRequestFinished.Remove(Entry.MoveFinishedHandle);
	}
	if (AAIController* ValidController = Controller.Get())
	{
		ValidController->OnEndPlay.RemoveDynamic(this, &AGridNavigationData::OnTrackedControllerEndPlay);
	}
	RefreshActiveDebugPathPointSets();
	MarkDebugRenderStateDirty();
}

void AGridNavigationData::ClearActiveDebugPaths()
{
	TArray<TWeakObjectPtr<AAIController>> Controllers;
	ActiveDebugPaths.GenerateKeyArray(Controllers);
	for (const TWeakObjectPtr<AAIController>& Controller : Controllers)
	{
		RemoveActiveDebugPath(Controller);
	}
	RefreshActiveDebugPathPointSets();
	MarkDebugRenderStateDirty();
}

void AGridNavigationData::OnActiveDebugPathEvent(FNavigationPath* Path, ENavPathEvent::Type Event, TWeakObjectPtr<AAIController> Controller)
{
	FActiveDebugPath* Entry = ActiveDebugPaths.Find(Controller);
	if (Entry == nullptr || Entry->Path.Pin().Get() != Path)
	{
		return;
	}

	switch (Event)
	{
	case ENavPathEvent::NewPath:
	case ENavPathEvent::UpdatedDueToGoalMoved:
	case ENavPathEvent::UpdatedDueToNavigationChanged:
	case ENavPathEvent::MetaPathUpdate:
		Entry->Points.Reset(Path != nullptr ? Path->GetPathPoints().Num() : 0);
		if (Path != nullptr)
		{
			for (const FNavPathPoint& PathPoint : Path->GetPathPoints())
			{
				Entry->Points.Add(PathPoint.Location);
			}
			Entry->RequiredStopPoints.Reset();
			Entry->CenterGates.Reset();
			Entry->bHasDriveData = false;
			if (const FGridNavigationPath* GridPath = Path->CastPath<FGridNavigationPath>())
			{
				GridPath->GetRequiredStopLocations(Entry->RequiredStopPoints);
				GridPath->GetCenterGateDebugData(Entry->CenterGates);
				Entry->bHasDriveData = GridPath->GetDriveDebugData(Entry->DriveData);
			}
		}
		break;

	case ENavPathEvent::Invalidated:
	case ENavPathEvent::Cleared:
	case ENavPathEvent::RePathFailed:
		Entry->Points.Reset();
		Entry->RequiredStopPoints.Reset();
		Entry->CenterGates.Reset();
		Entry->bHasDriveData = false;
		break;

	default:
		return;
	}

	RefreshActiveDebugPathPointSets();
	MarkDebugRenderStateDirty();
}

void AGridNavigationData::OnActiveDebugMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result, TWeakObjectPtr<AAIController> Controller)
{
	if (UWorld* World = GetWorld())
	{
		FTimerDelegate ReconcileDelegate;
		ReconcileDelegate.BindUObject(this, &AGridNavigationData::ReconcileActiveDebugPath, Controller);
		World->GetTimerManager().SetTimerForNextTick(ReconcileDelegate);
	}
}

void AGridNavigationData::ReconcileActiveDebugPath(TWeakObjectPtr<AAIController> Controller)
{
	AAIController* ValidController = Controller.Get();
	if (ValidController == nullptr)
	{
		RemoveActiveDebugPath(Controller);
		return;
	}

	UPathFollowingComponent* PathFollowingComponent = ValidController->GetPathFollowingComponent();
	const FNavPathSharedPtr CurrentPath = PathFollowingComponent != nullptr ? PathFollowingComponent->GetPath() : nullptr;
	if (CurrentPath.IsValid() && CurrentPath->GetNavigationDataUsed() == this && CurrentPath->CastPath<FGridNavigationPath>() != nullptr)
	{
		TrackActiveDebugPath(*ValidController, CurrentPath);
	}
	else
	{
		RemoveActiveDebugPath(Controller);
	}
}

void AGridNavigationData::OnTrackedControllerEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	if (AAIController* Controller = Cast<AAIController>(Actor))
	{
		PrecisePathWarningControllers.Remove(Controller);
		SlopeWarningControllers.Remove(Controller);
		SearchLimitWarningControllers.Remove(Controller);
		RemoveActiveDebugPath(Controller);
	}
}

void AGridNavigationData::RefreshRuntimeOverlay(bool bOccupancyOnly)
{
	if (!IsInGameThread())
	{
		GRIDWORLD_LOG_WARNING("Ignored runtime overlay refresh for '%s' outside the Game Thread.", *GetNameSafe(this));
		return;
	}
	UWorld* World = GetWorld();
	FGridWorldSnapshotPtr BaseSnapshot;
	FGridWorldSnapshotPtr PreviousSnapshot;
	{
		FReadScopeLock Lock(SnapshotLock);
		BaseSnapshot = BaseTopologySnapshot;
		PreviousSnapshot = PublishedSnapshot;
	}
	if (World == nullptr || !BaseSnapshot.IsValid())
	{
		return;
	}

	FGridChangeSet ChangeSet;
	const TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Composed = FGridOverlayComposer::Compose(
		*World,
		*BaseSnapshot,
		PreviousSnapshot.Get(),
		bOccupancyOnly,
		ChangeSet);
	{
		FWriteScopeLock Lock(SnapshotLock);
		PublishedSnapshot = StaticCastSharedRef<const FGridWorldSnapshot>(Composed);
		LastChangeSet = ChangeSet;
	}

	InvalidateAffectedPaths(ChangeSet);
	if (UGridWorldSubsystem* Subsystem = World->GetSubsystem<UGridWorldSubsystem>())
	{
		Subsystem->OnGridWorldChanged.Broadcast(ChangeSet);
	}
	if (RenderingComp != nullptr)
	{
		RenderingComp->MarkRenderStateDirty();
	}
	if (ChangeSet.PreviousRevisions.Occupancy != ChangeSet.NewRevisions.Occupancy)
	{
		TrafficReservationsChanged.Broadcast();
	}
}

bool AGridNavigationData::UpdateTrafficCorridor(
	const FGridTrafficCorridorRequest& Request,
	FGridTrafficCorridorResult& OutResult)
{
	if (!IsInGameThread() || !TrafficReservationManager.IsValid())
	{
		OutResult = FGridTrafficCorridorResult();
		return false;
	}
	const bool bValidRequest = TrafficReservationManager->UpdateCorridor(Request, OutResult);
	if (OutResult.bStateChanged)
	{
		TrafficReservationsChanged.Broadcast();
		MarkDebugRenderStateDirty();
	}
	return bValidRequest;
}

void AGridNavigationData::ReleaseTrafficCorridor(
	const FGuid& OwnerId,
	const UObject* Source,
	bool bKeepCurrentCell)
{
	if (IsInGameThread()
		&& TrafficReservationManager.IsValid()
		&& TrafficReservationManager->ReleaseCorridor(OwnerId, Source, bKeepCurrentCell))
	{
		TrafficReservationsChanged.Broadcast();
		MarkDebugRenderStateDirty();
	}
}

bool AGridNavigationData::CanClaimTrafficGoal(
	const FGridTrafficGoalClaimRequest& Request,
	FGuid* OutBlockingOwnerId) const
{
	if (OutBlockingOwnerId != nullptr)
	{
		OutBlockingOwnerId->Invalidate();
	}

	if (!IsInGameThread() || !TrafficReservationManager.IsValid())
	{
		return false;
	}
	const FGridWorldSnapshotPtr CurrentSnapshot = GetSnapshot();
	const FGridCellData* GoalCell = CurrentSnapshot.IsValid()
		? CurrentSnapshot->FindCell(Request.GoalCell.CellId)
		: nullptr;
	if (GoalCell == nullptr || !GoalCell->bWalkable)
	{
		return false;
	}
	const FGuid* BlockingOccupant = GoalCell->OccupancyOwners.FindByPredicate(
		[&Request](const FGuid& OccupantId)
		{
			return OccupantId.IsValid() && OccupantId != Request.OwnerId;
		});
	if (BlockingOccupant != nullptr)
	{
		if (OutBlockingOwnerId != nullptr)
		{
			*OutBlockingOwnerId = *BlockingOccupant;
		}
		return false;
	}
	return TrafficReservationManager->CanClaimGoal(Request, OutBlockingOwnerId);
}

bool AGridNavigationData::TryClaimTrafficGoal(const FGridTrafficGoalClaimRequest& Request)
{
	if (!IsInGameThread() || !TrafficReservationManager.IsValid())
	{
		return false;
	}
	if (!CanClaimTrafficGoal(Request))
	{
		return false;
	}
	bool bStateChanged = false;
	const bool bClaimed = TrafficReservationManager->TryClaimGoal(Request, bStateChanged);
	if (bStateChanged)
	{
		TrafficReservationsChanged.Broadcast();
		MarkDebugRenderStateDirty();
	}
	return bClaimed;
}

bool AGridNavigationData::IsTrafficGoalClaimedByOther(
	const FGridCellId& CellId,
	const UObject* Claimant,
	const FGuid& OwnerId) const
{
	return IsInGameThread()
		&& TrafficReservationManager.IsValid()
		&& TrafficReservationManager->IsGoalClaimedByOther(CellId, Claimant, OwnerId);
}

void AGridNavigationData::ReleaseTrafficGoalClaims(const UObject* Claimant)
{
	if (IsInGameThread()
		&& TrafficReservationManager.IsValid()
		&& TrafficReservationManager->ReleaseGoalClaims(Claimant))
	{
		TrafficReservationsChanged.Broadcast();
		MarkDebugRenderStateDirty();
	}
}

void AGridNavigationData::CommitTrafficParking(const FGridTrafficGoalClaimRequest& Request)
{
	if (IsInGameThread()
		&& TrafficReservationManager.IsValid()
		&& TrafficReservationManager->CommitParking(Request))
	{
		TrafficReservationsChanged.Broadcast();
		MarkDebugRenderStateDirty();
	}
}

void AGridNavigationData::RemoveTrafficOwner(const FGuid& OwnerId)
{
	if (IsInGameThread()
		&& TrafficReservationManager.IsValid()
		&& TrafficReservationManager->RemoveOwner(OwnerId))
	{
		TrafficReservationsChanged.Broadcast();
		MarkDebugRenderStateDirty();
	}
}

FGridTrafficReservationSnapshotPtr AGridNavigationData::GetTrafficReservationSnapshot() const
{
	return TrafficReservationManager.IsValid()
		? TrafficReservationManager->GetSnapshot()
		: FGridTrafficReservationSnapshotPtr();
}

int64 AGridNavigationData::BuildFilterSignature(
	const FSharedConstNavQueryFilter& QueryFilter,
	TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	if (!QueryFilter.IsValid())
	{
		return 0;
	}
	uint32 Hash = GetTypeHash(FilterClass.Get());
	Hash = HashCombineFast(Hash, GetTypeHash(QueryFilter->GetIncludeFlags()));
	Hash = HashCombineFast(Hash, GetTypeHash(QueryFilter->GetExcludeFlags()));
	Hash = HashCombineFast(Hash, GetTypeHash(QueryFilter->GetMaxSearchNodes()));
	if (const FGridNavigationQueryFilterImpl* GridFilter = UE::GridWorld::Private::GetGridFilter(QueryFilter))
	{
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(GridFilter->GetMovementMode())));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(GridFilter->GetPathOptimizationMode())));
		Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->GetBalancedTurnPenalty()));
		Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->AllowsCornerCutting()));
		Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->AllowsLinks()));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(GridFilter->GetOccupancyPolicy())));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(GridFilter->GetDynamicAgentPolicy())));
		Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->GetTraversalChannel()));
		Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->GetReservationId()));
		Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->GetIgnoredOccupancyOwnerId()));
		Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->GetAdditionalAgentSeparation()));
		for (uint8 AreaId = 0; AreaId < FGridNavigationQueryFilterImpl::MaxAreaCount; ++AreaId)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->GetAreaCost(AreaId)));
			Hash = HashCombineFast(Hash, GetTypeHash(GridFilter->GetEnteringCost(AreaId)));
		}
	}
	return static_cast<int64>(Hash);
}

FGridAStarQuery AGridNavigationData::BuildAStarQuery(
	const FNavAgentProperties& AgentProperties,
	const FPathFindingQuery& Query,
	int32 StartIndex,
	int32 GoalIndex) const
{
	FGridAStarQuery AStarQuery;
	AStarQuery.StartCellIndex = StartIndex;
	AStarQuery.GoalCellIndex = GoalIndex;
	AStarQuery.bAllowPartialPath = Query.bAllowPartialPaths;
	AStarQuery.TrafficAgentRadius = AgentProperties.AgentRadius > 0.0f ? AgentProperties.AgentRadius : 42.0f;
	AStarQuery.TrafficAgentHeight = AgentProperties.AgentHeight > 0.0f ? AgentProperties.AgentHeight : 192.0f;
	if (Query.QueryFilter.IsValid())
	{
		AStarQuery.MaxVisitedNodes = FMath::Min<uint32>(Query.QueryFilter->GetMaxSearchNodes(), MAX_int32);
		AStarQuery.IncludeFlags = Query.QueryFilter->GetIncludeFlags();
		AStarQuery.ExcludeFlags = Query.QueryFilter->GetExcludeFlags();
		if (const FGridNavigationQueryFilterImpl* GridFilter = UE::GridWorld::Private::GetGridFilter(Query.QueryFilter))
		{
			AStarQuery.MovementMode = GridFilter->GetMovementMode();
			AStarQuery.PathOptimizationMode = GridFilter->GetPathOptimizationMode();
			AStarQuery.BalancedTurnPenaltyCost = FMath::Max<int64>(
				0,
				FMath::RoundToInt64(static_cast<double>(GridFilter->GetBalancedTurnPenalty()) * FGridAStar::OrthogonalCost));
			AStarQuery.bAllowCornerCutting = GridFilter->AllowsCornerCutting();
			AStarQuery.bAllowLinks = GridFilter->AllowsLinks();
			AStarQuery.OccupancyPolicy = GridFilter->GetOccupancyPolicy();
			AStarQuery.DynamicAgentPolicy = GridFilter->GetDynamicAgentPolicy();
			AStarQuery.IgnoredOccupancyOwnerId = GridFilter->GetIgnoredOccupancyOwnerId();
			AStarQuery.TrafficAdditionalSeparation = GridFilter->GetAdditionalAgentSeparation();
			AStarQuery.ReservationId = GridFilter->GetReservationId();
			AStarQuery.TraversalChannel = GridFilter->GetTraversalChannel();
			for (uint8 AreaId = 0; AreaId < FGridNavigationQueryFilterImpl::MaxAreaCount; ++AreaId)
			{
				const float AreaCost = GridFilter->GetAreaCost(AreaId);
				AStarQuery.AreaCosts[AreaId] = AreaCost >= BIG_NUMBER * 0.5f
					? MAX_int32 / 2
					: FMath::RoundToInt(AreaCost * 1000.0f);
				AStarQuery.AreaEnteringCosts[AreaId] = FMath::RoundToInt(GridFilter->GetEnteringCost(AreaId) * 1000.0f);
			}
		}
	}
	if (AStarQuery.DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor)
	{
		AStarQuery.TrafficReservations = GetTrafficReservationSnapshot();
	}
	return AStarQuery;
}

FGridInjectedPathValidationResult AGridNavigationData::CreateExactInjectedPath(
	const UObject* Querier,
	const FNavAgentProperties& AgentProperties,
	TSubclassOf<UNavigationQueryFilter> FilterClass,
	const FVector& StartLocation,
	TConstArrayView<FGridCellId> Cells,
	const FGridCellId& OriginalGoalCell,
	bool bAllowPartialPath,
	bool bIsPartial,
	EGridInjectedPathInvalidationPolicy InvalidationPolicy,
	const FGuid& SourcePreviewId,
	FGridInjectedPath& OutInjectedPath,
	bool bAllowDynamicAgentConflictsDuringValidation) const
{
	OutInjectedPath = FGridInjectedPath();
	FGridInjectedPathValidationResult Output;
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	Output.CurrentRevisions = Snapshot.IsValid() ? Snapshot->Revisions : FGridRevisionSet();
	if (!Snapshot.IsValid())
	{
		Output.FailureReason = EGridInjectedPathFailureReason::InvalidNavigationData;
		Output.DiagnosticMessage = TEXT("GridWorld has no published navigation snapshot.");
		return Output;
	}
	if (Querier == nullptr || StartLocation.ContainsNaN() || Cells.IsEmpty() || !OriginalGoalCell.IsValid())
	{
		Output.FailureReason = EGridInjectedPathFailureReason::InvalidPath;
		Output.DiagnosticMessage = TEXT("Exact path creation requires a querier, finite start, ordered cells, and original goal.");
		return Output;
	}

	const int32* GoalIndexPtr = Snapshot->CellIndexById.Find(OriginalGoalCell);
	if (GoalIndexPtr == nullptr)
	{
		Output.FailureReason = EGridInjectedPathFailureReason::InvalidGoal;
		Output.InvalidCell = OriginalGoalCell;
		Output.DiagnosticMessage = TEXT("The original goal cell does not exist in the current topology.");
		return Output;
	}
	TSubclassOf<UNavigationQueryFilter> ResolvedFilterClass = FilterClass;
	if (ResolvedFilterClass == nullptr)
	{
		ResolvedFilterClass = UGridNavigationQueryFilter::StaticClass();
	}
	const FSharedConstNavQueryFilter BaseQueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*this,
		Querier,
		ResolvedFilterClass);
	if (!BaseQueryFilter.IsValid())
	{
		Output.FailureReason = EGridInjectedPathFailureReason::FilterMismatch;
		Output.DiagnosticMessage = TEXT("GridWorld could not initialize the requested navigation filter.");
		return Output;
	}
	FPathFindingQuery Query(
		Querier,
		*this,
		StartLocation,
		Snapshot->Cells[*GoalIndexPtr].WorldCenter,
		BaseQueryFilter);
	Query.SetAllowPartialPaths(bAllowPartialPath);
	Query.SetNavAgentProperties(AgentProperties);
	if (const AController* Controller = Cast<AController>(Querier))
	{
		if (UPathFollowingComponent* PathFollowing = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			PathFollowing->OnPathfindingQuery(Query);
		}
	}
	const FSharedConstNavQueryFilter QueryFilter = Query.QueryFilter;
	FNavLocation ProjectedStart;
	if (!ProjectPoint(StartLocation, ProjectedStart, GetDefaultQueryExtent(), QueryFilter, Querier))
	{
		Output.FailureReason = EGridInjectedPathFailureReason::InvalidStart;
		Output.DiagnosticMessage = TEXT("The current navigation-agent location cannot be projected to GridWorld.");
		return Output;
	}
	const int32 StartIndex = Snapshot->ResolveNodeRef(ProjectedStart.NodeRef);
	if (!Snapshot->Cells.IsValidIndex(StartIndex) || Snapshot->Cells[StartIndex].Id != Cells[0])
	{
		Output.FailureReason = EGridInjectedPathFailureReason::InvalidStart;
		Output.InvalidCell = Cells[0];
		Output.DiagnosticMessage = TEXT("The supplied exact path does not start in the querier's current GridWorld cell.");
		return Output;
	}

	TArray<int32, TInlineAllocator<64>> CellIndices;
	CellIndices.Reserve(Cells.Num());
	for (int32 PathIndex = 0; PathIndex < Cells.Num(); ++PathIndex)
	{
		const int32* CellIndexPtr = Snapshot->CellIndexById.Find(Cells[PathIndex]);
		if (CellIndexPtr == nullptr)
		{
			Output.FailureReason = EGridInjectedPathFailureReason::MissingCell;
			Output.InvalidCell = Cells[PathIndex];
			Output.DiagnosticMessage = TEXT("An exact-path cell does not exist in the current topology.");
			return Output;
		}
		CellIndices.Add(*CellIndexPtr);
	}

	const FGridAStarQuery AStarQuery = BuildAStarQuery(AgentProperties, Query, StartIndex, *GoalIndexPtr);
	const FGridAStarQuery ValidationQuery =
		UE::GridWorld::Private::MakeInjectedPathValidationQuery(
			AStarQuery,
			bAllowDynamicAgentConflictsDuringValidation);
	const FGridAStarPathValidationResult Validation = FGridAStar().ValidatePath(
		*Snapshot,
		ValidationQuery,
		CellIndices,
		bIsPartial);
	if (!Validation.IsValid())
	{
		Output.FailureReason = Validation.FailureReason;
		Output.InvalidSegmentIndex = Validation.InvalidSegmentIndex;
		if (Cells.IsValidIndex(Validation.InvalidCellIndex))
		{
			Output.InvalidCell = Cells[Validation.InvalidCellIndex];
		}
		Output.DiagnosticMessage = FString::Printf(
			TEXT("GridWorld rejected the exact path: %s."),
			*StaticEnum<EGridInjectedPathFailureReason>()->GetNameStringByValue(static_cast<int64>(Validation.FailureReason)));
		return Output;
	}

	OutInjectedPath.Cells.Append(Cells.GetData(), Cells.Num());
	OutInjectedPath.OriginalGoalCell = OriginalGoalCell;
	OutInjectedPath.RequestedGoalCell = OriginalGoalCell;
	OutInjectedPath.NavigationDataId = RuntimeNavigationDataId;
	OutInjectedPath.PathInstanceId = FGuid::NewGuid();
	OutInjectedPath.SourcePreviewId = SourcePreviewId;
	OutInjectedPath.AgentProperties = AgentProperties;
	OutInjectedPath.FilterClass = ResolvedFilterClass;
	OutInjectedPath.FilterSignature = BuildFilterSignature(QueryFilter, ResolvedFilterClass);
	OutInjectedPath.Revisions = Snapshot->Revisions;
	OutInjectedPath.TrafficReservationRevision = AStarQuery.TrafficReservations.IsValid()
		? AStarQuery.TrafficReservations->Revision
		: 0;
	OutInjectedPath.bAllowDynamicAgentConflictsDuringValidation =
		bAllowDynamicAgentConflictsDuringValidation;
	OutInjectedPath.bAllowPartialPath = bAllowPartialPath;
	OutInjectedPath.bIsPartial = bIsPartial;
	OutInjectedPath.InvalidationPolicy = InvalidationPolicy;
	Output.bIsValid = true;
	Output.FailureReason = EGridInjectedPathFailureReason::None;
	Output.DiagnosticMessage = TEXT("Exact GridWorld path is valid.");
	return Output;
}

FGridInjectedPathValidationResult AGridNavigationData::ValidateInjectedPath(
	const FGridInjectedPath& InjectedPath,
	const UObject* Querier,
	const FNavAgentProperties& AgentProperties,
	const FVector& StartLocation) const
{
	FGridInjectedPathValidationResult Output;
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	Output.CurrentRevisions = Snapshot.IsValid() ? Snapshot->Revisions : FGridRevisionSet();
	auto Fail = [&Output](EGridInjectedPathFailureReason Reason, const TCHAR* Message)
	{
		Output.FailureReason = Reason;
		Output.DiagnosticMessage = Message;
		return Output;
	};
	if (!Snapshot.IsValid())
	{
		return Fail(EGridInjectedPathFailureReason::InvalidNavigationData, TEXT("GridWorld has no published navigation snapshot."));
	}
	if (!InjectedPath.IsSet() || Querier == nullptr || StartLocation.ContainsNaN())
	{
		return Fail(EGridInjectedPathFailureReason::InvalidPath, TEXT("The injected path, querier, or current start is invalid."));
	}
	if (InjectedPath.NavigationDataId != RuntimeNavigationDataId)
	{
		return Fail(EGridInjectedPathFailureReason::NavigationDataMismatch, TEXT("The injected path belongs to a different GridWorld Navigation Data instance."));
	}
	if (!InjectedPath.AgentProperties.IsEquivalent(AgentProperties, 0.1f))
	{
		return Fail(EGridInjectedPathFailureReason::AgentMismatch, TEXT("The current navigation agent is incompatible with the injected path."));
	}
	if (InjectedPath.Revisions.Topology != Snapshot->Revisions.Topology)
	{
		return Fail(EGridInjectedPathFailureReason::StaleTopology, TEXT("GridWorld topology changed after the injected path was created."));
	}
	if (InjectedPath.Revisions.Traversal != Snapshot->Revisions.Traversal)
	{
		return Fail(EGridInjectedPathFailureReason::StaleTraversalState, TEXT("GridWorld traversal or cost state changed after the injected path was created."));
	}

	const FSharedConstNavQueryFilter BaseQueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*this,
		Querier,
		InjectedPath.FilterClass);
	if (!BaseQueryFilter.IsValid())
	{
		return Fail(EGridInjectedPathFailureReason::FilterMismatch, TEXT("The current navigation filter could not be initialized."));
	}
	const int32* GoalIndexPtr = Snapshot->CellIndexById.Find(InjectedPath.OriginalGoalCell);
	if (GoalIndexPtr == nullptr)
	{
		Output.InvalidCell = InjectedPath.OriginalGoalCell;
		return Fail(EGridInjectedPathFailureReason::InvalidGoal, TEXT("The injected path's original goal no longer exists."));
	}
	FPathFindingQuery Query(
		Querier,
		*this,
		StartLocation,
		Snapshot->Cells[*GoalIndexPtr].WorldCenter,
		BaseQueryFilter);
	Query.SetAllowPartialPaths(InjectedPath.bAllowPartialPath);
	Query.SetNavAgentProperties(AgentProperties);
	if (const AController* Controller = Cast<AController>(Querier))
	{
		if (UPathFollowingComponent* PathFollowing = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			PathFollowing->OnPathfindingQuery(Query);
		}
	}
	const FSharedConstNavQueryFilter QueryFilter = Query.QueryFilter;
	if (BuildFilterSignature(QueryFilter, InjectedPath.FilterClass) != InjectedPath.FilterSignature)
	{
		return Fail(EGridInjectedPathFailureReason::FilterMismatch, TEXT("The current initialized query filter differs from the injected path filter."));
	}
	const FGridNavigationQueryFilterImpl* GridFilter = UE::GridWorld::Private::GetGridFilter(QueryFilter);
	const bool bUsesBlockingOccupancy = GridFilter != nullptr
		&& GridFilter->GetOccupancyPolicy() != EGridOccupancyPolicy::Ignore;
	const bool bUsesDynamicAgents = GridFilter != nullptr
		&& GridFilter->GetDynamicAgentPolicy() != EGridDynamicAgentPolicy::Ignore;
	const bool bUsesOccupancy = !InjectedPath.bAllowDynamicAgentConflictsDuringValidation
		&& (bUsesBlockingOccupancy || bUsesDynamicAgents);
	if (bUsesOccupancy && InjectedPath.Revisions.Occupancy != Snapshot->Revisions.Occupancy)
	{
		return Fail(EGridInjectedPathFailureReason::StaleOccupancyState, TEXT("Relevant GridWorld occupancy changed after the injected path was created."));
	}
	if (!InjectedPath.bAllowDynamicAgentConflictsDuringValidation
		&& GridFilter != nullptr
		&& GridFilter->GetDynamicAgentPolicy() == EGridDynamicAgentPolicy::ReservedCorridor)
	{
		const FGridTrafficReservationSnapshotPtr TrafficSnapshot = GetTrafficReservationSnapshot();
		const int64 CurrentTrafficRevision = TrafficSnapshot.IsValid() ? TrafficSnapshot->Revision : 0;
		if (InjectedPath.TrafficReservationRevision != CurrentTrafficRevision)
		{
			return Fail(EGridInjectedPathFailureReason::StaleOccupancyState, TEXT("GridWorld traffic reservations changed after the injected path was created."));
		}
	}

	FNavLocation ProjectedStart;
	if (!ProjectPoint(StartLocation, ProjectedStart, GetDefaultQueryExtent(), QueryFilter, Querier))
	{
		return Fail(EGridInjectedPathFailureReason::InvalidStart, TEXT("The current navigation-agent location is not on GridWorld."));
	}
	const int32 StartIndex = Snapshot->ResolveNodeRef(ProjectedStart.NodeRef);
	if (!Snapshot->Cells.IsValidIndex(StartIndex) || Snapshot->Cells[StartIndex].Id != InjectedPath.Cells[0])
	{
		Output.InvalidCell = InjectedPath.Cells[0];
		return Fail(EGridInjectedPathFailureReason::InvalidStart, TEXT("The current agent cell no longer matches the injected path start."));
	}
	TArray<int32, TInlineAllocator<64>> CellIndices;
	CellIndices.Reserve(InjectedPath.Cells.Num());
	for (int32 PathIndex = 0; PathIndex < InjectedPath.Cells.Num(); ++PathIndex)
	{
		const int32* CellIndexPtr = Snapshot->CellIndexById.Find(InjectedPath.Cells[PathIndex]);
		if (CellIndexPtr == nullptr)
		{
			Output.InvalidCell = InjectedPath.Cells[PathIndex];
			return Fail(EGridInjectedPathFailureReason::MissingCell, TEXT("An injected path cell no longer exists."));
		}
		CellIndices.Add(*CellIndexPtr);
	}

	const FGridAStarQuery AStarQuery = BuildAStarQuery(AgentProperties, Query, StartIndex, *GoalIndexPtr);
	const FGridAStarQuery ValidationQuery =
		UE::GridWorld::Private::MakeInjectedPathValidationQuery(
			AStarQuery,
			InjectedPath.bAllowDynamicAgentConflictsDuringValidation);
	const FGridAStarPathValidationResult Validation = FGridAStar().ValidatePath(
		*Snapshot,
		ValidationQuery,
		CellIndices,
		InjectedPath.bIsPartial);
	if (!Validation.IsValid())
	{
		Output.FailureReason = Validation.FailureReason;
		Output.InvalidSegmentIndex = Validation.InvalidSegmentIndex;
		if (InjectedPath.Cells.IsValidIndex(Validation.InvalidCellIndex))
		{
			Output.InvalidCell = InjectedPath.Cells[Validation.InvalidCellIndex];
		}
		Output.DiagnosticMessage = FString::Printf(
			TEXT("GridWorld rejected the injected path: %s."),
			*StaticEnum<EGridInjectedPathFailureReason>()->GetNameStringByValue(static_cast<int64>(Validation.FailureReason)));
		return Output;
	}

	Output.bIsValid = true;
	Output.FailureReason = EGridInjectedPathFailureReason::None;
	Output.DiagnosticMessage = TEXT("Injected GridWorld path is valid.");
	return Output;
}

FPathFindingResult AGridNavigationData::MaterializeInjectedPath(
	const FGridInjectedPath& InjectedPath,
	const UObject* Querier,
	const FNavAgentProperties& AgentProperties,
	const FVector& StartLocation,
	FNavPathSharedPtr PathInstanceToFill) const
{
	const FGridInjectedPathValidationResult Validation = ValidateInjectedPath(
		InjectedPath,
		Querier,
		AgentProperties,
		StartLocation);
	if (!Validation.bIsValid)
	{
		GRIDWORLD_LOG_WARNING(
			"Exact path materialization failed for querier '%s': %s",
			*GetNameSafe(Querier),
			*Validation.DiagnosticMessage);
		return ENavigationQueryResult::Fail;
	}

	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	const int32* StartIndexPtr = Snapshot.IsValid() ? Snapshot->CellIndexById.Find(InjectedPath.Cells[0]) : nullptr;
	const int32* GoalIndexPtr = Snapshot.IsValid() ? Snapshot->CellIndexById.Find(InjectedPath.OriginalGoalCell) : nullptr;
	if (!Snapshot.IsValid() || StartIndexPtr == nullptr || GoalIndexPtr == nullptr)
	{
		return ENavigationQueryResult::Fail;
	}
	const FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*this,
		Querier,
		InjectedPath.FilterClass);
	FPathFindingQuery Query(
		Querier,
		*this,
		StartLocation,
		Snapshot->Cells[*GoalIndexPtr].WorldCenter,
		QueryFilter,
		PathInstanceToFill);
	Query.SetAllowPartialPaths(InjectedPath.bAllowPartialPath);
	Query.SetNavAgentProperties(AgentProperties);
	if (const AController* Controller = Cast<AController>(Querier))
	{
		if (UPathFollowingComponent* PathFollowing = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			PathFollowing->OnPathfindingQuery(Query);
		}
	}
	FGridAStarQuery AStarQuery = BuildAStarQuery(AgentProperties, Query, *StartIndexPtr, *GoalIndexPtr);
	const FGridAStarQuery ValidationQuery =
		UE::GridWorld::Private::MakeInjectedPathValidationQuery(
			AStarQuery,
			InjectedPath.bAllowDynamicAgentConflictsDuringValidation);
	TArray<int32, TInlineAllocator<64>> CellIndices;
	CellIndices.Reserve(InjectedPath.Cells.Num());
	for (const FGridCellId& CellId : InjectedPath.Cells)
	{
		CellIndices.Add(Snapshot->CellIndexById.FindChecked(CellId));
	}
	FGridAStarPathValidationResult PathValidation = FGridAStar().ValidatePath(
		*Snapshot,
		ValidationQuery,
		CellIndices,
		InjectedPath.bIsPartial);
	if (!PathValidation.IsValid())
	{
		return ENavigationQueryResult::Fail;
	}
	FPathFindingResult Result = MaterializeGridPath(
		AgentProperties,
		Query,
		*Snapshot,
		AStarQuery,
		PathValidation.PathResult,
		AStarQuery.DynamicAgentPolicy,
		false,
		EGridNavigationPathOrigin::Injected,
		InjectedPath.SourcePreviewId,
		InjectedPath.InvalidationPolicy);
	if (FGridNavigationPath* GridPath = Result.Path.IsValid()
		? Result.Path->CastPath<FGridNavigationPath>()
		: nullptr)
	{
		GridPath->PathInstanceId = InjectedPath.PathInstanceId;
		GridPath->ParentPathInstanceId = FGuid();
		GridPath->bAllowDynamicAgentConflictsDuringValidation =
			InjectedPath.bAllowDynamicAgentConflictsDuringValidation;
	}
	return Result;
}

void AGridNavigationData::InvalidateAffectedPaths(const FGridChangeSet& ChangeSet)
{
	if (!IsInGameThread() || (ChangeSet.ChangedCells.IsEmpty() && ChangeSet.ChangedLinks.IsEmpty()))
	{
		return;
	}
	TSet<FGridCellId> ChangedTraversalCells(ChangeSet.ChangedTraversalCells);
	TSet<FGridCellId> ChangedBlockingOccupancyCells(ChangeSet.ChangedBlockingOccupancyCells);
	TSet<FGridCellId> ChangedOccupancyCostCells(ChangeSet.ChangedOccupancyCostCells);
	TSet<FGuid> ChangedLinks(ChangeSet.ChangedLinks);
	const bool bTraversalChanged = ChangeSet.PreviousRevisions.Traversal != ChangeSet.NewRevisions.Traversal;
	const bool bOccupancyChanged = ChangeSet.PreviousRevisions.Occupancy != ChangeSet.NewRevisions.Occupancy;
	UE::TScopeLock PathLock(ActivePathsLock);
	for (int32 PathIndex = ActivePaths.Num() - 1; PathIndex >= 0; --PathIndex)
	{
		FNavPathSharedPtr SharedPath = ActivePaths[PathIndex].Pin();
		if (!SharedPath.IsValid())
		{
			ActivePaths.RemoveAtSwap(PathIndex, EAllowShrinking::No);
			continue;
		}
		FGridNavigationPath* GridPath = SharedPath->CastPath<FGridNavigationPath>();
		if (GridPath == nullptr || !GridPath->IsReady() || GridPath->GetIgnoreInvalidation())
		{
			continue;
		}
		const bool bTraversalCellChanged = GridPath->CellPath.ContainsByPredicate(
			[&ChangedTraversalCells](const FGridCellId& CellId)
			{
				return ChangedTraversalCells.Contains(CellId);
			});
		const bool bBlockingOccupancyCellChanged = GridPath->CellPath.ContainsByPredicate(
			[&ChangedBlockingOccupancyCells](const FGridCellId& CellId)
			{
				return ChangedBlockingOccupancyCells.Contains(CellId);
			});
		const bool bOccupancyCostCellChanged = GridPath->CellPath.ContainsByPredicate(
			[&ChangedOccupancyCostCells](const FGridCellId& CellId)
			{
				return ChangedOccupancyCostCells.Contains(CellId);
			});
		const bool bRelevantOccupancyChange = bOccupancyChanged
			&& ((GridPath->OccupancyPolicy == EGridOccupancyPolicy::Block
					&& bBlockingOccupancyCellChanged)
				|| (GridPath->OccupancyPolicy == EGridOccupancyPolicy::AddCost
					&& bOccupancyCostCellChanged));
		const bool bLinkChanged = GridPath->TraversedLinks.ContainsByPredicate([&ChangedLinks](const FGuid& LinkId)
		{
			return ChangedLinks.Contains(LinkId);
		});
		if ((bTraversalChanged && bTraversalCellChanged)
			|| bRelevantOccupancyChange
			|| bLinkChanged)
		{
			GridPath->Invalidate();
		}
	}
}

FPathFindingResult AGridNavigationData::FindPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query)
{
	const AGridNavigationData* Self = Cast<AGridNavigationData>(Query.NavData.Get());
	if (Self == nullptr)
	{
		return ENavigationQueryResult::Error;
	}

	const FGridWorldSnapshotPtr Snapshot = Self->GetSnapshot();
	if (!Snapshot.IsValid())
	{
		return ENavigationQueryResult::Fail;
	}

	FNavLocation ProjectedStart;
	FNavLocation ProjectedGoal;
	if (!Self->ProjectPoint(Query.StartLocation, ProjectedStart, Self->GetDefaultQueryExtent(), Query.QueryFilter, Query.Owner.Get())
		|| !Self->ProjectPoint(Query.EndLocation, ProjectedGoal, Self->GetDefaultQueryExtent(), Query.QueryFilter, Query.Owner.Get()))
	{
		return ENavigationQueryResult::Fail;
	}
	const int32 StartIndex = Snapshot->ResolveNodeRef(ProjectedStart.NodeRef);
	const int32 GoalIndex = Snapshot->ResolveNodeRef(ProjectedGoal.NodeRef);

	FGridAStarQuery AStarQuery = Self->BuildAStarQuery(AgentProperties, Query, StartIndex, GoalIndex);
	FGridAStar AStar;
	const EGridDynamicAgentPolicy RequestedDynamicAgentPolicy = AStarQuery.DynamicAgentPolicy;
	FGridAStarResult SearchResult = AStar.FindPath(*Snapshot, AStarQuery);
	bool bUsedDynamicAgentFallback = false;
	if (!SearchResult.IsSuccessful()
		&& (RequestedDynamicAgentPolicy == EGridDynamicAgentPolicy::YieldThenRepath
			|| RequestedDynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor))
	{
		// A one-cell corridor must remain a valid waiting route. Retry without treating
		// transient agents as closed topology when no alternative route exists.
		AStarQuery.DynamicAgentPolicy = EGridDynamicAgentPolicy::Yield;
		SearchResult = AStar.FindPath(*Snapshot, AStarQuery);
		bUsedDynamicAgentFallback = SearchResult.IsSuccessful();
	}
	if (!SearchResult.IsSuccessful())
	{
		return ENavigationQueryResult::Fail;
	}

	const FGridNavigationPath* ExistingPath = Query.PathInstanceToFill.IsValid()
		? Query.PathInstanceToFill->CastPath<FGridNavigationPath>()
		: nullptr;
	const EGridNavigationPathOrigin Origin = ExistingPath != nullptr
		? EGridNavigationPathOrigin::Recalculated
		: EGridNavigationPathOrigin::Computed;
	const FGuid SourcePreviewId = ExistingPath != nullptr ? ExistingPath->SourcePreviewId : FGuid();
	const EGridInjectedPathInvalidationPolicy InvalidationPolicy = ExistingPath != nullptr
		? ExistingPath->InjectedInvalidationPolicy
		: EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;
	return Self->MaterializeGridPath(
		AgentProperties,
		Query,
		*Snapshot,
		AStarQuery,
		SearchResult,
		RequestedDynamicAgentPolicy,
		bUsedDynamicAgentFallback,
		Origin,
		SourcePreviewId,
		InvalidationPolicy);
}

FPathFindingResult AGridNavigationData::MaterializeGridPath(
	const FNavAgentProperties& AgentProperties,
	const FPathFindingQuery& Query,
	const FGridWorldSnapshot& Snapshot,
	const FGridAStarQuery& AStarQuery,
	const FGridAStarResult& SearchResult,
	EGridDynamicAgentPolicy RequestedDynamicAgentPolicy,
	bool bUsedDynamicAgentFallback,
	EGridNavigationPathOrigin Origin,
	const FGuid& SourcePreviewId,
	EGridInjectedPathInvalidationPolicy InvalidationPolicy) const
{
	const int32 StartIndex = AStarQuery.StartCellIndex;
	const int32 GoalIndex = AStarQuery.GoalCellIndex;
	FPathFindingResult Result(SearchResult.Status == EGridQueryStatus::Partial
		? ENavigationQueryResult::Success
		: ENavigationQueryResult::Success);
	FNavPathSharedPtr SharedPath = Query.PathInstanceToFill;
	FGridNavigationPath* GridPath = SharedPath.IsValid() ? SharedPath->CastPath<FGridNavigationPath>() : nullptr;
	if (GridPath == nullptr)
	{
		SharedPath = this->CreatePathInstance<FGridNavigationPath>(Query);
		GridPath = SharedPath->CastPath<FGridNavigationPath>();
	}
	check(GridPath != nullptr);
	Result.Path = SharedPath;
	const FGuid PreviousPathInstanceId = GridPath->PathInstanceId;
	GridPath->ResetForRepath();
	GridPath->Origin = Origin;
	GridPath->PathInstanceId = FGuid::NewGuid();
	GridPath->ParentPathInstanceId = Origin == EGridNavigationPathOrigin::Recalculated
		? PreviousPathInstanceId
		: FGuid();
	GridPath->SourcePreviewId = SourcePreviewId;
	GridPath->InjectedInvalidationPolicy = InvalidationPolicy;
	GridPath->Revisions = Snapshot.Revisions;
	GridPath->OptimizationMode = AStarQuery.PathOptimizationMode;
	GridPath->TurnCount = SearchResult.TurnCount;
	GridPath->VisitedNodes = SearchResult.VisitedNodes;
	GridPath->OccupancyPolicy = AStarQuery.OccupancyPolicy;
	GridPath->DynamicAgentPolicy = RequestedDynamicAgentPolicy;
	GridPath->TrafficReservationRevision = AStarQuery.TrafficReservations.IsValid()
		? AStarQuery.TrafficReservations->Revision
		: 0;
	GridPath->bUsedDynamicAgentFallback = bUsedDynamicAgentFallback;
	if (Query.QueryFilter.IsValid())
	{
		if (const FGridNavigationQueryFilterImpl* GridFilter = UE::GridWorld::Private::GetGridFilter(Query.QueryFilter))
		{
			GridPath->MinimumAgentLookAheadCells = GridFilter->GetMinimumAgentLookAheadCells();
			GridPath->ReservedLookAheadCells = GridFilter->GetReservedLookAheadCells();
			GridPath->AdditionalAgentSeparation = GridFilter->GetAdditionalAgentSeparation();
			GridPath->StationaryAgentSpeedThreshold = GridFilter->GetStationaryAgentSpeedThreshold();
			GridPath->DynamicAgentRepathDelay = GridFilter->GetDynamicAgentRepathDelay();
			GridPath->IgnoredOccupancyOwnerId = GridFilter->GetIgnoredOccupancyOwnerId();
		}
	}
	TArray<int32, TInlineAllocator<64>> CellPathPointIndices;
	CellPathPointIndices.Reserve(SearchResult.CellIndices.Num());

	const FGridRegionData* StartRegion = Snapshot.FindRegion(Snapshot.Cells[StartIndex].Id.GridId);
	const bool bCenterStartCell = StartRegion != nullptr
		&& StartRegion->PathFollowingStyle != EGridPathFollowingStyle::Standard;
	if (bCenterStartCell)
	{
		GridPath->GetPathPoints().Emplace(Query.StartLocation, Snapshot.MakeNodeRef(StartIndex));
		FGridPathPointFollowingData& StartData = GridPath->PathPointFollowingData.AddDefaulted_GetRef();
		StartData.CellId = Snapshot.Cells[StartIndex].Id;
		StartData.GridRotation = StartRegion->GridTransform.Rotation;
		GridPath->CellPathPointOffset = 1;
	}

	for (int32 PathIndex = 0; PathIndex < SearchResult.CellIndices.Num(); ++PathIndex)
	{
		const int32 CellIndex = SearchResult.CellIndices[PathIndex];
		const FGridCellData& Cell = Snapshot.Cells[CellIndex];
		const NavNodeRef NodeRef = Snapshot.MakeNodeRef(CellIndex);
		GridPath->CellPath.Add(Cell.Id);
		GridPath->NodePath.Add(NodeRef);
		GridPath->MaximumFloorSlopeDegrees = FMath::Max(
			GridPath->MaximumFloorSlopeDegrees,
			static_cast<float>(UE::GridWorld::WalkingSurface::CalculateFloorSlopeDegrees(Cell.FloorNormal)));
		const int32 PointIndex = bCenterStartCell
			? GridPath->GetPathPoints().Emplace(Cell.WorldCenter, NodeRef)
			: UE::GridWorld::Private::AddPathPointIfDistinct(*GridPath, Cell.WorldCenter, NodeRef);
		CellPathPointIndices.Add(PointIndex);
		if (!GridPath->PathPointFollowingData.IsValidIndex(PointIndex))
		{
			const FGridRegionData* Region = Snapshot.FindRegion(Cell.Id.GridId);
			FGridPathPointFollowingData& PointData = GridPath->PathPointFollowingData.AddDefaulted_GetRef();
			PointData.CellId = Cell.Id;
			PointData.bIsCellCenter = true;
			if (Region != nullptr)
			{
				PointData.GridRotation = Region->GridTransform.Rotation;
				PointData.Style = Region->PathFollowingStyle;
				PointData.DriveMode = Region->PathDriveMode;
				PointData.bUseAcceleratedFinalApproach = Region->bUseAcceleratedFinalApproach;
				PointData.CellCenterTolerance = Region->CellCenterTolerance;
				PointData.StopSpeedTolerance = Region->StopSpeedTolerance;
				PointData.CenterGateHalfWidth = FMath::Max(
					Region->CellCenterTolerance,
					static_cast<float>(FMath::Min(
						FMath::Abs(Region->GridTransform.CellSize.X),
						FMath::Abs(Region->GridTransform.CellSize.Y)) * 0.25));
			}
		}

		if (PathIndex > 0)
		{
			const int32 PreviousCellIndex = SearchResult.CellIndices[PathIndex - 1];
			const FVector PreviousCenter = Snapshot.Cells[SearchResult.CellIndices[PathIndex - 1]].WorldCenter;
			GridPath->TotalLength += FVector::Distance(PreviousCenter, Cell.WorldCenter);
			GridPath->SegmentCosts.Add(static_cast<FVector::FReal>(SearchResult.TotalCost) / FMath::Max(1, SearchResult.CellIndices.Num() - 1) / 1000.0);
			for (const FGridLinkData& Link : Snapshot.Links)
			{
				if (Link.bEnabled && ((Link.FromCellIndex == PreviousCellIndex && Link.ToCellIndex == CellIndex)
					|| (Link.bBidirectional && Link.ToCellIndex == PreviousCellIndex && Link.FromCellIndex == CellIndex)))
				{
					GridPath->TraversedLinks.Add(Link.LinkId);
					GridPath->PathPointFollowingData[PointIndex].bIsLinkBoundary = true;
					const int32 PreviousPointIndex = CellPathPointIndices[PathIndex - 1];
					if (GridPath->PathPointFollowingData.IsValidIndex(PreviousPointIndex))
					{
						GridPath->PathPointFollowingData[PreviousPointIndex].bIsLinkBoundary = true;
					}
					break;
				}
			}
		}
	}

	for (int32 PathIndex = 0; PathIndex < SearchResult.CellIndices.Num(); ++PathIndex)
	{
		const int32 PointIndex = CellPathPointIndices[PathIndex];
		FGridPathPointFollowingData& PointData = GridPath->PathPointFollowingData[PointIndex];
		const bool bIsFinalCell = PathIndex == SearchResult.CellIndices.Num() - 1;
		PointData.bRequiresStop = PointData.Style != EGridPathFollowingStyle::Standard && bIsFinalCell;
		if (!PointData.bRequiresStop && PointData.Style != EGridPathFollowingStyle::Standard)
		{
			const bool bStyleBoundary = (PathIndex > 0
				&& GridPath->PathPointFollowingData[CellPathPointIndices[PathIndex - 1]].Style != PointData.Style)
				|| (PathIndex + 1 < SearchResult.CellIndices.Num()
					&& GridPath->PathPointFollowingData[CellPathPointIndices[PathIndex + 1]].Style != PointData.Style);
			PointData.bRequiresCenterGate = PointData.Style == EGridPathFollowingStyle::CellByCell
				|| PathIndex == 0
				|| PointData.bIsLinkBoundary
				|| bStyleBoundary
				|| UE::GridWorld::Private::IsPathTurn(GridPath->GetPathPoints(), PointIndex, PointData.GridRotation);
		}
	}

	GridPath->SetIsPartial(SearchResult.Status == EGridQueryStatus::Partial);
	GridPath->SetSearchReachedLimit(SearchResult.bReachedSearchLimit);
	GridPath->MarkReady();
	if (IsInGameThread())
	{
		AGridNavigationData* MutableSelf = const_cast<AGridNavigationData*>(this);
		if (const AAIController* Controller = Cast<AAIController>(Query.Owner.Get()))
		{
			if (AStarQuery.PathOptimizationMode != EGridPathOptimizationMode::ShortestPath
				&& SearchResult.Status == EGridQueryStatus::Partial
				&& SearchResult.bReachedSearchLimit
				&& !MutableSelf->SearchLimitWarningControllers.Contains(Controller))
			{
				MutableSelf->SearchLimitWarningControllers.Add(const_cast<AAIController*>(Controller));
				const FGridCellCoord& GoalCoord = Snapshot.Cells[GoalIndex].Id.Coord;
				const int32 PartialEndIndex = SearchResult.CellIndices.IsEmpty()
					? StartIndex
					: SearchResult.CellIndices.Last();
				const FGridCellCoord& PartialEndCoord = Snapshot.Cells[PartialEndIndex].Id.Coord;
				GRIDWORLD_LOG_WARNING(
					"Controller '%s' received a partial %s GridWorld path after reaching the Max Search States limit (%d). Goal=(%d,%d,%d), partial end=(%d,%d,%d). Increase Max Search States on the assigned Grid Navigation Query Filter or disable Accept Partial Path.",
					*GetNameSafe(Controller),
					*StaticEnum<EGridPathOptimizationMode>()->GetNameStringByValue(static_cast<int64>(AStarQuery.PathOptimizationMode)),
					AStarQuery.MaxVisitedNodes,
					GoalCoord.X,
					GoalCoord.Y,
					GoalCoord.Layer,
					PartialEndCoord.X,
					PartialEndCoord.Y,
					PartialEndCoord.Layer);
			}
			const UPathFollowingComponent* ControllerPathFollowing = Controller->GetPathFollowingComponent();
			if (GridPath->RequiresPrecisePathFollowing()
				&& (ControllerPathFollowing == nullptr || !ControllerPathFollowing->IsA<UGridWorldPathFollowingComponent>())
				&& !MutableSelf->PrecisePathWarningControllers.Contains(Controller))
			{
				MutableSelf->PrecisePathWarningControllers.Add(const_cast<AAIController*>(Controller));
				GRIDWORLD_LOG_WARNING(
					"Controller '%s' requested a precise GridWorld path using '%s'. The path will use Standard following; reparent the controller to GridWorldAIController to enforce cell centers.",
					*GetNameSafe(Controller),
					*GetNameSafe(ControllerPathFollowing));
			}
			const APawn* Pawn = Controller->GetPawn();
			const UCharacterMovementComponent* CharacterMovement = Pawn != nullptr
				? Pawn->FindComponentByClass<UCharacterMovementComponent>()
				: nullptr;
			if (CharacterMovement != nullptr
				&& UE::GridWorld::WalkingSurface::RequiresWalkableFloorWarning(
					GridPath->MaximumFloorSlopeDegrees,
					CharacterMovement->GetWalkableFloorAngle())
				&& !MutableSelf->SlopeWarningControllers.Contains(Controller))
			{
				MutableSelf->SlopeWarningControllers.Add(const_cast<AAIController*>(Controller));
				GRIDWORLD_LOG_WARNING(
					"Controller '%s' received a GridWorld path with %.1f degree floor slope, but pawn '%s' supports only %.1f degrees. Increase Character Movement > Walkable Floor Angle to traverse this path.",
					*GetNameSafe(Controller),
					GridPath->MaximumFloorSlopeDegrees,
					*GetNameSafe(Pawn),
					CharacterMovement->GetWalkableFloorAngle());
			}
			MutableSelf->TrackActiveDebugPath(*const_cast<AAIController*>(Controller), SharedPath);
		}
		else
		{
			this->SetDebugPath(GridPath->GetPathPoints());
		}
	}
	return Result;
}

bool AGridNavigationData::TestPath(const FNavAgentProperties& AgentProperties, const FPathFindingQuery& Query, int32* NumVisitedNodes)
{
	const FPathFindingResult Result = FindPath(AgentProperties, Query);
	if (NumVisitedNodes != nullptr)
	{
		const FGridNavigationPath* GridPath = Result.Path.IsValid() ? Result.Path->CastPath<FGridNavigationPath>() : nullptr;
		*NumVisitedNodes = GridPath != nullptr ? GridPath->VisitedNodes : 0;
	}
	return Result.IsSuccessful();
}

bool AGridNavigationData::NavigationRaycast(const ANavigationData* NavDataInstance, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FNavigationRaycastAdditionalResults* AdditionalResults, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier)
{
	const AGridNavigationData* GridNavData = Cast<AGridNavigationData>(NavDataInstance);
	const FGridWorldSnapshotPtr Snapshot = GridNavData != nullptr ? GridNavData->GetSnapshot() : nullptr;
	bool bReachedEnd = false;
	HitLocation = RayStart;
	if (Snapshot.IsValid() && !RayStart.ContainsNaN() && !RayEnd.ContainsNaN())
	{
		double MinimumCellSize = 50.0;
		for (const TPair<FGuid, FGridRegionData>& RegionPair : Snapshot->Regions)
		{
			MinimumCellSize = FMath::Min(MinimumCellSize, FMath::Min(RegionPair.Value.GridTransform.CellSize.X, RegionPair.Value.GridTransform.CellSize.Y));
		}
		const double Distance = FVector::Distance(RayStart, RayEnd);
		const int32 StepCount = FMath::Clamp(FMath::CeilToInt(Distance / FMath::Max(1.0, MinimumCellSize * 0.25)), 1, 65536);
		int32 PreviousCellIndex = INDEX_NONE;
		const FGridNavigationQueryFilterImpl* GridFilter = UE::GridWorld::Private::GetGridFilter(QueryFilter);
		for (int32 StepIndex = 0; StepIndex <= StepCount; ++StepIndex)
		{
			const double Alpha = static_cast<double>(StepIndex) / StepCount;
			const FVector Sample = FMath::Lerp(RayStart, RayEnd, Alpha);
			int32 CellIndex = INDEX_NONE;
			if (!UE::GridWorld::Private::FindProjectedCell(*Snapshot, Sample, CellIndex, QueryFilter)
				|| (PreviousCellIndex != INDEX_NONE && !UE::GridWorld::Private::AreCorridorNeighbors(*Snapshot, PreviousCellIndex, CellIndex, GridFilter)))
			{
				break;
			}
			HitLocation = Sample;
			PreviousCellIndex = CellIndex;
			bReachedEnd = StepIndex == StepCount;
		}
	}
	if (AdditionalResults != nullptr)
	{
		AdditionalResults->bIsRayEndInCorridor = bReachedEnd;
	}
	return !bReachedEnd;
}

void AGridNavigationData::BatchRaycast(TArray<FNavigationRaycastWork>& Workload, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier) const
{
	for (FNavigationRaycastWork& Work : Workload)
	{
		FVector HitLocation = Work.RayEnd;
		FNavigationRaycastAdditionalResults Results;
		Work.bDidHit = NavigationRaycast(this, Work.RayStart, Work.RayEnd, HitLocation, &Results, QueryFilter, Querier);
		FNavLocation Projected;
		ProjectPoint(HitLocation, Projected, GetDefaultQueryExtent(), QueryFilter, Querier);
		Work.HitLocation = Projected;
		Work.bIsRayEndInCorridor = Results.bIsRayEndInCorridor;
	}
}

bool AGridNavigationData::FindMoveAlongSurface(const FNavLocation& StartLocation, const FVector& TargetPosition, FNavLocation& OutLocation, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	FVector CorridorEnd = TargetPosition;
	FNavigationRaycastAdditionalResults AdditionalResults;
	NavigationRaycast(this, StartLocation.Location, TargetPosition, CorridorEnd, &AdditionalResults, Filter, Querier);
	return ProjectPoint(CorridorEnd, OutLocation, GetDefaultQueryExtent(), Filter, Querier);
}

void AGridNavigationData::BatchFindMoveAlongSurface(TArray<FNavigationFindMoveAlongSurfaceWork>& Workload, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	for (FNavigationFindMoveAlongSurfaceWork& Work : Workload)
	{
		Work.bResult = FindMoveAlongSurface(Work.StartLocation, Work.DesiredEndLocation, Work.ResultNavLocation, Filter, Querier);
	}
}

bool AGridNavigationData::FindOverlappingEdges(const FNavLocation& StartLocation, TConstArrayView<FVector> ConvexPolygon, TArray<FVector>& OutEdges, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	OutEdges.Reset();
	return false;
}

bool AGridNavigationData::GetPathSegmentBoundaryEdges(const FNavigationPath& Path, const FNavPathPoint& StartPoint, const FNavPathPoint& EndPoint, TConstArrayView<FVector> SearchArea, TArray<FVector>& OutEdges, float MaxAreaEnterCost, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	OutEdges.Reset();
	return false;
}

FNavLocation AGridNavigationData::GetRandomPoint(FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	if (!Snapshot.IsValid())
	{
		return FNavLocation();
	}
	TArray<int32, TInlineAllocator<256>> CandidateIndices;
	for (int32 Index = 0; Index < Snapshot->Cells.Num(); ++Index)
	{
		if (UE::GridWorld::Private::PassesFilter(Snapshot->Cells[Index], Filter))
		{
			CandidateIndices.Add(Index);
		}
	}
	if (!CandidateIndices.IsEmpty())
	{
		FRandomStream Random(static_cast<int32>(FPlatformTime::Cycles()));
		const int32 SelectedIndex = CandidateIndices[Random.RandRange(0, CandidateIndices.Num() - 1)];
		return FNavLocation(Snapshot->Cells[SelectedIndex].WorldCenter, Snapshot->MakeNodeRef(SelectedIndex));
	}
	return FNavLocation();
}

bool AGridNavigationData::GetRandomReachablePointInRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	if (Radius < 0.0f)
	{
		return false;
	}
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	int32 StartIndex = INDEX_NONE;
	if (!Snapshot.IsValid() || !UE::GridWorld::Private::FindProjectedCell(*Snapshot, Origin, StartIndex, Filter))
	{
		return false;
	}
	TArray<int32> Queue;
	TArray<int32> Candidates;
	TBitArray<> Visited(false, Snapshot->Cells.Num());
	Queue.Add(StartIndex);
	Visited[StartIndex] = true;
	for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
	{
		const int32 CellIndex = Queue[QueueIndex];
		const FGridCellData& Cell = Snapshot->Cells[CellIndex];
		if (FVector::DistSquared(Origin, Cell.WorldCenter) > FMath::Square(Radius))
		{
			continue;
		}
		Candidates.Add(CellIndex);
		for (const int32 NeighborIndex : Cell.Neighbors)
		{
			if (Snapshot->Cells.IsValidIndex(NeighborIndex)
				&& !Visited[NeighborIndex]
				&& UE::GridWorld::Private::PassesFilter(Snapshot->Cells[NeighborIndex], Filter))
			{
				Visited[NeighborIndex] = true;
				Queue.Add(NeighborIndex);
			}
		}
	}
	if (Candidates.IsEmpty())
	{
		return false;
	}
	FRandomStream Random(static_cast<int32>(FPlatformTime::Cycles()));
	const int32 SelectedIndex = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
	OutResult = FNavLocation(Snapshot->Cells[SelectedIndex].WorldCenter, Snapshot->MakeNodeRef(SelectedIndex));
	return true;
}

bool AGridNavigationData::GetRandomPointInNavigableRadius(const FVector& Origin, float Radius, FNavLocation& OutResult, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	if (Radius < 0.0f)
	{
		return false;
	}
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	if (!Snapshot.IsValid())
	{
		return false;
	}
	for (int32 Index = 0; Index < Snapshot->Cells.Num(); ++Index)
	{
		const FGridCellData& Cell = Snapshot->Cells[Index];
		if (UE::GridWorld::Private::PassesFilter(Cell, Filter) && FVector::DistSquared(Origin, Cell.WorldCenter) <= FMath::Square(Radius))
		{
			OutResult = FNavLocation(Cell.WorldCenter, Snapshot->MakeNodeRef(Index));
			return true;
		}
	}
	return false;
}

bool AGridNavigationData::ProjectPoint(const FVector& Point, FNavLocation& OutLocation, const FVector& Extent, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	if (!Snapshot.IsValid())
	{
		return false;
	}
	int32 BestIndex = INDEX_NONE;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (int32 CellIndex = 0; CellIndex < Snapshot->Cells.Num(); ++CellIndex)
	{
		const FGridCellData& Cell = Snapshot->Cells[CellIndex];
		if (!UE::GridWorld::Private::PassesFilter(Cell, Filter))
		{
			continue;
		}
		const FVector Delta = Cell.WorldCenter - Point;
		if (FMath::Abs(Delta.X) > Extent.X || FMath::Abs(Delta.Y) > Extent.Y || FMath::Abs(Delta.Z) > Extent.Z)
		{
			continue;
		}
		const double DistanceSquared = Delta.SquaredLength();
		if (DistanceSquared < BestDistanceSquared
			|| (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared) && BestIndex != INDEX_NONE && Cell.Id.Coord < Snapshot->Cells[BestIndex].Id.Coord))
		{
			BestDistanceSquared = DistanceSquared;
			BestIndex = CellIndex;
		}
	}
	if (!Snapshot->Cells.IsValidIndex(BestIndex))
	{
		return false;
	}
	OutLocation = FNavLocation(Snapshot->Cells[BestIndex].WorldCenter, Snapshot->MakeNodeRef(BestIndex));
	return true;
}

void AGridNavigationData::BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, const FVector& Extent, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	for (FNavigationProjectionWork& Work : Workload)
	{
		Work.bResult = Work.bIsValid && ProjectPoint(Work.Point, Work.OutLocation, Extent, Filter, Querier);
	}
}

void AGridNavigationData::BatchProjectPoints(TArray<FNavigationProjectionWork>& Workload, FSharedConstNavQueryFilter Filter, const UObject* Querier) const
{
	for (FNavigationProjectionWork& Work : Workload)
	{
		const FVector Extent = Work.ProjectionLimit.IsValid ? Work.ProjectionLimit.GetExtent() : GetDefaultQueryExtent();
		Work.bResult = Work.bIsValid && ProjectPoint(Work.Point, Work.OutLocation, Extent, Filter, Querier);
	}
}

ENavigationQueryResult::Type AGridNavigationData::CalcPathCost(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathCost, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier) const
{
	FPathFindingQuery Query(Querier, *this, PathStart, PathEnd, QueryFilter);
	const FPathFindingResult Result = FindPath(GetConfig(), Query);
	if (!Result.IsSuccessful())
	{
		return Result.Result;
	}
	OutPathCost = Result.Path->GetCost();
	return Result.Result;
}

ENavigationQueryResult::Type AGridNavigationData::CalcPathLength(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathLength, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier) const
{
	FVector::FReal IgnoredCost = 0.0;
	return CalcPathLengthAndCost(PathStart, PathEnd, OutPathLength, IgnoredCost, QueryFilter, Querier);
}

ENavigationQueryResult::Type AGridNavigationData::CalcPathLengthAndCost(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathLength, FVector::FReal& OutPathCost, FSharedConstNavQueryFilter QueryFilter, const UObject* Querier) const
{
	FPathFindingQuery Query(Querier, *this, PathStart, PathEnd, QueryFilter);
	const FPathFindingResult Result = FindPath(GetConfig(), Query);
	if (!Result.IsSuccessful())
	{
		return Result.Result;
	}
	OutPathLength = Result.Path->GetLength();
	OutPathCost = Result.Path->GetCost();
	return Result.Result;
}

bool AGridNavigationData::DoesNodeContainLocation(NavNodeRef NodeRef, const FVector& WorldSpaceLocation) const
{
	const FGridWorldSnapshotPtr Snapshot = GetSnapshot();
	const int32 CellIndex = Snapshot.IsValid() ? Snapshot->ResolveNodeRef(NodeRef) : INDEX_NONE;
	if (!Snapshot.IsValid() || !Snapshot->Cells.IsValidIndex(CellIndex))
	{
		return false;
	}
	const FGridRegionData* Region = Snapshot->FindRegion(Snapshot->Cells[CellIndex].Id.GridId);
	if (Region == nullptr)
	{
		return false;
	}
	const FVector LocalDelta = Region->GridTransform.WorldToLocal(WorldSpaceLocation) - Region->GridTransform.WorldToLocal(Snapshot->Cells[CellIndex].WorldCenter);
	const FVector HalfExtent = Region->GridTransform.CellSize * 0.5;
	return FMath::Abs(LocalDelta.X) <= HalfExtent.X
		&& FMath::Abs(LocalDelta.Y) <= HalfExtent.Y
		&& FMath::Abs(LocalDelta.Z) <= HalfExtent.Z;
}

UPrimitiveComponent* AGridNavigationData::ConstructRenderingComponent()
{
	return NewObject<UGridNavigationRenderingComponent>(this, TEXT("GridNavigationRenderingComponent"), RF_Transient);
}
