// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridPathLineVisualizationSubsystem.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GridWorldModule.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Presentation/GridPathLineVisualStyle.h"
#include "Presentation/GridPathLineVisualizationActor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/GridWorldSubsystem.h"

namespace UE::GridWorld::PathLinePresentation::Private
{
	const FSoftObjectPath DefaultStylePath(
		TEXT("/GridWorldSystem/Presentation/DA_GridRuntimePathLineStyle_Default.DA_GridRuntimePathLineStyle_Default"));

	struct FRenderInstance
	{
		FTransform Transform;
		EGridCellPathVisualState State = EGridCellPathVisualState::None;
		float Progress = 0.0f;
	};

	int32 GetPurposeRank(EGridPathPresentationPurpose Purpose)
	{
		return Purpose == EGridPathPresentationPurpose::Active ? 1 : 0;
	}

	float GetNormalizedProgress(int32 CellIndex, int32 NumCells)
	{
		return NumCells > 1 ? static_cast<float>(CellIndex) / static_cast<float>(NumCells - 1) : 1.0f;
	}

	FIntPoint GetDirection(const FGridCellId& From, const FGridCellId& To)
	{
		return FIntPoint(
			FMath::Sign(To.Coord.X - From.Coord.X),
			FMath::Sign(To.Coord.Y - From.Coord.Y));
	}

	bool IsEndpointOrTurn(TConstArrayView<FGridCellId> Cells, int32 CellIndex)
	{
		if (CellIndex <= 0 || CellIndex >= Cells.Num() - 1)
		{
			return true;
		}
		const FGridCellId& Previous = Cells[CellIndex - 1];
		const FGridCellId& Current = Cells[CellIndex];
		const FGridCellId& Next = Cells[CellIndex + 1];
		if (Previous.GridId != Current.GridId
			|| Current.GridId != Next.GridId
			|| Previous.Coord.Layer != Current.Coord.Layer
			|| Current.Coord.Layer != Next.Coord.Layer)
		{
			return true;
		}
		return GetDirection(Previous, Current) != GetDirection(Current, Next);
	}

	void BuildDisplayedIndices(const FGridPathLineRenderRecord& Record, TArray<int32>& OutIndices)
	{
		OutIndices.Reset();
		if (Record.Cells.IsEmpty())
		{
			return;
		}
		if (Record.bInvalid)
		{
			for (int32 Index = 0; Index < Record.Cells.Num(); ++Index)
			{
				OutIndices.Add(Index);
			}
			return;
		}
		switch (Record.ProgressMode)
		{
		case EGridPathProgressPresentationMode::AllCells:
		case EGridPathProgressPresentationMode::TraversedAndRemaining:
			for (int32 Index = 0; Index < Record.Cells.Num(); ++Index)
			{
				OutIndices.Add(Index);
			}
			break;
		case EGridPathProgressPresentationMode::RemainingOnly:
		case EGridPathProgressPresentationMode::CurrentAndRemaining:
			for (int32 Index = FMath::Clamp(Record.CurrentCellIndex, 0, Record.Cells.Num() - 1);
				Index < Record.Cells.Num();
				++Index)
			{
				OutIndices.Add(Index);
			}
			break;
		case EGridPathProgressPresentationMode::DestinationOnly:
			OutIndices.Add(Record.Cells.Num() - 1);
			break;
		case EGridPathProgressPresentationMode::EndpointsAndTurns:
			for (int32 Index = 0; Index < Record.Cells.Num(); ++Index)
			{
				if (IsEndpointOrTurn(Record.Cells, Index))
				{
					OutIndices.Add(Index);
				}
			}
			break;
		default:
			break;
		}
	}

