#include "Playback/ParadoxCloneReplayExecutionStrategy.h"

#include "Actions/GridMoveToCellActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayActionTags.h"
#include "Inventory/ParadoxDropAction.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Paradox.h"
#include "ParadoxCloneReplayExecutionStrategyPrivate.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "UObject/UnrealType.h"

namespace UE::Paradox::CloneReplay::Private
{
	FGameplayActionSubmissionResult RejectRequest(FString DiagnosticMessage)
	{
		FGameplayActionSubmissionResult Result;
		Result.Status = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
		Result.ReasonTag = GameplayActionTags::Result_Failure_InvalidRequest;
		Result.DiagnosticMessage = MoveTemp(DiagnosticMessage);
		return Result;
	}

	EGameplayActionParameterAccessResult SetInjectedPath(
		FGameplayActionRequest& Request,
		FParadoxCloneReplayGridMoveOverrides& Overrides)
	{
		const FProperty* Property = FindFProperty<FProperty>(
			FParadoxCloneReplayGridMoveOverrides::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(
				FParadoxCloneReplayGridMoveOverrides,
				InjectedPath));
		return UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Request,
			GridMoveToCellActionParameters::InjectedPath,
			Property,
			Property
				? Property->ContainerPtrToValuePtr<void>(&Overrides)
				: nullptr);
	}

	EGameplayActionParameterAccessResult SetGoalContentionPolicy(
		FGameplayActionRequest& Request,
		const UParadoxCloneReplayExecutionStrategy& Strategy)
	{
		const FProperty* Property = FindFProperty<FProperty>(
			UParadoxCloneReplayExecutionStrategy::StaticClass(),
			GET_MEMBER_NAME_CHECKED(
				UParadoxCloneReplayExecutionStrategy,
				GoalContentionPolicyOverride));
		return UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Request,
			GridMoveToCellActionParameters::GoalContentionPolicy,
			Property,
			Property
				? Property->ContainerPtrToValuePtr<void>(&Strategy)
				: nullptr);
	}
}

