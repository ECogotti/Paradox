#include "Interaction/ParadoxInteractionActionBase.h"

#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "Execution/GridMoveToCellExecution.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayActionTags.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Paradox.h"
#include "SmartObjectSubsystem.h"
#include "StructUtils/StructView.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "UObject/UnrealType.h"

namespace UE::Paradox::InteractionAction::Private
{
	const FName TargetParameterName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionActionParameters, Target);
	const FName InteractionTagParameterName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionActionParameters, InteractionTag);
	const FName NavigationFilterParameterName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionMovementParameters, NavigationFilter);
	const FName AcceptanceRadiusParameterName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionMovementParameters, AcceptanceRadius);
	const FName AllowStrafeParameterName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionMovementParameters, bAllowStrafe);

	const FProperty* FindParameterProperty(const FName PropertyName)
	{
		return FParadoxInteractionActionParameters::StaticStruct()->FindPropertyByName(
			PropertyName);
	}

	FGameplayTag MapRequestStatusToFailureTag(
		const EParadoxInteractionRequestStatus Status)
	{
		switch (Status)
		{
		case EParadoxInteractionRequestStatus::InvalidTarget:
		case EParadoxInteractionRequestStatus::UnrecordableTarget:
			return ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable;
		case EParadoxInteractionRequestStatus::InvalidCurrentPosition:
			return ParadoxGameplayTags::Result_Failure_Interaction_InvalidPosition;
		case EParadoxInteractionRequestStatus::SlotUnavailable:
			return ParadoxGameplayTags::Result_Failure_Interaction_SlotUnavailable;
		default:
			return ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		}
	}
}

UParadoxInteractionActionBase::UParadoxInteractionActionBase()
{
	bActionTickEnabled = false;
}

bool UParadoxInteractionActionBase::CanStartAction_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	FParadoxInteractionActionParameters SemanticValues;
	if (!ReadSemanticParameters(SemanticValues, OutFailureReason, OutDiagnostic))
	{
		return false;
	}

	AActor* RequesterActor = GetOwningComponent() ? GetOwningComponent()->GetOwner() : nullptr;
	AActor* Target = SemanticValues.Target.Get();
	UParadoxInteractionComponent* Component = IsValid(Target)
		? Target->FindComponentByClass<UParadoxInteractionComponent>()
		: nullptr;
	if (!IsValid(Component))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		OutDiagnostic = TEXT("The interaction Target has no Paradox Interaction Component.");
		return false;
	}

	FGameplayTag PreconditionsFailure;
	if (!CanSatisfyInteractionPreconditions(PreconditionsFailure, OutDiagnostic))
	{
		OutFailureReason = PreconditionsFailure.IsValid()
			? PreconditionsFailure
			: ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		return false;
	}

	if (!HasReachableExecutionCandidate(
		RequesterActor,
		Component,
		SemanticValues.InteractionTag,
		OutDiagnostic))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_InvalidPosition;
		return false;
	}
	return true;
}

void UParadoxInteractionActionBase::OnActionInit_Implementation()
{
	bCompletionRequested = false;
	bMovingToInteraction = false;
	FGameplayTag FailureReason;
	FString Diagnostic;
	if (ReadSemanticParameters(SemanticParameters, FailureReason, Diagnostic))
	{
		InteractionRequester = GetOwningComponent()
			? GetOwningComponent()->GetOwner()
			: nullptr;
		InteractionTarget = SemanticParameters.Target.Get();
		InteractionComponent = InteractionTarget.IsValid()
			? InteractionTarget->FindComponentByClass<UParadoxInteractionComponent>()
			: nullptr;
		if (AActor* Target = InteractionTarget.Get())
		{
			Target->OnDestroyed.AddUniqueDynamic(
				this,
				&ThisClass::HandleInteractionTargetDestroyed);
		}
	}
	else
	{
		PARADOX_LOG_ERROR(
			TEXT("Accepted interaction action '%s' could not cache parameters during init: %s"),
			*GetNameSafe(this),
			*Diagnostic);
	}
	Super::OnActionInit_Implementation();
}

