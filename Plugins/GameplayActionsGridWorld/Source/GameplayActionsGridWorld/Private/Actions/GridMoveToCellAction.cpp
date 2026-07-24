#include "Actions/GridMoveToCellAction.h"

#include "AI/GridMoveToCellTask.h"
#include "AIController.h"
#include "AISystem.h"
#include "Actions/GridMoveToCellActionDefinition.h"
#include "Blueprint/GridWorldBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayActionTags.h"
#include "GameplayActionsGridWorldTags.h"
#include "GameplayActionsGridWorldModule.h"
#include "GameplayTask.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"

namespace
{
	template <typename T>
	bool ReadRequired(const TValueOrError<T, EPropertyBagResult>& Result, T& OutValue)
	{
		if (!Result.HasValue())
		{
			return false;
		}
		OutValue = Result.GetValue();
		return true;
	}
}

void UGridMoveToCellAction::BeginDestroy()
{
	ReleaseMoveTask(true);
	ReleaseControllerMove(true);
	ReleaseControllerGoalClaim(false);
	Super::BeginDestroy();
}

bool UGridMoveToCellAction::CanStartAction_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	FMoveSettings Settings;
	if (!ReadSettings(Settings, OutDiagnostic))
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		return false;
	}
	AController* Controller = ResolveController();
	if (!Controller)
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		OutDiagnostic =
			TEXT("Grid Move To Cell requires a Controller on the component owner, its Pawn, or the request context.");
		return false;
	}
	if (!Cast<AAIController>(Controller)
		&& (!Controller->GetPawn()
			|| !Controller->FindComponentByClass<UPathFollowingComponent>()))
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		OutDiagnostic =
			TEXT("A non-AI Controller requires a possessed Pawn and a Path Following Component.");
		return false;
	}
	return true;
}

void UGridMoveToCellAction::OnActionInit_Implementation()
{
	FString Diagnostic;
	if (!ReadSettings(CachedSettings, Diagnostic))
	{
		// CanStartAction already rejects malformed snapshots. Keeping an explicit reset here prevents
		// partially cached values if a future subclass bypasses the native validation implementation.
		CachedSettings = FMoveSettings();
	}
}

void UGridMoveToCellAction::OnActionStarted_Implementation()
{
	AController* Controller = ResolveController();
	if (!Controller)
	{
		FailAction(
			GameplayActionTags::Result_Failure_CannotStart,
			TEXT("Controller became invalid before Grid Move To Cell started."));
		return;
	}

	AAIController* AIController = Cast<AAIController>(Controller);
	if (!AIController)
	{
		FString Diagnostic;
		if (!StartControllerMove(*Controller, Diagnostic))
		{
			FailAction(
				GameplayActionsGridWorldTags::Result_Failure_Invalid,
				Diagnostic);
		}
		return;
	}

	if (CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath)
	{
		MoveTask = UGridMoveToCellTask::MoveToGridExactPath(
			AIController,
			CachedSettings.InjectedPath,
			CachedSettings.AcceptanceRadius,
			CachedSettings.StopOnOverlap,
			CachedSettings.bLockAILogic,
			CachedSettings.RequireNavigableEndLocation,
			CachedSettings.bAllowStrafe,
			CachedSettings.GoalContentionPolicy,
			CachedSettings.AdditionalGoalSeparation,
			CachedSettings.bAutoRegisterPawnOccupancy);
	}
	else
	{
		MoveTask = UGridMoveToCellTask::MoveToGridCell(
			AIController,
			CachedSettings.GoalLocation,
			CachedSettings.GoalActor.Get(),
			CachedSettings.AcceptanceRadius,
			CachedSettings.StopOnOverlap,
			CachedSettings.AcceptPartialPath,
			CachedSettings.bUsePathfinding,
			CachedSettings.bLockAILogic,
			CachedSettings.bTrackMovingGoal,
			CachedSettings.RequireNavigableEndLocation,
			CachedSettings.FilterClass,
			CachedSettings.bAllowStrafe,
			CachedSettings.GoalContentionPolicy,
			CachedSettings.MaxAlternativeSearchRadius,
			CachedSettings.AdditionalGoalSeparation,
			CachedSettings.bAutoRegisterPawnOccupancy,
			CachedSettings.GoalAvailabilityTimeout,
			CachedSettings.GoalWaitWarningInterval);
	}
	if (!MoveTask)
	{
		FailAction(
			GameplayActionsGridWorldTags::Result_Failure_Invalid,
			TEXT("UGridMoveToCellTask::MoveToGridCell returned null."));
		return;
	}

	bAcceptMoveCallbacks = true;
	MoveFinishedHandle = MoveTask->OnMoveTaskFinished.AddUObject(
		this,
		&ThisClass::HandleMoveFinished);
	MoveTask->ReadyForActivation();
}

