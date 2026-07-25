// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/GridMoveToCellTask.h"

#include "AIController.h"
#include "AISystem.h"
#include "AI/GridGoalContention.h"
#include "AI/Navigation/NavAgentInterface.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GridWorldModule.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridNavigationQueryFilter.h"
#include "Navigation/GridTrafficReservation.h"
#include "Navigation/GridWorldSnapshot.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "TimerManager.h"

namespace UE::GridWorld::Private
{
	FVector ResolveActorMoveGoal(const AActor& GoalActor, const AAIController* Controller)
	{
		if (const INavAgentInterface* NavigationGoal = Cast<const INavAgentInterface>(&GoalActor))
		{
			return NavigationGoal->GetNavAgentLocation()
				+ GoalActor.GetActorQuat().RotateVector(NavigationGoal->GetMoveGoalOffset(Controller));
		}
		return GoalActor.GetActorLocation();
	}

	bool IsCellAllowedByFilter(
		const FGridCellData& Cell,
		const FSharedConstNavQueryFilter& Filter)
	{
		if (!Cell.bWalkable)
		{
			return false;
		}
		if (!Filter.IsValid())
		{
			return true;
		}
		if ((Cell.TraversalFlags & Filter->GetExcludeFlags()) != 0
			|| (Cell.TraversalFlags & Filter->GetIncludeFlags()) == 0)
		{
			return false;
		}
		const FGridNavigationQueryFilterImpl* GridFilter = static_cast<const FGridNavigationQueryFilterImpl*>(Filter->GetImplementation());
		if (GridFilter == nullptr)
		{
			return true;
		}
		const uint16 ChannelMask = static_cast<uint16>(1u << FMath::Min<uint8>(GridFilter->GetTraversalChannel(), 15));
		return GridFilter->GetAreaCost(Cell.AreaId) < BIG_NUMBER * 0.5f
			&& (Cell.TraversalChannels & ChannelMask) != 0;
	}
}

UGridMoveToCellTask::UGridMoveToCellTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UGridMoveToCellTask* UGridMoveToCellTask::MoveToGridCell(
	AAIController* Controller,
	FVector GoalLocation,
	AActor* GoalActor,
	float AcceptanceRadius,
	EAIOptionFlag::Type StopOnOverlap,
	EAIOptionFlag::Type AcceptPartialPath,
	bool bUsePathfinding,
	bool bLockAILogic,
	bool bTrackMovingGoal,
	EAIOptionFlag::Type RequireNavigableEndLocation,
	TSubclassOf<UNavigationQueryFilter> FilterClass,
	bool bAllowStrafe,
	EGridGoalContentionPolicy InGoalContentionPolicy,
	int32 InMaxAlternativeSearchRadius,
	float InAdditionalGoalSeparation,
	bool bInAutoRegisterPawnOccupancy,
	float InGoalAvailabilityTimeout,
	float InGoalWaitWarningInterval)
{
	UGridMoveToCellTask* Task = Controller
		? UAITask::NewAITask<UGridMoveToCellTask>(*Controller, EAITaskPriority::High)
		: nullptr;
	if (Task == nullptr)
	{
		return nullptr;
	}

	FAIMoveRequest Request;
	if (GoalActor != nullptr)
	{
		Request.SetGoalActor(GoalActor);
	}
	else
	{
		Request.SetGoalLocation(GoalLocation);
	}
	Request.SetAcceptanceRadius(AcceptanceRadius);
	Request.SetReachTestIncludesAgentRadius(FAISystem::PickAIOption(StopOnOverlap, Request.IsReachTestIncludingAgentRadius()));
	Request.SetReachTestIncludesGoalRadius(FAISystem::PickAIOption(StopOnOverlap, Request.IsReachTestIncludingGoalRadius()));
	Request.SetAllowPartialPath(FAISystem::PickAIOption(AcceptPartialPath, Request.IsUsingPartialPaths()));
	Request.SetUsePathfinding(bUsePathfinding);
	Request.SetRequireNavigableEndLocation(FAISystem::PickAIOption(RequireNavigableEndLocation, Request.IsNavigableEndLocationRequired()));
	Request.SetProjectGoalLocation(false);
	Request.SetNavigationFilter(FilterClass != nullptr ? FilterClass : Controller->GetDefaultNavigationFilterClass());
	Request.SetCanStrafe(bAllowStrafe);

	Task->SetUpGridMove(Controller, Request, GoalActor, bTrackMovingGoal);
	Task->SetGoalContentionSettings(
		InGoalContentionPolicy,
		InMaxAlternativeSearchRadius,
		InAdditionalGoalSeparation,
		bInAutoRegisterPawnOccupancy,
		InGoalAvailabilityTimeout,
		InGoalWaitWarningInterval);
	if (bLockAILogic)
	{
		Task->RequestAILogicLocking();
	}
	return Task;
}

UGridMoveToCellTask* UGridMoveToCellTask::MoveToGridExactPath(
	AAIController* Controller,
	const FGridInjectedPath& InInjectedPath,
	float AcceptanceRadius,
	EAIOptionFlag::Type StopOnOverlap,
	bool bLockAILogic,
	EAIOptionFlag::Type RequireNavigableEndLocation,
	bool bAllowStrafe,
	EGridGoalContentionPolicy InGoalContentionPolicy,
	float InAdditionalGoalSeparation,
	bool bInAutoRegisterPawnOccupancy)
{
	UGridMoveToCellTask* Task = Controller
		? UAITask::NewAITask<UGridMoveToCellTask>(*Controller, EAITaskPriority::High)
		: nullptr;
	if (Task == nullptr)
	{
		return nullptr;
	}

	FAIMoveRequest Request(FVector::ZeroVector);
	Request.SetAcceptanceRadius(AcceptanceRadius);
	Request.SetReachTestIncludesAgentRadius(FAISystem::PickAIOption(StopOnOverlap, Request.IsReachTestIncludingAgentRadius()));
	Request.SetReachTestIncludesGoalRadius(FAISystem::PickAIOption(StopOnOverlap, Request.IsReachTestIncludingGoalRadius()));
	Request.SetAllowPartialPath(InInjectedPath.bAllowPartialPath);
	Request.SetUsePathfinding(true);
	Request.SetRequireNavigableEndLocation(FAISystem::PickAIOption(
		RequireNavigableEndLocation,
		Request.IsNavigableEndLocationRequired()));
	Request.SetProjectGoalLocation(false);
	Request.SetNavigationFilter(InInjectedPath.FilterClass);
	Request.SetCanStrafe(bAllowStrafe);
	Task->SetUpExactGridMove(Controller, InInjectedPath, Request);
	Task->SetGoalContentionSettings(
		InGoalContentionPolicy,
		3,
		InAdditionalGoalSeparation,
		bInAutoRegisterPawnOccupancy);
	if (bLockAILogic)
	{
		Task->RequestAILogicLocking();
	}
	return Task;
}

void UGridMoveToCellTask::SetUpGridMove(
	AAIController* Controller,
	const FAIMoveRequest& InMoveRequest,
	AActor* InSourceGoalActor,
	bool bInTrackSourceGoalActor)
{
	UnbindTrackedGoal();
	ResetContentionState();
	MovePathSource = EGridMovePathSource::Destination;
	InjectedPath = FGridInjectedPath();
	SourceMoveRequest = InMoveRequest;
	SourceGoalActor = InSourceGoalActor != nullptr ? InSourceGoalActor : InMoveRequest.GetGoalActor();
	bHasSourceGoalActor = InSourceGoalActor != nullptr || InMoveRequest.IsMoveToActorRequest();
	bTrackSourceGoalActor = bInTrackSourceGoalActor;
	SourceGoalLocation = SourceGoalActor.IsValid()
		? UE::GridWorld::Private::ResolveActorMoveGoal(*SourceGoalActor.Get(), Controller)
		: InMoveRequest.GetGoalLocation();
	SetUp(Controller, InMoveRequest);
}

void UGridMoveToCellTask::SetUpExactGridMove(
	AAIController* Controller,
	const FGridInjectedPath& InInjectedPath,
	const FAIMoveRequest& InMoveRequest)
{
	UnbindTrackedGoal();
	ResetContentionState();
	MovePathSource = EGridMovePathSource::ExactInjectedPath;
	InjectedPath = InInjectedPath;
	SourceMoveRequest = InMoveRequest;
	SourceGoalActor.Reset();
	bHasSourceGoalActor = false;
	bTrackSourceGoalActor = false;
	SourceGoalLocation = InMoveRequest.GetGoalLocation();
	SetUp(Controller, InMoveRequest);
}