void UParadoxInteractionActionBase::OnActionStarted_Implementation()
{
	FString Diagnostic;
	FGameplayTag ParameterFailure;
	if (!ReadSemanticParameters(
		SemanticParameters,
		ParameterFailure,
		Diagnostic))
	{
		FailInteraction(ParameterFailure, Diagnostic);
		return;
	}
	InteractionRequester = GetOwningComponent()
		? GetOwningComponent()->GetOwner()
		: nullptr;
	InteractionTarget = SemanticParameters.Target.Get();
	InteractionComponent = InteractionTarget.IsValid()
		? InteractionTarget->FindComponentByClass<UParadoxInteractionComponent>()
		: nullptr;
	if (!InteractionComponent.IsValid())
	{
		FailInteraction(
			ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable,
			TEXT("The interaction component became unavailable before start."));
		return;
	}

	if (IsInteractionOutcomeSatisfied())
	{
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, TEXT("The requested interaction outcome was already satisfied at start."));
		return;
	}

	FGameplayTag PreconditionsFailure;
	if (!CanSatisfyInteractionPreconditions(PreconditionsFailure, Diagnostic))
	{
		FailInteraction(
			PreconditionsFailure.IsValid() ? PreconditionsFailure : ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest,
			Diagnostic);
		return;
	}
	if (!BuildExecutionCandidates(Diagnostic) || !TryStartNextCandidate(Diagnostic))
	{
		FailInteraction(
			ParadoxGameplayTags::Result_Failure_Interaction_SlotUnavailable,
			Diagnostic.IsEmpty() ? TEXT("No reachable interaction slot could be claimed.") : Diagnostic);
	}
}

void UParadoxInteractionActionBase::OnActionPaused_Implementation()
{
	if (MovementExecution)
	{
		MovementExecution->Pause();
	}
	Super::OnActionPaused_Implementation();
}

void UParadoxInteractionActionBase::OnActionResumed_Implementation()
{
	if (MovementExecution)
	{
		MovementExecution->Resume();
	}
	Super::OnActionResumed_Implementation();
}

void UParadoxInteractionActionBase::OnActionCancelled_Implementation(
	const FGameplayTag ReasonTag)
{
	OnInteractionCancelled(ReasonTag);
	LogDebugState(TEXT("Cancelled"), ReasonTag);
	Super::OnActionCancelled_Implementation(ReasonTag);
}

void UParadoxInteractionActionBase::OnActionInterrupted_Implementation(
	const FGameplayTag ReasonTag)
{
	OnInteractionInterrupted(ReasonTag);
	LogDebugState(TEXT("Interrupted"), ReasonTag);
	Super::OnActionInterrupted_Implementation(ReasonTag);
}

void UParadoxInteractionActionBase::OnActionAborted_Implementation(
	const FGameplayTag ReasonTag)
{
	OnInteractionAborted(ReasonTag);
	LogDebugState(TEXT("Aborted"), ReasonTag);
	Super::OnActionAborted_Implementation(ReasonTag);
}

void UParadoxInteractionActionBase::OnActionCleanup_Implementation()
{
	ReleaseMovement(true);
	if (UParadoxInteractionComponent* Component = InteractionComponent.Get())
	{
		Component->OnInteractionAffordanceChangedNative().Remove(
			InteractionAffordanceChangedHandle);
	}
	InteractionAffordanceChangedHandle.Reset();
	if (AActor* Target = InteractionTarget.Get())
	{
		Target->OnDestroyed.RemoveDynamic(
			this,
			&ThisClass::HandleInteractionTargetDestroyed);
	}
	ReleaseInteractionClaim();
	OnInteractionCleanup();
	ExecutionCandidates.Reset();
	AttemptedMovementCells.Reset();
	NextCandidateIndex = 0;
	bMovingToInteraction = false;
	ResolvedOption = FParadoxInteractionOption();
	SemanticParameters = FParadoxInteractionActionParameters();
	InteractionComponent.Reset();
	InteractionTarget.Reset();
	InteractionRequester.Reset();
	Super::OnActionCleanup_Implementation();
}

bool UParadoxInteractionActionBase::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	(void)OutFailureReason;
	(void)OutDiagnostic;
	return true;
}

bool UParadoxInteractionActionBase::IsInteractionOutcomeSatisfied_Implementation() const
{
	return false;
}

bool UParadoxInteractionActionBase::CanExecuteInteraction_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	(void)OutFailureReason;
	(void)OutDiagnostic;
	return true;
}

void UParadoxInteractionActionBase::ExecuteInteraction_Implementation()
{
	CompleteInteractionFailure(
		ParadoxGameplayTags::Result_Failure_Interaction_NotImplemented,
		TEXT("The interaction action has no concrete ExecuteInteraction implementation."));
}

void UParadoxInteractionActionBase::CompleteInteractionSuccess(
	const FGameplayTag ReasonTag,
	const FString& DiagnosticMessage)
{
	if (bCompletionRequested)
	{
		return;
	}
	bCompletionRequested = true;
	OnInteractionSucceeded();
	LogDebugState(TEXT("Succeeded"), ReasonTag);
	SucceedAction(
		ReasonTag.IsValid() ? ReasonTag : GameplayActionTags::Result_Success,
		DiagnosticMessage);
}

