// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridRuntimeVisualizationSubsystem.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GridWorldModule.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Navigation/GridNavigationData.h"
#include "Presentation/GridCellVisualStyle.h"
#include "Presentation/GridRuntimeVisualizationActor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/GridWorldSubsystem.h"

namespace UE::GridWorld::Presentation::Private
{
	const FSoftObjectPath DefaultStylePath(TEXT("/GridWorldSystem/Presentation/DA_GridRuntimeCellStyle_Default.DA_GridRuntimeCellStyle_Default"));

	bool LessCellId(const FGridCellId& Left, const FGridCellId& Right)
	{
		return Left.GridId != Right.GridId ? Left.GridId < Right.GridId : Left.Coord < Right.Coord;
	}
}

bool UGridRuntimeVisualizationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer) || IsRunningDedicatedServer())
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->GetNetMode() != NM_DedicatedServer;
}

bool UGridRuntimeVisualizationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGridRuntimeVisualizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGridWorldSubsystem>();
	if (UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem())
	{
		GridSubsystem->OnGridWorldChanged.AddDynamic(this, &UGridRuntimeVisualizationSubsystem::HandleGridWorldChanged);
	}
}

void UGridRuntimeVisualizationSubsystem::Deinitialize()
{
	if (UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem())
	{
		GridSubsystem->OnGridWorldChanged.RemoveDynamic(this, &UGridRuntimeVisualizationSubsystem::HandleGridWorldChanged);
	}
	DestroyVisualizationActor();
	ActiveStyle = nullptr;
	HoveredCells.Reset();
	SelectedCells.Reset();
	OverlayStates.Reset();
	PathStates.Reset();
	bVisualizationEnabled = false;
	Super::Deinitialize();
}

bool UGridRuntimeVisualizationSubsystem::EnableVisualization(UGridCellVisualStyle* Style)
{
	UGridCellVisualStyle* ResolvedStyle = ResolveStyle(Style);
	FString ValidationError;
	if (ResolvedStyle == nullptr || !ResolvedStyle->Validate(ValidationError))
	{
		GRIDWORLD_LOG_ERROR(
			"Cannot enable runtime cell visualization in world '%s': %s",
			*GetNameSafe(GetWorld()),
			ResolvedStyle != nullptr ? *ValidationError : TEXT("the requested/default style could not be loaded."));
		return false;
	}

	const bool bStyleChanged = ActiveStyle != ResolvedStyle;
	ActiveStyle = ResolvedStyle;
	bVisualizationEnabled = true;
	bVisualizationVisible = true;
	if (!EnsureVisualizationActor())
	{
		bVisualizationEnabled = false;
		return false;
	}
	if (bStyleChanged || CellHandles.IsEmpty())
	{
		return RebuildVisualization();
	}
	ApplyVisibility();
	return true;
}

void UGridRuntimeVisualizationSubsystem::DisableVisualization()
{
	bVisualizationEnabled = false;
	DestroyVisualizationActor();
}

void UGridRuntimeVisualizationSubsystem::SetVisualizationVisible(bool bVisible)
{
	bVisualizationVisible = bVisible;
	ApplyVisibility();
}