void UGridMoveToCellTask::SetGoalContentionSettings(
	EGridGoalContentionPolicy InPolicy,
	int32 InMaxAlternativeSearchRadius,
	float InAdditionalGoalSeparation,
	bool bInAutoRegisterPawnOccupancy,
	float InGoalAvailabilityTimeout,
	float InGoalWaitWarningInterval)
{
	GoalContentionPolicy = InPolicy;
	MaxAlternativeSearchRadius = FMath::Max(1, InMaxAlternativeSearchRadius);
	AdditionalGoalSeparation = FMath::Max(0.0f, InAdditionalGoalSeparation);
	bAutoRegisterPawnOccupancy = bInAutoRegisterPawnOccupancy;
	GoalAvailabilityTimeout = FMath::Max(0.1f, InGoalAvailabilityTimeout);
	GoalWaitWarningInterval = FMath::Clamp(InGoalWaitWarningInterval, 0.1f, GoalAvailabilityTimeout);
}

void UGridMoveToCellTask::PauseGridMove()
{
	if (IsActive())
	{
		Pause();
	}
}

void UGridMoveToCellTask::ResumeGridMove()
{
	if (IsPaused())
	{
		Resume();
	}
}

bool UGridMoveToCellTask::ResolveGridGoal(FGridCellId& OutCellId, FVector& OutCellCenter, FString* OutError) const
{
	auto Fail = [OutError](const TCHAR* Error)
	{
		if (OutError != nullptr)
		{
			*OutError = Error;
		}
		return false;
	};

	if (!IsValid(OwnerController) || OwnerController->GetWorld() == nullptr)
	{
		return Fail(TEXT("The AIController or its World is invalid."));
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(OwnerController->GetWorld());
	ANavigationData* SelectedNavigationData = NavigationSystem != nullptr
		? NavigationSystem->GetNavDataForProps(OwnerController->GetNavAgentPropertiesRef(), OwnerController->GetNavAgentLocation())
		: nullptr;
	AGridNavigationData* GridNavigationData = Cast<AGridNavigationData>(SelectedNavigationData);
	if (GridNavigationData == nullptr)
	{
		return Fail(TEXT("The Supported Agent did not resolve an AGridNavigationData instance."));
	}

	FVector GoalLocation = SourceGoalLocation;
	if (bHasSourceGoalActor)
	{
		const AActor* GoalActor = SourceGoalActor.Get();
		if (!IsValid(GoalActor))
		{
			return Fail(TEXT("The goal Actor is invalid."));
		}
		if (bTrackSourceGoalActor)
		{
			GoalLocation = UE::GridWorld::Private::ResolveActorMoveGoal(*GoalActor, OwnerController);
		}
	}
	if (GoalLocation.ContainsNaN())
	{
		return Fail(TEXT("The goal location is invalid."));
	}

	const FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*GridNavigationData,
		OwnerController,
		SourceMoveRequest.GetNavigationFilter());
	FNavLocation ProjectedLocation;
	if (!GridNavigationData->ProjectPoint(
		GoalLocation,
		ProjectedLocation,
		GridNavigationData->GetDefaultQueryExtent(),
		QueryFilter,
		OwnerController))
	{
		return Fail(TEXT("The goal could not be projected to a navigable GridWorld cell."));
	}

	const FGridWorldSnapshotPtr Snapshot = GridNavigationData->GetSnapshot();
	const int32 CellIndex = Snapshot.IsValid() ? Snapshot->ResolveNodeRef(ProjectedLocation.NodeRef) : INDEX_NONE;
	if (!Snapshot.IsValid() || !Snapshot->Cells.IsValidIndex(CellIndex))
	{
		return Fail(TEXT("The projected GridWorld cell became stale."));
	}

	OutCellId = Snapshot->Cells[CellIndex].Id;
	OutCellCenter = Snapshot->Cells[CellIndex].WorldCenter;
	return true;
}

bool UGridMoveToCellTask::ResolveCellCenter(const FGridCellId& CellId, FVector& OutCellCenter) const
{
	if (!CellId.IsValid() || !IsValid(OwnerController) || OwnerController->GetWorld() == nullptr)
	{
		return false;
	}
	const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(OwnerController->GetWorld());
	const AGridNavigationData* GridNavigationData = NavigationSystem != nullptr
		? Cast<AGridNavigationData>(NavigationSystem->GetNavDataForProps(
			OwnerController->GetNavAgentPropertiesRef(),
			OwnerController->GetNavAgentLocation()))
		: nullptr;
	const FGridWorldSnapshotPtr Snapshot = GridNavigationData != nullptr ? GridNavigationData->GetSnapshot() : nullptr;
	const FGridCellData* Cell = Snapshot.IsValid() ? Snapshot->FindCell(CellId) : nullptr;
	if (Cell == nullptr || !Cell->bWalkable)
	{
		return false;
	}
	OutCellCenter = Cell->WorldCenter;
	return true;
}

void UGridMoveToCellTask::EnsurePawnOccupancy()
{
	APawn* Pawn = IsValid(OwnerController) ? OwnerController->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		PawnOccupancyComponent.Reset();
		return;
	}
	const FNavAgentProperties& AgentProperties = OwnerController->GetNavAgentPropertiesRef();
	const float AgentRadius = AgentProperties.AgentRadius > 0.0f ? AgentProperties.AgentRadius : 42.0f;
	const float AgentHeight = AgentProperties.AgentHeight > 0.0f ? AgentProperties.AgentHeight : 192.0f;
	UGridNavigationOccupancyComponent* Component =
		UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
			*Pawn,
			AgentRadius,
			AgentHeight,
			bAutoRegisterPawnOccupancy);
	if (Component == nullptr && bAutoRegisterPawnOccupancy)
	{
		GRIDWORLD_LOG_WARNING(
			"Move To Grid Cell could not create occupancy tracking for pawn '%s'.",
			*GetNameSafe(Pawn));
	}
	PawnOccupancyComponent = Component;
}

AGridNavigationData* UGridMoveToCellTask::ResolveNavigationData() const
{
	if (!IsValid(OwnerController) || OwnerController->GetWorld() == nullptr)
	{
		return nullptr;
	}
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(OwnerController->GetWorld());
	return NavigationSystem != nullptr
		? Cast<AGridNavigationData>(NavigationSystem->GetNavDataForProps(
			OwnerController->GetNavAgentPropertiesRef(),
			OwnerController->GetNavAgentLocation()))
		: nullptr;
}

bool UGridMoveToCellTask::ResolveCurrentPawnCell(FGridCellId& OutCellId, FVector& OutCellCenter) const
{
	OutCellId = FGridCellId();
	OutCellCenter = FVector::ZeroVector;
	AGridNavigationData* GridNavigationData = ResolveNavigationData();
	if (GridNavigationData == nullptr)
	{
		return false;
	}
	const FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*GridNavigationData,
		OwnerController,
		SourceMoveRequest.GetNavigationFilter());
	FNavLocation ProjectedLocation;
	if (!GridNavigationData->ProjectPoint(
		OwnerController->GetNavAgentLocation(),
		ProjectedLocation,
		GridNavigationData->GetDefaultQueryExtent(),
		QueryFilter,
		OwnerController))
	{
		return false;
	}
	const FGridWorldSnapshotPtr Snapshot = GridNavigationData->GetSnapshot();
	const int32 CellIndex = Snapshot.IsValid() ? Snapshot->ResolveNodeRef(ProjectedLocation.NodeRef) : INDEX_NONE;
	if (!Snapshot.IsValid() || !Snapshot->Cells.IsValidIndex(CellIndex))
	{
		return false;
	}
	OutCellId = Snapshot->Cells[CellIndex].Id;
	OutCellCenter = Snapshot->Cells[CellIndex].WorldCenter;
	return true;
}