void UParadoxInteractionActionBase::CompleteInteractionFailure(
	const FGameplayTag ReasonTag,
	const FString& DiagnosticMessage)
{
	FailInteraction(
		ReasonTag.IsValid()
			? ReasonTag
			: ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest,
		DiagnosticMessage);
}

bool UParadoxInteractionActionBase::ReadSemanticParameters(
	FParadoxInteractionActionParameters& OutParameters,
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	using namespace UE::Paradox::InteractionAction::Private;

	OutParameters = FParadoxInteractionActionParameters();
	const FProperty* TargetProperty = FindParameterProperty(TargetParameterName);
	const FProperty* TagProperty = FindParameterProperty(InteractionTagParameterName);
	const EGameplayActionParameterAccessResult TargetRead =
		UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			GetParameters(),
			TargetParameterName,
			TargetProperty,
			TargetProperty
				? TargetProperty->ContainerPtrToValuePtr<void>(&OutParameters)
				: nullptr);
	const EGameplayActionParameterAccessResult TagRead =
		UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			GetParameters(),
			InteractionTagParameterName,
			TagProperty,
			TagProperty
				? TagProperty->ContainerPtrToValuePtr<void>(&OutParameters)
				: nullptr);
	if (TargetRead != EGameplayActionParameterAccessResult::Success
		|| TagRead != EGameplayActionParameterAccessResult::Success
		|| !OutParameters.InteractionTag.IsValid())
	{
		OutFailureReason =
			ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		OutDiagnostic = TEXT(
			"Interaction action parameters require a compatible Target and valid InteractionTag.");
		return false;
	}

	AActor* Target = OutParameters.Target.Get();
	if (!IsValid(Target)
		|| !Target->HasAnyFlags(RF_WasLoaded)
		|| Target->GetWorld() != GetWorld())
	{
		OutFailureReason =
			ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable;
		OutDiagnostic = TEXT(
			"The replay-safe interaction Target is unresolved, runtime-created, or belongs to another World.");
		return false;
	}
	return true;
}

bool UParadoxInteractionActionBase::ResolveCurrentContext(
	FParadoxInteractionOption& OutOption,
	EParadoxInteractionRequestStatus& OutStatus,
	EParadoxInteractionQueryStatus& OutQueryStatus,
	FString& OutDiagnostic) const
{
	FParadoxInteractionActionParameters SemanticValues;
	FGameplayTag FailureReason;
	if (!ReadSemanticParameters(SemanticValues, FailureReason, OutDiagnostic))
	{
		OutStatus = FailureReason
			== ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable
			? EParadoxInteractionRequestStatus::InvalidTarget
			: EParadoxInteractionRequestStatus::InvalidDefinition;
		OutQueryStatus = EParadoxInteractionQueryStatus::NoOptions;
		return false;
	}

	AActor* RequesterActor = GetOwningComponent()
		? GetOwningComponent()->GetOwner()
		: nullptr;
	AActor* Target = SemanticValues.Target.Get();
	UParadoxInteractionComponent* Component = IsValid(Target)
		? Target->FindComponentByClass<UParadoxInteractionComponent>()
		: nullptr;
	if (!IsValid(Component))
	{
		OutStatus = EParadoxInteractionRequestStatus::NoMatchingInteraction;
		OutQueryStatus = EParadoxInteractionQueryStatus::NoOptions;
		OutDiagnostic = TEXT("The interaction Target has no Paradox Interaction Component.");
		return false;
	}
	return Component->ResolveCurrentExecutionOption(
		RequesterActor,
		SemanticValues.InteractionTag,
		OutOption,
		OutStatus,
		OutQueryStatus,
		OutDiagnostic);
}

