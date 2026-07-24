// Copyright Epic Games, Inc. All Rights Reserved.

#include "Prediction/GridPathPreviewComponent.h"

#include "AI/GridGoalContention.h"
#include "Engine/World.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GridWorldModule.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridNavigationQueryFilter.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Presentation/GridPathLineVisualizationSubsystem.h"
#include "Presentation/GridPathLineVisualStyle.h"
#include "Presentation/GridPathPresentationSubsystem.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "Presentation/GridCellVisualStyle.h"
#include "Subsystems/GridWorldSubsystem.h"

namespace
{
	bool UsesAtomicExactGoalPolicy(const EGridGoalContentionPolicy Policy)
	{
		return Policy == EGridGoalContentionPolicy::RejectOccupied
			|| Policy == EGridGoalContentionPolicy::StopBeforeOccupied;
	}
}

UGridPathPreviewComponent::UGridPathPreviewComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGridPathPreviewComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (UGridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGridWorldSubsystem>())
		{
			GridSubsystem->OnGridWorldChanged.AddDynamic(this, &UGridPathPreviewComponent::HandleGridWorldChanged);
		}
	}
}

void UGridPathPreviewComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UGridWorldSubsystem* GridSubsystem = World->GetSubsystem<UGridWorldSubsystem>())
		{
			GridSubsystem->OnGridWorldChanged.RemoveDynamic(this, &UGridPathPreviewComponent::HandleGridWorldChanged);
		}
	}
	UnbindNavigationData();
	ClearPreview();
	Super::EndPlay(EndPlayReason);
}

FGridPathPreviewResult UGridPathPreviewComponent::UpdatePreviewForController(
	AController* Controller,
	const FGridCellId& GoalCell)
{
	if (!IsValid(Controller) || Controller->GetWorld() != GetWorld())
	{
		PreviewController.Reset();
		PreviewGoalCell = FGridCellId();
		PublishFailure(EGridQueryStatus::InvalidInput, EGridPathPreviewFailureReason::InvalidController);
		return LatestResult;
	}
	if (!GoalCell.IsValid())
	{
		PreviewController = Controller;
		PreviewGoalCell = FGridCellId();
		PublishFailure(EGridQueryStatus::GoalNotNavigable, EGridPathPreviewFailureReason::InvalidGoal);
		return LatestResult;
	}
	PreviewController = Controller;
	PreviewGoalCell = GoalCell;
	PreviewFilterClass = NavigationFilter;
	if (PreviewFilterClass == nullptr)
	{
		PreviewFilterClass = UGridNavigationQueryFilter::StaticClass();
	}
	return ExecutePreview(false);
}

FGridPathPreviewResult UGridPathPreviewComponent::RefreshPreview()
{
	return ExecutePreview(true);
}

void UGridPathPreviewComponent::ClearPreview()
{
	if (!NativePreviewPath.IsValid()
		&& !LatestInjectedPath.IsSet()
		&& !PresentationHandle.IsSet()
		&& LatestResult.Status == EGridQueryStatus::Cancelled)
	{
		return;
	}
	NativePreviewPath.Reset();
	LatestInjectedPath = FGridInjectedPath();
	PreviewController.Reset();
	PreviewGoalCell = FGridCellId();
	PreviewFilterClass = nullptr;
	UnbindNavigationData();
	LastFilterSignature = 0;
	LastRelevantRevisions = FGridRevisionSet();
	LastTrafficRevision = 0;
	LatestResult = FGridPathPreviewResult();
	LatestResult.Status = EGridQueryStatus::Cancelled;
	LatestResult.FailureReason = EGridPathPreviewFailureReason::None;
	ClearPresentation();
	OnPreviewChanged.Broadcast(LatestResult);
}