bool UGridRuntimeVisualizationSubsystem::RebuildVisualization()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GridWorld_RuntimeVisualizationBuild);
	if (!bVisualizationEnabled || ActiveStyle == nullptr || !EnsureVisualizationActor())
	{
		return false;
	}

	for (const TPair<FGridChunkCoord, FGridChunkRenderer>& Pair : ChunkRenderers)
	{
		if (UHierarchicalInstancedStaticMeshComponent* Component = Pair.Value.Component.Get())
		{
			Component->DestroyComponent();
		}
	}
	ChunkRenderers.Reset();
	CellHandles.Reset();
	++RendererGeneration;

	UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid())
	{
		HoveredCells.Reset();
		SelectedCells.Reset();
		OverlayStates.Reset();
		PathStates.Reset();
		CachedRevisions = FGridRevisionSet();
		ApplyVisibility();
		return true;
	}

	PruneStaleInteractionState(*Snapshot);
	TArray<FGridChunkCoord> SortedChunks;
	Snapshot->Chunks.GenerateKeyArray(SortedChunks);
	SortedChunks.Sort();
	int32 RendererId = 0;
	bool bBuildSucceeded = true;
	AGridRuntimeVisualizationActor* Owner = Cast<AGridRuntimeVisualizationActor>(VisualizationActor);
	for (const FGridChunkCoord& ChunkCoord : SortedChunks)
	{
		const FGridChunkData* Chunk = Snapshot->Chunks.Find(ChunkCoord);
		if (Chunk == nullptr || Chunk->CellIndices.IsEmpty())
		{
			continue;
		}

		TArray<int32> SortedCellIndices = Chunk->CellIndices;
		SortedCellIndices.Sort([&Snapshot](int32 LeftIndex, int32 RightIndex)
		{
			return Snapshot->Cells.IsValidIndex(LeftIndex)
				&& Snapshot->Cells.IsValidIndex(RightIndex)
				&& UE::GridWorld::Presentation::Private::LessCellId(Snapshot->Cells[LeftIndex].Id, Snapshot->Cells[RightIndex].Id);
		});

		UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(
			Owner,
			MakeUniqueObjectName(Owner, UHierarchicalInstancedStaticMeshComponent::StaticClass(), TEXT("GridCellChunk")),
			RF_Transient);
		Owner->AddInstanceComponent(Component);
		Component->SetupAttachment(Owner->GetPresentationRoot());
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(ActiveStyle->CellMesh);
		Component->SetMaterial(0, ActiveStyle->CellMaterial);
		Component->SetNumCustomDataFloats(FGridCellMaterialDataLayout::NumFloats);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(ActiveStyle->bCastShadow);
		Component->bReceivesDecals = false;
		Component->SetCullDistances(ActiveStyle->StartCullDistance, ActiveStyle->EndCullDistance);
		Component->RegisterComponent();

		TArray<FTransform> Transforms;
		TArray<const FGridCellData*> Cells;
		Transforms.Reserve(SortedCellIndices.Num());
		Cells.Reserve(SortedCellIndices.Num());
		for (const int32 CellIndex : SortedCellIndices)
		{
			if (!Snapshot->Cells.IsValidIndex(CellIndex))
			{
				continue;
			}
			const FGridCellData& Cell = Snapshot->Cells[CellIndex];
			const FGridRegionData* Region = Snapshot->FindRegion(Cell.Id.GridId);
			if (Region == nullptr)
			{
				continue;
			}
			Transforms.Add(BuildCellTransform(*Region, Cell));
			Cells.Add(&Cell);
		}
		if (Cells.Num() != SortedCellIndices.Num())
		{
			GRIDWORLD_LOG_ERROR(
				"Runtime visualization chunk (%d,%d,%d) on grid %s contains invalid cells or regions.",
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Layer,
				*ChunkCoord.GridId.ToString(EGuidFormats::Short));
			Component->DestroyComponent();
			bBuildSucceeded = false;
			continue;
		}

		const TArray<int32> InstanceIndices = Component->AddInstances(Transforms, true, true, false);
		if (InstanceIndices.Num() != Cells.Num())
		{
			GRIDWORLD_LOG_ERROR(
				"Runtime visualization chunk (%d,%d,%d) on grid %s created %d of %d requested instances.",
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Layer,
				*ChunkCoord.GridId.ToString(EGuidFormats::Short),
				InstanceIndices.Num(),
				Cells.Num());
			Component->DestroyComponent();
			bBuildSucceeded = false;
			continue;
		}

		FGridChunkRenderer& Renderer = ChunkRenderers.Add(ChunkCoord);
		Renderer.Component = Component;
		Renderer.RendererId = RendererId;
		for (int32 CellOffset = 0; CellOffset < Cells.Num(); ++CellOffset)
		{
			FGridCellVisualHandle& Handle = CellHandles.Add(Cells[CellOffset]->Id);
			Handle.RendererId = RendererId;
			Handle.InstanceIndex = InstanceIndices[CellOffset];
			Handle.RendererGeneration = RendererGeneration;
			bBuildSucceeded &= ApplyCellCustomData(Cells[CellOffset]->Id, false);
		}
		Component->BuildTreeIfOutdated(false, true);
		Component->MarkRenderStateDirty();
		++RendererId;
	}

	CachedRevisions = Snapshot->Revisions;
	ApplyVisibility();
	if (!bBuildSucceeded)
	{
		GRIDWORLD_LOG_ERROR("Runtime cell visualization build failed in world '%s'; partial rendering resources were released.", *GetNameSafe(GetWorld()));
		bVisualizationEnabled = false;
		DestroyVisualizationActor();
		return false;
	}
	return true;
}