	EGridCellPathVisualState ResolveState(const FGridPathLineRenderRecord& Record, int32 CellIndex)
	{
		if (Record.bInvalid)
		{
			return EGridCellPathVisualState::Invalid;
		}
		if (Record.Purpose == EGridPathPresentationPurpose::Preview)
		{
			return EGridCellPathVisualState::Preview;
		}
		if (CellIndex == Record.Cells.Num() - 1)
		{
			return EGridCellPathVisualState::Destination;
		}
		if (Record.ProgressMode == EGridPathProgressPresentationMode::AllCells)
		{
			return EGridCellPathVisualState::ActiveRemaining;
		}
		if (CellIndex < Record.CurrentCellIndex)
		{
			return EGridCellPathVisualState::ActiveTraversed;
		}
		if (CellIndex == Record.CurrentCellIndex)
		{
			return EGridCellPathVisualState::ActiveCurrent;
		}
		return EGridCellPathVisualState::ActiveRemaining;
	}

	bool AreCellsPresentBetween(
		const FGridWorldSnapshot& Snapshot,
		TConstArrayView<FGridCellId> Cells,
		int32 FirstIndex,
		int32 LastIndex)
	{
		for (int32 Index = FirstIndex; Index <= LastIndex; ++Index)
		{
			if (!Cells.IsValidIndex(Index) || Snapshot.FindCell(Cells[Index]) == nullptr)
			{
				return false;
			}
		}
		return true;
	}

	FTransform BuildMarkerTransform(const UGridPathLineVisualStyle& Style, const FGridCellData& Cell)
	{
		const FVector Normal = FVector(Cell.FloorNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector MeshSize = Style.MarkerMesh->GetBounds().BoxExtent * 2.0;
		const FVector Scale(
			Style.MarkerSize / MeshSize.X,
			Style.MarkerSize / MeshSize.Y,
			Style.MarkerSize / MeshSize.Z);
		return FTransform(
			FQuat::FindBetweenNormals(FVector::UpVector, Normal),
			Cell.WorldCenter + Normal * Style.SurfaceOffset,
			Scale);
	}

	bool BuildSegmentTransform(
		const UGridPathLineVisualStyle& Style,
		const FGridCellData& StartCell,
		const FGridCellData& EndCell,
		FTransform& OutTransform)
	{
		const FVector StartNormal = FVector(StartCell.FloorNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector EndNormal = FVector(EndCell.FloorNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FVector Start = StartCell.WorldCenter + StartNormal * Style.SurfaceOffset;
		const FVector End = EndCell.WorldCenter + EndNormal * Style.SurfaceOffset;
		const FVector Delta = End - Start;
		const double Length = Delta.Size();
		if (!FMath::IsFinite(Length) || Length <= UE_SMALL_NUMBER)
		{
			return false;
		}
		const FVector Direction = Delta / Length;
		FVector Up = FVector::VectorPlaneProject((StartNormal + EndNormal).GetSafeNormal(), Direction).GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			Up = FVector::VectorPlaneProject(FVector::UpVector, Direction).GetSafeNormal();
		}
		if (Up.IsNearlyZero())
		{
			Up = FVector::VectorPlaneProject(FVector::RightVector, Direction).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		}
		const FVector MeshSize = Style.SegmentMesh->GetBounds().BoxExtent * 2.0;
		OutTransform = FTransform(
			FRotationMatrix::MakeFromXZ(Direction, Up).ToQuat(),
			(Start + End) * 0.5,
			FVector(Length / MeshSize.X, Style.LineWidth / MeshSize.Y, Style.LineThickness / MeshSize.Z));
		return true;
	}

	void ConfigureComponent(
		UHierarchicalInstancedStaticMeshComponent& Component,
		UStaticMesh& Mesh,
		UMaterialInterface& Material,
		const UGridPathLineVisualStyle& Style)
	{
		Component.SetMobility(EComponentMobility::Movable);
		Component.SetStaticMesh(&Mesh);
		Component.SetMaterial(0, &Material);
		Component.SetNumCustomDataFloats(FGridPathLineMaterialDataLayout::NumFloats);
		Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component.SetGenerateOverlapEvents(false);
		Component.SetCanEverAffectNavigation(false);
		Component.SetCastShadow(Style.bCastShadow);
		Component.bReceivesDecals = false;
		Component.SetCullDistances(Style.StartCullDistance, Style.EndCullDistance);
	}

	bool AddInstances(
		UHierarchicalInstancedStaticMeshComponent& Component,
		TConstArrayView<FRenderInstance> Instances,
		const UGridPathLineVisualStyle& Style)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(Instances.Num());
		for (const FRenderInstance& Instance : Instances)
		{
			Transforms.Add(Instance.Transform);
		}
		const TArray<int32> InstanceIndices = Component.AddInstances(Transforms, true, true, false);
		if (InstanceIndices.Num() != Instances.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Instances.Num(); ++Index)
		{
			float CustomData[FGridPathLineMaterialDataLayout::NumFloats];
			FGridPathLineMaterialDataLayout::Write(
				Instances[Index].State,
				Instances[Index].Progress,
				Style.ResolveColor(Instances[Index].State),
				MakeArrayView(CustomData));
			if (!Component.SetCustomData(InstanceIndices[Index], MakeArrayView(CustomData), false))
			{
				return false;
			}
		}
		Component.BuildTreeIfOutdated(false, true);
		Component.MarkRenderStateDirty();
		return true;
	}
}

bool UGridPathLineVisualizationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer) || IsRunningDedicatedServer())
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->GetNetMode() != NM_DedicatedServer;
}