bool UParadoxInteractionActionBase::BuildExecutionCandidates(FString& OutDiagnostic)
{
	ExecutionCandidates.Reset();
	AttemptedMovementCells.Reset();
	NextCandidateIndex = 0;

	UParadoxInteractionComponent* Component = InteractionComponent.Get();
	AActor* RequesterActor = InteractionRequester.Get();
	UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	if (!IsValid(Component) || !IsValid(RequesterActor) || !GridWorld)
	{
		OutDiagnostic = TEXT("Interaction candidates require a valid component, requester and GridWorld subsystem.");
		return false;
	}

	const FParadoxInteractionQueryResult Query = Component->QueryInteractionOptionsByTag(
		RequesterActor,
		SemanticParameters.InteractionTag);
	if (!Query.IsSuccess())
	{
		OutDiagnostic = Query.DiagnosticMessage;
		return false;
	}

	FParadoxInteractionMovementParameters MovementParameters;
	if (!ReadMovementParameters(MovementParameters, OutDiagnostic))
	{
		return false;
	}
	AController* Controller = ResolveMovementController();
	const FGridCellQueryResult CurrentCell = GridWorld->ProjectPoint(RequesterActor->GetActorLocation());
	TMap<FGridCellId, FGridMoveToCellEvaluationResult> Evaluations;

	for (const FParadoxInteractionOption& Option : Query.Options)
	{
		if (Option.InteractionTag != SemanticParameters.InteractionTag
			|| Option.State != EParadoxInteractionOptionState::Free
			|| !Option.GridCellId.IsValid())
		{
			continue;
		}

		FExecutionCandidate Candidate;
		Candidate.Option = Option;
		Candidate.bAlreadyInPlace = CurrentCell.Status == EGridQueryStatus::Success
			&& CurrentCell.CellId == Option.GridCellId;
		if (Candidate.bAlreadyInPlace)
		{
			Candidate.PathCost = -1.0;
			ExecutionCandidates.Add(MoveTemp(Candidate));
			continue;
		}
		if (!IsValid(Controller))
		{
			continue;
		}

		FGridMoveToCellEvaluationResult* Evaluation = Evaluations.Find(Option.GridCellId);
		if (!Evaluation)
		{
			const FGridCellQueryResult GoalCell = GridWorld->GetCell(Option.GridCellId);
			if (GoalCell.Status != EGridQueryStatus::Success || !GoalCell.bWalkable)
			{
				continue;
			}
			FGridMoveToCellExecutionRequest Request;
			Request.Controller = Controller;
			Request.GoalLocation = GoalCell.WorldCenter;
			Request.AcceptanceRadius = MovementParameters.AcceptanceRadius;
			Request.AcceptPartialPath = EAIOptionFlag::Disable;
			Request.RequireNavigableEndLocation = EAIOptionFlag::Enable;
			Request.FilterClass = MovementParameters.NavigationFilter;
			Request.bAllowStrafe = MovementParameters.bAllowStrafe;
			Request.bTrackMovingGoal = false;
			Request.GoalContentionPolicy = EGridGoalContentionPolicy::RejectOccupied;
			Evaluation = &Evaluations.Add(Option.GridCellId, UGridMoveToCellExecution::Evaluate(Request));
		}
		if (Evaluation->bCanExecute)
		{
			Candidate.PathCost = Evaluation->PathCost;
			Candidate.bAlreadyInPlace = Evaluation->bAlreadyAtGoal;
			ExecutionCandidates.Add(MoveTemp(Candidate));
		}
	}

	ExecutionCandidates.Sort([](const FExecutionCandidate& Left, const FExecutionCandidate& Right)
	{
		if (!FMath::IsNearlyEqual(Left.PathCost, Right.PathCost))
		{
			return Left.PathCost < Right.PathCost;
		}
		const FGridCellCoord& A = Left.Option.GridCellId.Coord;
		const FGridCellCoord& B = Right.Option.GridCellId.Coord;
		if (A.X != B.X) { return A.X < B.X; }
		if (A.Y != B.Y) { return A.Y < B.Y; }
		if (A.Layer != B.Layer) { return A.Layer < B.Layer; }
		return LexToString(Left.Option.SlotHandle) < LexToString(Right.Option.SlotHandle);
	});

	if (ExecutionCandidates.IsEmpty())
	{
		OutDiagnostic = IsValid(Controller)
			? TEXT("No free interaction slot has a complete GridWorld path.")
			: TEXT("The requester is outside every interaction cell and has no movement Controller.");
		return false;
	}

	if (!InteractionAffordanceChangedHandle.IsValid())
	{
		InteractionAffordanceChangedHandle = Component->OnInteractionAffordanceChangedNative().AddUObject(
			this,
			&ThisClass::HandleInteractionAffordanceChanged);
	}
	return true;
}