bool UGridMoveToCellTask::BuildGoalClaimRequest(
	const FGridCellId& CellId,
	FGridTrafficGoalClaimRequest& OutRequest) const
{
	OutRequest = FGridTrafficGoalClaimRequest();
	APawn* Pawn = IsValid(OwnerController) ? OwnerController->GetPawn() : nullptr;
	const UGridNavigationOccupancyComponent* Occupancy = PawnOccupancyComponent.Get();
	FVector CellCenter = FVector::ZeroVector;
	if (!IsValid(Pawn)
		|| Occupancy == nullptr
		|| !Occupancy->OccupantId.IsValid()
		|| !ResolveCellCenter(CellId, CellCenter))
	{
		return false;
	}

	const FNavAgentProperties& AgentProperties = OwnerController->GetNavAgentPropertiesRef();
	OutRequest.OwnerId = Occupancy->OccupantId;
	OutRequest.Claimant = const_cast<UGridMoveToCellTask*>(this);
	OutRequest.Pawn = Pawn;
	OutRequest.GoalCell = {CellId, CellCenter};
	OutRequest.AgentRadius = AgentProperties.AgentRadius > 0.0f ? AgentProperties.AgentRadius : 42.0f;
	OutRequest.AgentHeight = AgentProperties.AgentHeight > 0.0f ? AgentProperties.AgentHeight : 192.0f;
	OutRequest.AdditionalSeparation = AdditionalGoalSeparation;
	return true;
}

bool UGridMoveToCellTask::TryClaimExactGoal(const FGridCellId& CellId)
{
	AGridNavigationData* GridNavigationData = ResolveNavigationData();
	FGridTrafficGoalClaimRequest ClaimRequest;
	if (GridNavigationData == nullptr
		|| !BuildGoalClaimRequest(CellId, ClaimRequest)
		|| !GridNavigationData->TryClaimTrafficGoal(ClaimRequest))
	{
		return false;
	}

	ContentionNavigationData = GridNavigationData;
	ClaimedGoalCell = CellId;
	bHasGoalClaim = true;
	return true;
}

void UGridMoveToCellTask::CommitCurrentGoalAsParking()
{
	AGridNavigationData* GridNavigationData = ResolveNavigationData();
	FGridTrafficGoalClaimRequest Request;
	if (GridNavigationData != nullptr && BuildGoalClaimRequest(ProjectedGoalCell, Request))
	{
		GridNavigationData->CommitTrafficParking(Request);
	}
}

void UGridMoveToCellTask::FinishAlreadyAtBestCell(
	const FGridCellId& CellId,
	const FVector& CellCenter)
{
	ProjectedGoalCell = CellId;
	ProjectedGoalLocation = CellCenter;
	if (bWaitingForGoalAvailability)
	{
		UnbindTrackedGoal();
	}
	EndGoalAvailabilityWait();
	CommitCurrentGoalAsParking();
	ReleaseGoalClaim();
	FinishMoveTask(EPathFollowingResult::Success);
}

bool UGridMoveToCellTask::SelectAndClaimAlternative(
	const FGridCellId& DesiredCell,
	bool bIncludeDesiredCell,
	FGridCellId& OutCellId)
{
	check(IsInGameThread());
	if (!DesiredCell.IsValid() || !IsValid(OwnerController) || OwnerController->GetWorld() == nullptr)
	{
		return false;
	}

	AGridNavigationData* GridNavigationData = ResolveNavigationData();
	const FGridWorldSnapshotPtr Snapshot = GridNavigationData != nullptr ? GridNavigationData->GetSnapshot() : nullptr;
	const int32* DesiredIndexPtr = Snapshot.IsValid() ? Snapshot->CellIndexById.Find(DesiredCell) : nullptr;
	const int32 DesiredIndex = DesiredIndexPtr != nullptr ? *DesiredIndexPtr : INDEX_NONE;
	if (!Snapshot.IsValid() || !Snapshot->Cells.IsValidIndex(DesiredIndex))
	{
		return false;
	}

	const FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*GridNavigationData,
		OwnerController,
		SourceMoveRequest.GetNavigationFilter());
	TArray<UE::GridWorld::Private::FGridGoalCandidate> Candidates;
	UE::GridWorld::Private::GatherGridGoalCandidates(
		*Snapshot,
		DesiredIndex,
		bIncludeDesiredCell,
		MaxAlternativeSearchRadius,
		RejectedGoalCells,
		Candidates);

	const FNavAgentProperties& AgentProperties = OwnerController->GetNavAgentPropertiesRef();
	const float AgentRadius = AgentProperties.AgentRadius > 0.0f ? AgentProperties.AgentRadius : 42.0f;
	const float AgentHeight = AgentProperties.AgentHeight > 0.0f ? AgentProperties.AgentHeight : 192.0f;
	const FGuid OwnOccupantId = PawnOccupancyComponent.IsValid() ? PawnOccupancyComponent->OccupantId : FGuid();

	struct FEligibleGoal
	{
		const UE::GridWorld::Private::FGridGoalCandidate* Candidate = nullptr;
		FGridTrafficGoalClaimRequest ClaimRequest;
	};
	TArray<FEligibleGoal, TInlineAllocator<32>> EligibleGoals;
	int32 MinimumSearchDistance = MAX_int32;
	FGridCellId CurrentPawnCell;
	FVector CurrentPawnCellCenter = FVector::ZeroVector;
	ResolveCurrentPawnCell(CurrentPawnCell, CurrentPawnCellCenter);

	for (const UE::GridWorld::Private::FGridGoalCandidate& Candidate : Candidates)
	{
		const FGridCellData& CandidateCell = Snapshot->Cells[Candidate.CellIndex];
		bool bHasRequiredSeparation = UE::GridWorld::Private::HasGridGoalOccupancySeparation(
			*Snapshot,
			CandidateCell,
			OwnOccupantId,
			AgentRadius,
			AgentHeight,
			AdditionalGoalSeparation);
		if (!bHasRequiredSeparation)
		{
			continue;
		}
		if (!UE::GridWorld::Private::IsCellAllowedByFilter(CandidateCell, QueryFilter))
		{
			continue;
		}

		FGridTrafficGoalClaimRequest ClaimRequest;
		if (!bHasRequiredSeparation
			|| !BuildGoalClaimRequest(CandidateCell.Id, ClaimRequest)
			|| !GridNavigationData->CanClaimTrafficGoal(ClaimRequest))
		{
			continue;
		}
		EligibleGoals.Add({&Candidate, MoveTemp(ClaimRequest)});
		MinimumSearchDistance = FMath::Min(MinimumSearchDistance, Candidate.SearchDistance);
	}

	const FEligibleGoal* SelectedGoal = EligibleGoals.FindByPredicate([&CurrentPawnCell, MinimumSearchDistance](const FEligibleGoal& Goal)
	{
		return Goal.Candidate != nullptr
			&& Goal.Candidate->SearchDistance == MinimumSearchDistance
			&& Goal.ClaimRequest.GoalCell.CellId == CurrentPawnCell;
	});
	if (SelectedGoal == nullptr)
	{
		SelectedGoal = EligibleGoals.IsEmpty() ? nullptr : &EligibleGoals[0];
	}
	if (SelectedGoal == nullptr || !GridNavigationData->TryClaimTrafficGoal(SelectedGoal->ClaimRequest))
	{
		return false;
	}

	ContentionNavigationData = GridNavigationData;
	ClaimedGoalCell = SelectedGoal->ClaimRequest.GoalCell.CellId;
	bHasGoalClaim = true;
	OutCellId = ClaimedGoalCell;
	return true;
}

bool UGridMoveToCellTask::IsProjectedGoalContested() const
{
	check(IsInGameThread());
	if (!ProjectedGoalCell.IsValid() || !IsValid(OwnerController) || OwnerController->GetWorld() == nullptr)
	{
		return false;
	}
	UWorld& World = *OwnerController->GetWorld();
	AGridNavigationData* GridNavigationData = ResolveNavigationData();
	const APawn* Pawn = OwnerController->GetPawn();
	const UGridNavigationOccupancyComponent* OwnOccupancy = PawnOccupancyComponent.Get();
	if (OwnOccupancy == nullptr && IsValid(Pawn))
	{
		OwnOccupancy = UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*Pawn);
	}
	const FGuid OwnOccupantId = OwnOccupancy != nullptr ? OwnOccupancy->OccupantId : FGuid();
	if (GridNavigationData != nullptr
		&& GridNavigationData->IsTrafficGoalClaimedByOther(ProjectedGoalCell, this, OwnOccupantId))
	{
		return true;
	}

	const FGridWorldSnapshotPtr Snapshot = GridNavigationData != nullptr ? GridNavigationData->GetSnapshot() : nullptr;
	const FGridCellData* GoalCell = Snapshot.IsValid() ? Snapshot->FindCell(ProjectedGoalCell) : nullptr;
	if (GoalCell == nullptr || GoalCell->OccupancyOwners.IsEmpty())
	{
		return false;
	}

	FGuid WinningOccupantId;
	double WinningDistanceSquared = TNumericLimits<double>::Max();
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		TInlineComponentArray<UGridNavigationOccupancyComponent*> Components(*It);
		for (const UGridNavigationOccupancyComponent* Component : Components)
		{
			if (!IsValid(Component)
				|| !Component->IsRegistered()
				|| !Component->IsActive()
				|| !GoalCell->OccupancyOwners.Contains(Component->OccupantId)
				|| !Component->AffectsPoint(GoalCell->WorldCenter))
			{
				continue;
			}
			const double DistanceSquared = FVector::DistSquared(Component->GetComponentLocation(), GoalCell->WorldCenter);
			if (DistanceSquared < WinningDistanceSquared
				|| (FMath::IsNearlyEqual(DistanceSquared, WinningDistanceSquared) && Component->OccupantId < WinningOccupantId))
			{
				WinningDistanceSquared = DistanceSquared;
				WinningOccupantId = Component->OccupantId;
			}
		}
	}
	if (!WinningOccupantId.IsValid())
	{
		return !OwnOccupantId.IsValid() || !GoalCell->OccupancyOwners.Contains(OwnOccupantId);
	}
	return WinningOccupantId != OwnOccupantId;
}