bool UGridPathPreviewComponent::PreparePreviewForCommit(
	FGridInjectedPath& OutInjectedPath,
	FGridPathPreviewResult& OutPreview)
{
	OutInjectedPath = FGridInjectedPath();
	if (!PreviewController.IsValid() || !PreviewGoalCell.IsValid())
	{
		OutPreview = LatestResult;
		return false;
	}
	if (LatestResult.bIsStale || !LatestResult.IsSuccessful())
	{
		ExecutePreview(true);
	}
	else
	{
		ExecutePreview(false);
	}
	OutPreview = LatestResult;
	if (!LatestResult.bIsCommittable || !LatestInjectedPath.IsSet())
	{
		return false;
	}
	OutInjectedPath = LatestInjectedPath;
	return true;
}

void UGridPathPreviewComponent::HandleGridWorldChanged(const FGridChangeSet& ChangeSet)
{
	if (!PreviewController.IsValid() || !PreviewGoalCell.IsValid() || bRefreshInProgress)
	{
		return;
	}
	const bool bTopologyChanged = ChangeSet.PreviousRevisions.Topology != ChangeSet.NewRevisions.Topology;
	const bool bTraversalChanged = ChangeSet.PreviousRevisions.Traversal != ChangeSet.NewRevisions.Traversal;
	bool bOccupancyRelevant = false;
	if (AGridNavigationData* NavData = PreviewNavigationData.Get())
	{
		const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(
			*NavData,
			PreviewController.Get(),
			PreviewFilterClass);
		const FGridNavigationQueryFilterImpl* GridFilter = Filter.IsValid()
			? static_cast<const FGridNavigationQueryFilterImpl*>(Filter->GetImplementation())
			: nullptr;
		bOccupancyRelevant = UsesAtomicExactGoalPolicy(GoalContentionPolicy)
			|| (GridFilter != nullptr
			&& (GridFilter->GetOccupancyPolicy() != EGridOccupancyPolicy::Ignore
				|| GridFilter->GetDynamicAgentPolicy() != EGridDynamicAgentPolicy::Ignore));
	}
	const bool bOccupancyChanged = bOccupancyRelevant
		&& ChangeSet.PreviousRevisions.Occupancy != ChangeSet.NewRevisions.Occupancy;
	if (!bTopologyChanged && !bTraversalChanged && !bOccupancyChanged)
	{
		return;
	}

	switch (StalePolicy)
	{
	case EGridPathPreviewStalePolicy::KeepButMarkStale:
		LatestResult.Status = EGridQueryStatus::Stale;
		LatestResult.FailureReason = EGridPathPreviewFailureReason::Stale;
		LatestResult.bIsStale = true;
		LatestResult.bIsCommittable = false;
		if (UWorld* World = GetWorld())
		{
			if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
			{
				Presentation->MarkPathPresentationInvalid(PresentationHandle);
			}
		}
		OnPreviewChanged.Broadcast(LatestResult);
		break;
	case EGridPathPreviewStalePolicy::ClearImmediately:
		PublishFailure(EGridQueryStatus::Stale, EGridPathPreviewFailureReason::Stale);
		break;
	case EGridPathPreviewStalePolicy::RecalculateAutomatically:
	default:
		ExecutePreview(true);
		break;
	}
}

void UGridPathPreviewComponent::HandleTrafficReservationsChanged()
{
	if (!PreviewController.IsValid() || !PreviewGoalCell.IsValid() || bRefreshInProgress)
	{
		return;
	}
	AGridNavigationData* NavData = PreviewNavigationData.Get();
	const FSharedConstNavQueryFilter Filter = NavData != nullptr
		? UNavigationQueryFilter::GetQueryFilter(*NavData, PreviewController.Get(), PreviewFilterClass)
		: FSharedConstNavQueryFilter();
	const FGridNavigationQueryFilterImpl* GridFilter = Filter.IsValid()
		? static_cast<const FGridNavigationQueryFilterImpl*>(Filter->GetImplementation())
		: nullptr;
	if (!UsesAtomicExactGoalPolicy(GoalContentionPolicy)
		&& (GridFilter == nullptr || GridFilter->GetDynamicAgentPolicy() != EGridDynamicAgentPolicy::ReservedCorridor))
	{
		return;
	}
	if (StalePolicy == EGridPathPreviewStalePolicy::RecalculateAutomatically)
	{
		ExecutePreview(true);
	}
	else if (StalePolicy == EGridPathPreviewStalePolicy::ClearImmediately)
	{
		PublishFailure(EGridQueryStatus::Stale, EGridPathPreviewFailureReason::Stale);
	}
	else
	{
		LatestResult.Status = EGridQueryStatus::Stale;
		LatestResult.FailureReason = EGridPathPreviewFailureReason::Stale;
		LatestResult.bIsStale = true;
		LatestResult.bIsCommittable = false;
		if (UWorld* World = GetWorld())
		{
			if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
			{
				Presentation->MarkPathPresentationInvalid(PresentationHandle);
			}
		}
		OnPreviewChanged.Broadcast(LatestResult);
	}
}

