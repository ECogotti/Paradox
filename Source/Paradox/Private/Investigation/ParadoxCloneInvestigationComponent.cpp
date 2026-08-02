#include "Investigation/ParadoxCloneInvestigationComponent.h"

#include "AITypes.h"
#include "Actions/GridMoveToCellActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayActionTags.h"
#include "Investigation/ParadoxInvestigationWaitActionDefinition.h"
#include "Paradox.h"
#include "UObject/UnrealType.h"

namespace
{
	EGameplayActionParameterAccessResult SetRequestValue(
		FGameplayActionRequest& Request,
		const FName ParameterName,
		const UStruct* SourceStruct,
		const FName SourcePropertyName,
		const void* Source)
	{
		const FProperty* Property =
			FindFProperty<FProperty>(SourceStruct, SourcePropertyName);
		return UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Request,
			ParameterName,
			Property,
			Property ? Property->ContainerPtrToValuePtr<void>(Source) : nullptr);
	}
}

UParadoxCloneInvestigationComponent::UParadoxCloneInvestigationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneInvestigationComponent::InitializeInvestigation(
	UGameplayActionComponent* InActionComponent)
{
	if (!IsValid(InActionComponent) || InActionComponent->GetOwner() != GetOwner())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidArgument,
			TEXT("Investigation requires the owning clone's GameplayActionComponent."));
	}
	if (bInitialized && ActionComponent == InActionComponent)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::AlreadyInState,
			TEXT("Investigation is already initialized."));
	}
	if (bActive)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("Investigation cannot be rebound while execution is active."));
	}
	if (ActionComponent && ActionEndedHandle.IsValid())
	{
		ActionComponent->OnActionEndedNative().Remove(ActionEndedHandle);
	}

	ActionComponent = InActionComponent;
	MoveDefinition = NewObject<UGridMoveToCellActionDefinition>(
		this,
		TEXT("ParadoxInvestigationMoveDefinition"));
	WaitDefinition = NewObject<UParadoxInvestigationWaitActionDefinition>(
		this,
		TEXT("ParadoxInvestigationWaitDefinition"));
	if (!MoveDefinition || !WaitDefinition)
	{
		ActionComponent = nullptr;
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ConfigurationError,
			TEXT("Investigation could not create its native GameplayAction Definitions."));
	}
	ActionEndedHandle = ActionComponent->OnActionEndedNative().AddUObject(
		this,
		&UParadoxCloneInvestigationComponent::HandleActionEnded);
	bInitialized = true;
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("Investigation GameplayActions binding initialized."));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneInvestigationComponent::StartInvestigation(
	const FParadoxInvestigationContext& Context)
{
	if (!bInitialized || !ActionComponent)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::NotInitialized,
			TEXT("Investigation has not been initialized."));
	}
	if (!Context.IsValid())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidArgument,
			TEXT("Investigation context is invalid."));
	}
	if (bActive)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("An investigation or recovery move is already active."));
	}

	FGameplayActionRequest Request;
	FString Diagnostic;
	const FGuid CandidateCorrelationId = FGuid::NewGuid();
	if (!BuildMoveRequest(
		Context.InvestigationLocation,
		Context.ObservationType == EPerceptionKnowledgeObservationType::State
			? Context.SourceActor.Get()
			: nullptr,
		Context.InvestigationPriority,
		Request,
		Diagnostic,
		CandidateCorrelationId))
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			MoveTemp(Diagnostic));
	}
	const FGameplayActionSubmissionResult Preflight =
		ActionComponent->PreflightAction(Request);
	if (!Preflight.IsAccepted())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			FString::Printf(
				TEXT("Investigation movement preflight failed: %s"),
				*Preflight.DiagnosticMessage));
	}

	ActiveContext = Context;
	bActive = true;
	bCompletionBroadcast = false;
	const FParadoxCloneBehaviorOperationResult Submission =
		SubmitPreparedAction(Request, EExecutionPhase::Moving);
	if (!Submission.IsSuccess())
	{
		ResetExecution(false);
		return Submission;
	}
	InvestigationRetargetedNative.Broadcast(ActiveContext);
	return Submission;
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneInvestigationComponent::ValidateInvestigation(
	const FParadoxInvestigationContext& Context) const
{
	if (!bInitialized || !ActionComponent)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::NotInitialized,
			TEXT("Investigation has not been initialized."));
	}
	if (!Context.IsValid())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidArgument,
			TEXT("Investigation context is invalid."));
	}
	FGameplayActionRequest Request;
	FString Diagnostic;
	const FGuid CandidateCorrelationId = FGuid::NewGuid();
	if (!BuildMoveRequest(
		Context.InvestigationLocation,
		Context.ObservationType == EPerceptionKnowledgeObservationType::State
			? Context.SourceActor.Get()
			: nullptr,
		Context.InvestigationPriority,
		Request,
		Diagnostic,
		CandidateCorrelationId))
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			MoveTemp(Diagnostic));
	}
	const FGameplayActionSubmissionResult Preflight =
		ActionComponent->PreflightAction(Request);
	return Preflight.IsAccepted()
		? MakeResult(
			EParadoxCloneBehaviorOperationStatus::Succeeded,
			TEXT("Investigation request is currently valid."))
		: MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			FString::Printf(
				TEXT("Investigation request preflight failed: %s"),
				*Preflight.DiagnosticMessage));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneInvestigationComponent::RetargetInvestigation(
	const FParadoxInvestigationContext& Context)
{
	if (!bInitialized || !ActionComponent || !bActive)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("Retarget requires an active investigation or recovery move."));
	}
	if (!Context.IsValid()
		|| Context.InvestigationRevision <= ActiveContext.InvestigationRevision
		|| Context.InvestigationPriority <= ActiveContext.InvestigationPriority)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidArgument,
			TEXT("Retarget context must have a newer revision and strictly higher priority."));
	}

	FGameplayActionRequest Request;
	FString Diagnostic;
	const FGuid CandidateCorrelationId = FGuid::NewGuid();
	if (!BuildMoveRequest(
		Context.InvestigationLocation,
		Context.ObservationType == EPerceptionKnowledgeObservationType::State
			? Context.SourceActor.Get()
			: nullptr,
		Context.InvestigationPriority,
		Request,
		Diagnostic,
		CandidateCorrelationId))
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			MoveTemp(Diagnostic));
	}
	const FGameplayActionSubmissionResult Preflight =
		ActionComponent->PreflightAction(Request);
	if (!Preflight.IsAccepted())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			FString::Printf(
				TEXT("Higher-priority retarget was rejected before commit: %s"),
				*Preflight.DiagnosticMessage));
	}

	// Invalidate the previous callback identity before ending its handle synchronously.
	const FGameplayActionHandle SupersededHandle = ActiveActionHandle;
	ActiveActionHandle = FGameplayActionHandle();
	ActiveCorrelationId = CandidateCorrelationId;
	ActiveContext = Context;
	ExecutionPhase = EExecutionPhase::Moving;
	bCompletionBroadcast = false;
	if (SupersededHandle.IsValid())
	{
		ActionComponent->InterruptAction(
			SupersededHandle,
			ParadoxGameplayTags::Result_Interrupted_InvestigationSuperseded);
	}

	const FParadoxCloneBehaviorOperationResult SubmitResult =
		SubmitPreparedAction(Request, EExecutionPhase::Moving);
	if (!SubmitResult.IsSuccess())
	{
		FGameplayActionResult Failure;
		Failure.TerminalState = EGameplayActionState::Failed;
		Failure.ReasonTag = GameplayActionTags::Result_Failure_CannotStart;
		Failure.DiagnosticMessage = SubmitResult.DiagnosticMessage;
		FinishInvestigationOnce(Failure);
		return SubmitResult;
	}
	InvestigationRetargetedNative.Broadcast(ActiveContext);
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Replaced,
		TEXT("Higher-priority investigation target replaced the previous target."),
		TEXT("Replaced"));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneInvestigationComponent::CancelInvestigation(
	const FGameplayTag ReasonTag)
{
	if (!bActive)
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::AlreadyInState,
			TEXT("No investigation action is active."));
	}
	const FGameplayActionHandle Handle = ActiveActionHandle;
	ResetExecution(false);
	if (Handle.IsValid() && ActionComponent)
	{
		ActionComponent->CancelAction(Handle, ReasonTag);
	}
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("Investigation execution cancelled."));
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneInvestigationComponent::StartReplayRecoveryMove(
	const FVector& Location,
	const int32 InvestigationRevision,
	const int32 SchedulingPriority)
{
	if (!bInitialized || !ActionComponent || bActive || Location.ContainsNaN())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::InvalidState,
			TEXT("Replay recovery move cannot start in the current investigation state."));
	}
	FGameplayActionRequest Request;
	FString Diagnostic;
	if (!BuildMoveRequest(
		Location,
		nullptr,
		SchedulingPriority,
		Request,
		Diagnostic))
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			MoveTemp(Diagnostic));
	}
	const FGameplayActionSubmissionResult Preflight =
		ActionComponent->PreflightAction(Request);
	if (!Preflight.IsAccepted())
	{
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			Preflight.DiagnosticMessage);
	}
	ActiveContext.InvestigationRevision = InvestigationRevision;
	bActive = true;
	bCompletionBroadcast = false;
	return SubmitPreparedAction(Request, EExecutionPhase::RecoveryMoving);
}