void UGridMoveToCellAction::OnActionPaused_Implementation()
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

void UGridMoveToCellAction::OnActionResumed_Implementation()
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

void UGridMoveToCellAction::OnActionCleanup_Implementation()
{
	ReleaseMoveTask(true);
	ReleaseControllerMove(true);
	ReleaseControllerGoalClaim(false);
	CachedSettings = FMoveSettings();
}

bool UGridMoveToCellAction::ReadSettings(
	FMoveSettings& OutSettings,
	FString& OutDiagnostic) const
{
	const FInstancedPropertyBag& SnapshotParameters = GetParameters();
	const TValueOrError<FStructView, EPropertyBagResult> InjectedPath =
		SnapshotParameters.GetValueStruct(
			GridMoveToCellActionParameters::InjectedPath,
			FGridInjectedPath::StaticStruct());
	const TValueOrError<FStructView, EPropertyBagResult> GoalLocation =
		SnapshotParameters.GetValueStruct(
			GridMoveToCellActionParameters::GoalLocation,
			TBaseStructure<FVector>::Get());
	const TValueOrError<UObject*, EPropertyBagResult> GoalActor =
		SnapshotParameters.GetValueObject(
			GridMoveToCellActionParameters::GoalActor,
			AActor::StaticClass());
	const TValueOrError<UClass*, EPropertyBagResult> FilterClass =
		SnapshotParameters.GetValueClass(GridMoveToCellActionParameters::FilterClass);

	if (!InjectedPath.HasValue()
		|| !GoalLocation.HasValue()
		|| !GoalActor.HasValue()
		|| !FilterClass.HasValue()
		|| !ReadRequired(
			SnapshotParameters.GetValueEnum<EGridMovePathSource>(
				GridMoveToCellActionParameters::PathSource),
			OutSettings.PathSource)
		|| !ReadRequired(
			SnapshotParameters.GetValueFloat(GridMoveToCellActionParameters::AcceptanceRadius),
			OutSettings.AcceptanceRadius)
		|| !ReadRequired(
			SnapshotParameters.GetValueEnum<EAIOptionFlag::Type>(
				GridMoveToCellActionParameters::StopOnOverlap),
			OutSettings.StopOnOverlap)
		|| !ReadRequired(
			SnapshotParameters.GetValueEnum<EAIOptionFlag::Type>(
				GridMoveToCellActionParameters::AcceptPartialPath),
			OutSettings.AcceptPartialPath)
		|| !ReadRequired(
			SnapshotParameters.GetValueBool(GridMoveToCellActionParameters::UsePathfinding),
			OutSettings.bUsePathfinding)
		|| !ReadRequired(
			SnapshotParameters.GetValueBool(GridMoveToCellActionParameters::LockAILogic),
			OutSettings.bLockAILogic)
		|| !ReadRequired(
			SnapshotParameters.GetValueBool(GridMoveToCellActionParameters::TrackMovingGoal),
			OutSettings.bTrackMovingGoal)
		|| !ReadRequired(
			SnapshotParameters.GetValueEnum<EAIOptionFlag::Type>(
				GridMoveToCellActionParameters::RequireNavigableEndLocation),
			OutSettings.RequireNavigableEndLocation)
		|| !ReadRequired(
			SnapshotParameters.GetValueBool(GridMoveToCellActionParameters::AllowStrafe),
			OutSettings.bAllowStrafe)
		|| !ReadRequired(
			SnapshotParameters.GetValueEnum<EGridGoalContentionPolicy>(
				GridMoveToCellActionParameters::GoalContentionPolicy),
			OutSettings.GoalContentionPolicy)
		|| !ReadRequired(
			SnapshotParameters.GetValueInt32(
				GridMoveToCellActionParameters::MaxAlternativeSearchRadius),
			OutSettings.MaxAlternativeSearchRadius)
		|| !ReadRequired(
			SnapshotParameters.GetValueFloat(
				GridMoveToCellActionParameters::AdditionalGoalSeparation),
			OutSettings.AdditionalGoalSeparation)
		|| !ReadRequired(
			SnapshotParameters.GetValueBool(
				GridMoveToCellActionParameters::AutoRegisterPawnOccupancy),
			OutSettings.bAutoRegisterPawnOccupancy)
		|| !ReadRequired(
			SnapshotParameters.GetValueFloat(
				GridMoveToCellActionParameters::GoalAvailabilityTimeout),
			OutSettings.GoalAvailabilityTimeout)
		|| !ReadRequired(
			SnapshotParameters.GetValueFloat(
				GridMoveToCellActionParameters::GoalWaitWarningInterval),
			OutSettings.GoalWaitWarningInterval))
	{
		OutDiagnostic =
			TEXT("Grid Move To Cell parameter schema is missing a required field or contains an incompatible type.");
		return false;
	}

	OutSettings.InjectedPath = *InjectedPath.GetValue().GetPtr<FGridInjectedPath>();
	OutSettings.GoalLocation = *GoalLocation.GetValue().GetPtr<FVector>();
	OutSettings.GoalActor = Cast<AActor>(GoalActor.GetValue());
	OutSettings.FilterClass = FilterClass.GetValue();

	if ((OutSettings.PathSource == EGridMovePathSource::ExactInjectedPath && !OutSettings.InjectedPath.IsSet())
		|| (OutSettings.PathSource == EGridMovePathSource::Destination
			&& !OutSettings.GoalActor.IsValid()
			&& OutSettings.GoalLocation.ContainsNaN())
		|| !FMath::IsFinite(OutSettings.AcceptanceRadius)
		|| OutSettings.MaxAlternativeSearchRadius < 1
		|| !FMath::IsFinite(OutSettings.AdditionalGoalSeparation)
		|| OutSettings.AdditionalGoalSeparation < 0.0f
		|| !FMath::IsFinite(OutSettings.GoalAvailabilityTimeout)
		|| OutSettings.GoalAvailabilityTimeout <= 0.0f
		|| !FMath::IsFinite(OutSettings.GoalWaitWarningInterval)
		|| OutSettings.GoalWaitWarningInterval <= 0.0f)
	{
		OutDiagnostic =
			TEXT("Grid Move To Cell contains an invalid goal, radius, separation, or timeout value.");
		return false;
	}
	return true;
}