bool UGridMoveToCellTask::StartContentionRedirect()
{
	RejectedGoalCells.Add(ProjectedGoalCell);
	ReleaseGoalClaim();
	if (!SelectAndClaimAlternative(RequestedGoalCell, false, PendingGoalCell))
	{
		bUsePendingGoalCell = false;
		bWaitingForAlternativeGoal = true;
		return false;
	}
	bUsePendingGoalCell = true;
	bWaitingForAlternativeGoal = false;
	return true;
}

void UGridMoveToCellTask::ReleaseGoalClaim()
{
	if (bHasGoalClaim)
	{
		AGridNavigationData* GridNavigationData = ContentionNavigationData.Get();
		if (GridNavigationData == nullptr)
		{
			GridNavigationData = ResolveNavigationData();
		}
		if (GridNavigationData != nullptr)
		{
			GridNavigationData->ReleaseTrafficGoalClaims(this);
		}
	}
	bHasGoalClaim = false;
	ClaimedGoalCell = FGridCellId();
	ContentionNavigationData.Reset();
}

void UGridMoveToCellTask::ResetContentionState()
{
	if (bWaitingForGoalAvailability)
	{
		UnbindTrackedGoal();
	}
	EndGoalAvailabilityWait();
	ReleaseGoalClaim();
	RequestedGoalCell = FGridCellId();
	PendingGoalCell = FGridCellId();
	RejectedGoalCells.Reset();
	bUsePendingGoalCell = false;
	bWaitingForAlternativeGoal = false;
}

void UGridMoveToCellTask::BeginGoalAvailabilityWait()
{
	if (bWaitingForGoalAvailability || !IsActive())
	{
		return;
	}
	UWorld* World = GetWorld();
	AGridNavigationData* GridNavigationData = ResolveNavigationData();
	if (World == nullptr || GridNavigationData == nullptr)
	{
		FinishMoveTask(EPathFollowingResult::Invalid);
		return;
	}

	bWaitingForGoalAvailability = true;
	GoalWaitStartedAt = World->GetTimeSeconds();
	ContentionNavigationData = GridNavigationData;
	if (!TrafficReservationsChangedHandle.IsValid())
	{
		TrafficReservationsChangedHandle = GridNavigationData->OnTrafficReservationsChanged().AddUObject(
			this,
			&UGridMoveToCellTask::HandleTrafficReservationsChanged);
	}
	World->GetTimerManager().SetTimer(
		GoalAvailabilityTimerHandle,
		this,
		&UGridMoveToCellTask::HandleGoalAvailabilityTimer,
		GoalWaitWarningInterval,
		true);
	BindTrackedGoal();
}

void UGridMoveToCellTask::EndGoalAvailabilityWait()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GoalAvailabilityTimerHandle);
	}
	if (AGridNavigationData* GridNavigationData = ContentionNavigationData.Get();
		GridNavigationData != nullptr && TrafficReservationsChangedHandle.IsValid())
	{
		GridNavigationData->OnTrafficReservationsChanged().Remove(TrafficReservationsChangedHandle);
	}
	TrafficReservationsChangedHandle.Reset();
	bWaitingForGoalAvailability = false;
	bGoalAvailabilityRetryScheduled = false;
	GoalWaitStartedAt = -1.0;
}

void UGridMoveToCellTask::HandleGoalAvailabilityTimer()
{
	if (!IsActive() || !bWaitingForGoalAvailability)
	{
		EndGoalAvailabilityWait();
		return;
	}
	const UWorld* World = GetWorld();
	const double Elapsed = World != nullptr && GoalWaitStartedAt >= 0.0
		? World->GetTimeSeconds() - GoalWaitStartedAt
		: GoalAvailabilityTimeout;
	const double Remaining = FMath::Max(0.0, static_cast<double>(GoalAvailabilityTimeout) - Elapsed);
	if (Elapsed + UE_KINDA_SMALL_NUMBER >= GoalAvailabilityTimeout)
	{
		GRIDWORLD_LOG_WARNING(
			"Move To Grid Cell timed out after %.2f seconds waiting for a separated destination for controller '%s' near requested cell (%d,%d,%d). The task finishes Blocked.",
			Elapsed,
			*GetNameSafe(OwnerController),
			RequestedGoalCell.Coord.X,
			RequestedGoalCell.Coord.Y,
			RequestedGoalCell.Coord.Layer);
		EndGoalAvailabilityWait();
		ReleaseGoalClaim();
		FinishMoveTask(EPathFollowingResult::Blocked);
		return;
	}

	GRIDWORLD_LOG_WARNING(
		"Move To Grid Cell controller '%s' is waiting for a separated destination near cell (%d,%d,%d): %.2f seconds elapsed, %.2f seconds remaining.",
		*GetNameSafe(OwnerController),
		RequestedGoalCell.Coord.X,
		RequestedGoalCell.Coord.Y,
		RequestedGoalCell.Coord.Layer,
		Elapsed,
		Remaining);
}

void UGridMoveToCellTask::HandleTrafficReservationsChanged()
{
	if (!IsActive() || !bWaitingForGoalAvailability || bGoalAvailabilityRetryScheduled)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		bGoalAvailabilityRetryScheduled = true;
		World->GetTimerManager().SetTimerForNextTick(this, &UGridMoveToCellTask::HandleTrafficReservationRetry);
	}
}

void UGridMoveToCellTask::HandleTrafficReservationRetry()
{
	bGoalAvailabilityRetryScheduled = false;
	if (IsActive() && bWaitingForGoalAvailability)
	{
		ConditionalPerformMove();
	}
}