bool UGridPathLineVisualizationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGridPathLineVisualizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGridWorldSubsystem>();
}

void UGridPathLineVisualizationSubsystem::Deinitialize()
{
	DestroyVisualizationActor();
	ActiveStyle = nullptr;
	RenderRecords.Reset();
	bVisualizationEnabled = false;
	Super::Deinitialize();
}

bool UGridPathLineVisualizationSubsystem::EnableLineVisualization(UGridPathLineVisualStyle* Style)
{
	UGridPathLineVisualStyle* ResolvedStyle = ResolveStyle(Style);
	FString ValidationError;
	if (ResolvedStyle == nullptr || !ResolvedStyle->Validate(ValidationError))
	{
		GRIDWORLD_LOG_ERROR(
			"Cannot enable runtime path-line visualization in world '%s': %s",
			*GetNameSafe(GetWorld()),
			ResolvedStyle != nullptr ? *ValidationError : TEXT("the requested/default line style could not be loaded."));
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
	if (bStyleChanged || SegmentComponent == nullptr)
	{
		return RebuildLineVisualization();
	}
	ApplyVisibility();
	return true;
}

void UGridPathLineVisualizationSubsystem::DisableLineVisualization()
{
	bVisualizationEnabled = false;
	DestroyVisualizationActor();
}

void UGridPathLineVisualizationSubsystem::SetLineVisualizationVisible(bool bVisible)
{
	bVisualizationVisible = bVisible;
	ApplyVisibility();
}

bool UGridPathLineVisualizationSubsystem::RebuildLineVisualization()
{
	using namespace UE::GridWorld::PathLinePresentation::Private;
	TRACE_CPUPROFILER_EVENT_SCOPE(GridWorld_PathLineVisualizationBuild);
	if (!bVisualizationEnabled || ActiveStyle == nullptr || !EnsureVisualizationActor())
	{
		return false;
	}

	if (SegmentComponent != nullptr)
	{
		SegmentComponent->DestroyComponent();
		SegmentComponent = nullptr;
	}
	if (MarkerComponent != nullptr)
	{
		MarkerComponent->DestroyComponent();
		MarkerComponent = nullptr;
	}
	SegmentInstanceCount = 0;
	MarkerInstanceCount = 0;
	++RendererGeneration;
	if (!CreateRenderComponents())
	{
		return false;
	}

	const UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	const AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid())
	{
		ApplyVisibility();
		return true;
	}

	TArray<FRenderInstance> SegmentInstances;
	TArray<FRenderInstance> MarkerInstances;
	TArray<int32> DisplayedIndices;
	for (const FGridPathLineRenderRecord& Record : RenderRecords)
	{
		BuildDisplayedIndices(Record, DisplayedIndices);
		for (int32 DisplayIndex = 0; DisplayIndex < DisplayedIndices.Num(); ++DisplayIndex)
		{
			const int32 CellIndex = DisplayedIndices[DisplayIndex];
			const FGridCellData* Cell = Snapshot->FindCell(Record.Cells[CellIndex]);
			if (Cell == nullptr)
			{
				continue;
			}
			const EGridCellPathVisualState State = ResolveState(Record, CellIndex);
			const float Progress = GetNormalizedProgress(CellIndex, Record.Cells.Num());
			if (ActiveStyle->MarkerMesh != nullptr)
			{
				FRenderInstance& Marker = MarkerInstances.AddDefaulted_GetRef();
				Marker.Transform = BuildMarkerTransform(*ActiveStyle, *Cell);
				Marker.State = State;
				Marker.Progress = Progress;
			}

			if (DisplayIndex == 0)
			{
				continue;
			}
			const int32 PreviousCellIndex = DisplayedIndices[DisplayIndex - 1];
			if (!AreCellsPresentBetween(*Snapshot, Record.Cells, PreviousCellIndex, CellIndex))
			{
				continue;
			}
			const FGridCellData* PreviousCell = Snapshot->FindCell(Record.Cells[PreviousCellIndex]);
			FTransform SegmentTransform;
			if (PreviousCell != nullptr && BuildSegmentTransform(*ActiveStyle, *PreviousCell, *Cell, SegmentTransform))
			{
				FRenderInstance& Segment = SegmentInstances.AddDefaulted_GetRef();
				Segment.Transform = SegmentTransform;
				Segment.State = State;
				Segment.Progress = Progress;
			}
		}

		if (ActiveStyle->MarkerMesh != nullptr)
		{
			for (const TPair<FGridCellId, float>& Preserved : Record.PreservedTraversedCells)
			{
				if (const FGridCellData* Cell = Snapshot->FindCell(Preserved.Key))
				{
					FRenderInstance& Marker = MarkerInstances.AddDefaulted_GetRef();
					Marker.Transform = BuildMarkerTransform(*ActiveStyle, *Cell);
					Marker.State = EGridCellPathVisualState::ActiveTraversed;
					Marker.Progress = Preserved.Value;
				}
			}
		}
	}

	if (!AddInstances(*SegmentComponent, SegmentInstances, *ActiveStyle)
		|| (MarkerComponent != nullptr && !AddInstances(*MarkerComponent, MarkerInstances, *ActiveStyle)))
	{
		GRIDWORLD_LOG_ERROR("Runtime path-line instance build failed in world '%s'.", *GetNameSafe(GetWorld()));
		bVisualizationEnabled = false;
		DestroyVisualizationActor();
		return false;
	}
	SegmentInstanceCount = SegmentInstances.Num();
	MarkerInstanceCount = MarkerInstances.Num();
	ApplyVisibility();
	return true;
}