FGridPathPreviewResult UGridPathPreviewComponent::ExecutePreview(bool bForce)
{
	if (bRefreshInProgress)
	{
		return LatestResult;
	}
	TGuardValue<bool> RefreshGuard(bRefreshInProgress, true);
	AController* Controller = PreviewController.Get();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World != nullptr
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	if (!IsValid(Controller) || Controller->GetWorld() != World || NavigationSystem == nullptr)
	{
		PublishFailure(EGridQueryStatus::InvalidInput, EGridPathPreviewFailureReason::InvalidController);
		return LatestResult;
	}
	AGridNavigationData* NavData = Cast<AGridNavigationData>(NavigationSystem->GetNavDataForProps(
		Controller->GetNavAgentPropertiesRef(),
		Controller->GetNavAgentLocation()));
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid())
	{
		BindNavigationData(nullptr);
		PublishFailure(EGridQueryStatus::InvalidGrid, EGridPathPreviewFailureReason::InvalidNavigationData);
		return LatestResult;
	}
	BindNavigationData(NavData);
	const FGridCellData* GoalData = Snapshot->FindCell(PreviewGoalCell);
	if (GoalData == nullptr || !GoalData->bWalkable)
	{
		PublishFailure(EGridQueryStatus::GoalNotNavigable, EGridPathPreviewFailureReason::InvalidGoal);
		return LatestResult;
	}

	const FSharedConstNavQueryFilter BaseQueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*NavData,
		Controller,
		PreviewFilterClass);
	if (!BaseQueryFilter.IsValid())
	{
		PublishFailure(EGridQueryStatus::InvalidInput, EGridPathPreviewFailureReason::InternalError);
		return LatestResult;
	}
	const FNavAgentProperties AgentProperties = Controller->GetNavAgentPropertiesRef();
	FPathFindingQuery Query(
		Controller,
		*NavData,
		Controller->GetNavAgentLocation(),
		GoalData->WorldCenter,
		BaseQueryFilter);
	Query.SetAllowPartialPaths(true);
	Query.SetNavAgentProperties(AgentProperties);
	if (UPathFollowingComponent* PathFollowing = Controller->FindComponentByClass<UPathFollowingComponent>())
	{
		PathFollowing->OnPathfindingQuery(Query);
	}
	const FSharedConstNavQueryFilter QueryFilter = Query.QueryFilter;
	FNavLocation ProjectedStart;
	if (!NavData->ProjectPoint(
		Controller->GetNavAgentLocation(),
		ProjectedStart,
		NavData->GetDefaultQueryExtent(),
		QueryFilter,
		Controller))
	{
		PublishFailure(EGridQueryStatus::StartNotNavigable, EGridPathPreviewFailureReason::StartNotNavigable);
		return LatestResult;
	}
	const int32 StartIndex = Snapshot->ResolveNodeRef(ProjectedStart.NodeRef);
	if (!Snapshot->Cells.IsValidIndex(StartIndex))
	{
		PublishFailure(EGridQueryStatus::Stale, EGridPathPreviewFailureReason::Stale);
		return LatestResult;
	}
	const FGridCellId StartCell = Snapshot->Cells[StartIndex].Id;
	const int64 FilterSignature = AGridNavigationData::BuildFilterSignature(QueryFilter, PreviewFilterClass);
	FGridRevisionSet RelevantRevisions = Snapshot->Revisions;
	const FGridNavigationQueryFilterImpl* GridFilter = static_cast<const FGridNavigationQueryFilterImpl*>(QueryFilter->GetImplementation());
	const bool bUsesOccupancy = UsesAtomicExactGoalPolicy(GoalContentionPolicy)
		|| (GridFilter != nullptr
		&& (GridFilter->GetOccupancyPolicy() != EGridOccupancyPolicy::Ignore
			|| GridFilter->GetDynamicAgentPolicy() != EGridDynamicAgentPolicy::Ignore));
	if (!bUsesOccupancy)
	{
		RelevantRevisions.Occupancy = 0;
	}
	const FGridTrafficReservationSnapshotPtr TrafficSnapshot =
		UsesAtomicExactGoalPolicy(GoalContentionPolicy)
			|| (GridFilter != nullptr
				&& GridFilter->GetDynamicAgentPolicy() == EGridDynamicAgentPolicy::ReservedCorridor)
		? NavData->GetTrafficReservationSnapshot()
		: FGridTrafficReservationSnapshotPtr();
	const int64 TrafficRevision = TrafficSnapshot.IsValid() ? TrafficSnapshot->Revision : 0;
	const bool bSameSemanticRequest = PreviewNavigationData.Get() == NavData
		&& LatestResult.StartCell == StartCell
		&& LatestResult.RequestedGoalCell == PreviewGoalCell
		&& LastAgentProperties.IsEquivalent(AgentProperties, 0.1f)
		&& LastFilterSignature == FilterSignature
		&& LastRelevantRevisions.Topology == RelevantRevisions.Topology
		&& LastRelevantRevisions.Traversal == RelevantRevisions.Traversal
		&& LastRelevantRevisions.Occupancy == RelevantRevisions.Occupancy
		&& LastTrafficRevision == TrafficRevision
		&& LastPartialPathPolicy == PartialPathPolicy
		&& LastInjectedPathInvalidationPolicy == InjectedPathInvalidationPolicy
		&& LastGoalContentionPolicy == GoalContentionPolicy
		&& FMath::IsNearlyEqual(LastAdditionalGoalSeparation, AdditionalGoalSeparation)
		&& (NativePreviewPath.IsValid()
			|| LatestResult.FailureReason == EGridPathPreviewFailureReason::GoalOccupied);
	if (!bForce && bSameSemanticRequest && !LatestResult.bIsStale)
	{
		return LatestResult;
	}

	++RequestGeneration;
	const FGuid QuerySignature = FGuid::NewGuid();
	const auto CacheSemanticInputs = [this, &AgentProperties, FilterSignature, &RelevantRevisions, TrafficRevision]()
	{
		LastAgentProperties = AgentProperties;
		LastFilterSignature = FilterSignature;
		LastRelevantRevisions = RelevantRevisions;
		LastTrafficRevision = TrafficRevision;
		LastPartialPathPolicy = PartialPathPolicy;
		LastInjectedPathInvalidationPolicy = InjectedPathInvalidationPolicy;
		LastGoalContentionPolicy = GoalContentionPolicy;
		LastAdditionalGoalSeparation = AdditionalGoalSeparation;
	};
	bool bRequestedGoalContested = false;
	if (UsesAtomicExactGoalPolicy(GoalContentionPolicy))
	{
		FGridTrafficGoalClaimRequest GoalClaimRequest;
		if (!BuildGoalClaimRequest(*Controller, *GoalData, GoalClaimRequest))
		{
			GRIDWORLD_LOG_WARNING(
				"GridWorld preview could not build an occupied-goal request for controller '%s' and goal cell (%d,%d,%d). Verify that the controlled Pawn has an active occupancy identity.",
				*GetNameSafe(Controller),
				GoalData->Id.Coord.X,
				GoalData->Id.Coord.Y,
				GoalData->Id.Coord.Layer);
			PublishFailure(EGridQueryStatus::InvalidInput, EGridPathPreviewFailureReason::InternalError);
			return LatestResult;
		}
		bRequestedGoalContested = !NavData->CanClaimTrafficGoal(GoalClaimRequest);
		if (bRequestedGoalContested && GoalContentionPolicy == EGridGoalContentionPolicy::RejectOccupied)
		{
			NativePreviewPath.Reset();
			LatestInjectedPath = FGridInjectedPath();
			LatestResult = FGridPathPreviewResult();
			LatestResult.Status = EGridQueryStatus::Unreachable;
			LatestResult.Path.Status = EGridQueryStatus::Unreachable;
			LatestResult.FailureReason = EGridPathPreviewFailureReason::GoalOccupied;
			LatestResult.StartCell = StartCell;
			LatestResult.GoalCell = PreviewGoalCell;
			LatestResult.RequestedGoalCell = PreviewGoalCell;
			LatestResult.QuerySignature = QuerySignature;
			LatestResult.RequestGeneration = RequestGeneration;
			CacheSemanticInputs();
			ClearPresentation();
			OnPreviewChanged.Broadcast(LatestResult);
			return LatestResult;
		}
	}
	const FPathFindingResult PathResult = NavigationSystem->FindPathSync(AgentProperties, Query);
	FGridNavigationPath* GridPath = PathResult.Path.IsValid()
		? PathResult.Path->CastPath<FGridNavigationPath>()
		: nullptr;
	if (!PathResult.IsSuccessful() || GridPath == nullptr || GridPath->CellPath.IsEmpty())
	{
		NativePreviewPath.Reset();
		LatestInjectedPath = FGridInjectedPath();
		LatestResult = FGridPathPreviewResult();
		LatestResult.Status = EGridQueryStatus::Unreachable;
		LatestResult.Path.Status = EGridQueryStatus::Unreachable;
		LatestResult.FailureReason = EGridPathPreviewFailureReason::NoPath;
		LatestResult.RequestGeneration = RequestGeneration;
		LatestResult.QuerySignature = QuerySignature;
		LatestResult.StartCell = StartCell;
		LatestResult.GoalCell = PreviewGoalCell;
		LatestResult.RequestedGoalCell = PreviewGoalCell;
		ClearPresentation();
		OnPreviewChanged.Broadcast(LatestResult);
		return LatestResult;
	}

	GridPath->Origin = EGridNavigationPathOrigin::Preview;
	GridPath->SourcePreviewId = QuerySignature;
	TArray<FGridCellId> EffectiveCells = GridPath->CellPath;
	FGridCellId EffectiveGoalCell = PreviewGoalCell;
	bool bGoalAdjustedForContention = false;
	if (bRequestedGoalContested
		&& GoalContentionPolicy == EGridGoalContentionPolicy::StopBeforeOccupied)
	{
		if (GridPath->IsPartial()
			|| EffectiveCells.Num() < 2
			|| EffectiveCells.Last() != PreviewGoalCell)
		{
			NativePreviewPath.Reset();
			LatestInjectedPath = FGridInjectedPath();
			LatestResult = FGridPathPreviewResult();
			LatestResult.Status = EGridQueryStatus::Unreachable;
			LatestResult.Path.Status = EGridQueryStatus::Unreachable;
			LatestResult.FailureReason = EGridPathPreviewFailureReason::GoalOccupied;
			LatestResult.StartCell = StartCell;
			LatestResult.GoalCell = PreviewGoalCell;
			LatestResult.RequestedGoalCell = PreviewGoalCell;
			LatestResult.QuerySignature = QuerySignature;
			LatestResult.RequestGeneration = RequestGeneration;
			CacheSemanticInputs();
			ClearPresentation();
			OnPreviewChanged.Broadcast(LatestResult);
			return LatestResult;
		}

		if (!UE::GridWorld::Private::BuildStopBeforeOccupiedCells(
			GridPath->CellPath,
			PreviewGoalCell,
			EffectiveCells,
			EffectiveGoalCell))
		{
			PublishFailure(EGridQueryStatus::Unreachable, EGridPathPreviewFailureReason::GoalOccupied);
			return LatestResult;
		}
		const FGridCellData* EffectiveGoalData = Snapshot->FindCell(EffectiveGoalCell);
		FGridTrafficGoalClaimRequest EffectiveClaimRequest;
		if (EffectiveGoalData == nullptr
			|| !BuildGoalClaimRequest(*Controller, *EffectiveGoalData, EffectiveClaimRequest)
			|| !NavData->CanClaimTrafficGoal(EffectiveClaimRequest))
		{
			NativePreviewPath.Reset();
			LatestInjectedPath = FGridInjectedPath();
			LatestResult = FGridPathPreviewResult();
			LatestResult.Status = EGridQueryStatus::Unreachable;
			LatestResult.Path.Status = EGridQueryStatus::Unreachable;
			LatestResult.FailureReason = EGridPathPreviewFailureReason::GoalOccupied;
			LatestResult.StartCell = StartCell;
			LatestResult.GoalCell = EffectiveGoalCell;
			LatestResult.RequestedGoalCell = PreviewGoalCell;
			LatestResult.QuerySignature = QuerySignature;
			LatestResult.RequestGeneration = RequestGeneration;
			CacheSemanticInputs();
			ClearPresentation();
			OnPreviewChanged.Broadcast(LatestResult);
			return LatestResult;
		}
		bGoalAdjustedForContention = true;
	}

	const FGridInjectedPathValidationResult InjectionResult = NavData->CreateExactInjectedPath(
		Controller,
		AgentProperties,
		PreviewFilterClass,
		Controller->GetNavAgentLocation(),
		EffectiveCells,
		EffectiveGoalCell,
		true,
		GridPath->IsPartial(),
		InjectedPathInvalidationPolicy,
		QuerySignature,
		LatestInjectedPath);
	if (InjectionResult.bIsValid)
	{
		LatestInjectedPath.RequestedGoalCell = PreviewGoalCell;
	}

	FGridNavigationPath* PresentedGridPath = GridPath;
	if (InjectionResult.bIsValid && bGoalAdjustedForContention)
	{
		const FPathFindingResult MaterializedPath = NavData->MaterializeInjectedPath(
			LatestInjectedPath,
			Controller,
			AgentProperties,
			Controller->GetNavAgentLocation());
		if (MaterializedPath.IsSuccessful() && MaterializedPath.Path.IsValid())
		{
			NativePreviewPath = MaterializedPath.Path;
			PresentedGridPath = MaterializedPath.Path->CastPath<FGridNavigationPath>();
			if (PresentedGridPath != nullptr)
			{
				PresentedGridPath->Origin = EGridNavigationPathOrigin::Preview;
				PresentedGridPath->SourcePreviewId = QuerySignature;
			}
		}
		else
		{
			PresentedGridPath = nullptr;
		}
	}
	else
	{
		NativePreviewPath = PathResult.Path;
	}

	LatestResult = FGridPathPreviewResult();
	LatestResult.Status = PresentedGridPath != nullptr && PresentedGridPath->IsPartial()
		? EGridQueryStatus::Partial
		: EGridQueryStatus::Success;
	LatestResult.FailureReason = EGridPathPreviewFailureReason::None;
	LatestResult.StartCell = StartCell;
	LatestResult.GoalCell = EffectiveGoalCell;
	LatestResult.RequestedGoalCell = PreviewGoalCell;
	LatestResult.bGoalAdjustedForContention = bGoalAdjustedForContention;
	LatestResult.QuerySignature = QuerySignature;
	LatestResult.RequestGeneration = RequestGeneration;
	LatestResult.Path.Status = LatestResult.Status;
	LatestResult.Path.Cells = EffectiveCells;
	if (PresentedGridPath != nullptr)
	{
		for (const FNavPathPoint& Point : PresentedGridPath->GetPathPoints())
		{
			LatestResult.Path.WorldPoints.Add(Point.Location);
		}
		LatestResult.Path.Length = PresentedGridPath->GetLength();
		LatestResult.Path.Cost = PresentedGridPath->GetCost();
		LatestResult.Path.OptimizationMode = PresentedGridPath->OptimizationMode;
		LatestResult.Path.TurnCount = PresentedGridPath->TurnCount;
		LatestResult.Path.VisitedNodes = PresentedGridPath->VisitedNodes;
		LatestResult.Path.Revisions = PresentedGridPath->Revisions;
	}
	LatestResult.bIsStale = false;
	LatestResult.bIsCommittable = PresentedGridPath != nullptr
		&& (!PresentedGridPath->IsPartial()
			|| PartialPathPolicy == EGridPartialPathPreviewPolicy::ShowAndAllowCommit);

	if (!InjectionResult.bIsValid || PresentedGridPath == nullptr)
	{
		GRIDWORLD_LOG_WARNING(
			"GridWorld preview found a path for controller '%s' but could not create its exact injected payload: %s",
			*GetNameSafe(Controller),
			*InjectionResult.DiagnosticMessage);
		LatestInjectedPath = FGridInjectedPath();
		LatestResult.Status = EGridQueryStatus::InternalError;
		LatestResult.FailureReason = EGridPathPreviewFailureReason::InternalError;
		LatestResult.bIsCommittable = false;
	}
	else if (PresentedGridPath->IsPartial() && PartialPathPolicy != EGridPartialPathPreviewPolicy::ShowAndAllowCommit)
	{
		LatestResult.FailureReason = EGridPathPreviewFailureReason::PartialPathBlocked;
	}

	CacheSemanticInputs();
	UpdatePresentation();
	OnPreviewChanged.Broadcast(LatestResult);
	return LatestResult;
}