void UGridMoveToCellTask::PerformMove()
{
	if (MovePathSource == EGridMovePathSource::ExactInjectedPath)
	{
		PerformExactMove();
		return;
	}

	if (GoalContentionPolicy != EGridGoalContentionPolicy::Ignore)
	{
		EnsurePawnOccupancy();
	}

	FString Error;
	FGridCellId DesiredGoalCell;
	FVector GoalCellCenter = FVector::ZeroVector;
	if (!ResolveGridGoal(DesiredGoalCell, GoalCellCenter, &Error))
	{
		GRIDWORLD_LOG_WARNING(
			"Move To Grid Cell failed for controller '%s': %s",
			*GetNameSafe(OwnerController),
			*Error);
		FinishMoveTask(EPathFollowingResult::Invalid);
		return;
	}

	if (RequestedGoalCell.IsValid() && RequestedGoalCell != DesiredGoalCell)
	{
		ResetContentionState();
	}
	RequestedGoalCell = DesiredGoalCell;
	const bool bUsesAtomicExactGoal =
		GoalContentionPolicy == EGridGoalContentionPolicy::RejectOccupied
		|| GoalContentionPolicy == EGridGoalContentionPolicy::StopBeforeOccupied;
	if (bUsesAtomicExactGoal
		&& !bHasGoalClaim
		&& !TryClaimExactGoal(DesiredGoalCell))
	{
		if (GoalContentionPolicy == EGridGoalContentionPolicy::StopBeforeOccupied)
		{
			AGridNavigationData* GridNavigationData = ResolveNavigationData();
			UPathFollowingComponent* PathFollowing = IsValid(OwnerController)
				? OwnerController->GetPathFollowingComponent()
				: nullptr;
			UNavigationSystemV1* NavigationSystem = IsValid(OwnerController)
				? FNavigationSystem::GetCurrent<UNavigationSystemV1>(OwnerController->GetWorld())
				: nullptr;
			const FSharedConstNavQueryFilter QueryFilter =
				GridNavigationData != nullptr && IsValid(OwnerController)
					? UNavigationQueryFilter::GetQueryFilter(
						*GridNavigationData,
						OwnerController,
						SourceMoveRequest.GetNavigationFilter())
					: FSharedConstNavQueryFilter();
			if (GridNavigationData == nullptr
				|| PathFollowing == nullptr
				|| NavigationSystem == nullptr
				|| !QueryFilter.IsValid())
			{
				FinishMoveTask(EPathFollowingResult::Invalid);
				return;
			}

			FPathFindingQuery Query(
				OwnerController,
				*GridNavigationData,
				OwnerController->GetNavAgentLocation(),
				GoalCellCenter,
				QueryFilter,
				nullptr,
				TNumericLimits<FVector::FReal>::Max(),
				SourceMoveRequest.IsNavigableEndLocationRequired());
			Query.SetAllowPartialPaths(SourceMoveRequest.IsUsingPartialPaths());
			Query.SetNavAgentProperties(OwnerController->GetNavAgentPropertiesRef());
			PathFollowing->OnPathfindingQuery(Query);
			const FPathFindingResult FullPath = NavigationSystem->FindPathSync(
				OwnerController->GetNavAgentPropertiesRef(),
				Query);

			FPathFindingResult AdjustedPath;
			FGridCellId EffectiveGoalCell;
			FVector EffectiveGoalLocation = FVector::ZeroVector;
			if (!BuildStopBeforeOccupiedPath(
				*GridNavigationData,
				DesiredGoalCell,
				FullPath,
				AdjustedPath,
				EffectiveGoalCell,
				EffectiveGoalLocation,
				Error))
			{
				GRIDWORLD_LOG_INFO(
					"Move To Grid Cell could not stop before occupied destination (%d,%d,%d) for controller '%s': %s",
					DesiredGoalCell.Coord.X,
					DesiredGoalCell.Coord.Y,
					DesiredGoalCell.Coord.Layer,
					*GetNameSafe(OwnerController),
					*Error);
				FinishMoveTask(EPathFollowingResult::Blocked);
				return;
			}
			if (!TryClaimExactGoal(EffectiveGoalCell))
			{
				GRIDWORLD_LOG_INFO(
					"Move To Grid Cell could not claim the preceding cell (%d,%d,%d) for controller '%s'.",
					EffectiveGoalCell.Coord.X,
					EffectiveGoalCell.Coord.Y,
					EffectiveGoalCell.Coord.Layer,
					*GetNameSafe(OwnerController));
				FinishMoveTask(EPathFollowingResult::Blocked);
				return;
			}

			ProjectedGoalCell = EffectiveGoalCell;
			ProjectedGoalLocation = EffectiveGoalLocation;
			MoveRequest = FAIMoveRequest(EffectiveGoalLocation);
			MoveRequest
				.SetNavigationFilter(SourceMoveRequest.GetNavigationFilter())
				.SetUsePathfinding(true)
				.SetAllowPartialPath(false)
				.SetRequireNavigableEndLocation(SourceMoveRequest.IsNavigableEndLocationRequired())
				.SetApplyCostLimitFromHeuristic(
					SourceMoveRequest.IsApplyingCostLimitFromHeuristic(),
					SourceMoveRequest.GetCostLimitFactor(),
					SourceMoveRequest.GetMinimumCostLimit())
				.SetProjectGoalLocation(false)
				.SetCanStrafe(SourceMoveRequest.CanStrafe())
				.SetReachTestIncludesAgentRadius(SourceMoveRequest.IsReachTestIncludingAgentRadius())
				.SetReachTestIncludesGoalRadius(SourceMoveRequest.IsReachTestIncludingGoalRadius())
				.SetAcceptanceRadius(SourceMoveRequest.GetAcceptanceRadius())
				.SetUserData(SourceMoveRequest.GetUserData())
				.SetUserFlags(SourceMoveRequest.GetUserFlags())
				.SetStartFromPreviousPath(SourceMoveRequest.ShouldStartFromPreviousPath());
			StartMaterializedMove(*PathFollowing, AdjustedPath.Path);
			MoveRequest = SourceMoveRequest;
			if (!IsFinished())
			{
				BindTrackedGoal();
			}
			return;
		}

		GRIDWORLD_LOG_INFO(
			"Move To Grid Cell rejected occupied destination (%d,%d,%d) for controller '%s'.",
			DesiredGoalCell.Coord.X,
			DesiredGoalCell.Coord.Y,
			DesiredGoalCell.Coord.Layer,
			*GetNameSafe(OwnerController));
		FinishMoveTask(EPathFollowingResult::Blocked);
		return;
	}
	FGridCellId CurrentPawnCell;
	FVector CurrentPawnCellCenter = FVector::ZeroVector;
	if (ResolveCurrentPawnCell(CurrentPawnCell, CurrentPawnCellCenter) && CurrentPawnCell == DesiredGoalCell)
	{
		FinishAlreadyAtBestCell(CurrentPawnCell, CurrentPawnCellCenter);
		return;
	}

	FGridCellId GoalCell = DesiredGoalCell;
	if (bWaitingForAlternativeGoal && !bHasGoalClaim)
	{
		if (!SelectAndClaimAlternative(DesiredGoalCell, false, PendingGoalCell))
		{
			BeginGoalAvailabilityWait();
			return;
		}
		bUsePendingGoalCell = true;
		bWaitingForAlternativeGoal = false;
	}
	else if (GoalContentionPolicy == EGridGoalContentionPolicy::ReserveBeforeMove && !bHasGoalClaim)
	{
		if (!SelectAndClaimAlternative(DesiredGoalCell, true, PendingGoalCell))
		{
			BeginGoalAvailabilityWait();
			return;
		}
		bUsePendingGoalCell = true;
	}
	if (bWaitingForGoalAvailability)
	{
		UnbindTrackedGoal();
	}
	EndGoalAvailabilityWait();
	if (bUsePendingGoalCell)
	{
		GoalCell = PendingGoalCell;
		if (!ResolveCellCenter(GoalCell, GoalCellCenter))
		{
			ReleaseGoalClaim();
			GRIDWORLD_LOG_WARNING(
				"Move To Grid Cell destination became stale for controller '%s'.",
				*GetNameSafe(OwnerController));
			FinishMoveTask(EPathFollowingResult::Invalid);
			return;
		}
	}
	if (ResolveCurrentPawnCell(CurrentPawnCell, CurrentPawnCellCenter) && CurrentPawnCell == GoalCell)
	{
		FinishAlreadyAtBestCell(CurrentPawnCell, CurrentPawnCellCenter);
		return;
	}

	ProjectedGoalCell = GoalCell;
	ProjectedGoalLocation = GoalCellCenter;
	MoveRequest = FAIMoveRequest(GoalCellCenter);
	MoveRequest
		.SetNavigationFilter(SourceMoveRequest.GetNavigationFilter())
		.SetUsePathfinding(SourceMoveRequest.IsUsingPathfinding())
		.SetAllowPartialPath(SourceMoveRequest.IsUsingPartialPaths())
		.SetRequireNavigableEndLocation(SourceMoveRequest.IsNavigableEndLocationRequired())
		.SetApplyCostLimitFromHeuristic(
			SourceMoveRequest.IsApplyingCostLimitFromHeuristic(),
			SourceMoveRequest.GetCostLimitFactor(),
			SourceMoveRequest.GetMinimumCostLimit())
		.SetProjectGoalLocation(false)
		.SetCanStrafe(SourceMoveRequest.CanStrafe())
		.SetReachTestIncludesAgentRadius(SourceMoveRequest.IsReachTestIncludingAgentRadius())
		.SetReachTestIncludesGoalRadius(SourceMoveRequest.IsReachTestIncludingGoalRadius())
		.SetAcceptanceRadius(SourceMoveRequest.GetAcceptanceRadius())
		.SetUserData(SourceMoveRequest.GetUserData())
		.SetUserFlags(SourceMoveRequest.GetUserFlags())
		.SetStartFromPreviousPath(SourceMoveRequest.ShouldStartFromPreviousPath());
	Super::PerformMove();
	// StateTree's native Vector tracking compares this public request with its bound raw destination.
	// Restore the source request after the controller has copied the projected request.
	MoveRequest = SourceMoveRequest;
	if (!IsFinished())
	{
		BindTrackedGoal();
	}
}