UGridWorldSubsystem* UGridPathLineVisualizationSubsystem::GetGridWorldSubsystem() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UGridWorldSubsystem>() : nullptr;
}

UGridPathLineVisualStyle* UGridPathLineVisualizationSubsystem::ResolveStyle(UGridPathLineVisualStyle* RequestedStyle) const
{
	if (RequestedStyle != nullptr)
	{
		return RequestedStyle;
	}
	return Cast<UGridPathLineVisualStyle>(
		UE::GridWorld::PathLinePresentation::Private::DefaultStylePath.TryLoad());
}

bool UGridPathLineVisualizationSubsystem::EnsureVisualizationActor()
{
	if (IsValid(VisualizationActor))
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(World, AGridPathLineVisualizationActor::StaticClass(), TEXT("GridPathLineVisualization"));
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	VisualizationActor = World->SpawnActor<AGridPathLineVisualizationActor>(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!IsValid(VisualizationActor))
	{
		GRIDWORLD_LOG_ERROR("Failed to spawn the transient path-line visualization Actor in world '%s'.", *GetNameSafe(World));
		return false;
	}
	ApplyVisibility();
	return true;
}

bool UGridPathLineVisualizationSubsystem::CreateRenderComponents()
{
	AGridPathLineVisualizationActor* Owner = Cast<AGridPathLineVisualizationActor>(VisualizationActor);
	if (Owner == nullptr || ActiveStyle == nullptr || ActiveStyle->SegmentMesh == nullptr || ActiveStyle->SegmentMaterial == nullptr)
	{
		return false;
	}
	SegmentComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(
		Owner,
		MakeUniqueObjectName(Owner, UHierarchicalInstancedStaticMeshComponent::StaticClass(), TEXT("GridPathLineSegments")),
		RF_Transient);
	Owner->AddInstanceComponent(SegmentComponent);
	SegmentComponent->SetupAttachment(Owner->GetPresentationRoot());
	UE::GridWorld::PathLinePresentation::Private::ConfigureComponent(
		*SegmentComponent,
		*ActiveStyle->SegmentMesh,
		*ActiveStyle->SegmentMaterial,
		*ActiveStyle);
	SegmentComponent->RegisterComponent();

	if (ActiveStyle->MarkerMesh != nullptr)
	{
		UMaterialInterface* MarkerMaterial = ActiveStyle->MarkerMaterial != nullptr
			? ActiveStyle->MarkerMaterial.Get()
			: ActiveStyle->SegmentMaterial.Get();
		MarkerComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(
			Owner,
			MakeUniqueObjectName(Owner, UHierarchicalInstancedStaticMeshComponent::StaticClass(), TEXT("GridPathLineMarkers")),
			RF_Transient);
		Owner->AddInstanceComponent(MarkerComponent);
		MarkerComponent->SetupAttachment(Owner->GetPresentationRoot());
		UE::GridWorld::PathLinePresentation::Private::ConfigureComponent(
			*MarkerComponent,
			*ActiveStyle->MarkerMesh,
			*MarkerMaterial,
			*ActiveStyle);
		MarkerComponent->RegisterComponent();
	}
	return true;
}

