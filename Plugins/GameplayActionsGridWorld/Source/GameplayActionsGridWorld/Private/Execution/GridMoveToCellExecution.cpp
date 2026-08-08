#include "Execution/GridMoveToCellExecution.h"

#include "AI/GridMoveToCellTask.h"
#include "AIController.h"
#include "AISystem.h"
#include "Blueprint/GridWorldBlueprintLibrary.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayActionsGridWorldModule.h"
#include "GameplayTask.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"

UWorld* UGridMoveToCellExecution::GetWorld() const
{
	return ActiveRequest.Controller ? ActiveRequest.Controller->GetWorld() : nullptr;
}

void UGridMoveToCellExecution::BeginDestroy()
{
	Cancel();
	OnFinished.Clear();
	Super::BeginDestroy();
}

FGridMoveToCellEvaluationResult UGridMoveToCellExecution::Evaluate(
	const FGridMoveToCellExecutionRequest& Request)
{
	FGridMoveToCellEvaluationResult Output;
	AController* Controller = Request.Controller;
	if (!IsValid(Controller) || !IsValid(Controller->GetPawn()))
	{
		Output.DiagnosticMessage = TEXT("A valid Controller with a possessed Pawn is required.");
		return Output;
	}

	UPathFollowingComponent* PathFollowing =
		Controller->FindComponentByClass<UPathFollowingComponent>();
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld());
	if (!PathFollowing || !PathFollowing->IsPathFollowingAllowed() || !NavigationSystem)
	{
		Output.DiagnosticMessage =
			TEXT("The Controller requires an available Path Following Component and Navigation System.");
		return Output;
	}

	const FVector StartLocation = Controller->GetNavAgentLocation();
	AGridNavigationData* GridNavigationData = Cast<AGridNavigationData>(
		NavigationSystem->GetNavDataForProps(
			Controller->GetNavAgentPropertiesRef(),
			StartLocation));
	if (!GridNavigationData)
	{
		Output.DiagnosticMessage = TEXT("No GridWorld Navigation Data is available for the Controller.");
		return Output;
	}

	FVector GoalLocation = FVector::ZeroVector;
	FGridCellId GoalCellId;
	if (Request.PathSource == EGridMovePathSource::ExactInjectedPath)
	{
		const FGridWorldSnapshotPtr Snapshot = GridNavigationData->GetSnapshot();
		const FGridCellData* GoalCell = Snapshot.IsValid()
			? Snapshot->FindCell(Request.InjectedPath.OriginalGoalCell)
			: nullptr;
		if (!GoalCell || !GoalCell->bWalkable)
		{
			Output.DiagnosticMessage = TEXT("The exact path goal cell is missing or blocked.");
			return Output;
		}
		GoalCellId = GoalCell->Id;
		GoalLocation = GoalCell->WorldCenter;
	}
	else
	{
		const FVector SourceGoal = Request.GoalActor.IsValid()
			? Request.GoalActor->GetActorLocation()
			: Request.GoalLocation;
		const FGridCellQueryResult ProjectedGoal =
			UGridWorldBlueprintLibrary::ProjectPointToGrid(Controller, SourceGoal);
		if (ProjectedGoal.Status != EGridQueryStatus::Success || !ProjectedGoal.bWalkable)
		{
			Output.DiagnosticMessage = TEXT("The requested destination does not project to a walkable GridWorld cell.");
			return Output;
		}
		GoalCellId = ProjectedGoal.CellId;
		GoalLocation = ProjectedGoal.WorldCenter;
	}

	Output.GoalCell = GoalCellId;
	Output.GoalLocation = GoalLocation;
	const TSubclassOf<UNavigationQueryFilter> EffectiveFilter =
		Request.PathSource == EGridMovePathSource::ExactInjectedPath
			? Request.InjectedPath.FilterClass
			: Request.FilterClass;
	FAIMoveRequest MoveRequest(GoalLocation);
	const bool bStopOnOverlap = FAISystem::PickAIOption(
		Request.StopOnOverlap,
		MoveRequest.IsReachTestIncludingAgentRadius());
	MoveRequest.SetAcceptanceRadius(Request.AcceptanceRadius);
	MoveRequest.SetReachTestIncludesAgentRadius(bStopOnOverlap);
	MoveRequest.SetReachTestIncludesGoalRadius(bStopOnOverlap);
	MoveRequest.SetAllowPartialPath(
		Request.PathSource == EGridMovePathSource::ExactInjectedPath
			? Request.InjectedPath.bAllowPartialPath
			: FAISystem::PickAIOption(Request.AcceptPartialPath, false));
	MoveRequest.SetUsePathfinding(true);
	MoveRequest.SetRequireNavigableEndLocation(true);
	MoveRequest.SetProjectGoalLocation(false);
	MoveRequest.SetNavigationFilter(EffectiveFilter);
	MoveRequest.SetCanStrafe(Request.bAllowStrafe);

	if (PathFollowing->HasReached(MoveRequest))
	{
		Output.bCanExecute = true;
		Output.bAlreadyAtGoal = true;
		Output.DiagnosticMessage = TEXT("The Controller already occupies the requested GridWorld cell.");
		return Output;
	}

	const FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*GridNavigationData,
		Controller,
		EffectiveFilter);
	FPathFindingQuery Query(
		Controller,
		*GridNavigationData,
		StartLocation,
		GoalLocation,
		QueryFilter,
		nullptr,
		TNumericLimits<FVector::FReal>::Max(),
		true);
	Query.SetAllowPartialPaths(MoveRequest.IsUsingPartialPaths());
	Query.SetNavAgentProperties(Controller->GetNavAgentPropertiesRef());
	PathFollowing->OnPathfindingQuery(Query);

	FPathFindingResult PathResult;
	if (Request.PathSource == EGridMovePathSource::ExactInjectedPath)
	{
		const FGridInjectedPathValidationResult Validation = GridNavigationData->ValidateInjectedPath(
			Request.InjectedPath,
			Controller,
			Controller->GetNavAgentPropertiesRef(),
			StartLocation);
		if (Validation.bIsValid)
		{
			PathResult = GridNavigationData->MaterializeInjectedPath(
				Request.InjectedPath,
				Controller,
				Controller->GetNavAgentPropertiesRef(),
				StartLocation);
		}
		else if (Request.InjectedPath.InvalidationPolicy
			== EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal)
		{
			PathResult = NavigationSystem->FindPathSync(
				Controller->GetNavAgentPropertiesRef(),
				Query);
		}
		else
		{
			Output.DiagnosticMessage = Validation.DiagnosticMessage;
			return Output;
		}
	}
	else
	{
		PathResult = NavigationSystem->FindPathSync(
			Controller->GetNavAgentPropertiesRef(),
			Query);
	}

	if (!PathResult.IsSuccessful() || !PathResult.Path.IsValid()
		|| (PathResult.Path->IsPartial() && !MoveRequest.IsUsingPartialPaths()))
	{
		Output.DiagnosticMessage = TEXT("No complete controller-aware GridWorld path reaches the requested cell.");
		return Output;
	}
	Output.bCanExecute = true;
	Output.PathCost = PathResult.Path->GetCost();
	Output.DiagnosticMessage = TEXT("A controller-aware GridWorld path reaches the requested cell.");
	return Output;
}