void UGridMoveToCellTask::PerformExactMove()
{
	if (GoalContentionPolicy != EGridGoalContentionPolicy::Ignore)
	{
		EnsurePawnOccupancy();
	}

	AGridNavigationData* GridNavigationData = ResolveNavigationData();
	UPathFollowingComponent* PathFollowing = IsValid(OwnerController)
		? OwnerController->GetPathFollowingComponent()
		: nullptr;
	FVector GoalLocation = FVector::ZeroVector;
	if (GridNavigationData == nullptr
		|| PathFollowing == nullptr
		|| !InjectedPath.IsSet()
		|| !ResolveCellCenter(InjectedPath.OriginalGoalCell, GoalLocation))
	{
		GRIDWORLD_LOG_WARNING(
			"Move Along Exact Grid Path failed for controller '%s': invalid nav data, path follower, payload, or original goal.",
			*GetNameSafe(OwnerController));
		FinishMoveTask(EPathFollowingResult::Invalid);
		return;
	}

	ProjectedGoalCell = InjectedPath.OriginalGoalCell;
	ProjectedGoalLocation = GoalLocation;
	RequestedGoalCell = InjectedPath.RequestedGoalCell.IsValid()
		? InjectedPath.RequestedGoalCell
		: ProjectedGoalCell;
	MoveRequest = FAIMoveRequest(GoalLocation);
	MoveRequest
		.SetNavigationFilter(InjectedPath.FilterClass)
		.SetUsePathfinding(true)
		.SetAllowPartialPath(InjectedPath.bAllowPartialPath)
		.SetRequireNavigableEndLocation(SourceMoveRequest.IsNavigableEndLocationRequired())
		.SetApplyCostLimitFromHeuristic(
			SourceMoveRequest.IsApplyingCostLimitFromHeuristic(),
			SourceMoveRequest.GetCostLimitFactor(),
			SourceMoveRequest.GetMinimumCostLimit())
		.SetProjectGoalLocation(false)
		.SetCanStrafe(SourceMoveRequest.CanStrafe())
		.SetReachTestIncludesAgentRadius(SourceMoveRequest.IsReachTestIncludingAgentRadius())
		.SetReachTestIncludesGoalRadius(SourceMoveRequest.IsReachTestIncludingGoalRadius())
		.SetAcceptanceRadius(SourceMoveRequest.GetAcceptanceRadius())
		.SetUserData(SourceMoveRequest.GetUserData())
		.SetUserFlags(SourceMoveRequest.GetUserFlags());

	const FGridInjectedPathValidationResult Validation = GridNavigationData->ValidateInjectedPath(
		InjectedPath,
		OwnerController,
		OwnerController->GetNavAgentPropertiesRef(),
		OwnerController->GetNavAgentLocation());
	if (!Validation.bIsValid)
	{
		if (InjectedPath.InvalidationPolicy == EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal)
		{
			if (GoalContentionPolicy == EGridGoalContentionPolicy::RejectOccupied)
			{
				EnsurePawnOccupancy();
				if (!bHasGoalClaim && !TryClaimExactGoal(ProjectedGoalCell))
				{
					GRIDWORLD_LOG_INFO(
						"Move Along Exact Grid Path rejected occupied destination (%d,%d,%d) for controller '%s'.",
						ProjectedGoalCell.Coord.X,
						ProjectedGoalCell.Coord.Y,
						ProjectedGoalCell.Coord.Layer,
						*GetNameSafe(OwnerController));
					FinishMoveTask(EPathFollowingResult::Blocked);
					return;
				}
			}
			GRIDWORLD_LOG_INFO(
				"Exact GridWorld path '%s' became stale for controller '%s'; recalculating to its original goal. Reason: %s",
				*InjectedPath.PathInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(OwnerController),
				*Validation.DiagnosticMessage);
			RecalculateInjectedPathToOriginalGoal(GoalLocation);
			return;
		}
		GRIDWORLD_LOG_WARNING(
			"Move Along Exact Grid Path rejected stale payload '%s' for controller '%s': %s",
			*InjectedPath.PathInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetNameSafe(OwnerController),
			*Validation.DiagnosticMessage);
		FinishMoveTask(EPathFollowingResult::Invalid);
		return;
	}

	FPathFindingResult Materialized = GridNavigationData->MaterializeInjectedPath(
		InjectedPath,
		OwnerController,
		OwnerController->GetNavAgentPropertiesRef(),
		OwnerController->GetNavAgentLocation());
	if (!Materialized.IsSuccessful() || !Materialized.Path.IsValid())
	{
		FinishMoveTask(EPathFollowingResult::Invalid);
		return;
	}

	// Validate and materialize against exactly the revisions captured by the preview before
	// taking the destination claim. The claim itself advances the traffic revision and would
	// otherwise make a Reserved Corridor payload reject its own atomic commit as stale.
	const bool bUsesAtomicExactGoal =
		GoalContentionPolicy == EGridGoalContentionPolicy::RejectOccupied
		|| GoalContentionPolicy == EGridGoalContentionPolicy::StopBeforeOccupied;
	if (bUsesAtomicExactGoal)
	{
		EnsurePawnOccupancy();
		if (!bHasGoalClaim && !TryClaimExactGoal(ProjectedGoalCell))
		{
			const bool bPathWasAlreadyAdjusted = RequestedGoalCell != ProjectedGoalCell;
			if (GoalContentionPolicy == EGridGoalContentionPolicy::StopBeforeOccupied
				&& !bPathWasAlreadyAdjusted)
			{
				FPathFindingResult AdjustedPath;
				FGridCellId EffectiveGoalCell;
				FVector EffectiveGoalLocation = FVector::ZeroVector;
				FString Error;
				if (BuildStopBeforeOccupiedPath(
						*GridNavigationData,
						RequestedGoalCell,
						Materialized,
						AdjustedPath,
						EffectiveGoalCell,
						EffectiveGoalLocation,
						Error)
					&& TryClaimExactGoal(EffectiveGoalCell))
				{
					Materialized = MoveTemp(AdjustedPath);
					ProjectedGoalCell = EffectiveGoalCell;
					ProjectedGoalLocation = EffectiveGoalLocation;
					MoveRequest.UpdateGoalLocation(EffectiveGoalLocation);
					MoveRequest.SetAllowPartialPath(false);
				}
				else
				{
					GRIDWORLD_LOG_INFO(
						"Move Along Exact Grid Path could not stop before occupied destination (%d,%d,%d) for controller '%s': %s",
						RequestedGoalCell.Coord.X,
						RequestedGoalCell.Coord.Y,
						RequestedGoalCell.Coord.Layer,
						*GetNameSafe(OwnerController),
						*Error);
					FinishMoveTask(EPathFollowingResult::Blocked);
					return;
				}
			}
			else
			{
			GRIDWORLD_LOG_INFO(
				"Move Along Exact Grid Path rejected occupied destination (%d,%d,%d) for controller '%s'.",
				ProjectedGoalCell.Coord.X,
				ProjectedGoalCell.Coord.Y,
				ProjectedGoalCell.Coord.Layer,
				*GetNameSafe(OwnerController));
			FinishMoveTask(EPathFollowingResult::Blocked);
			return;
			}
		}
	}

	StartMaterializedMove(*PathFollowing, Materialized.Path);
}

void UGridMoveToCellTask::StartMaterializedMove(
	UPathFollowingComponent& PathFollowing,
	const FNavPathSharedPtr& MaterializedPath)
{
	ResetObservers();
	ResetTimers();
	if (PathFollowing.HasReached(MoveRequest))
	{
		MoveRequestID = PathFollowing.RequestMoveWithImmediateFinish(EPathFollowingResult::Success);
		OnRequestFinished(
			MoveRequestID,
			FPathFollowingResult(EPathFollowingResult::Success, FPathFollowingResultFlags::AlreadyAtGoal));
		return;
	}

	MoveRequestID = OwnerController->RequestMove(MoveRequest, MaterializedPath);
	if (!MoveRequestID.IsValid())
	{
		FinishMoveTask(EPathFollowingResult::Invalid);
		return;
	}
	OwnerController->bAllowStrafe = MoveRequest.CanStrafe();
	PathFinishDelegateHandle = PathFollowing.OnRequestFinished.AddUObject(
		this,
		&UGridMoveToCellTask::OnRequestFinished);
	SetObservedPath(MaterializedPath);
}