void UGridPathLineVisualizationSubsystem::DestroyVisualizationActor()
{
	SegmentComponent = nullptr;
	MarkerComponent = nullptr;
	SegmentInstanceCount = 0;
	MarkerInstanceCount = 0;
	++RendererGeneration;
	if (IsValid(VisualizationActor))
	{
		VisualizationActor->Destroy();
	}
	VisualizationActor = nullptr;
}

void UGridPathLineVisualizationSubsystem::ApplyVisibility()
{
	if (IsValid(VisualizationActor))
	{
		VisualizationActor->SetActorHiddenInGame(!bVisualizationVisible);
		VisualizationActor->SetActorTickEnabled(false);
	}
}

void UGridPathLineVisualizationSubsystem::ReplaceRenderRecordsInternal(
	TArray<FGridPathLineRenderRecord>&& NewRecords)
{
	RenderRecords = MoveTemp(NewRecords);
	RenderRecords.Sort([](const FGridPathLineRenderRecord& Left, const FGridPathLineRenderRecord& Right)
	{
		if (Left.Priority != Right.Priority)
		{
			return Left.Priority > Right.Priority;
		}
		const int32 LeftPurposeRank = UE::GridWorld::PathLinePresentation::Private::GetPurposeRank(Left.Purpose);
		const int32 RightPurposeRank = UE::GridWorld::PathLinePresentation::Private::GetPurposeRank(Right.Purpose);
		return LeftPurposeRank != RightPurposeRank
			? LeftPurposeRank > RightPurposeRank
			: Left.CreationSequence < Right.CreationSequence;
	});
	if (bVisualizationEnabled)
	{
		RebuildLineVisualization();
	}
}