bool UGridPathPreviewComponent::BuildGoalClaimRequest(
	AController& Controller,
	const FGridCellData& GoalCell,
	FGridTrafficGoalClaimRequest& OutRequest) const
{
	OutRequest = FGridTrafficGoalClaimRequest();
	APawn* Pawn = Controller.GetPawn();
	const UGridNavigationOccupancyComponent* Occupancy = IsValid(Pawn)
		? UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*Pawn)
		: nullptr;
	if (Occupancy == nullptr || !Occupancy->OccupantId.IsValid())
	{
		return false;
	}

	const FNavAgentProperties& AgentProperties = Controller.GetNavAgentPropertiesRef();
	OutRequest.OwnerId = Occupancy->OccupantId;
	OutRequest.Claimant = const_cast<UGridPathPreviewComponent*>(this);
	OutRequest.Pawn = Pawn;
	OutRequest.GoalCell = {GoalCell.Id, GoalCell.WorldCenter};
	OutRequest.AgentRadius = AgentProperties.AgentRadius > 0.0f ? AgentProperties.AgentRadius : 42.0f;
	OutRequest.AgentHeight = AgentProperties.AgentHeight > 0.0f ? AgentProperties.AgentHeight : 192.0f;
	OutRequest.AdditionalSeparation = FMath::Max(0.0f, AdditionalGoalSeparation);
	return true;
}