bool UGridMoveToCellExecution::Start(
	const FGridMoveToCellExecutionRequest& Request,
	FString& OutDiagnostic)
{
	if (bStarted)
	{
		OutDiagnostic = TEXT("A Grid Move execution is one-shot and has already been started.");
		return false;
	}
	if (!IsValid(Request.Controller))
	{
		OutDiagnostic = TEXT("A valid Controller is required to start GridWorld movement.");
		return false;
	}

	bStarted = true;
	ActiveRequest = Request;
	if (AAIController* AIController = Cast<AAIController>(Request.Controller))
	{
		if (ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath)
		{
			MoveTask = UGridMoveToCellTask::MoveToGridExactPath(
				AIController,
				ActiveRequest.InjectedPath,
				ActiveRequest.AcceptanceRadius,
				ActiveRequest.StopOnOverlap,
				ActiveRequest.bLockAILogic,
				ActiveRequest.RequireNavigableEndLocation,
				ActiveRequest.bAllowStrafe,
				ActiveRequest.GoalContentionPolicy,
				ActiveRequest.AdditionalGoalSeparation,
				ActiveRequest.bAutoRegisterPawnOccupancy);
		}
		else
		{
			MoveTask = UGridMoveToCellTask::MoveToGridCell(
				AIController,
				ActiveRequest.GoalLocation,
				ActiveRequest.GoalActor.Get(),
				ActiveRequest.AcceptanceRadius,
				ActiveRequest.StopOnOverlap,
				ActiveRequest.AcceptPartialPath,
				ActiveRequest.bUsePathfinding,
				ActiveRequest.bLockAILogic,
				ActiveRequest.bTrackMovingGoal,
				ActiveRequest.RequireNavigableEndLocation,
				ActiveRequest.FilterClass,
				ActiveRequest.bAllowStrafe,
				ActiveRequest.GoalContentionPolicy,
				ActiveRequest.MaxAlternativeSearchRadius,
				ActiveRequest.AdditionalGoalSeparation,
				ActiveRequest.bAutoRegisterPawnOccupancy,
				ActiveRequest.GoalAvailabilityTimeout,
				ActiveRequest.GoalWaitWarningInterval);
		}
		if (!MoveTask)
		{
			OutDiagnostic = TEXT("UGridMoveToCellTask could not be created.");
			bFinished = true;
			return false;
		}
		bAcceptMoveCallbacks = true;
		MoveFinishedHandle = MoveTask->OnMoveTaskFinished.AddUObject(
			this,
			&ThisClass::HandleMoveFinished);
		MoveTask->ReadyForActivation();
		return true;
	}

	if (!StartControllerMove(*Request.Controller, OutDiagnostic))
	{
		if (!bFinished)
		{
			bFinished = true;
		}
		return false;
	}
	return true;
}