void UGridRuntimeVisualizationSubsystem::RefreshCells(const TArray<FGridCellId>& CellIds)
{
	ApplyCustomDataBatch(CellIds);
}

bool UGridRuntimeVisualizationSubsystem::SetCellHovered(const FGridCellId& CellId, bool bHovered)
{
	UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	if (GridSubsystem == nullptr || GridSubsystem->GetCell(CellId).Status != EGridQueryStatus::Success)
	{
		return false;
	}
	const bool bChanged = bHovered ? !HoveredCells.Contains(CellId) : HoveredCells.Contains(CellId);
	if (!bChanged)
	{
		return true;
	}
	if (bHovered)
	{
		HoveredCells.Add(CellId);
	}
	else
	{
		HoveredCells.Remove(CellId);
	}
	ApplyCellCustomData(CellId, true);
	return true;
}

bool UGridRuntimeVisualizationSubsystem::SetCellSelected(const FGridCellId& CellId, bool bSelected)
{
	UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	if (GridSubsystem == nullptr || GridSubsystem->GetCell(CellId).Status != EGridQueryStatus::Success)
	{
		return false;
	}
	const bool bChanged = bSelected ? !SelectedCells.Contains(CellId) : SelectedCells.Contains(CellId);
	if (!bChanged)
	{
		return true;
	}
	if (bSelected)
	{
		SelectedCells.Add(CellId);
	}
	else
	{
		SelectedCells.Remove(CellId);
	}
	ApplyCellCustomData(CellId, true);
	return true;
}

void UGridRuntimeVisualizationSubsystem::ClearHoveredCells()
{
	TArray<FGridCellId> ChangedCells = HoveredCells.Array();
	HoveredCells.Reset();
	ApplyCustomDataBatch(ChangedCells);
}

void UGridRuntimeVisualizationSubsystem::ClearSelectedCells()
{
	TArray<FGridCellId> ChangedCells = SelectedCells.Array();
	SelectedCells.Reset();
	ApplyCustomDataBatch(ChangedCells);
}

void UGridRuntimeVisualizationSubsystem::ClearInteractionStates()
{
	TSet<FGridCellId> ChangedCells = HoveredCells;
	ChangedCells.Append(SelectedCells);
	HoveredCells.Reset();
	SelectedCells.Reset();
	ApplyCustomDataBatch(ChangedCells.Array());
}

bool UGridRuntimeVisualizationSubsystem::GetCellVisualState(const FGridCellId& CellId, FGridCellVisualState& OutState) const
{
	OutState = FGridCellVisualState();
	const UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	const AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	const FGridCellData* Cell = Snapshot.IsValid() ? Snapshot->FindCell(CellId) : nullptr;
	if (Cell == nullptr)
	{
		return false;
	}
	OutState = BuildCellVisualState(*Cell);
	return true;
}