void UGridPathPreviewComponent::PublishFailure(
	EGridQueryStatus Status,
	EGridPathPreviewFailureReason Reason)
{
	NativePreviewPath.Reset();
	LatestInjectedPath = FGridInjectedPath();
	LatestResult = FGridPathPreviewResult();
	LatestResult.Status = Status;
	LatestResult.Path.Status = Status;
	LatestResult.FailureReason = Reason;
	LatestResult.GoalCell = PreviewGoalCell;
	LatestResult.RequestedGoalCell = PreviewGoalCell;
	LatestResult.RequestGeneration = RequestGeneration;
	ClearPresentation();
	OnPreviewChanged.Broadcast(LatestResult);
}

void UGridPathPreviewComponent::UpdatePresentation()
{
	const bool bHidePartial = LatestResult.Status == EGridQueryStatus::Partial
		&& PartialPathPolicy == EGridPartialPathPreviewPolicy::HideAndBlockCommit;
	if (!bAutoPresentPreview || !LatestResult.IsSuccessful() || bHidePartial || LatestResult.Path.Cells.IsEmpty())
	{
		ClearPresentation();
		return;
	}
	UWorld* World = GetWorld();
	UGridPathPresentationSubsystem* Presentation = World != nullptr
		? World->GetSubsystem<UGridPathPresentationSubsystem>()
		: nullptr;
	if (Presentation == nullptr)
	{
		return;
	}
	if (bAutoEnableRenderers)
	{
		if (bRenderCellOverlay)
		{
			if (UGridRuntimeVisualizationSubsystem* Cells = World->GetSubsystem<UGridRuntimeVisualizationSubsystem>())
			{
				Cells->EnableVisualization(CellVisualStyle);
			}
		}
		if (bRenderLine)
		{
			if (UGridPathLineVisualizationSubsystem* Lines = World->GetSubsystem<UGridPathLineVisualizationSubsystem>())
			{
				Lines->EnableLineVisualization(LineVisualStyle);
			}
		}
	}
	if (Presentation->IsPathPresentationValid(PresentationHandle))
	{
		if (Presentation->UpdatePathPresentationFromQueryResult(PresentationHandle, LatestResult.Path, 0))
		{
			Presentation->SetPathPresentationPriority(PresentationHandle, PresentationPriority);
			Presentation->SetPathPresentationRenderers(PresentationHandle, bRenderCellOverlay, bRenderLine);
			return;
		}
		Presentation->ReleasePathPresentation(PresentationHandle);
		PresentationHandle = FGridPathPresentationHandle();
	}
	FGridPathPresentationRequest Request;
	Request.Purpose = EGridPathPresentationPurpose::Preview;
	Request.ProgressMode = EGridPathProgressPresentationMode::AllCells;
	Request.Lifetime = EGridPathPresentationLifetime::OwnerLifetime;
	Request.Owner = this;
	Request.Priority = PresentationPriority;
	Request.bRenderCellOverlay = bRenderCellOverlay;
	Request.bRenderLine = bRenderLine;
	Presentation->CreatePathPresentationFromQueryResult(LatestResult.Path, Request, PresentationHandle);
}