void UGridMoveToCellExecution::Pause()
{
	if (MoveTask && MoveTask->GetState() == EGameplayTaskState::Active)
	{
		MoveTask->PauseGridMove();
	}
	else if (ControllerPathFollowingComponent && ControllerMoveRequestId.IsValid())
	{
		ControllerPathFollowingComponent->PauseMove(ControllerMoveRequestId);
	}
}

void UGridMoveToCellExecution::Resume()
{
	if (MoveTask && MoveTask->GetState() == EGameplayTaskState::Paused)
	{
		MoveTask->ResumeGridMove();
	}
	else if (ControllerPathFollowingComponent && ControllerMoveRequestId.IsValid())
	{
		ControllerPathFollowingComponent->ResumeMove(ControllerMoveRequestId);
	}
}

void UGridMoveToCellExecution::Cancel()
{
	if (bFinished && !MoveTask && !ControllerPathFollowingComponent && !bHasControllerGoalClaim)
	{
		return;
	}
	bFinished = true;
	ReleaseMoveTask(true);
	ReleaseControllerMove(true);
	ReleaseControllerGoalClaim(false);
}

bool UGridMoveToCellExecution::StartControllerMove(
	AController& Controller,
	FString& OutDiagnostic)
{
	UPathFollowingComponent* PathFollowing =
		Controller.FindComponentByClass<UPathFollowingComponent>();
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller.GetWorld());
	if (!PathFollowing || !PathFollowing->IsPathFollowingAllowed())
	{
		OutDiagnostic = TEXT("Grid movement requires an available Path Following Component.");
		return false;
	}
	if (!NavigationSystem || !Controller.GetPawn())
	{
		OutDiagnostic = TEXT("Grid movement requires a Navigation System and a possessed Pawn.");
		return false;
	}

	const FVector StartLocation = Controller.GetNavAgentLocation();
	AGridNavigationData* GridNavigationData = Cast<AGridNavigationData>(
		NavigationSystem->GetNavDataForProps(
			Controller.GetNavAgentPropertiesRef(),
			StartLocation));
	if (!GridNavigationData)
	{
		OutDiagnostic = TEXT("Grid movement could not resolve AGridNavigationData for the Controller.");
		return false;
	}

	FVector GoalLocation = FVector::ZeroVector;
	FGridCellId GoalCellId;
	if (ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath)
	{
		const FGridWorldSnapshotPtr Snapshot = GridNavigationData->GetSnapshot();
		const FGridCellData* GoalCell = Snapshot.IsValid()
			? Snapshot->FindCell(ActiveRequest.InjectedPath.OriginalGoalCell)
			: nullptr;
		if (!GoalCell || !GoalCell->bWalkable)
		{
			OutDiagnostic = TEXT("The exact GridWorld path goal no longer exists or is blocked.");
			return false;
		}
		GoalCellId = GoalCell->Id;
		GoalLocation = GoalCell->WorldCenter;
	}
	else
	{
		const FVector SourceGoal = ActiveRequest.GoalActor.IsValid()
			? ActiveRequest.GoalActor->GetActorLocation()
			: ActiveRequest.GoalLocation;
		const FGridCellQueryResult ProjectedGoal = UGridWorldBlueprintLibrary::ProjectPointToGrid(
			&Controller,
			SourceGoal);
		if (ProjectedGoal.Status != EGridQueryStatus::Success || !ProjectedGoal.bWalkable)
		{
			OutDiagnostic = TEXT("The movement destination does not project to a walkable GridWorld cell.");
			return false;
		}
		GoalCellId = ProjectedGoal.CellId;
		GoalLocation = ProjectedGoal.WorldCenter;
	}

	const TSubclassOf<UNavigationQueryFilter> EffectiveFilter =
		ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath
			? ActiveRequest.InjectedPath.FilterClass
			: ActiveRequest.FilterClass;
	FAIMoveRequest MoveRequest(GoalLocation);
	const bool bStopOnOverlap = FAISystem::PickAIOption(
		ActiveRequest.StopOnOverlap,
		MoveRequest.IsReachTestIncludingAgentRadius());
	MoveRequest.SetAcceptanceRadius(ActiveRequest.AcceptanceRadius);
	MoveRequest.SetReachTestIncludesAgentRadius(bStopOnOverlap);
	MoveRequest.SetReachTestIncludesGoalRadius(bStopOnOverlap);
	MoveRequest.SetAllowPartialPath(
		ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath
			? ActiveRequest.InjectedPath.bAllowPartialPath
			: FAISystem::PickAIOption(ActiveRequest.AcceptPartialPath, MoveRequest.IsUsingPartialPaths()));
	MoveRequest.SetUsePathfinding(
		ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath || ActiveRequest.bUsePathfinding);
	MoveRequest.SetRequireNavigableEndLocation(FAISystem::PickAIOption(
		ActiveRequest.RequireNavigableEndLocation,
		MoveRequest.IsNavigableEndLocationRequired()));
	MoveRequest.SetProjectGoalLocation(false);
	MoveRequest.SetNavigationFilter(EffectiveFilter);
	MoveRequest.SetCanStrafe(ActiveRequest.bAllowStrafe);

	if (PathFollowing->HasReached(MoveRequest))
	{
		if (TryClaimControllerGoal(Controller, *GridNavigationData, GoalCellId, GoalLocation, OutDiagnostic))
		{
			ReleaseControllerGoalClaim(true);
			Finish(EPathFollowingResult::Success, &Controller);
			return true;
		}
		if (ActiveRequest.GoalContentionPolicy != EGridGoalContentionPolicy::StopBeforeOccupied)
		{
			return false;
		}
	}

	const ANavigationData* NavigationData = MoveRequest.IsUsingPathfinding()
		? GridNavigationData
		: NavigationSystem->GetAbstractNavData();
	if (!NavigationData)
	{
		OutDiagnostic = TEXT("Grid movement could not resolve Navigation Data.");
		return false;
	}
	const FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*NavigationData,
		&Controller,
		EffectiveFilter);
	FPathFindingQuery Query(
		&Controller,
		*NavigationData,
		StartLocation,
		GoalLocation,
		QueryFilter,
		nullptr,
		TNumericLimits<FVector::FReal>::Max(),
		MoveRequest.IsNavigableEndLocationRequired());
	Query.SetAllowPartialPaths(MoveRequest.IsUsingPartialPaths());
	Query.SetNavAgentProperties(Controller.GetNavAgentPropertiesRef());
	PathFollowing->OnPathfindingQuery(Query);

	FPathFindingResult PathResult;
	bool bRecalculatedInjectedPath = false;
	if (ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath)
	{
		const FGridInjectedPathValidationResult Validation = GridNavigationData->ValidateInjectedPath(
			ActiveRequest.InjectedPath,
			&Controller,
			Controller.GetNavAgentPropertiesRef(),
			StartLocation);
		if (Validation.bIsValid)
		{
			PathResult = GridNavigationData->MaterializeInjectedPath(
				ActiveRequest.InjectedPath,
				&Controller,
				Controller.GetNavAgentPropertiesRef(),
				StartLocation);
		}
		else if (ActiveRequest.InjectedPath.InvalidationPolicy
			== EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal)
		{
			bRecalculatedInjectedPath = true;
			PathResult = NavigationSystem->FindPathSync(Controller.GetNavAgentPropertiesRef(), Query);
		}
		else
		{
			OutDiagnostic = FString::Printf(
				TEXT("The exact GridWorld path is stale: %s"),
				*Validation.DiagnosticMessage);
			return false;
		}
	}
	else
	{
		PathResult = NavigationSystem->FindPathSync(Controller.GetNavAgentPropertiesRef(), Query);
	}
	if (!PathResult.IsSuccessful() || !PathResult.Path.IsValid())
	{
		OutDiagnostic = TEXT("Grid movement could not build a path to the projected cell.");
		return false;
	}
	if (!TryClaimControllerGoal(Controller, *GridNavigationData, GoalCellId, GoalLocation, OutDiagnostic))
	{
		const FGridCellId RequestedGoalCell =
			ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath
			&& ActiveRequest.InjectedPath.RequestedGoalCell.IsValid()
				? ActiveRequest.InjectedPath.RequestedGoalCell
				: GoalCellId;
		const bool bPathWasAlreadyAdjusted =
			ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath
			&& RequestedGoalCell != ActiveRequest.InjectedPath.OriginalGoalCell;
		if (ActiveRequest.GoalContentionPolicy != EGridGoalContentionPolicy::StopBeforeOccupied
			|| bPathWasAlreadyAdjusted)
		{
			return false;
		}

		FPathFindingResult AdjustedPath;
		if (!BuildStopBeforeOccupiedPath(
			Controller,
			*GridNavigationData,
			EffectiveFilter,
			StartLocation,
			RequestedGoalCell,
			PathResult,
			AdjustedPath,
			GoalCellId,
			GoalLocation,
			OutDiagnostic))
		{
			return false;
		}
		PathResult = MoveTemp(AdjustedPath);
		MoveRequest.UpdateGoalLocation(GoalLocation);
		MoveRequest.SetAllowPartialPath(false);
		if (!TryClaimControllerGoal(Controller, *GridNavigationData, GoalCellId, GoalLocation, OutDiagnostic))
		{
			return false;
		}
	}
	bControllerMovePathIsPartial = PathResult.Path->IsPartial();
	if (bRecalculatedInjectedPath)
	{
		if (FGridNavigationPath* GridPath = PathResult.Path->CastPath<FGridNavigationPath>())
		{
			GridPath->Origin = EGridNavigationPathOrigin::Recalculated;
			GridPath->ParentPathInstanceId = ActiveRequest.InjectedPath.PathInstanceId;
			GridPath->SourcePreviewId = ActiveRequest.InjectedPath.SourcePreviewId;
			GridPath->InjectedInvalidationPolicy = ActiveRequest.InjectedPath.InvalidationPolicy;
		}
	}
	PathResult.Path->EnableRecalculationOnInvalidation(
		ActiveRequest.PathSource != EGridMovePathSource::ExactInjectedPath
		|| ActiveRequest.InjectedPath.InvalidationPolicy
			== EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal);

	if (PathFollowing->GetStatus() != EPathFollowingStatus::Idle)
	{
		PathFollowing->AbortMove(
			*this,
			FPathFollowingResultFlags::NewRequest,
			FAIRequestID::CurrentRequest,
			EPathFollowingVelocityMode::Keep);
	}
	ControllerPathFollowingComponent = PathFollowing;
	bAcceptMoveCallbacks = true;
	ControllerMoveFinishedHandle = PathFollowing->OnRequestFinished.AddUObject(
		this,
		&ThisClass::HandleControllerMoveFinished);
	bStartingControllerMove = true;
	ControllerMoveRequestId = PathFollowing->RequestMove(MoveRequest, PathResult.Path);
	bStartingControllerMove = false;
	if (!ControllerMoveRequestId.IsValid())
	{
		ReleaseControllerMove(false);
		ReleaseControllerGoalClaim(false);
		OutDiagnostic = TEXT("The Path Following Component rejected the GridWorld movement request.");
		return false;
	}
	if (bHasDeferredControllerMoveResult)
	{
		const FAIRequestID DeferredRequestId = DeferredControllerMoveRequestId;
		const FPathFollowingResult DeferredResult = DeferredControllerMoveResult;
		bHasDeferredControllerMoveResult = false;
		HandleControllerMoveFinished(DeferredRequestId, DeferredResult);
	}
	return true;
}