bool UParadoxInteractionActionBase::HasReachableExecutionCandidate(
	AActor* RequesterActor,
	UParadoxInteractionComponent* Component,
	const FGameplayTag ExactInteractionTag,
	FString& OutDiagnostic) const
{
	UGridWorldSubsystem* GridWorld = GetWorld() ? GetWorld()->GetSubsystem<UGridWorldSubsystem>() : nullptr;
	if (!IsValid(RequesterActor) || !IsValid(Component) || !GridWorld)
	{
		OutDiagnostic = TEXT("Reachability preflight requires a requester, interaction component, and GridWorld.");
		return false;
	}
	const FParadoxInteractionQueryResult Query = Component->QueryInteractionOptionsByTag(RequesterActor, ExactInteractionTag);
	if (!Query.IsSuccess())
	{
		OutDiagnostic = Query.DiagnosticMessage;
		return false;
	}
	FParadoxInteractionMovementParameters MovementParameters;
	if (!ReadMovementParameters(MovementParameters, OutDiagnostic))
	{
		return false;
	}
	AController* Controller = Cast<AController>(RequesterActor);
	if (!Controller)
	{
		if (const APawn* Pawn = Cast<APawn>(RequesterActor))
		{
			Controller = Pawn->GetController();
		}
	}
	const FGridCellQueryResult CurrentCell = GridWorld->ProjectPoint(RequesterActor->GetActorLocation());
	TSet<FGridCellId> EvaluatedCells;
	for (const FParadoxInteractionOption& Option : Query.Options)
	{
		if (Option.InteractionTag != ExactInteractionTag
			|| Option.State != EParadoxInteractionOptionState::Free
			|| !Option.GridCellId.IsValid())
		{
			continue;
		}
		if (CurrentCell.Status == EGridQueryStatus::Success && CurrentCell.CellId == Option.GridCellId)
		{
			return true;
		}
		if (!IsValid(Controller) || EvaluatedCells.Contains(Option.GridCellId))
		{
			continue;
		}
		EvaluatedCells.Add(Option.GridCellId);
		const FGridCellQueryResult GoalCell = GridWorld->GetCell(Option.GridCellId);
		if (GoalCell.Status != EGridQueryStatus::Success || !GoalCell.bWalkable)
		{
			continue;
		}
		FGridMoveToCellExecutionRequest Request;
		Request.Controller = Controller;
		Request.GoalLocation = GoalCell.WorldCenter;
		Request.AcceptanceRadius = MovementParameters.AcceptanceRadius;
		Request.AcceptPartialPath = EAIOptionFlag::Disable;
		Request.RequireNavigableEndLocation = EAIOptionFlag::Enable;
		Request.FilterClass = MovementParameters.NavigationFilter;
		Request.bAllowStrafe = MovementParameters.bAllowStrafe;
		Request.bTrackMovingGoal = false;
		Request.GoalContentionPolicy = EGridGoalContentionPolicy::RejectOccupied;
		if (UGridMoveToCellExecution::Evaluate(Request).bCanExecute)
		{
			return true;
		}
	}
	OutDiagnostic = TEXT("No free exact interaction slot has a complete path under the authored NavigationFilter.");
	return false;
}

bool UParadoxInteractionActionBase::TryStartNextCandidate(FString& OutDiagnostic)
{
	ReleaseMovement(false);
	ReleaseInteractionClaim();
	while (ExecutionCandidates.IsValidIndex(NextCandidateIndex))
	{
		const FExecutionCandidate Candidate = ExecutionCandidates[NextCandidateIndex++];
		if (!Candidate.bAlreadyInPlace && AttemptedMovementCells.Contains(Candidate.Option.GridCellId))
		{
			continue;
		}

		ResolvedOption = Candidate.Option;
		OnInteractionContextResolved();
		if (bCompletionRequested)
		{
			return true;
		}
		if (!ClaimResolvedOption(OutDiagnostic))
		{
			continue;
		}
		LogDebugState(TEXT("Claimed"));
		OnInteractionSlotClaimed();
		if (bCompletionRequested)
		{
			return true;
		}

		if (Candidate.bAlreadyInPlace)
		{
			ExecuteResolvedInteraction();
			return true;
		}

		AttemptedMovementCells.Add(Candidate.Option.GridCellId);
		if (StartMovementToResolvedOption(OutDiagnostic))
		{
			return true;
		}
		ReleaseInteractionClaim();
	}
	OutDiagnostic = OutDiagnostic.IsEmpty()
		? TEXT("Every reachable interaction slot was exhausted by claim or movement contention.")
		: OutDiagnostic;
	return false;
}

bool UParadoxInteractionActionBase::ClaimResolvedOption(FString& OutDiagnostic)
{
	USmartObjectSubsystem* SmartObjects = USmartObjectSubsystem::GetCurrent(GetWorld());
	AActor* RequesterActor = InteractionRequester.Get();
	if (!SmartObjects || !IsValid(RequesterActor))
	{
		OutDiagnostic = TEXT("The Smart Object subsystem or interaction requester is unavailable.");
		return false;
	}
	ClaimHandle = SmartObjects->MarkSlotAsClaimed(
		ResolvedOption.SlotHandle,
		ESmartObjectClaimPriority::Normal,
		FConstStructView::Make(FSmartObjectActorUserData(RequesterActor)));
	if (!ClaimHandle.IsValid())
	{
		OutDiagnostic = TEXT("The candidate Smart Object slot lost its claim race.");
		return false;
	}
	SmartObjects->RegisterSlotInvalidationCallback(
		ClaimHandle,
		FOnSlotInvalidated::CreateUObject(this, &ThisClass::HandleInteractionSlotInvalidated));
	bSlotInvalidationCallbackRegistered = true;
	return true;
}