bool UGridMoveToCellTask::BuildStopBeforeOccupiedPath(
	AGridNavigationData& NavigationData,
	const FGridCellId& RequestedCell,
	const FPathFindingResult& FullPath,
	FPathFindingResult& OutAdjustedPath,
	FGridCellId& OutEffectiveCell,
	FVector& OutEffectiveLocation,
	FString& OutError) const
{
	OutAdjustedPath = FPathFindingResult(ENavigationQueryResult::Error);
	const FGridNavigationPath* GridPath = FullPath.Path.IsValid()
		? FullPath.Path->CastPath<FGridNavigationPath>()
		: nullptr;
	if (GridPath == nullptr
		|| GridPath->IsPartial()
		|| GridPath->CellPath.Num() < 2
		|| GridPath->CellPath.Last() != RequestedCell)
	{
		OutError =
			TEXT("A complete GridWorld path ending at the requested occupied cell is required.");
		return false;
	}

	TArray<FGridCellId> AdjustedCells;
	if (!UE::GridWorld::Private::BuildStopBeforeOccupiedCells(
		GridPath->CellPath,
		RequestedCell,
		AdjustedCells,
		OutEffectiveCell))
	{
		OutError =
			TEXT("The complete GridWorld path has no immediate predecessor for the occupied destination.");
		return false;
	}
	const FGridWorldSnapshotPtr Snapshot = NavigationData.GetSnapshot();
	const FGridCellData* EffectiveGoal = Snapshot.IsValid()
		? Snapshot->FindCell(OutEffectiveCell)
		: nullptr;
	if (EffectiveGoal == nullptr || !EffectiveGoal->bWalkable)
	{
		OutError =
			TEXT("The cell immediately before the occupied destination no longer exists or is blocked.");
		return false;
	}
	OutEffectiveLocation = EffectiveGoal->WorldCenter;

	const TSubclassOf<UNavigationQueryFilter> FilterClass =
		MovePathSource == EGridMovePathSource::ExactInjectedPath
			? InjectedPath.FilterClass
			: SourceMoveRequest.GetNavigationFilter();
	const EGridInjectedPathInvalidationPolicy InvalidationPolicy =
		MovePathSource == EGridMovePathSource::ExactInjectedPath
			? InjectedPath.InvalidationPolicy
			: EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;
	const FGuid SourcePreviewId =
		MovePathSource == EGridMovePathSource::ExactInjectedPath
			? InjectedPath.SourcePreviewId
			: FGuid();
	FGridInjectedPath AdjustedPayload;
	const FGridInjectedPathValidationResult Creation = NavigationData.CreateExactInjectedPath(
		OwnerController,
		OwnerController->GetNavAgentPropertiesRef(),
		FilterClass,
		OwnerController->GetNavAgentLocation(),
		AdjustedCells,
		OutEffectiveCell,
		false,
		false,
		InvalidationPolicy,
		SourcePreviewId,
		AdjustedPayload);
	if (!Creation.bIsValid)
	{
		OutError = Creation.DiagnosticMessage;
		return false;
	}
	AdjustedPayload.RequestedGoalCell = RequestedCell;
	OutAdjustedPath = NavigationData.MaterializeInjectedPath(
		AdjustedPayload,
		OwnerController,
		OwnerController->GetNavAgentPropertiesRef(),
		OwnerController->GetNavAgentLocation());
	if (!OutAdjustedPath.IsSuccessful() || !OutAdjustedPath.Path.IsValid())
	{
		OutError = TEXT("The adjusted GridWorld path could not be materialized.");
		return false;
	}
	return true;
}

void UGridMoveToCellTask::RecalculateInjectedPathToOriginalGoal(const FVector& GoalLocation)
{
	MoveRequest.UpdateGoalLocation(GoalLocation);
	// Exact validation owns the initial hand-off only. Once a relevant runtime change
	// requires a repath, subsequent invalidations must use normal destination semantics
	// from the Pawn's current cell instead of revalidating the recorded first cell.
	MovePathSource = EGridMovePathSource::Destination;
	SourceMoveRequest = MoveRequest;
	SourceGoalLocation = GoalLocation;
	if (GoalContentionPolicy == EGridGoalContentionPolicy::StopBeforeOccupied)
	{
		EnsurePawnOccupancy();
		if (!bHasGoalClaim && TryClaimExactGoal(ProjectedGoalCell))
		{
			Super::PerformMove();
			if (FGridNavigationPath* GridPath = Path.IsValid() ? Path->CastPath<FGridNavigationPath>() : nullptr)
			{
				GridPath->Origin = EGridNavigationPathOrigin::Recalculated;
				GridPath->ParentPathInstanceId = InjectedPath.PathInstanceId;
				GridPath->SourcePreviewId = InjectedPath.SourcePreviewId;
				GridPath->InjectedInvalidationPolicy = InjectedPath.InvalidationPolicy;
			}
			return;
		}

		AGridNavigationData* GridNavigationData = ResolveNavigationData();
		UPathFollowingComponent* PathFollowing = IsValid(OwnerController)
			? OwnerController->GetPathFollowingComponent()
			: nullptr;
		UNavigationSystemV1* NavigationSystem = IsValid(OwnerController)
			? FNavigationSystem::GetCurrent<UNavigationSystemV1>(OwnerController->GetWorld())
			: nullptr;
		const FSharedConstNavQueryFilter QueryFilter =
			GridNavigationData != nullptr && IsValid(OwnerController)
				? UNavigationQueryFilter::GetQueryFilter(
					*GridNavigationData,
					OwnerController,
					InjectedPath.FilterClass)
				: FSharedConstNavQueryFilter();
		if (GridNavigationData == nullptr
			|| PathFollowing == nullptr
			|| NavigationSystem == nullptr
			|| !QueryFilter.IsValid())
		{
			FinishMoveTask(EPathFollowingResult::Invalid);
			return;
		}

		FPathFindingQuery Query(
			OwnerController,
			*GridNavigationData,
			OwnerController->GetNavAgentLocation(),
			GoalLocation,
			QueryFilter,
			nullptr,
			TNumericLimits<FVector::FReal>::Max(),
			MoveRequest.IsNavigableEndLocationRequired());
		Query.SetAllowPartialPaths(MoveRequest.IsUsingPartialPaths());
		Query.SetNavAgentProperties(OwnerController->GetNavAgentPropertiesRef());
		PathFollowing->OnPathfindingQuery(Query);
		const FPathFindingResult FullPath = NavigationSystem->FindPathSync(
			OwnerController->GetNavAgentPropertiesRef(),
			Query);

		FPathFindingResult AdjustedPath;
		FGridCellId EffectiveGoalCell;
		FVector EffectiveGoalLocation = FVector::ZeroVector;
		FString Error;
		if (!BuildStopBeforeOccupiedPath(
				*GridNavigationData,
				ProjectedGoalCell,
				FullPath,
				AdjustedPath,
				EffectiveGoalCell,
				EffectiveGoalLocation,
				Error)
			|| !TryClaimExactGoal(EffectiveGoalCell))
		{
			GRIDWORLD_LOG_INFO(
				"Recalculated exact path could not stop before occupied destination (%d,%d,%d) for controller '%s': %s",
				ProjectedGoalCell.Coord.X,
				ProjectedGoalCell.Coord.Y,
				ProjectedGoalCell.Coord.Layer,
				*GetNameSafe(OwnerController),
				*Error);
			FinishMoveTask(EPathFollowingResult::Blocked);
			return;
		}

		ProjectedGoalCell = EffectiveGoalCell;
		ProjectedGoalLocation = EffectiveGoalLocation;
		MoveRequest.UpdateGoalLocation(EffectiveGoalLocation);
		MoveRequest.SetAllowPartialPath(false);
		if (FGridNavigationPath* GridPath = AdjustedPath.Path->CastPath<FGridNavigationPath>())
		{
			GridPath->Origin = EGridNavigationPathOrigin::Recalculated;
			GridPath->ParentPathInstanceId = InjectedPath.PathInstanceId;
			GridPath->SourcePreviewId = InjectedPath.SourcePreviewId;
			GridPath->InjectedInvalidationPolicy = InjectedPath.InvalidationPolicy;
		}
		StartMaterializedMove(*PathFollowing, AdjustedPath.Path);
		return;
	}

	Super::PerformMove();
	if (FGridNavigationPath* GridPath = Path.IsValid() ? Path->CastPath<FGridNavigationPath>() : nullptr)
	{
		GridPath->Origin = EGridNavigationPathOrigin::Recalculated;
		GridPath->ParentPathInstanceId = InjectedPath.PathInstanceId;
		GridPath->SourcePreviewId = InjectedPath.SourcePreviewId;
		GridPath->InjectedInvalidationPolicy = InjectedPath.InvalidationPolicy;
	}
}