void UGridMoveToCellExecution::HandleMoveFinished(
	const TEnumAsByte<EPathFollowingResult::Type> Result,
	AAIController* Controller)
{
	if (!bAcceptMoveCallbacks || !MoveTask)
	{
		return;
	}
	ReleaseMoveTask(false);
	Finish(Result.GetValue(), Controller);
}

void UGridMoveToCellExecution::HandleControllerMoveFinished(
	const FAIRequestID RequestId,
	const FPathFollowingResult& Result)
{
	if (!bAcceptMoveCallbacks || !ControllerPathFollowingComponent)
	{
		return;
	}
	if (bStartingControllerMove)
	{
		DeferredControllerMoveRequestId = RequestId;
		DeferredControllerMoveResult = Result;
		bHasDeferredControllerMoveResult = true;
		return;
	}
	if (!ControllerMoveRequestId.IsEquivalent(RequestId))
	{
		return;
	}

	AController* Controller = ActiveRequest.Controller;
	ReleaseControllerGoalClaim(Result.IsSuccess() && !bControllerMovePathIsPartial);
	ReleaseControllerMove(false);
	Finish(Result.Code.GetValue(), Controller);
}

bool UGridMoveToCellExecution::TryClaimControllerGoal(
	AController& Controller,
	AGridNavigationData& NavigationData,
	const FGridCellId& GoalCell,
	const FVector& GoalLocation,
	FString& OutDiagnostic)
{
	if (ActiveRequest.GoalContentionPolicy != EGridGoalContentionPolicy::RejectOccupied
		&& ActiveRequest.GoalContentionPolicy != EGridGoalContentionPolicy::StopBeforeOccupied)
	{
		return true;
	}
	APawn* Pawn = Controller.GetPawn();
	const FNavAgentProperties& AgentProperties = Controller.GetNavAgentPropertiesRef();
	UGridNavigationOccupancyComponent* Occupancy = IsValid(Pawn)
		? UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
			*Pawn,
			AgentProperties.AgentRadius,
			AgentProperties.AgentHeight,
			ActiveRequest.bAutoRegisterPawnOccupancy)
		: nullptr;
	if (!Occupancy || !Occupancy->OccupantId.IsValid())
	{
		OutDiagnostic = TEXT("The occupied-goal policy requires a valid Pawn occupancy identity.");
		return false;
	}
	FGridTrafficGoalClaimRequest ClaimRequest;
	ClaimRequest.OwnerId = Occupancy->OccupantId;
	ClaimRequest.Claimant = this;
	ClaimRequest.Pawn = Pawn;
	ClaimRequest.GoalCell = {GoalCell, GoalLocation};
	ClaimRequest.AgentRadius = AgentProperties.AgentRadius > 0.0f ? AgentProperties.AgentRadius : 42.0f;
	ClaimRequest.AgentHeight = AgentProperties.AgentHeight > 0.0f ? AgentProperties.AgentHeight : 192.0f;
	ClaimRequest.AdditionalSeparation = FMath::Max(0.0f, ActiveRequest.AdditionalGoalSeparation);
	if (!NavigationData.TryClaimTrafficGoal(ClaimRequest))
	{
		OutDiagnostic = FString::Printf(
			TEXT("Grid movement could not claim destination (%d,%d,%d) because it is occupied."),
			GoalCell.Coord.X,
			GoalCell.Coord.Y,
			GoalCell.Coord.Layer);
		return false;
	}
	ControllerGoalClaim = ClaimRequest;
	ControllerClaimNavigationData = &NavigationData;
	bHasControllerGoalClaim = true;
	return true;
}