bool UParadoxInteractionActionBase::StartMovementToResolvedOption(FString& OutDiagnostic)
{
	AController* Controller = ResolveMovementController();
	UGridWorldSubsystem* GridWorld = GetWorld() ? GetWorld()->GetSubsystem<UGridWorldSubsystem>() : nullptr;
	if (!IsValid(Controller) || !GridWorld)
	{
		OutDiagnostic = TEXT("The requester has no movement Controller or GridWorld subsystem.");
		return false;
	}
	const FGridCellQueryResult GoalCell = GridWorld->GetCell(ResolvedOption.GridCellId);
	if (GoalCell.Status != EGridQueryStatus::Success || !GoalCell.bWalkable)
	{
		OutDiagnostic = TEXT("The claimed interaction cell is no longer walkable.");
		return false;
	}
	FParadoxInteractionMovementParameters MovementParameters;
	if (!ReadMovementParameters(MovementParameters, OutDiagnostic))
	{
		return false;
	}

	MovementExecution = NewObject<UGridMoveToCellExecution>(this);
	MovementFinishedHandle = MovementExecution->OnFinishedNative().AddUObject(
		this,
		&ThisClass::HandleMovementFinished);
	FGridMoveToCellExecutionRequest Request;
	Request.Controller = Controller;
	Request.GoalLocation = GoalCell.WorldCenter;
	Request.AcceptanceRadius = MovementParameters.AcceptanceRadius;
	Request.AcceptPartialPath = EAIOptionFlag::Disable;
	Request.RequireNavigableEndLocation = EAIOptionFlag::Enable;
	Request.FilterClass = MovementParameters.NavigationFilter;
	Request.bAllowStrafe = MovementParameters.bAllowStrafe;
	Request.bTrackMovingGoal = false;
	Request.GoalContentionPolicy = EGridGoalContentionPolicy::RejectOccupied;
	bMovingToInteraction = true;
	OnInteractionMovementStarted();
	if (!MovementExecution || !MovementExecution->Start(Request, OutDiagnostic))
	{
		ReleaseMovement(false);
		return false;
	}
	return true;
}

void UParadoxInteractionActionBase::ExecuteResolvedInteraction()
{
	if (bCompletionRequested)
	{
		return;
	}
	AActor* RequesterActor = InteractionRequester.Get();
	UGridWorldSubsystem* GridWorld = GetWorld() ? GetWorld()->GetSubsystem<UGridWorldSubsystem>() : nullptr;
	const FGridCellQueryResult CurrentCell = IsValid(RequesterActor) && GridWorld
		? GridWorld->ProjectPoint(RequesterActor->GetActorLocation())
		: FGridCellQueryResult();
	if (CurrentCell.Status != EGridQueryStatus::Success || CurrentCell.CellId != ResolvedOption.GridCellId)
	{
		FailInteraction(ParadoxGameplayTags::Result_Failure_Interaction_InvalidPosition, TEXT("The requester did not reach the claimed interaction cell."));
		return;
	}
	USmartObjectSubsystem* SmartObjects = USmartObjectSubsystem::GetCurrent(GetWorld());
	if (!SmartObjects || !ClaimHandle.IsValid() || !SmartObjects->IsClaimedSmartObjectValid(ClaimHandle))
	{
		FailInteraction(ParadoxGameplayTags::Result_Failure_Interaction_ClaimFailed, TEXT("The Smart Object claim was lost before interaction execution."));
		return;
	}
	if (IsInteractionOutcomeSatisfied())
	{
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, TEXT("The requested outcome was satisfied while approaching the target."));
		return;
	}
	FGameplayTag FailureReason;
	FString Diagnostic;
	if (!CanSatisfyInteractionPreconditions(FailureReason, Diagnostic)
		|| !CanExecuteInteraction(FailureReason, Diagnostic))
	{
		FailInteraction(FailureReason.IsValid() ? FailureReason : ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest, Diagnostic);
		return;
	}
	ExecuteInteraction();
}

AController* UParadoxInteractionActionBase::ResolveMovementController() const
{
	AActor* RequesterActor = InteractionRequester.Get();
	if (AController* Controller = Cast<AController>(RequesterActor))
	{
		return Controller;
	}
	if (const APawn* Pawn = Cast<APawn>(RequesterActor))
	{
		return Pawn->GetController();
	}
	return nullptr;
}