FGameplayActionSubmissionResult
UParadoxCloneReplayExecutionStrategy::SubmitPreparedRequest(
	UGameplayActionComponent* ActionComponent,
	const FGameplayActionRequest& Request) const
{
	if (!IsValid(ActionComponent)
		|| !Request.IsInitialized()
		|| !IsValid(Request.GetDefinition()))
	{
		return Super::SubmitPreparedRequest(ActionComponent, Request);
	}

	const bool bIsGridMoveDefinition =
		Request.GetDefinition()->IsA<UGridMoveToCellActionDefinition>();
	const bool bIsDropDefinition =
		Request.GetDefinition()->IsA<UParadoxDropActionDefinition>();
	if (!bIsGridMoveDefinition && !bIsDropDefinition)
	{
		return Super::SubmitPreparedRequest(ActionComponent, Request);
	}

	const TValueOrError<EGridMovePathSource, EPropertyBagResult> PathSource =
		Request.GetParameters().GetValueEnum<EGridMovePathSource>(
			GridMoveToCellActionParameters::PathSource);
	if (!PathSource.HasValue())
	{
		return UE::Paradox::CloneReplay::Private::RejectRequest(
			TEXT("Clone replay could not read the Grid Move To Cell PathSource parameter."));
	}
	if (PathSource.GetValue() != EGridMovePathSource::ExactInjectedPath)
	{
		return Super::SubmitPreparedRequest(ActionComponent, Request);
	}

	const TValueOrError<FStructView, EPropertyBagResult> RecordedPathValue =
		Request.GetParameters().GetValueStruct(
			GridMoveToCellActionParameters::InjectedPath,
			FGridInjectedPath::StaticStruct());
	const FGridInjectedPath* RecordedPath = RecordedPathValue.HasValue()
		? RecordedPathValue.GetValue().GetPtr<FGridInjectedPath>()
		: nullptr;
	if (RecordedPath == nullptr || !RecordedPath->IsSet())
	{
		return UE::Paradox::CloneReplay::Private::RejectRequest(
			TEXT("Clone replay received an invalid exact GridWorld path payload."));
	}

	APawn* ClonePawn = Cast<APawn>(ActionComponent->GetOwner());
	AController* CloneController = IsValid(ClonePawn)
		? ClonePawn->GetController()
		: nullptr;
	UWorld* World = IsValid(CloneController)
		? CloneController->GetWorld()
		: nullptr;
	UGridWorldSubsystem* GridWorld = World
		? World->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	if (!IsValid(ClonePawn) || !IsValid(CloneController) || !GridWorld)
	{
		return UE::Paradox::CloneReplay::Private::RejectRequest(
			TEXT("Clone replay cannot re-stamp an exact GridWorld path without a possessed clone and GridWorld subsystem."));
	}

	FParadoxCloneReplayGridMoveOverrides Overrides;
	const bool bPreserveExactPathAcrossDynamicAgents =
		bIsGridMoveDefinition
		&& bOverrideGoalContentionPolicy
		&& GoalContentionPolicyOverride
			== EGridGoalContentionPolicy::RedirectOnCompletion;
	FGridInjectedPathValidationResult RestampResult;
	if (bPreserveExactPathAcrossDynamicAgents)
	{
		AGridNavigationData* NavigationData = GridWorld->GetNavigationData();
		if (NavigationData == nullptr)
		{
			return UE::Paradox::CloneReplay::Private::RejectRequest(
				TEXT("Clone replay cannot re-stamp an exact GridWorld path without active navigation data."));
		}
		RestampResult = NavigationData->CreateExactInjectedPath(
			CloneController,
			CloneController->GetNavAgentPropertiesRef(),
			RecordedPath->FilterClass,
			CloneController->GetNavAgentLocation(),
			RecordedPath->Cells,
			RecordedPath->OriginalGoalCell,
			RecordedPath->bAllowPartialPath,
			RecordedPath->bIsPartial,
			RecordedPath->InvalidationPolicy,
			RecordedPath->SourcePreviewId,
			Overrides.InjectedPath,
			true);
	}
	else
	{
		RestampResult = GridWorld->CreateExactInjectedPath(
			CloneController,
			RecordedPath->Cells,
			RecordedPath->OriginalGoalCell,
			RecordedPath->FilterClass,
			RecordedPath->bAllowPartialPath,
			RecordedPath->bIsPartial,
			RecordedPath->InvalidationPolicy,
			Overrides.InjectedPath);
	}
	if (!RestampResult.bIsValid)
	{
		if (RecordedPath->InvalidationPolicy
				!= EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal)
		{
			return UE::Paradox::CloneReplay::Private::RejectRequest(
				FString::Printf(
					TEXT("Clone replay could not re-stamp the exact GridWorld path for controller '%s': %s"),
					*GetNameSafe(CloneController),
					*RestampResult.DiagnosticMessage));
		}

		AGridNavigationData* NavigationData = GridWorld->GetNavigationData();
		UPathFollowingComponent* PathFollowing =
			CloneController->FindComponentByClass<UPathFollowingComponent>();
		const FGridCellId RecoveryGoal =
			RecordedPath->RequestedGoalCell.IsValid()
				? RecordedPath->RequestedGoalCell
				: RecordedPath->OriginalGoalCell;
		const FGridCellQueryResult CurrentCell =
			GridWorld->ProjectPoint(CloneController->GetNavAgentLocation());
		const FGridCellQueryResult GoalCell =
			GridWorld->GetCell(RecoveryGoal);
		const FSharedConstNavQueryFilter QueryFilter =
			NavigationData
				? UNavigationQueryFilter::GetQueryFilter(
					*NavigationData,
					CloneController,
					RecordedPath->FilterClass)
				: FSharedConstNavQueryFilter();
		if (!NavigationData
			|| CurrentCell.Status != EGridQueryStatus::Success
			|| GoalCell.Status != EGridQueryStatus::Success
			|| !QueryFilter.IsValid())
		{
			return UE::Paradox::CloneReplay::Private::RejectRequest(
				TEXT("Clone replay could not resolve the controller-aware query required to recover an exact path from a different start cell."));
		}

		const FGuid WarningIdentity = Request.GetCorrelation().Id.IsValid()
			? Request.GetCorrelation().Id
			: RecordedPath->PathInstanceId;
		if (!WarnedExactPathRecoveryIntents.Contains(WarningIdentity))
		{
			WarnedExactPathRecoveryIntents.Add(WarningIdentity);
			const FGridCellId RecordedStart = RecordedPath->Cells[0];
			PARADOX_LOG_WARNING(
				TEXT("Clone '%s' is recovering exact-path intent '%s' after %s; "
					"recorded=(%d,%d,%d), current=(%d,%d,%d), goal=(%d,%d,%d). "
					"The recorded runtime path is discarded and a fresh ExactInjectedPath is queried to the same recovery goal."),
				*GetNameSafe(ClonePawn),
				*WarningIdentity.ToString(EGuidFormats::DigitsWithHyphens),
				*StaticEnum<EGridInjectedPathFailureReason>()->GetNameStringByValue(
					static_cast<int64>(RestampResult.FailureReason)),
				RecordedStart.Coord.X,
				RecordedStart.Coord.Y,
				RecordedStart.Coord.Layer,
				CurrentCell.CellId.Coord.X,
				CurrentCell.CellId.Coord.Y,
				CurrentCell.CellId.Coord.Layer,
				RecoveryGoal.Coord.X,
				RecoveryGoal.Coord.Y,
				RecoveryGoal.Coord.Layer);
		}

		FPathFindingQuery RecoveryQuery(
			CloneController,
			*NavigationData,
			CloneController->GetNavAgentLocation(),
			GoalCell.WorldCenter,
			QueryFilter);
		RecoveryQuery.SetAllowPartialPaths(RecordedPath->bAllowPartialPath);
		RecoveryQuery.SetNavAgentProperties(
			CloneController->GetNavAgentPropertiesRef());
		if (PathFollowing)
		{
			PathFollowing->OnPathfindingQuery(RecoveryQuery);
		}
		const FPathFindingResult RecoveryPath =
			AGridNavigationData::FindPath(
				CloneController->GetNavAgentPropertiesRef(),
				RecoveryQuery);
		const FGridNavigationPath* GridRecoveryPath =
			RecoveryPath.Path.IsValid()
				? RecoveryPath.Path->CastPath<FGridNavigationPath>()
				: nullptr;
		if (!RecoveryPath.IsSuccessful() || !GridRecoveryPath
			|| GridRecoveryPath->CellPath.IsEmpty())
		{
			return UE::Paradox::CloneReplay::Private::RejectRequest(
				TEXT("Clone replay could not find a fresh GridWorld path from the clone's current cell to the recorded semantic goal."));
		}

		RestampResult = NavigationData->CreateExactInjectedPath(
			CloneController,
			CloneController->GetNavAgentPropertiesRef(),
			RecordedPath->FilterClass,
			CloneController->GetNavAgentLocation(),
			GridRecoveryPath->CellPath,
			RecoveryGoal,
			RecordedPath->bAllowPartialPath,
			RecoveryPath.Path->IsPartial(),
			RecordedPath->InvalidationPolicy,
			RecordedPath->SourcePreviewId,
			Overrides.InjectedPath,
			RecordedPath->bAllowDynamicAgentConflictsDuringValidation);
		if (!RestampResult.bIsValid)
		{
			return UE::Paradox::CloneReplay::Private::RejectRequest(
				FString::Printf(
					TEXT("Clone replay found a recovery path but could not stamp it as an ExactInjectedPath: %s"),
					*RestampResult.DiagnosticMessage));
		}
	}

	// These fields are semantic recording metadata and do not participate in path validation.
	Overrides.InjectedPath.RequestedGoalCell = RecordedPath->RequestedGoalCell;
	Overrides.InjectedPath.SourcePreviewId = RecordedPath->SourcePreviewId;

	FGameplayActionRequest AdaptedRequest = Request;
	if (UE::Paradox::CloneReplay::Private::SetInjectedPath(
		AdaptedRequest,
		Overrides) != EGameplayActionParameterAccessResult::Success)
	{
		return UE::Paradox::CloneReplay::Private::RejectRequest(
			TEXT("Clone replay could not write the re-stamped GridWorld path into its runtime request copy."));
	}
	if (bIsGridMoveDefinition && bOverrideGoalContentionPolicy
		&& UE::Paradox::CloneReplay::Private::SetGoalContentionPolicy(
			AdaptedRequest,
			*this) != EGameplayActionParameterAccessResult::Success)
	{
		return UE::Paradox::CloneReplay::Private::RejectRequest(
			TEXT("Clone replay could not override the GridWorld goal-contention policy in its runtime request copy."));
	}

	return Super::SubmitPreparedRequest(ActionComponent, AdaptedRequest);
}