bool UGridMoveToCellExecution::BuildStopBeforeOccupiedPath(
	AController& Controller,
	AGridNavigationData& NavigationData,
	TSubclassOf<UNavigationQueryFilter> FilterClass,
	const FVector& StartLocation,
	const FGridCellId& RequestedGoalCell,
	const FPathFindingResult& FullPath,
	FPathFindingResult& OutAdjustedPath,
	FGridCellId& OutEffectiveGoalCell,
	FVector& OutEffectiveGoalLocation,
	FString& OutDiagnostic) const
{
	OutAdjustedPath = FPathFindingResult(ENavigationQueryResult::Error);
	const FGridNavigationPath* GridPath = FullPath.Path.IsValid()
		? FullPath.Path->CastPath<FGridNavigationPath>()
		: nullptr;
	if (!GridPath || GridPath->IsPartial() || GridPath->CellPath.Num() < 2
		|| GridPath->CellPath.Last() != RequestedGoalCell)
	{
		OutDiagnostic = TEXT("Stop Before Occupied requires a complete path ending at the requested cell.");
		return false;
	}
	TArray<FGridCellId> AdjustedCells = GridPath->CellPath;
	AdjustedCells.Pop(EAllowShrinking::No);
	OutEffectiveGoalCell = AdjustedCells.Last();
	const FGridWorldSnapshotPtr Snapshot = NavigationData.GetSnapshot();
	const FGridCellData* EffectiveGoal = Snapshot.IsValid()
		? Snapshot->FindCell(OutEffectiveGoalCell)
		: nullptr;
	if (!EffectiveGoal || !EffectiveGoal->bWalkable)
	{
		OutDiagnostic = TEXT("The cell before the occupied destination is unavailable.");
		return false;
	}
	OutEffectiveGoalLocation = EffectiveGoal->WorldCenter;
	const EGridInjectedPathInvalidationPolicy InvalidationPolicy =
		ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath
			? ActiveRequest.InjectedPath.InvalidationPolicy
			: EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;
	const FGuid SourcePreviewId =
		ActiveRequest.PathSource == EGridMovePathSource::ExactInjectedPath
			? ActiveRequest.InjectedPath.SourcePreviewId
			: FGuid();
	FGridInjectedPath AdjustedPayload;
	const FGridInjectedPathValidationResult Creation = NavigationData.CreateExactInjectedPath(
		&Controller,
		Controller.GetNavAgentPropertiesRef(),
		FilterClass,
		StartLocation,
		AdjustedCells,
		OutEffectiveGoalCell,
		false,
		false,
		InvalidationPolicy,
		SourcePreviewId,
		AdjustedPayload);
	if (!Creation.bIsValid)
	{
		OutDiagnostic = Creation.DiagnosticMessage;
		return false;
	}
	AdjustedPayload.RequestedGoalCell = RequestedGoalCell;
	OutAdjustedPath = NavigationData.MaterializeInjectedPath(
		AdjustedPayload,
		&Controller,
		Controller.GetNavAgentPropertiesRef(),
		StartLocation);
	if (!OutAdjustedPath.IsSuccessful() || !OutAdjustedPath.Path.IsValid())
	{
		OutDiagnostic = TEXT("GridWorld could not materialize the adjusted path.");
		return false;
	}
	return true;
}