bool UParadoxInteractionActionBase::ReadMovementParameters(
	FParadoxInteractionMovementParameters& OutParameters,
	FString& OutDiagnostic) const
{
	using namespace UE::Paradox::InteractionAction::Private;
	OutParameters = FParadoxInteractionMovementParameters();
	const auto ReadOptional = [this, &OutDiagnostic, &OutParameters](const FName Name) -> bool
	{
		const FProperty* Property = FParadoxInteractionMovementParameters::StaticStruct()->FindPropertyByName(Name);
		const EGameplayActionParameterAccessResult Result = UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			GetParameters(),
			Name,
			Property,
			Property ? Property->ContainerPtrToValuePtr<void>(&OutParameters) : nullptr);
		if (Result == EGameplayActionParameterAccessResult::Success
			|| Result == EGameplayActionParameterAccessResult::ParameterNotFound)
		{
			return true;
		}
		OutDiagnostic = FString::Printf(TEXT("Movement parameter '%s' has an incompatible schema."), *Name.ToString());
		return false;
	};
	return ReadOptional(NavigationFilterParameterName)
		&& ReadOptional(AcceptanceRadiusParameterName)
		&& ReadOptional(AllowStrafeParameterName);
}

void UParadoxInteractionActionBase::HandleMovementFinished(const FGridMoveToCellExecutionResult& Result)
{
	if (bCompletionRequested)
	{
		return;
	}
	const bool bSucceeded = Result.IsSuccess();
	FString Diagnostic = Result.DiagnosticMessage;
	ReleaseMovement(false);
	if (bSucceeded)
	{
		OnInteractionMovementCompleted();
		ExecuteResolvedInteraction();
		return;
	}
	ReleaseInteractionClaim();
	if (!TryStartNextCandidate(Diagnostic))
	{
		FailInteraction(
			ParadoxGameplayTags::Result_Failure_Interaction_InvalidPosition,
			Diagnostic.IsEmpty() ? TEXT("Movement could not reach any interaction cell.") : Diagnostic);
	}
}

void UParadoxInteractionActionBase::ReleaseMovement(const bool bCancel)
{
	UGridMoveToCellExecution* Execution = MovementExecution;
	MovementExecution = nullptr;
	bMovingToInteraction = false;
	if (!Execution)
	{
		MovementFinishedHandle.Reset();
		return;
	}
	Execution->OnFinishedNative().Remove(MovementFinishedHandle);
	MovementFinishedHandle.Reset();
	if (bCancel)
	{
		Execution->Cancel();
	}
}

void UParadoxInteractionActionBase::ReevaluateRunningInteraction()
{
	if (!bMovingToInteraction || bCompletionRequested)
	{
		return;
	}
	if (IsInteractionOutcomeSatisfied())
	{
		ReleaseMovement(true);
		CompleteInteractionSuccess(GameplayActionTags::Result_Success, TEXT("The requested outcome was satisfied externally during movement."));
		return;
	}
	FGameplayTag FailureReason;
	FString Diagnostic;
	if (!CanSatisfyInteractionPreconditions(FailureReason, Diagnostic))
	{
		ReleaseMovement(true);
		FailInteraction(FailureReason.IsValid() ? FailureReason : ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest, Diagnostic);
	}
}

void UParadoxInteractionActionBase::FailInteraction(
	const FGameplayTag ReasonTag,
	const FString& DiagnosticMessage)
{
	if (bCompletionRequested)
	{
		return;
	}
	bCompletionRequested = true;
	OnInteractionFailed(ReasonTag, DiagnosticMessage);
	LogDebugState(TEXT("Failed"), ReasonTag);
	FailAction(ReasonTag, DiagnosticMessage);
}

void UParadoxInteractionActionBase::ReleaseInteractionClaim()
{
	if (!ClaimHandle.IsValid())
	{
		return;
	}

	const FSmartObjectClaimHandle ClaimToRelease = ClaimHandle;
	ClaimHandle.Invalidate();
	USmartObjectSubsystem* SmartObjects =
		USmartObjectSubsystem::GetCurrent(GetWorld());
	if (SmartObjects && bSlotInvalidationCallbackRegistered)
	{
		SmartObjects->UnregisterSlotInvalidationCallback(ClaimToRelease);
	}
	bSlotInvalidationCallbackRegistered = false;
	if (SmartObjects
		&& SmartObjects->IsClaimedSmartObjectValid(ClaimToRelease)
		&& !SmartObjects->MarkSlotAsFree(ClaimToRelease))
	{
		PARADOX_LOG_WARNING(
			TEXT("Interaction action '%s' could not release Smart Object claim '%s'."),
			*GetNameSafe(this),
			*LexToString(ClaimToRelease));
	}
	LogDebugState(TEXT("ClaimReleased"));
}