void UGridMoveToCellTask::OnPathEvent(FNavigationPath* InPath, ENavPathEvent::Type Event)
{
	if (MovePathSource == EGridMovePathSource::ExactInjectedPath
		&& InjectedPath.InvalidationPolicy == EGridInjectedPathInvalidationPolicy::FailOnInvalidation
		&& Event == ENavPathEvent::Invalidated)
	{
		GRIDWORLD_LOG_WARNING(
			"Exact GridWorld path '%s' was invalidated while followed by controller '%s'; strict policy aborts the move.",
			*InjectedPath.PathInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetNameSafe(OwnerController));
		FinishMoveTask(EPathFollowingResult::Aborted);
		return;
	}
	Super::OnPathEvent(InPath, Event);
}

void UGridMoveToCellTask::OnRequestFinished(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	if (RequestID != MoveRequestID)
	{
		Super::OnRequestFinished(RequestID, Result);
		return;
	}

	const bool bReachedFullDestination = Result.IsSuccess() && (!Path.IsValid() || !Path->IsPartial());
	if (GoalContentionPolicy == EGridGoalContentionPolicy::Ignore)
	{
		if (bReachedFullDestination)
		{
			CommitCurrentGoalAsParking();
		}
		Super::OnRequestFinished(RequestID, Result);
		return;
	}
	const FNavAgentProperties& AgentProperties = OwnerController->GetNavAgentPropertiesRef();
	const float AgentRadius = AgentProperties.AgentRadius > 0.0f ? AgentProperties.AgentRadius : 42.0f;
	const bool bBlockedNearDestination = Result.Code == EPathFollowingResult::Blocked
		&& FVector::Dist2D(OwnerController->GetNavAgentLocation(), ProjectedGoalLocation)
			<= AgentRadius * 2.0f + AdditionalGoalSeparation + UE_KINDA_SMALL_NUMBER;
	FGridCellId CurrentPawnCell;
	FVector CurrentPawnCellCenter = FVector::ZeroVector;
	const bool bBlockedAtExactGoalPredecessor =
		Result.Code == EPathFollowingResult::Blocked
		&& MovePathSource == EGridMovePathSource::ExactInjectedPath
		&& InjectedPath.bAllowDynamicAgentConflictsDuringValidation
		&& InjectedPath.Cells.Num() >= 2
		&& ProjectedGoalCell == InjectedPath.Cells.Last()
		&& ResolveCurrentPawnCell(CurrentPawnCell, CurrentPawnCellCenter)
		&& CurrentPawnCell == InjectedPath.Cells[InjectedPath.Cells.Num() - 2];
	const bool bMayBeGoalContention =
		bReachedFullDestination || bBlockedNearDestination || bBlockedAtExactGoalPredecessor;
	if (bMayBeGoalContention && IsProjectedGoalContested())
	{
		if (GoalContentionPolicy == EGridGoalContentionPolicy::RejectOccupied
			|| GoalContentionPolicy == EGridGoalContentionPolicy::StopBeforeOccupied)
		{
			ReleaseGoalClaim();
			FinishMoveTask(EPathFollowingResult::Blocked);
			return;
		}
		MoveRequestID = FAIRequestID::InvalidRequest;
		ResetObservers();
		if (StartContentionRedirect())
		{
			GRIDWORLD_LOG_INFO(
				"Move To Grid Cell redirected controller '%s' from occupied cell (%d,%d,%d) to (%d,%d,%d).",
				*GetNameSafe(OwnerController),
				ProjectedGoalCell.Coord.X,
				ProjectedGoalCell.Coord.Y,
				ProjectedGoalCell.Coord.Layer,
				PendingGoalCell.Coord.X,
				PendingGoalCell.Coord.Y,
				PendingGoalCell.Coord.Layer);
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimerForNextTick(this, &UGridMoveToCellTask::ConditionalPerformMove);
			}
			return;
		}

		BeginGoalAvailabilityWait();
		return;
	}

	if (bReachedFullDestination)
	{
		CommitCurrentGoalAsParking();
	}
	ReleaseGoalClaim();
	Super::OnRequestFinished(RequestID, Result);
}

void UGridMoveToCellTask::ResetObservers()
{
	EndGoalAvailabilityWait();
	UnbindTrackedGoal();
	Super::ResetObservers();
}

void UGridMoveToCellTask::ResetTimers()
{
	EndGoalAvailabilityWait();
	bGoalUpdateScheduled = false;
	bGoalAvailabilityRetryScheduled = false;
	Super::ResetTimers();
}

void UGridMoveToCellTask::OnDestroy(bool bInOwnerFinished)
{
	EndGoalAvailabilityWait();
	ReleaseGoalClaim();
	Super::OnDestroy(bInOwnerFinished);
}

void UGridMoveToCellTask::BindTrackedGoal()
{
	AActor* GoalActor = SourceGoalActor.Get();
	if (!IsValid(GoalActor))
	{
		return;
	}
	GoalActor->OnDestroyed.AddUniqueDynamic(this, &UGridMoveToCellTask::HandleGoalActorDestroyed);
	if (!bTrackSourceGoalActor)
	{
		return;
	}
	USceneComponent* RootComponent = IsValid(GoalActor) ? GoalActor->GetRootComponent() : nullptr;
	if (RootComponent == nullptr)
	{
		return;
	}
	ObservedGoalRootComponent = RootComponent;
	GoalTransformUpdatedHandle = RootComponent->TransformUpdated.AddUObject(this, &UGridMoveToCellTask::HandleGoalTransformUpdated);
}

void UGridMoveToCellTask::UnbindTrackedGoal()
{
	if (USceneComponent* RootComponent = ObservedGoalRootComponent.Get(); RootComponent != nullptr && GoalTransformUpdatedHandle.IsValid())
	{
		RootComponent->TransformUpdated.Remove(GoalTransformUpdatedHandle);
	}
	ObservedGoalRootComponent.Reset();
	GoalTransformUpdatedHandle.Reset();
	if (AActor* GoalActor = SourceGoalActor.Get())
	{
		GoalActor->OnDestroyed.RemoveDynamic(this, &UGridMoveToCellTask::HandleGoalActorDestroyed);
	}
}

void UGridMoveToCellTask::HandleGoalTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	if (!IsActive() || bGoalUpdateScheduled)
	{
		return;
	}
	FGridCellId CurrentGoalCell;
	FVector CurrentGoalCenter = FVector::ZeroVector;
	FString Error;
	if (!ResolveGridGoal(CurrentGoalCell, CurrentGoalCenter, &Error))
	{
		GRIDWORLD_LOG_WARNING(
			"Tracked Move To Grid Cell failed for controller '%s': %s",
			*GetNameSafe(OwnerController),
			*Error);
		FinishMoveTask(EPathFollowingResult::Invalid);
		return;
	}
	const FGridCellId& ComparisonGoalCell = RequestedGoalCell.IsValid() ? RequestedGoalCell : ProjectedGoalCell;
	if (CurrentGoalCell == ComparisonGoalCell)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		bGoalUpdateScheduled = true;
		World->GetTimerManager().SetTimerForNextTick(this, &UGridMoveToCellTask::HandleTrackedGoalCellChange);
	}
}

void UGridMoveToCellTask::HandleTrackedGoalCellChange()
{
	bGoalUpdateScheduled = false;
	if (!IsActive())
	{
		return;
	}
	FGridCellId CurrentGoalCell;
	FVector CurrentGoalCenter = FVector::ZeroVector;
	const FGridCellId& ComparisonGoalCell = RequestedGoalCell.IsValid() ? RequestedGoalCell : ProjectedGoalCell;
	if (!ResolveGridGoal(CurrentGoalCell, CurrentGoalCenter) || CurrentGoalCell != ComparisonGoalCell)
	{
		ConditionalPerformMove();
	}
}

void UGridMoveToCellTask::HandleGoalActorDestroyed(AActor* DestroyedActor)
{
	if (IsActive())
	{
		GRIDWORLD_LOG_WARNING(
			"Move To Grid Cell failed because goal Actor '%s' was destroyed.",
			*GetNameSafe(DestroyedActor));
		FinishMoveTask(EPathFollowingResult::Invalid);
	}
}