void UGridMoveToCellExecution::ReleaseControllerGoalClaim(const bool bCommitParking)
{
	if (!bHasControllerGoalClaim)
	{
		return;
	}
	if (AGridNavigationData* NavigationData = ControllerClaimNavigationData.Get())
	{
		if (bCommitParking)
		{
			NavigationData->CommitTrafficParking(ControllerGoalClaim);
		}
		NavigationData->ReleaseTrafficGoalClaims(this);
	}
	ControllerGoalClaim = FGridTrafficGoalClaimRequest();
	ControllerClaimNavigationData.Reset();
	bHasControllerGoalClaim = false;
}

void UGridMoveToCellExecution::Finish(
	const EPathFollowingResult::Type Result,
	const AController* Controller,
	FString Diagnostic)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	if (Diagnostic.IsEmpty())
	{
		Diagnostic = FString::Printf(
			TEXT("Grid movement completed for Controller '%s' with result %s."),
			*GetNameSafe(Controller),
			*UEnum::GetValueAsString(Result));
	}
	FGridMoveToCellExecutionResult Output;
	Output.Result = Result;
	Output.Controller = const_cast<AController*>(Controller);
	Output.DiagnosticMessage = MoveTemp(Diagnostic);
	OnFinished.Broadcast(Output);
}