void UParadoxInteractionActionBase::LogDebugState(
	const TCHAR* EventName,
	const FGameplayTag ReasonTag) const
{
	if (!bEnableDebug || !IsParadoxInteractionDebugEnabled())
	{
		return;
	}
	PARADOX_LOG_INFO(
		TEXT("Interaction action '%s' event='%s' state=%d requester='%s' target='%s' tag='%s' slot='%s' grid='%s' cell=(%d,%d,%d) claimed=%s reason='%s'."),
		*GetNameSafe(this),
		EventName,
		static_cast<int32>(GetState()),
		*GetNameSafe(InteractionRequester.Get()),
		*GetNameSafe(InteractionTarget.Get()),
		*SemanticParameters.InteractionTag.ToString(),
		*LexToString(ResolvedOption.SlotHandle),
		*ResolvedOption.GridCellId.GridId.ToString(),
		ResolvedOption.GridCellId.Coord.X,
		ResolvedOption.GridCellId.Coord.Y,
		ResolvedOption.GridCellId.Coord.Layer,
		ClaimHandle.IsValid() ? TEXT("true") : TEXT("false"),
		*ReasonTag.ToString());
}

void UParadoxInteractionActionBase::HandleInteractionTargetDestroyed(
	AActor* DestroyedActor)
{
	if (DestroyedActor == InteractionTarget.Get() && !bCompletionRequested)
	{
		FailInteraction(
			ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable,
			TEXT("The interaction Target was destroyed before the action completed."));
	}
}

void UParadoxInteractionActionBase::HandleInteractionAffordanceChanged(
	UParadoxInteractionComponent* ChangedInteractionComponent)
{
	if (bCompletionRequested
		|| ChangedInteractionComponent != InteractionComponent.Get()
		|| !ClaimHandle.IsValid())
	{
		return;
	}

	USmartObjectSubsystem* SmartObjects =
		USmartObjectSubsystem::GetCurrent(GetWorld());
	if (!SmartObjects
		|| !SmartObjects->IsClaimedSmartObjectValid(ClaimHandle))
	{
		FailInteraction(
			ParadoxGameplayTags::Result_Failure_Interaction_ClaimFailed,
			TEXT("The running interaction lost its Smart Object claim."));
		return;
	}
	ReevaluateRunningInteraction();
	if (bCompletionRequested || bMovingToInteraction)
	{
		return;
	}

	FParadoxInteractionOption CurrentOption;
	EParadoxInteractionRequestStatus RequestStatus =
		EParadoxInteractionRequestStatus::InvalidRequester;
	EParadoxInteractionQueryStatus QueryStatus =
		EParadoxInteractionQueryStatus::NoOptions;
	FString Diagnostic;
	if (!ChangedInteractionComponent->ResolveCurrentExecutionOption(
		InteractionRequester.Get(),
		SemanticParameters.InteractionTag,
		CurrentOption,
		RequestStatus,
		QueryStatus,
		Diagnostic)
		|| CurrentOption.SlotHandle != ResolvedOption.SlotHandle)
	{
		(void)QueryStatus;
		FailInteraction(
			RequestStatus == EParadoxInteractionRequestStatus::InvalidCurrentPosition
				? ParadoxGameplayTags::Result_Failure_Interaction_InvalidPosition
				: ParadoxGameplayTags::Result_Failure_Interaction_SlotUnavailable,
			Diagnostic.IsEmpty()
				? TEXT("The claimed interaction slot changed while the action was running.")
				: Diagnostic);
	}
}

void UParadoxInteractionActionBase::HandleInteractionSlotInvalidated(
	const FSmartObjectClaimHandle& InvalidatedClaim,
	const ESmartObjectSlotState CurrentState)
{
	if (bCompletionRequested || InvalidatedClaim != ClaimHandle)
	{
		return;
	}

	const FString Diagnostic = FString::Printf(
		TEXT("The running interaction's Smart Object slot was invalidated in state %d."),
		static_cast<int32>(CurrentState));
	if (bMovingToInteraction)
	{
		ReleaseMovement(true);
		ClaimHandle.Invalidate();
		bSlotInvalidationCallbackRegistered = false;
		FString NextDiagnostic = Diagnostic;
		if (TryStartNextCandidate(NextDiagnostic))
		{
			return;
		}
	}
	FailInteraction(ParadoxGameplayTags::Result_Failure_Interaction_ClaimFailed, Diagnostic);
}
