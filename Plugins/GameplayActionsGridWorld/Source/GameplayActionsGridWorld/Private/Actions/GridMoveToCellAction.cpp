#include "Actions/GridMoveToCellAction.h"

#include "Actions/GridMoveToCellActionDefinition.h"
#include "AIController.h"
#include "Components/GameplayActionComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayActionTags.h"
#include "GameplayActionsGridWorldTags.h"
#include "Navigation/PathFollowingComponent.h"
#include "StructUtils/StructView.h"

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
	ReleaseExecution(true);
	Super::BeginDestroy();
}

bool UGridMoveToCellAction::CanStartAction_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	FGridMoveToCellExecutionRequest Settings;
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
			TEXT("Grid Move To Cell requires a Controller on the component owner, its Pawn, or request context.");
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
		CachedSettings = FGridMoveToCellExecutionRequest();
	}
	Super::OnActionInit_Implementation();
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

	CachedSettings.Controller = Controller;
	MoveExecution = NewObject<UGridMoveToCellExecution>(this);
	if (!MoveExecution)
	{
		FailAction(
			GameplayActionsGridWorldTags::Result_Failure_Invalid,
			TEXT("Failed to allocate the GridWorld movement executor."));
		return;
	}
	ExecutionFinishedHandle = MoveExecution->OnFinishedNative().AddUObject(
		this,
		&ThisClass::HandleExecutionFinished);
	FString Diagnostic;
	if (!MoveExecution->Start(CachedSettings, Diagnostic))
	{
		ReleaseExecution(false);
		FailAction(
			GameplayActionsGridWorldTags::Result_Failure_Invalid,
			Diagnostic.IsEmpty()
				? TEXT("The GridWorld movement executor rejected the request.")
				: Diagnostic);
	}
}

void UGridMoveToCellAction::OnActionPaused_Implementation()
{
	if (MoveExecution)
	{
		MoveExecution->Pause();
	}
}

void UGridMoveToCellAction::OnActionResumed_Implementation()
{
	if (MoveExecution)
	{
		MoveExecution->Resume();
	}
}

void UGridMoveToCellAction::OnActionCleanup_Implementation()
{
	ReleaseExecution(true);
	CachedSettings = FGridMoveToCellExecutionRequest();
	Super::OnActionCleanup_Implementation();
}

bool UGridMoveToCellAction::ReadSettings(
	FGridMoveToCellExecutionRequest& OutSettings,
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
	EAIOptionFlag::Type StopOnOverlap = EAIOptionFlag::Default;
	EAIOptionFlag::Type AcceptPartialPath = EAIOptionFlag::Default;
	EAIOptionFlag::Type RequireNavigableEndLocation = EAIOptionFlag::Default;

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
			StopOnOverlap)
		|| !ReadRequired(
			SnapshotParameters.GetValueEnum<EAIOptionFlag::Type>(
				GridMoveToCellActionParameters::AcceptPartialPath),
			AcceptPartialPath)
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
			RequireNavigableEndLocation)
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

	OutSettings.StopOnOverlap = StopOnOverlap;
	OutSettings.AcceptPartialPath = AcceptPartialPath;
	OutSettings.RequireNavigableEndLocation = RequireNavigableEndLocation;
	OutSettings.InjectedPath = *InjectedPath.GetValue().GetPtr<FGridInjectedPath>();
	OutSettings.GoalLocation = *GoalLocation.GetValue().GetPtr<FVector>();
	OutSettings.GoalActor = Cast<AActor>(GoalActor.GetValue());
	OutSettings.FilterClass = FilterClass.GetValue();

	if ((OutSettings.PathSource == EGridMovePathSource::ExactInjectedPath
			&& !OutSettings.InjectedPath.IsSet())
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

void UGridMoveToCellAction::HandleExecutionFinished(
	const FGridMoveToCellExecutionResult& Result)
{
	ReleaseExecution(false);
	switch (Result.Result.GetValue())
	{
	case EPathFollowingResult::Success:
		SucceedAction(GameplayActionTags::Result_Success, Result.DiagnosticMessage);
		break;
	case EPathFollowingResult::Blocked:
		FailAction(GameplayActionsGridWorldTags::Result_Failure_Blocked, Result.DiagnosticMessage);
		break;
	case EPathFollowingResult::OffPath:
		FailAction(GameplayActionsGridWorldTags::Result_Failure_OffPath, Result.DiagnosticMessage);
		break;
	case EPathFollowingResult::Aborted:
		FailAction(GameplayActionsGridWorldTags::Result_Failure_Aborted, Result.DiagnosticMessage);
		break;
	case EPathFollowingResult::Invalid:
	default:
		FailAction(GameplayActionsGridWorldTags::Result_Failure_Invalid, Result.DiagnosticMessage);
		break;
	}
}

void UGridMoveToCellAction::ReleaseExecution(const bool bCancel)
{
	UGridMoveToCellExecution* Execution = MoveExecution;
	if (Execution && ExecutionFinishedHandle.IsValid())
	{
		Execution->OnFinishedNative().Remove(ExecutionFinishedHandle);
	}
	ExecutionFinishedHandle.Reset();
	MoveExecution = nullptr;
	if (bCancel && Execution)
	{
		Execution->Cancel();
	}
}