bool UParadoxCloneInvestigationComponent::BuildMoveRequest(
	const FVector& Location,
	AActor* SourceActor,
	const int32 SchedulingPriority,
	FGameplayActionRequest& OutRequest,
	FString& OutDiagnostic,
	const FGuid CorrelationOverride) const
{
	if (!MoveDefinition || Location.ContainsNaN())
	{
		OutDiagnostic = TEXT("Investigation movement Definition or destination is invalid.");
		return false;
	}
	FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(MoveDefinition);
	if (!Creation.WasCreated())
	{
		OutDiagnostic = Creation.DiagnosticMessage;
		return false;
	}

	FParadoxInvestigationMoveRequestValues Values;
	Values.GoalLocation = Location;
	Values.GoalActor = SourceActor;
	Values.GoalContentionPolicy =
		MovementGoalContentionPolicy;
	const UScriptStruct* ValuesStruct =
		FParadoxInvestigationMoveRequestValues::StaticStruct();
	const bool bWritten =
		SetRequestValue(
			Creation.Request,
			GridMoveToCellActionParameters::PathSource,
			ValuesStruct,
			GET_MEMBER_NAME_CHECKED(FParadoxInvestigationMoveRequestValues, PathSource),
			&Values) == EGameplayActionParameterAccessResult::Success
		&& SetRequestValue(
			Creation.Request,
			GridMoveToCellActionParameters::GoalLocation,
			ValuesStruct,
			GET_MEMBER_NAME_CHECKED(FParadoxInvestigationMoveRequestValues, GoalLocation),
			&Values) == EGameplayActionParameterAccessResult::Success
		&& SetRequestValue(
			Creation.Request,
			GridMoveToCellActionParameters::GoalActor,
			ValuesStruct,
			GET_MEMBER_NAME_CHECKED(FParadoxInvestigationMoveRequestValues, GoalActor),
			&Values) == EGameplayActionParameterAccessResult::Success
		&& SetRequestValue(
			Creation.Request,
			GridMoveToCellActionParameters::GoalContentionPolicy,
			ValuesStruct,
			GET_MEMBER_NAME_CHECKED(FParadoxInvestigationMoveRequestValues, GoalContentionPolicy),
			&Values) == EGameplayActionParameterAccessResult::Success;
	if (!bWritten)
	{
		OutDiagnostic =
			TEXT("Investigation could not populate the Grid Move To Cell request schema.");
		return false;
	}

	UGameplayActionBlueprintLibrary::SetRequestPriority(
		Creation.Request,
		FMath::Max(1, SchedulingPriority));
	UGameplayActionBlueprintLibrary::SetRequestBlockedPolicy(
		Creation.Request,
		EGameplayActionBlockedPolicy::Reject);
	FGameplayActionCorrelationData Correlation;
	Correlation.Id = CorrelationOverride.IsValid()
		? CorrelationOverride
		: ActiveCorrelationId.IsValid()
			? ActiveCorrelationId
		: FGuid::NewGuid();
	UGameplayActionBlueprintLibrary::SetRequestContext(
		Creation.Request,
		ParadoxGameplayTags::Origin_Investigation,
		const_cast<UParadoxCloneInvestigationComponent*>(this),
		Correlation);
	OutRequest = MoveTemp(Creation.Request);
	return true;
}