void UGridMoveToCellExecution::ReleaseMoveTask(const bool bCancelActiveTask)
{
	bAcceptMoveCallbacks = false;
	UGridMoveToCellTask* Task = MoveTask;
	if (Task && MoveFinishedHandle.IsValid())
	{
		Task->OnMoveTaskFinished.Remove(MoveFinishedHandle);
	}
	MoveFinishedHandle.Reset();
	MoveTask = nullptr;
	if (bCancelActiveTask && Task && Task->GetState() != EGameplayTaskState::Finished)
	{
		Task->ExternalCancel();
	}
}

void UGridMoveToCellExecution::ReleaseControllerMove(const bool bCancelActiveMove)
{
	bAcceptMoveCallbacks = false;
	bStartingControllerMove = false;
	bHasDeferredControllerMoveResult = false;
	bControllerMovePathIsPartial = false;
	UPathFollowingComponent* PathFollowing = ControllerPathFollowingComponent;
	const FAIRequestID RequestId = ControllerMoveRequestId;
	if (PathFollowing && ControllerMoveFinishedHandle.IsValid())
	{
		PathFollowing->OnRequestFinished.Remove(ControllerMoveFinishedHandle);
	}
	ControllerMoveFinishedHandle.Reset();
	ControllerPathFollowingComponent = nullptr;
	ControllerMoveRequestId = FAIRequestID::InvalidRequest;
	DeferredControllerMoveRequestId = FAIRequestID::InvalidRequest;
	DeferredControllerMoveResult = FPathFollowingResult();
	if (bCancelActiveMove && PathFollowing && RequestId.IsValid()
		&& RequestId.IsEquivalent(PathFollowing->GetCurrentRequestId())
		&& PathFollowing->GetStatus() != EPathFollowingStatus::Idle)
	{
		PathFollowing->AbortMove(
			*this,
			FPathFollowingResultFlags::OwnerFinished,
			RequestId);
	}
}