void UGridRuntimeVisualizationSubsystem::HandleGridWorldChanged(const FGridChangeSet& ChangeSet)
{
	if (!bVisualizationEnabled)
	{
		return;
	}
	if (ChangeSet.PreviousRevisions.Topology != ChangeSet.NewRevisions.Topology)
	{
		RebuildVisualization();
		return;
	}
	RefreshCells(ChangeSet.ChangedCells);
	CachedRevisions = ChangeSet.NewRevisions;
}

UGridWorldSubsystem* UGridRuntimeVisualizationSubsystem::GetGridWorldSubsystem() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UGridWorldSubsystem>() : nullptr;
}

UGridCellVisualStyle* UGridRuntimeVisualizationSubsystem::ResolveStyle(UGridCellVisualStyle* RequestedStyle) const
{
	if (RequestedStyle != nullptr)
	{
		return RequestedStyle;
	}
	TSoftObjectPtr<UGridCellVisualStyle> DefaultStyle(UE::GridWorld::Presentation::Private::DefaultStylePath);
	return DefaultStyle.LoadSynchronous();
}

bool UGridRuntimeVisualizationSubsystem::EnsureVisualizationActor()
{
	if (IsValid(VisualizationActor))
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		GRIDWORLD_LOG_ERROR("Cannot create runtime visualization without a render-capable GridWorld game world.");
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, AGridRuntimeVisualizationActor::StaticClass(), TEXT("GridRuntimeVisualization"));
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	VisualizationActor = World->SpawnActor<AGridRuntimeVisualizationActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	if (!IsValid(VisualizationActor))
	{
		GRIDWORLD_LOG_ERROR("Failed to spawn the transient runtime visualization Actor in world '%s'.", *GetNameSafe(World));
		return false;
	}
	ApplyVisibility();
	return true;
}

void UGridRuntimeVisualizationSubsystem::DestroyVisualizationActor()
{
	ChunkRenderers.Reset();
	CellHandles.Reset();
	CachedRevisions = FGridRevisionSet();
	++RendererGeneration;
	if (IsValid(VisualizationActor))
	{
		VisualizationActor->Destroy();
	}
	VisualizationActor = nullptr;
}

void UGridRuntimeVisualizationSubsystem::ApplyVisibility()
{
	if (IsValid(VisualizationActor))
	{
		VisualizationActor->SetActorHiddenInGame(!bVisualizationVisible);
		VisualizationActor->SetActorTickEnabled(false);
	}
}

FGridCellVisualState UGridRuntimeVisualizationSubsystem::BuildCellVisualState(const FGridCellData& Cell) const
{
	FGridCellVisualState State;
	State.InteractionState = SelectedCells.Contains(Cell.Id)
		? EGridCellInteractionVisualState::Selected
		: (HoveredCells.Contains(Cell.Id) ? EGridCellInteractionVisualState::Hovered : EGridCellInteractionVisualState::Unselected);
	if (const EGridCellOverlayVisualState* OverlayState = OverlayStates.Find(Cell.Id))
	{
		State.OverlayState = *OverlayState;
		State.CustomStyleValue = static_cast<float>(*OverlayState);
	}
	if (const FGridResolvedPathVisualState* PathState = PathStates.Find(Cell.Id))
	{
		State.PathState = PathState->State;
		State.PathProgress = PathState->PathProgress;
	}
	EGridCellNavigationVisualFlags Flags = Cell.bWalkable
		? EGridCellNavigationVisualFlags::Traversable
		: EGridCellNavigationVisualFlags::Blocked;
	if (Cell.TraversalCost > 1000)
	{
		Flags |= EGridCellNavigationVisualFlags::HighCost;
	}
	if (Cell.bOccupied)
	{
		Flags |= EGridCellNavigationVisualFlags::Occupied;
	}
	if (!Cell.ReservationOwners.IsEmpty())
	{
		Flags |= EGridCellNavigationVisualFlags::Reserved;
	}
	State.NavigationFlags = static_cast<int32>(Flags);
	return State;
}