bool UParadoxCloneInvestigationComponent::BuildWaitRequest(
	const FParadoxInvestigationContext& Context,
	FGameplayActionRequest& OutRequest,
	FString& OutDiagnostic) const
{
	if (!WaitDefinition)
	{
		OutDiagnostic = TEXT("Investigation wait Definition is unavailable.");
		return false;
	}
	FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(WaitDefinition);
	if (!Creation.WasCreated())
	{
		OutDiagnostic = Creation.DiagnosticMessage;
		return false;
	}
	FParadoxInvestigationWaitRequestValues Values;
	Values.Duration = FMath::Max(
		0.0,
		static_cast<double>(InspectionDuration));
	if (SetRequestValue(
			Creation.Request,
			TEXT("Duration"),
			FParadoxInvestigationWaitRequestValues::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(
				FParadoxInvestigationWaitRequestValues,
				Duration),
			&Values)
		!= EGameplayActionParameterAccessResult::Success)
	{
		OutDiagnostic = TEXT("Investigation could not write the inspection Duration.");
		return false;
	}
	UGameplayActionBlueprintLibrary::SetRequestPriority(
		Creation.Request,
		FMath::Max(1, Context.InvestigationPriority));
	UGameplayActionBlueprintLibrary::SetRequestBlockedPolicy(
		Creation.Request,
		EGameplayActionBlockedPolicy::Reject);
	FGameplayActionCorrelationData Correlation;
	Correlation.Id = ActiveCorrelationId;
	UGameplayActionBlueprintLibrary::SetRequestContext(
		Creation.Request,
		ParadoxGameplayTags::Origin_Investigation,
		const_cast<UParadoxCloneInvestigationComponent*>(this),
		Correlation);
	OutRequest = MoveTemp(Creation.Request);
	return true;
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneInvestigationComponent::SubmitPreparedAction(
	const FGameplayActionRequest& Request,
	const EExecutionPhase NewPhase)
{
	ExecutionPhase = NewPhase;
	if (!ActiveCorrelationId.IsValid())
	{
		ActiveCorrelationId = Request.GetCorrelation().Id;
	}
	const FGameplayActionSubmissionResult Submission =
		ActionComponent->SubmitAction(Request);
	if (!Submission.IsAccepted())
	{
		ActiveActionHandle = FGameplayActionHandle();
		return MakeResult(
			EParadoxCloneBehaviorOperationStatus::ActionRejected,
			FString::Printf(
				TEXT("Investigation GameplayAction submission failed: %s"),
				*Submission.DiagnosticMessage));
	}
	FGameplayActionResult ExistingResult;
	if (!ActionComponent->GetActionResult(Submission.Handle, ExistingResult))
	{
		ActiveActionHandle = Submission.Handle;
	}
	return MakeResult(
		EParadoxCloneBehaviorOperationStatus::Succeeded,
		TEXT("Investigation GameplayAction submitted."));
}

void UParadoxCloneInvestigationComponent::HandleActionEnded(
	const FGameplayActionEvent& Event)
{
	if (!bActive
		|| Event.EventType != EGameplayActionEventType::Ended
		|| !Event.bHasResult
		|| Event.OriginTag != ParadoxGameplayTags::Origin_Investigation
		|| Event.Correlation.Id != ActiveCorrelationId
		|| (ActiveActionHandle.IsValid() && Event.Handle != ActiveActionHandle))
	{
		return;
	}
	ActiveActionHandle = FGameplayActionHandle();
	if (ExecutionPhase == EExecutionPhase::Moving)
	{
		HandleMovementFinished(Event.Result);
	}
	else if (ExecutionPhase == EExecutionPhase::Inspecting)
	{
		HandleInspectionFinished(Event.Result);
	}
	else if (ExecutionPhase == EExecutionPhase::RecoveryMoving)
	{
		const int32 Revision = ActiveContext.InvestigationRevision;
		const bool bSucceeded =
			Event.Result.TerminalState == EGameplayActionState::Succeeded;
		ResetExecution(true);
		RecoveryMoveFinishedNative.Broadcast(Revision, bSucceeded);
	}
}

void UParadoxCloneInvestigationComponent::HandleMovementFinished(
	const FGameplayActionResult& Result)
{
	if (Result.TerminalState != EGameplayActionState::Succeeded)
	{
		FinishInvestigationOnce(Result);
		return;
	}
	OrientTowardInvestigation();
	FGameplayActionRequest WaitRequest;
	FString Diagnostic;
	ActiveCorrelationId = FGuid::NewGuid();
	if (!BuildWaitRequest(ActiveContext, WaitRequest, Diagnostic))
	{
		FGameplayActionResult Failure;
		Failure.TerminalState = EGameplayActionState::Failed;
		Failure.ReasonTag = GameplayActionTags::Result_Failure_InvalidRequest;
		Failure.DiagnosticMessage = MoveTemp(Diagnostic);
		FinishInvestigationOnce(Failure);
		return;
	}
	const FGameplayActionSubmissionResult Preflight =
		ActionComponent->PreflightAction(WaitRequest);
	if (!Preflight.IsAccepted())
	{
		FGameplayActionResult Failure;
		Failure.TerminalState = EGameplayActionState::Failed;
		Failure.ReasonTag = GameplayActionTags::Result_Failure_CannotStart;
		Failure.DiagnosticMessage = Preflight.DiagnosticMessage;
		FinishInvestigationOnce(Failure);
		return;
	}
	const FParadoxCloneBehaviorOperationResult Submit =
		SubmitPreparedAction(WaitRequest, EExecutionPhase::Inspecting);
	if (!Submit.IsSuccess())
	{
		FGameplayActionResult Failure;
		Failure.TerminalState = EGameplayActionState::Failed;
		Failure.ReasonTag = GameplayActionTags::Result_Failure_CannotStart;
		Failure.DiagnosticMessage = Submit.DiagnosticMessage;
		FinishInvestigationOnce(Failure);
	}
}

void UParadoxCloneInvestigationComponent::HandleInspectionFinished(
	const FGameplayActionResult& Result)
{
	FinishInvestigationOnce(Result);
}

void UParadoxCloneInvestigationComponent::OrientTowardInvestigation()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	FVector Direction = ActiveContext.InvestigationLocation - Owner->GetActorLocation();
	Direction.Z = 0.0f;
	if (!Direction.IsNearlyZero())
	{
		Owner->SetActorRotation(Direction.Rotation());
	}
}

