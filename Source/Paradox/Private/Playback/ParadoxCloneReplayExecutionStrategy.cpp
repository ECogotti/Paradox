#include "Playback/ParadoxCloneReplayExecutionStrategy.h"

#include "Actions/GridMoveToCellActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayActionTags.h"
#include "Navigation/GridNavigationData.h"
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

	if (!Request.GetDefinition()->IsA<UGridMoveToCellActionDefinition>())
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
		bOverrideGoalContentionPolicy
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
		return UE::Paradox::CloneReplay::Private::RejectRequest(
			FString::Printf(
				TEXT("Clone replay could not re-stamp the exact GridWorld path for controller '%s': %s"),
				*GetNameSafe(CloneController),
				*RestampResult.DiagnosticMessage));
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
	if (bOverrideGoalContentionPolicy
		&& UE::Paradox::CloneReplay::Private::SetGoalContentionPolicy(
			AdaptedRequest,
			*this) != EGameplayActionParameterAccessResult::Success)
	{
		return UE::Paradox::CloneReplay::Private::RejectRequest(
			TEXT("Clone replay could not override the GridWorld goal-contention policy in its runtime request copy."));
	}

	return Super::SubmitPreparedRequest(ActionComponent, AdaptedRequest);
}