void UGridPathPreviewComponent::ClearPresentation()
{
	if (!PresentationHandle.IsSet())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UGridPathPresentationSubsystem* Presentation = World->GetSubsystem<UGridPathPresentationSubsystem>())
		{
			Presentation->ReleasePathPresentation(PresentationHandle);
		}
	}
	PresentationHandle = FGridPathPresentationHandle();
}

void UGridPathPreviewComponent::BindNavigationData(AGridNavigationData* NavigationData)
{
	if (PreviewNavigationData.Get() == NavigationData)
	{
		return;
	}
	UnbindNavigationData();
	PreviewNavigationData = NavigationData;
	if (NavigationData != nullptr)
	{
		TrafficReservationsChangedHandle = NavigationData->OnTrafficReservationsChanged().AddUObject(
			this,
			&UGridPathPreviewComponent::HandleTrafficReservationsChanged);
	}
}

void UGridPathPreviewComponent::UnbindNavigationData()
{
	if (AGridNavigationData* NavigationData = PreviewNavigationData.Get())
	{
		if (TrafficReservationsChangedHandle.IsValid())
		{
			NavigationData->OnTrafficReservationsChanged().Remove(TrafficReservationsChangedHandle);
		}
	}
	TrafficReservationsChangedHandle.Reset();
	PreviewNavigationData.Reset();
}