FTransform UGridRuntimeVisualizationSubsystem::BuildCellTransform(const FGridRegionData& Region, const FGridCellData& Cell) const
{
	const FVector FloorNormal = FVector(Cell.FloorNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const FVector RegionXAxis = Region.GridTransform.Rotation.RotateVector(FVector::ForwardVector);
	FVector TangentXAxis = FVector::VectorPlaneProject(RegionXAxis, FloorNormal).GetSafeNormal();
	if (TangentXAxis.IsNearlyZero())
	{
		TangentXAxis = FVector::CrossProduct(FVector::RightVector, FloorNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	}
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(TangentXAxis, FloorNormal).ToQuat();
	const FVector MeshSize = ActiveStyle->CellMesh->GetBounds().BoxExtent * 2.0;
	const double CoverageScale = 1.0 - FMath::Clamp(static_cast<double>(ActiveStyle->CellInsetFraction), 0.0, 0.49) * 2.0;
	const FVector Scale(
		FMath::Abs(Region.GridTransform.CellSize.X) * CoverageScale / MeshSize.X,
		FMath::Abs(Region.GridTransform.CellSize.Y) * CoverageScale / MeshSize.Y,
		1.0);
	return FTransform(Rotation, Cell.WorldCenter + FloorNormal * ActiveStyle->SurfaceOffset, Scale);
}

bool UGridRuntimeVisualizationSubsystem::ApplyCellCustomData(const FGridCellId& CellId, bool bMarkRenderStateDirty)
{
	if (!bVisualizationEnabled || ActiveStyle == nullptr)
	{
		return false;
	}
	const FGridCellVisualHandle* Handle = CellHandles.Find(CellId);
	if (Handle == nullptr || Handle->RendererGeneration != RendererGeneration)
	{
		return false;
	}
	UHierarchicalInstancedStaticMeshComponent* Component = nullptr;
	for (const TPair<FGridChunkCoord, FGridChunkRenderer>& Pair : ChunkRenderers)
	{
		if (Pair.Value.RendererId == Handle->RendererId)
		{
			Component = Pair.Value.Component.Get();
			break;
		}
	}
	if (Component == nullptr)
	{
		return false;
	}
	FGridCellVisualState State;
	if (!GetCellVisualState(CellId, State))
	{
		return false;
	}
	float CustomData[FGridCellMaterialDataLayout::NumFloats];
	FGridCellMaterialDataLayout::Write(State, ActiveStyle->ResolveColor(State), CustomData);
	return Component->SetCustomData(Handle->InstanceIndex, MakeArrayView(CustomData), bMarkRenderStateDirty);
}

void UGridRuntimeVisualizationSubsystem::ApplyCustomDataBatch(TConstArrayView<FGridCellId> CellIds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GridWorld_RuntimeVisualizationCustomDataUpdate);
	TSet<UHierarchicalInstancedStaticMeshComponent*> DirtyComponents;
	for (const FGridCellId& CellId : CellIds)
	{
		const FGridCellVisualHandle* Handle = CellHandles.Find(CellId);
		if (Handle == nullptr || !ApplyCellCustomData(CellId, false))
		{
			continue;
		}
		for (const TPair<FGridChunkCoord, FGridChunkRenderer>& Pair : ChunkRenderers)
		{
			if (Pair.Value.RendererId == Handle->RendererId)
			{
				if (UHierarchicalInstancedStaticMeshComponent* Component = Pair.Value.Component.Get())
				{
					DirtyComponents.Add(Component);
				}
				break;
			}
		}
	}
	for (UHierarchicalInstancedStaticMeshComponent* Component : DirtyComponents)
	{
		Component->MarkRenderStateDirty();
	}
}

void UGridRuntimeVisualizationSubsystem::PruneStaleInteractionState(const FGridWorldSnapshot& Snapshot)
{
	for (auto It = HoveredCells.CreateIterator(); It; ++It)
	{
		if (Snapshot.FindCell(*It) == nullptr)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = SelectedCells.CreateIterator(); It; ++It)
	{
		if (Snapshot.FindCell(*It) == nullptr)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = PathStates.CreateIterator(); It; ++It)
	{
		if (Snapshot.FindCell(It.Key()) == nullptr)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = OverlayStates.CreateIterator(); It; ++It)
	{
		if (Snapshot.FindCell(It.Key()) == nullptr)
		{
			It.RemoveCurrent();
		}
	}
}

bool UGridRuntimeVisualizationSubsystem::SetCellPathStateInternal(
	const FGridCellId& CellId,
	EGridCellPathVisualState PathState,
	float PathProgress)
{
	UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	if (GridSubsystem == nullptr || GridSubsystem->GetCell(CellId).Status != EGridQueryStatus::Success)
	{
		return false;
	}
	if (PathState == EGridCellPathVisualState::None)
	{
		PathStates.Remove(CellId);
	}
	else
	{
		FGridResolvedPathVisualState& ResolvedState = PathStates.FindOrAdd(CellId);
		ResolvedState.State = PathState;
		ResolvedState.PathProgress = FMath::Clamp(PathProgress, 0.0f, 1.0f);
	}
	ApplyCellCustomData(CellId, true);
	return true;
}

void UGridRuntimeVisualizationSubsystem::ReplaceResolvedPathStatesInternal(
	TMap<FGridCellId, FGridResolvedPathVisualState>&& NewStates)
{
	TSet<FGridCellId> ChangedCells;
	for (const TPair<FGridCellId, FGridResolvedPathVisualState>& Pair : PathStates)
	{
		const FGridResolvedPathVisualState* NewState = NewStates.Find(Pair.Key);
		if (NewState == nullptr || *NewState != Pair.Value)
		{
			ChangedCells.Add(Pair.Key);
		}
	}
	for (const TPair<FGridCellId, FGridResolvedPathVisualState>& Pair : NewStates)
	{
		const FGridResolvedPathVisualState* PreviousState = PathStates.Find(Pair.Key);
		if (PreviousState == nullptr || *PreviousState != Pair.Value)
		{
			ChangedCells.Add(Pair.Key);
		}
	}

	PathStates = MoveTemp(NewStates);
	ApplyCustomDataBatch(ChangedCells.Array());
}

void UGridRuntimeVisualizationSubsystem::ReplaceResolvedOverlayStatesInternal(
	TMap<FGridCellId, EGridCellOverlayVisualState>&& NewStates)
{
	TSet<FGridCellId> ChangedCells;
	for (const TPair<FGridCellId, EGridCellOverlayVisualState>& Pair : OverlayStates)
	{
		const EGridCellOverlayVisualState* NewState = NewStates.Find(Pair.Key);
		if (NewState == nullptr || *NewState != Pair.Value)
		{
			ChangedCells.Add(Pair.Key);
		}
	}
	for (const TPair<FGridCellId, EGridCellOverlayVisualState>& Pair : NewStates)
	{
		const EGridCellOverlayVisualState* PreviousState = OverlayStates.Find(Pair.Key);
		if (PreviousState == nullptr || *PreviousState != Pair.Value)
		{
			ChangedCells.Add(Pair.Key);
		}
	}

	OverlayStates = MoveTemp(NewStates);
	ApplyCustomDataBatch(ChangedCells.Array());
}

bool UGridRuntimeVisualizationSubsystem::GetVisualHandleForTesting(
	const FGridCellId& CellId,
	FGridCellVisualHandle& OutHandle) const
{
	if (const FGridCellVisualHandle* Handle = CellHandles.Find(CellId))
	{
		OutHandle = *Handle;
		return true;
	}
	OutHandle = FGridCellVisualHandle();
	return false;
}