AController* UGridMoveToCellAction::ResolveController() const
{
	const UGameplayActionComponent* Component = GetOwningComponent();
	AActor* Owner = Component ? Component->GetOwner() : nullptr;
	if (AController* Controller = Cast<AController>(Owner))
	{
		return Controller;
	}
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (AController* Controller = Pawn->GetController())
		{
			return Controller;
		}
	}
	if (AController* Controller = Cast<AController>(GetRequester()))
	{
		return Controller;
	}
	if (const APawn* Pawn = Cast<APawn>(GetRequester()))
	{
		return Pawn->GetController();
	}
	return nullptr;
}

bool UGridMoveToCellAction::StartControllerMove(
	AController& Controller,
	FString& OutDiagnostic)
{
	UPathFollowingComponent* PathFollowing =
		Controller.FindComponentByClass<UPathFollowingComponent>();
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller.GetWorld());
	if (!PathFollowing || !PathFollowing->IsPathFollowingAllowed())
	{
		OutDiagnostic =
			TEXT("Grid Move To Cell requires an available Path Following Component on a non-AI Controller.");
		return false;
	}
	if (!NavigationSystem || !Controller.GetPawn())
	{
		OutDiagnostic =
			TEXT("Grid Move To Cell requires a Navigation System and a possessed Pawn.");
		return false;
	}

	const FVector StartLocation = Controller.GetNavAgentLocation();
	AGridNavigationData* GridNavigationData = Cast<AGridNavigationData>(
		NavigationSystem->GetNavDataForProps(
			Controller.GetNavAgentPropertiesRef(),
			StartLocation));
	if (GridNavigationData == nullptr)
	{
		OutDiagnostic = TEXT("Grid Move To Cell could not resolve AGridNavigationData for the controller.");
		return false;
	}

	FVector GoalLocation = FVector::ZeroVector;
	FGridCellId GoalCellId;
	if (CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath)
	{
		const FGridWorldSnapshotPtr Snapshot = GridNavigationData->GetSnapshot();
		const FGridCellData* GoalCell = Snapshot.IsValid()
			? Snapshot->FindCell(CachedSettings.InjectedPath.OriginalGoalCell)
			: nullptr;
		if (GoalCell == nullptr || !GoalCell->bWalkable)
		{
			OutDiagnostic = TEXT("The exact GridWorld path's original goal cell no longer exists or is blocked.");
			return false;
		}
		GoalCellId = GoalCell->Id;
		GoalLocation = GoalCell->WorldCenter;
	}
	else
	{
		const FVector SourceGoal = CachedSettings.GoalActor.IsValid()
			? CachedSettings.GoalActor->GetActorLocation()
			: CachedSettings.GoalLocation;
		const FGridCellQueryResult ProjectedGoal = UGridWorldBlueprintLibrary::ProjectPointToGrid(
			&Controller,
			SourceGoal);
		if (ProjectedGoal.Status != EGridQueryStatus::Success || !ProjectedGoal.bWalkable)
		{
			OutDiagnostic = TEXT("Grid Move To Cell could not project the requested destination to a walkable GridWorld cell.");
			return false;
		}
		GoalCellId = ProjectedGoal.CellId;
		GoalLocation = ProjectedGoal.WorldCenter;
	}

	const TSubclassOf<UNavigationQueryFilter> EffectiveFilter =
		CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath
			? CachedSettings.InjectedPath.FilterClass
			: CachedSettings.FilterClass;
	FAIMoveRequest MoveRequest(GoalLocation);
	const bool bStopOnOverlap = FAISystem::PickAIOption(
		CachedSettings.StopOnOverlap,
		MoveRequest.IsReachTestIncludingAgentRadius());
	MoveRequest.SetAcceptanceRadius(CachedSettings.AcceptanceRadius);
	MoveRequest.SetReachTestIncludesAgentRadius(bStopOnOverlap);
	MoveRequest.SetReachTestIncludesGoalRadius(bStopOnOverlap);
	MoveRequest.SetAllowPartialPath(
		CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath
			? CachedSettings.InjectedPath.bAllowPartialPath
			: FAISystem::PickAIOption(CachedSettings.AcceptPartialPath, MoveRequest.IsUsingPartialPaths()));
	MoveRequest.SetUsePathfinding(
		CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath || CachedSettings.bUsePathfinding);
	MoveRequest.SetRequireNavigableEndLocation(FAISystem::PickAIOption(
		CachedSettings.RequireNavigableEndLocation,
		MoveRequest.IsNavigableEndLocationRequired()));
	MoveRequest.SetProjectGoalLocation(false);
	MoveRequest.SetNavigationFilter(EffectiveFilter);
	MoveRequest.SetCanStrafe(CachedSettings.bAllowStrafe);

	if (PathFollowing->HasReached(MoveRequest))
	{
		if (TryClaimControllerGoal(
			Controller,
			*GridNavigationData,
			GoalCellId,
			GoalLocation,
			OutDiagnostic))
		{
			ReleaseControllerGoalClaim(true);
			CompleteMove(EPathFollowingResult::Success, &Controller);
			return true;
		}
		if (CachedSettings.GoalContentionPolicy != EGridGoalContentionPolicy::StopBeforeOccupied)
		{
			return false;
		}
	}

	const ANavigationData* NavigationData = MoveRequest.IsUsingPathfinding()
		? GridNavigationData
		: NavigationSystem->GetAbstractNavData();
	if (!NavigationData)
	{
		OutDiagnostic =
			TEXT("Grid Move To Cell could not resolve Navigation Data for the player controller.");
		return false;
	}

	const FSharedConstNavQueryFilter QueryFilter =
		UNavigationQueryFilter::GetQueryFilter(
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
	if (CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath)
	{
		const FGridInjectedPathValidationResult Validation = GridNavigationData->ValidateInjectedPath(
			CachedSettings.InjectedPath,
			&Controller,
			Controller.GetNavAgentPropertiesRef(),
			StartLocation);
		if (Validation.bIsValid)
		{
			PathResult = GridNavigationData->MaterializeInjectedPath(
				CachedSettings.InjectedPath,
				&Controller,
				Controller.GetNavAgentPropertiesRef(),
				StartLocation);
		}
		else if (CachedSettings.InjectedPath.InvalidationPolicy
			== EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal)
		{
			bRecalculatedInjectedPath = true;
			PathResult = NavigationSystem->FindPathSync(Controller.GetNavAgentPropertiesRef(), Query);
		}
		else
		{
			OutDiagnostic = FString::Printf(
				TEXT("The exact GridWorld path is stale and strict invalidation rejected it: %s"),
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
		OutDiagnostic =
			TEXT("Grid Move To Cell could not build a path to the projected GridWorld cell.");
		return false;
	}
	if (!TryClaimControllerGoal(
		Controller,
		*GridNavigationData,
		GoalCellId,
		GoalLocation,
		OutDiagnostic))
	{
		const FGridCellId RequestedGoalCell =
			CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath
			&& CachedSettings.InjectedPath.RequestedGoalCell.IsValid()
				? CachedSettings.InjectedPath.RequestedGoalCell
				: GoalCellId;
		const bool bPathWasAlreadyAdjusted =
			CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath
			&& RequestedGoalCell != CachedSettings.InjectedPath.OriginalGoalCell;
		if (CachedSettings.GoalContentionPolicy != EGridGoalContentionPolicy::StopBeforeOccupied
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
		if (!TryClaimControllerGoal(
			Controller,
			*GridNavigationData,
			GoalCellId,
			GoalLocation,
			OutDiagnostic))
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
			GridPath->ParentPathInstanceId = CachedSettings.InjectedPath.PathInstanceId;
			GridPath->SourcePreviewId = CachedSettings.InjectedPath.SourcePreviewId;
			GridPath->InjectedInvalidationPolicy = CachedSettings.InjectedPath.InvalidationPolicy;
		}
	}
	PathResult.Path->EnableRecalculationOnInvalidation(
		CachedSettings.PathSource != EGridMovePathSource::ExactInjectedPath
		|| CachedSettings.InjectedPath.InvalidationPolicy
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
	ControllerMoveFinishedHandle =
		PathFollowing->OnRequestFinished.AddUObject(
			this,
			&ThisClass::HandleControllerMoveFinished);
	bStartingControllerMove = true;
	ControllerMoveRequestId =
		PathFollowing->RequestMove(MoveRequest, PathResult.Path);
	bStartingControllerMove = false;

	if (!ControllerMoveRequestId.IsValid())
	{
		ReleaseControllerMove(false);
		ReleaseControllerGoalClaim(false);
		OutDiagnostic =
			TEXT("The Path Following Component rejected the GridWorld movement request.");
		return false;
	}

	if (bHasDeferredControllerMoveResult)
	{
		const FAIRequestID DeferredRequestId =
			DeferredControllerMoveRequestId;
		const FPathFollowingResult DeferredResult =
			DeferredControllerMoveResult;
		bHasDeferredControllerMoveResult = false;
		HandleControllerMoveFinished(
			DeferredRequestId,
			DeferredResult);
	}
	return true;
}

void UGridMoveToCellAction::HandleMoveFinished(
	const TEnumAsByte<EPathFollowingResult::Type> Result,
	AAIController* Controller)
{
	if (!bAcceptMoveCallbacks || !MoveTask)
	{
		return;
	}

	ReleaseMoveTask(false);
	CompleteMove(Result.GetValue(), Controller);
}

void UGridMoveToCellAction::HandleControllerMoveFinished(
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

	AController* Controller = ResolveController();
	ReleaseControllerGoalClaim(Result.IsSuccess() && !bControllerMovePathIsPartial);
	ReleaseControllerMove(false);
	CompleteMove(Result.Code.GetValue(), Controller);
}

bool UGridMoveToCellAction::TryClaimControllerGoal(
	AController& Controller,
	AGridNavigationData& NavigationData,
	const FGridCellId& GoalCell,
	const FVector& GoalLocation,
	FString& OutDiagnostic)
{
	if (CachedSettings.GoalContentionPolicy != EGridGoalContentionPolicy::RejectOccupied
		&& CachedSettings.GoalContentionPolicy != EGridGoalContentionPolicy::StopBeforeOccupied)
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
			CachedSettings.bAutoRegisterPawnOccupancy)
		: nullptr;
	if (Occupancy == nullptr || !Occupancy->OccupantId.IsValid())
	{
		OutDiagnostic =
			TEXT("The selected occupied-goal policy requires a valid Pawn occupancy identity for the controller.");
		return false;
	}

	FGridTrafficGoalClaimRequest Request;
	Request.OwnerId = Occupancy->OccupantId;
	Request.Claimant = this;
	Request.Pawn = Pawn;
	Request.GoalCell = {GoalCell, GoalLocation};
	Request.AgentRadius = AgentProperties.AgentRadius > 0.0f ? AgentProperties.AgentRadius : 42.0f;
	Request.AgentHeight = AgentProperties.AgentHeight > 0.0f ? AgentProperties.AgentHeight : 192.0f;
	Request.AdditionalSeparation = FMath::Max(0.0f, CachedSettings.AdditionalGoalSeparation);
	if (!NavigationData.TryClaimTrafficGoal(Request))
	{
		OutDiagnostic = FString::Printf(
			TEXT("Grid Move To Cell could not claim destination (%d,%d,%d) because it is occupied."),
			GoalCell.Coord.X,
			GoalCell.Coord.Y,
			GoalCell.Coord.Layer);
		return false;
	}

	ControllerGoalClaim = Request;
	ControllerClaimNavigationData = &NavigationData;
	bHasControllerGoalClaim = true;
	return true;
}

bool UGridMoveToCellAction::BuildStopBeforeOccupiedPath(
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
	if (GridPath == nullptr
		|| GridPath->IsPartial()
		|| GridPath->CellPath.Num() < 2
		|| GridPath->CellPath.Last() != RequestedGoalCell)
	{
		OutDiagnostic =
			TEXT("Stop Before Occupied requires a complete GridWorld path ending at the requested occupied cell.");
		return false;
	}

	TArray<FGridCellId> AdjustedCells = GridPath->CellPath;
	AdjustedCells.Pop(EAllowShrinking::No);
	OutEffectiveGoalCell = AdjustedCells.Last();
	const FGridWorldSnapshotPtr Snapshot = NavigationData.GetSnapshot();
	const FGridCellData* EffectiveGoal = Snapshot.IsValid()
		? Snapshot->FindCell(OutEffectiveGoalCell)
		: nullptr;
	if (EffectiveGoal == nullptr || !EffectiveGoal->bWalkable)
	{
		OutDiagnostic =
			TEXT("The cell immediately before the occupied destination no longer exists or is blocked.");
		return false;
	}
	OutEffectiveGoalLocation = EffectiveGoal->WorldCenter;

	const EGridInjectedPathInvalidationPolicy InvalidationPolicy =
		CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath
			? CachedSettings.InjectedPath.InvalidationPolicy
			: EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;
	const FGuid SourcePreviewId =
		CachedSettings.PathSource == EGridMovePathSource::ExactInjectedPath
			? CachedSettings.InjectedPath.SourcePreviewId
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
		OutDiagnostic = FString::Printf(
			TEXT("GridWorld could not validate the path prefix before the occupied destination: %s"),
			*Creation.DiagnosticMessage);
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
		OutDiagnostic =
			TEXT("GridWorld could not materialize the path prefix before the occupied destination.");
		return false;
	}
	return true;
}

void UGridMoveToCellAction::ReleaseControllerGoalClaim(const bool bCommitParking)
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

void UGridMoveToCellAction::CompleteMove(
	const EPathFollowingResult::Type Result,
	const AController* Controller)
{
	const FString Diagnostic = FString::Printf(
		TEXT("Grid Move To Cell completed for controller '%s' with result %s."),
		*GetNameSafe(Controller),
		*UEnum::GetValueAsString(Result));

	switch (Result)
	{
	case EPathFollowingResult::Success:
		SucceedAction(GameplayActionTags::Result_Success, Diagnostic);
		break;
	case EPathFollowingResult::Blocked:
		FailAction(GameplayActionsGridWorldTags::Result_Failure_Blocked, Diagnostic);
		break;
	case EPathFollowingResult::OffPath:
		FailAction(GameplayActionsGridWorldTags::Result_Failure_OffPath, Diagnostic);
		break;
	case EPathFollowingResult::Aborted:
		FailAction(GameplayActionsGridWorldTags::Result_Failure_Aborted, Diagnostic);
		break;
	case EPathFollowingResult::Invalid:
	default:
		FailAction(GameplayActionsGridWorldTags::Result_Failure_Invalid, Diagnostic);
		break;
	}
}

void UGridMoveToCellAction::ReleaseMoveTask(const bool bCancelActiveTask)
{
	bAcceptMoveCallbacks = false;
	UGridMoveToCellTask* Task = MoveTask;
	if (Task && MoveFinishedHandle.IsValid())
	{
		Task->OnMoveTaskFinished.Remove(MoveFinishedHandle);
	}
	MoveFinishedHandle.Reset();
	MoveTask = nullptr;

	if (bCancelActiveTask
		&& Task
		&& Task->GetState() != EGameplayTaskState::Finished)
	{
		Task->ExternalCancel();
	}
}

void UGridMoveToCellAction::ReleaseControllerMove(
	const bool bCancelActiveMove)
{
	bAcceptMoveCallbacks = false;
	bStartingControllerMove = false;
	bHasDeferredControllerMoveResult = false;
	bControllerMovePathIsPartial = false;

	UPathFollowingComponent* PathFollowing =
		ControllerPathFollowingComponent;
	const FAIRequestID RequestId = ControllerMoveRequestId;
	if (PathFollowing && ControllerMoveFinishedHandle.IsValid())
	{
		PathFollowing->OnRequestFinished.Remove(
			ControllerMoveFinishedHandle);
	}
	ControllerMoveFinishedHandle.Reset();
	ControllerPathFollowingComponent = nullptr;
	ControllerMoveRequestId = FAIRequestID::InvalidRequest;
	DeferredControllerMoveRequestId = FAIRequestID::InvalidRequest;
	DeferredControllerMoveResult = FPathFollowingResult();

	if (bCancelActiveMove
		&& PathFollowing
		&& RequestId.IsValid()
		&& RequestId.IsEquivalent(PathFollowing->GetCurrentRequestId())
		&& PathFollowing->GetStatus() != EPathFollowingStatus::Idle)
	{
		PathFollowing->AbortMove(
			*this,
			FPathFollowingResultFlags::OwnerFinished,
			RequestId);
	}
}