void UParadoxCloneInvestigationComponent::FinishInvestigationOnce(
	const FGameplayActionResult& Result)
{
	if (bCompletionBroadcast)
	{
		return;
	}
	bCompletionBroadcast = true;
	const FParadoxInvestigationContext CompletedContext = ActiveContext;
	ResetExecution(true);
	InvestigationFinishedNative.Broadcast(CompletedContext, Result);
}

void UParadoxCloneInvestigationComponent::ResetExecution(const bool bKeepContext)
{
	bActive = false;
	ActiveActionHandle = FGameplayActionHandle();
	ActiveCorrelationId.Invalidate();
	ExecutionPhase = EExecutionPhase::None;
	if (!bKeepContext)
	{
		ActiveContext = FParadoxInvestigationContext();
	}
}

FParadoxCloneBehaviorOperationResult
UParadoxCloneInvestigationComponent::MakeResult(
	const EParadoxCloneBehaviorOperationStatus Status,
	FString Diagnostic,
	const FName Reason) const
{
	FParadoxCloneBehaviorOperationResult Result;
	Result.Status = Status;
	Result.Reason = Reason;
	Result.DiagnosticMessage = MoveTemp(Diagnostic);
	Result.InvestigationRevision = ActiveContext.InvestigationRevision;
	return Result;
}

void UParadoxCloneInvestigationComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	const FGameplayActionHandle Handle = ActiveActionHandle;
	ResetExecution(false);
	if (ActionComponent && Handle.IsValid())
	{
		ActionComponent->CancelAction(
			Handle,
			GameplayActionTags::Result_Cancelled_ByRequester);
	}
	if (ActionComponent && ActionEndedHandle.IsValid())
	{
		ActionComponent->OnActionEndedNative().Remove(ActionEndedHandle);
	}
	ActionEndedHandle.Reset();
	ActionComponent = nullptr;
	Super::EndPlay(EndPlayReason);
}
