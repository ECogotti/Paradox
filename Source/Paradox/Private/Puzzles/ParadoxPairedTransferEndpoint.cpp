#include "Puzzles/ParadoxPairedTransferEndpoint.h"

#include "Actions/GameplayActionInstance.h"
#include "Components/ArrowComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimerManager.h"
#include "Types/GameplayActionTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxPairedTransferEndpoint"

namespace
{
	FString GetObjectDiagnosticName(const UObject* Object)
	{
		return IsValid(Object) ? Object->GetPathName() : TEXT("<invalid>");
	}
}

AParadoxPairedTransferEndpoint::AParadoxPairedTransferEndpoint()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Static);

	TransferAnchor = CreateDefaultSubobject<UArrowComponent>(TEXT("TransferAnchor"));
	TransferAnchor->SetupAttachment(SceneRoot);
	TransferAnchor->SetArrowColor(FColor::Cyan);
	TransferAnchor->SetArrowSize(1.5f);
	TransferAnchor->SetIsScreenSizeScaled(true);

	PuzzleReceiver = CreateDefaultSubobject<UPuzzleReceiverComponent>(TEXT("PuzzleReceiver"));
	PuzzleReceiver->bActivateWhenUncontrolled = true;
}

void AParadoxPairedTransferEndpoint::BeginPlay()
{
	Super::BeginPlay();

	bRuntimeInitialized = true;
	CachedInteractionComponent = FindComponentByClass<UParadoxInteractionComponent>();
	BindLinkedEndpointObservers();
	BindWorldStateObservers();
	NotifyInteractionAffordanceChanged();
	LogDebugState(TEXT("Initialized"));
}

void AParadoxPairedTransferEndpoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bRuntimeInitialized = false;
	bWorldStateRestoreInProgress = true;

	if (IsTransferInProgress())
	{
		if (TransferState == EParadoxTransferEndpointState::Receiving)
		{
			if (AParadoxPairedTransferEndpoint* Source = ActiveTransferSource.Get(true))
			{
				Source->InternalCancelTransfer(EParadoxTransferCancellationReason::EndpointDestroyed, true);
			}
		}
		else
		{
			InternalCancelTransfer(EParadoxTransferCancellationReason::EndPlay, false);
		}
	}

	if (CurrentTransferSubject)
	{
		CurrentTransferSubject->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleCurrentSubjectDestroyed);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransferOutTimerHandle);
		World->GetTimerManager().ClearTimer(TransferInTimerHandle);
	}
	UnbindLinkedEndpointObservers();
	UnbindWorldStateObservers();

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult AParadoxPairedTransferEndpoint::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	auto AddError = [&Context, &Result](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (!SceneRoot || !TransferAnchor || !PuzzleReceiver)
	{
		AddError(LOCTEXT("MissingRequiredComponents", "A paired transfer endpoint requires its Scene Root, Transfer Anchor, and Puzzle Receiver components."));
	}
	if (!FMath::IsFinite(TransferOutDuration) || TransferOutDuration < 0.0f
		|| !FMath::IsFinite(TransferInDuration) || TransferInDuration < 0.0f)
	{
		AddError(LOCTEXT("InvalidDurations", "Transfer Out Duration and Transfer In Duration must be finite, non-negative values."));
	}

	if (!IsTemplate())
	{
		if (!IsValid(LinkedEndpoint))
		{
			AddError(LOCTEXT("MissingLinkedEndpoint", "A placed paired transfer endpoint requires a Linked Endpoint."));
		}
		else if (LinkedEndpoint == this)
		{
			AddError(LOCTEXT("SelfLinkedEndpoint", "A paired transfer endpoint cannot link to itself."));
		}
		else
		{
			if (LinkedEndpoint->LinkedEndpoint != this)
			{
				AddError(LOCTEXT("NonReciprocalPair", "Linked Endpoint must point back to this endpoint."));
			}
			if (GetWorld() && LinkedEndpoint->GetWorld() && GetWorld() != LinkedEndpoint->GetWorld())
			{
				AddError(LOCTEXT("DifferentWorld", "Paired endpoints must belong to the same World."));
			}

			FString Diagnostic;
			if (!IsLinkedEndpointCompatible(LinkedEndpoint, Diagnostic)
				|| !LinkedEndpoint->IsLinkedEndpointCompatible(this, Diagnostic))
			{
				AddError(FText::Format(
					LOCTEXT("IncompatiblePair", "Linked Endpoint is incompatible: {0}"),
					FText::FromString(Diagnostic)));
			}
		}
	}

	return Result;
}
#endif

FParadoxTransferOperationResult AParadoxPairedTransferEndpoint::EvaluateTransfer(
	AActor* TransferSubject,
	AActor* Requester,
	UGameplayActionInstance* RequestingAction) const
{
	return EvaluateTransferInternal(TransferSubject, Requester, RequestingAction, false);
}

bool AParadoxPairedTransferEndpoint::CanRequestTransfer(
	AActor* TransferSubject,
	AActor* Requester,
	UGameplayActionInstance* RequestingAction) const
{
	return EvaluateTransfer(TransferSubject, Requester, RequestingAction).IsSuccess();
}

FParadoxTransferOperationResult AParadoxPairedTransferEndpoint::RequestTransfer(
	AActor* TransferSubject,
	AActor* Requester,
	UGameplayActionInstance* RequestingAction)
{
	FParadoxTransferOperationResult Result = EvaluateTransferInternal(
		TransferSubject,
		Requester,
		RequestingAction,
		RequestingAction != nullptr);
	if (!Result.IsSuccess())
	{
		RecordOperationResult(Result);
		return Result;
	}

	const FGuid OperationId = FGuid::NewGuid();
	if (!AcquirePair(TransferSubject, Requester, OperationId))
	{
		Result = MakeResult(
			EParadoxTransferOperationStatus::PhaseStartFailed,
			TEXT("The pair changed while the transaction was being acquired."));
		RecordOperationResult(Result);
		return Result;
	}

	const FParadoxTransferOperationContext Context = BuildCurrentContext();
	BroadcastPairAcquired(Context);
	if (!IsPairTransactionConsistent())
	{
		Result = MakeResult(
			EParadoxTransferOperationStatus::PhaseStartFailed,
			TEXT("The pair was released during the transfer-request notification."),
			OperationId);
		RecordOperationResult(Result);
		return Result;
	}

	FString Diagnostic;
	if (!PrepareSubjectForTransfer(Context, Diagnostic))
	{
		Result = MakeResult(
			EParadoxTransferOperationStatus::PreparationFailed,
			Diagnostic.IsEmpty() ? TEXT("The source failed to prepare the transfer subject.") : MoveTemp(Diagnostic),
			OperationId);
		RecordOperationResult(Result);
		InternalCancelTransfer(EParadoxTransferCancellationReason::PreparationFailed, true);
		return Result;
	}

	if (!StartTransferOutPhase())
	{
		Result = MakeResult(
			EParadoxTransferOperationStatus::PhaseStartFailed,
			TEXT("The transfer-out phase could not be started."),
			OperationId);
		RecordOperationResult(Result);
		InternalCancelTransfer(EParadoxTransferCancellationReason::PhaseStartFailed, true);
		return Result;
	}

	Result = MakeResult(EParadoxTransferOperationStatus::Succeeded, TEXT("The paired transfer transaction was acquired."), OperationId);
	RecordOperationResult(Result);
	return Result;
}

FParadoxTransferOperationResult AParadoxPairedTransferEndpoint::CancelTransfer(const FGuid OperationId)
{
	AParadoxPairedTransferEndpoint* Source = TransferState == EParadoxTransferEndpointState::Receiving
		? ActiveTransferSource.Get()
		: this;
	if (!IsValid(Source) || !Source->IsTransferInProgress())
	{
		return MakeResult(EParadoxTransferOperationStatus::NoActiveTransfer, TEXT("No paired transfer is active."));
	}
	if (!OperationId.IsValid() || Source->CurrentOperationId != OperationId)
	{
		return MakeResult(EParadoxTransferOperationStatus::OperationMismatch, TEXT("The operation ID does not identify the active transfer."));
	}

	Source->InternalCancelTransfer(EParadoxTransferCancellationReason::Requested, true);
	const FParadoxTransferOperationResult Result = MakeResult(
		EParadoxTransferOperationStatus::Succeeded,
		TEXT("The paired transfer was cancelled."),
		OperationId);
	Source->RecordOperationResult(Result);
	return Result;
}

bool AParadoxPairedTransferEndpoint::ResetTransferEndpoint()
{
	if (!IsTransferInProgress())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TransferOutTimerHandle);
			World->GetTimerManager().ClearTimer(TransferInTimerHandle);
		}
		return false;
	}

	AParadoxPairedTransferEndpoint* Source = TransferState == EParadoxTransferEndpointState::Receiving
		? ActiveTransferSource.Get()
		: this;
	return IsValid(Source)
		&& Source->InternalCancelTransfer(EParadoxTransferCancellationReason::Requested, true);
}

FTransform AParadoxPairedTransferEndpoint::GetTransferAnchorTransform() const
{
	return TransferAnchor ? TransferAnchor->GetComponentTransform() : GetActorTransform();
}

void AParadoxPairedTransferEndpoint::SetTransferDebugEnabled(const bool bInEnableDebug)
{
	bEnableDebug = bInEnableDebug;
	if (bEnableDebug)
	{
		LogDebugState(TEXT("DebugEnabled"));
		DrawTransferDebug();
	}
}

bool AParadoxPairedTransferEndpoint::CanTransferSubject_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	FString& OutDiagnostic) const
{
	(void)Requester;
	if (!IsValid(TransferSubject))
	{
		OutDiagnostic = TEXT("The transfer subject is invalid.");
		return false;
	}
	const USceneComponent* SubjectRoot = TransferSubject->GetRootComponent();
	if (!IsValid(SubjectRoot))
	{
		OutDiagnostic = TEXT("The transfer subject has no root scene component.");
		return false;
	}
	if (SubjectRoot->Mobility != EComponentMobility::Movable)
	{
		OutDiagnostic = TEXT("The transfer subject root component must be Movable.");
		return false;
	}
	if (TransferSubject->GetAttachParentActor() != nullptr)
	{
		OutDiagnostic = TEXT("The generic transfer policy accepts only unattached Actors.");
		return false;
	}
	return true;
}

bool AParadoxPairedTransferEndpoint::CanRequestTransferWithAction_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	UGameplayActionInstance* RequestingAction,
	FString& OutDiagnostic) const
{
	(void)TransferSubject;
	(void)Requester;
	(void)RequestingAction;
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxPairedTransferEndpoint::CanSourceStartTransfer_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	AParadoxPairedTransferEndpoint* Destination,
	FString& OutDiagnostic) const
{
	(void)TransferSubject;
	(void)Requester;
	(void)Destination;
	(void)OutDiagnostic;
	return true;
}

bool AParadoxPairedTransferEndpoint::CanDestinationReceiveTransfer_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	AParadoxPairedTransferEndpoint* Source,
	FString& OutDiagnostic) const
{
	(void)TransferSubject;
	(void)Requester;
	(void)Source;
	(void)OutDiagnostic;
	return true;
}

bool AParadoxPairedTransferEndpoint::PrepareSubjectForTransfer_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	(void)Context;
	(void)OutDiagnostic;
	return true;
}

bool AParadoxPairedTransferEndpoint::PerformTransferCommit_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	if (!IsValid(Context.Subject) || !IsValid(Context.Destination))
	{
		OutDiagnostic = TEXT("The subject or destination became invalid before commit.");
		return false;
	}
	if (!Context.Subject->SetActorTransform(
		Context.Destination->GetTransferAnchorTransform(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics))
	{
		OutDiagnostic = TEXT("SetActorTransform rejected the destination anchor transform.");
		return false;
	}
	return true;
}

bool AParadoxPairedTransferEndpoint::FinalizeTransferredSubject_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	(void)Context;
	(void)OutDiagnostic;
	return true;
}

void AParadoxPairedTransferEndpoint::HandleTransferCancelled_Implementation(
	const FParadoxTransferOperationContext& Context,
	const EParadoxTransferCancellationReason Reason,
	const bool bWasCommitted)
{
	(void)Context;
	(void)Reason;
	(void)bWasCommitted;
}

bool AParadoxPairedTransferEndpoint::IsLinkedEndpointCompatible(
	const AParadoxPairedTransferEndpoint* Candidate,
	FString& OutDiagnostic) const
{
	if (!IsValid(Candidate) || Candidate == this)
	{
		OutDiagnostic = TEXT("The candidate endpoint is invalid or self-referential.");
		return false;
	}
	return true;
}

bool AParadoxPairedTransferEndpoint::CompleteTransferOut(const FGuid OperationId)
{
	if (!IsActiveSourceOperation(OperationId, EParadoxTransferPhase::TransferOut))
	{
		return false;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransferOutTimerHandle);
	}
	if (!IsPairTransactionConsistent())
	{
		InternalCancelTransfer(EParadoxTransferCancellationReason::InvalidPairState, true);
		return false;
	}
	if (!IsValid(CurrentTransferSubject))
	{
		InternalCancelTransfer(EParadoxTransferCancellationReason::SubjectDestroyed, true);
		return false;
	}

	ApplyPairPhase(EParadoxTransferPhase::Committing);
	const FParadoxTransferOperationContext Context = BuildCurrentContext();
	FString Diagnostic;
	if (!PerformTransferCommit(Context, Diagnostic))
	{
		PARADOX_LOG_ERROR(
			TEXT("Paired transfer commit failed. Source=%s Destination=%s Subject=%s Operation=%s Diagnostic=%s"),
			*GetObjectDiagnosticName(Context.Source),
			*GetObjectDiagnosticName(Context.Destination),
			*GetObjectDiagnosticName(Context.Subject),
			*Context.OperationId.ToString(),
			*Diagnostic);
		LastTransferDiagnostic = Diagnostic;
		InternalCancelTransfer(EParadoxTransferCancellationReason::CommitFailed, true);
		return false;
	}

	bTransferCommitted = true;
	if (AParadoxPairedTransferEndpoint* Destination = CurrentTransferPartner.Get())
	{
		Destination->bTransferCommitted = true;
	}
	BroadcastPairCommitted(Context);
	if (!StartTransferInPhase())
	{
		InternalCancelTransfer(EParadoxTransferCancellationReason::PhaseStartFailed, true);
		return false;
	}
	return true;
}

bool AParadoxPairedTransferEndpoint::CompleteTransferIn(const FGuid OperationId)
{
	if (TransferState == EParadoxTransferEndpointState::Receiving)
	{
		if (AParadoxPairedTransferEndpoint* Source = ActiveTransferSource.Get())
		{
			return Source != this && Source->CompleteTransferIn(OperationId);
		}
		return false;
	}
	if (!IsActiveSourceOperation(OperationId, EParadoxTransferPhase::TransferIn))
	{
		return false;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransferInTimerHandle);
	}
	if (!IsPairTransactionConsistent())
	{
		InternalCancelTransfer(EParadoxTransferCancellationReason::InvalidPairState, true);
		return false;
	}

	ApplyPairPhase(EParadoxTransferPhase::Finalizing);
	const FParadoxTransferOperationContext Context = BuildCurrentContext();
	FString Diagnostic;
	if (!FinalizeTransferredSubject(Context, Diagnostic))
	{
		LastTransferDiagnostic = Diagnostic;
		InternalCancelTransfer(EParadoxTransferCancellationReason::FinalizationFailed, true);
		return false;
	}

	ReleasePairState();
	BroadcastPairCompleted(Context);
	const FParadoxTransferOperationResult Result = MakeResult(
		EParadoxTransferOperationStatus::Succeeded,
		TEXT("The paired transfer completed."),
		OperationId);
	RecordOperationResult(Result);
	return true;
}

FParadoxTransferOperationResult AParadoxPairedTransferEndpoint::EvaluateTransferInternal(
	AActor* TransferSubject,
	AActor* Requester,
	UGameplayActionInstance* RequestingAction,
	const bool bRequireRunningAction) const
{
	auto Failure = [this](const EParadoxTransferOperationStatus Status, const TCHAR* Diagnostic)
	{
		return MakeResult(Status, Diagnostic);
	};

	if (!bRuntimeInitialized)
	{
		return Failure(EParadoxTransferOperationStatus::NotInitialized, TEXT("The endpoint has not entered play."));
	}
	if (bWorldStateRestoreInProgress)
	{
		return Failure(EParadoxTransferOperationStatus::ResetInProgress, TEXT("World State restore is in progress."));
	}
	if (!IsValid(Requester))
	{
		return Failure(EParadoxTransferOperationStatus::InvalidRequester, TEXT("The requester is invalid."));
	}
	FString Diagnostic;
	if (!ValidateRequesterAuthority(Requester, RequestingAction, bRequireRunningAction, Diagnostic))
	{
		return MakeResult(EParadoxTransferOperationStatus::RequesterBlocked, MoveTemp(Diagnostic));
	}
	if (!CanRequestTransferWithAction(TransferSubject, Requester, RequestingAction, Diagnostic))
	{
		return MakeResult(EParadoxTransferOperationStatus::RequesterBlocked, MoveTemp(Diagnostic));
	}
	if (!IsValid(TransferSubject) || TransferSubject == this || TransferSubject == LinkedEndpoint)
	{
		return Failure(EParadoxTransferOperationStatus::InvalidSubject, TEXT("The transfer subject is invalid or is one of the endpoints."));
	}
	if (!IsValid(LinkedEndpoint))
	{
		return Failure(EParadoxTransferOperationStatus::MissingLinkedEndpoint, TEXT("No valid Linked Endpoint is configured."));
	}
	if (LinkedEndpoint == this)
	{
		return Failure(EParadoxTransferOperationStatus::SelfLinkedEndpoint, TEXT("An endpoint cannot transfer to itself."));
	}
	if (LinkedEndpoint->LinkedEndpoint != this)
	{
		return Failure(EParadoxTransferOperationStatus::NonReciprocalPair, TEXT("Linked Endpoint does not point back to the source."));
	}
	if (GetWorld() != LinkedEndpoint->GetWorld())
	{
		return Failure(EParadoxTransferOperationStatus::DifferentWorld, TEXT("Paired endpoints must belong to the same World."));
	}
	if (!IsLinkedEndpointCompatible(LinkedEndpoint, Diagnostic)
		|| !LinkedEndpoint->IsLinkedEndpointCompatible(this, Diagnostic))
	{
		return MakeResult(EParadoxTransferOperationStatus::IncompatibleEndpoint, MoveTemp(Diagnostic));
	}
	if (!IsValid(PuzzleReceiver) || !PuzzleReceiver->IsReceiverActive())
	{
		return Failure(EParadoxTransferOperationStatus::SourceInactive, TEXT("The source Puzzle Receiver is inactive."));
	}
	if (!IsValid(LinkedEndpoint->PuzzleReceiver) || !LinkedEndpoint->PuzzleReceiver->IsReceiverActive())
	{
		return Failure(EParadoxTransferOperationStatus::DestinationInactive, TEXT("The destination Puzzle Receiver is inactive."));
	}
	if (TransferState != EParadoxTransferEndpointState::Idle)
	{
		return Failure(EParadoxTransferOperationStatus::SourceBusy, TEXT("The source endpoint is already participating in a transfer."));
	}
	if (LinkedEndpoint->TransferState != EParadoxTransferEndpointState::Idle)
	{
		return Failure(EParadoxTransferOperationStatus::DestinationBusy, TEXT("The destination endpoint is already participating in a transfer."));
	}
	if (!CanTransferSubject(TransferSubject, Requester, Diagnostic))
	{
		return MakeResult(EParadoxTransferOperationStatus::SubjectRejected, MoveTemp(Diagnostic));
	}
	if (!CanSourceStartTransfer(TransferSubject, Requester, LinkedEndpoint, Diagnostic))
	{
		return MakeResult(EParadoxTransferOperationStatus::SourceRejected, MoveTemp(Diagnostic));
	}
	if (!LinkedEndpoint->CanDestinationReceiveTransfer(TransferSubject, Requester, const_cast<AParadoxPairedTransferEndpoint*>(this), Diagnostic))
	{
		return MakeResult(EParadoxTransferOperationStatus::DestinationRejected, MoveTemp(Diagnostic));
	}

	return MakeResult(EParadoxTransferOperationStatus::Succeeded, TEXT("The transfer request is valid."));
}

FParadoxTransferOperationResult AParadoxPairedTransferEndpoint::MakeResult(
	const EParadoxTransferOperationStatus Status,
	FString Diagnostic,
	const FGuid OperationId) const
{
	FParadoxTransferOperationResult Result;
	Result.Status = Status;
	Result.OperationId = OperationId;
	Result.DiagnosticMessage = MoveTemp(Diagnostic);
	return Result;
}

bool AParadoxPairedTransferEndpoint::ValidateRequesterAuthority(
	AActor* Requester,
	UGameplayActionInstance* RequestingAction,
	const bool bRequireRunningAction,
	FString& OutDiagnostic) const
{
	UGameplayActionComponent* Actions = Requester->FindComponentByClass<UGameplayActionComponent>();
	if (!Actions)
	{
		if (RequestingAction)
		{
			OutDiagnostic = TEXT("The requester has no Gameplay Action Component for the supplied action.");
			return false;
		}
		return true;
	}
	if (!Actions->IsAcceptingSubmissions())
	{
		OutDiagnostic = TEXT("The requester's Gameplay Action Component is not operational.");
		return false;
	}
	if (Actions->IsActionsPaused())
	{
		OutDiagnostic = TEXT("The requester's Gameplay Action Component is paused.");
		return false;
	}
	if (Actions->IsExternalExecutionLockHeld(ParadoxGameplayTags::Lock_Interaction))
	{
		OutDiagnostic = TEXT("An external authority currently owns the Interaction execution lock.");
		return false;
	}

	FGameplayActionHandle RequestingHandle;
	if (RequestingAction)
	{
		if (RequestingAction->GetOwningComponent() != Actions)
		{
			OutDiagnostic = TEXT("The supplied Gameplay Action belongs to a different component.");
			return false;
		}
		if (!RequestingAction->GetExecutionLocks().HasTagExact(ParadoxGameplayTags::Lock_Interaction))
		{
			OutDiagnostic = TEXT("The supplied Gameplay Action does not own the Interaction execution lock.");
			return false;
		}
		if (bRequireRunningAction && RequestingAction->GetState() != EGameplayActionState::Running)
		{
			OutDiagnostic = TEXT("The supplied Gameplay Action is not Running.");
			return false;
		}
		RequestingHandle = RequestingAction->GetHandle();
	}

	for (const FGameplayActionDebugEntry& Entry : Actions->GetDebugSnapshot().ActiveActions)
	{
		if (RequestingHandle.IsValid() && Entry.Handle == RequestingHandle)
		{
			continue;
		}
		if (Entry.ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Interaction))
		{
			OutDiagnostic = TEXT("Another active Gameplay Action owns the Interaction execution lock.");
			return false;
		}
	}
	return true;
}

bool AParadoxPairedTransferEndpoint::AcquirePair(
	AActor* TransferSubject,
	AActor* Requester,
	const FGuid& OperationId)
{
	AParadoxPairedTransferEndpoint* Destination = LinkedEndpoint.Get();
	if (!IsValid(Destination)
		|| Destination == this
		|| Destination->LinkedEndpoint != this
		|| TransferState != EParadoxTransferEndpointState::Idle
		|| Destination->TransferState != EParadoxTransferEndpointState::Idle)
	{
		return false;
	}

	const EParadoxTransferEndpointState PreviousSourceState = TransferState;
	const EParadoxTransferEndpointState PreviousDestinationState = Destination->TransferState;

	TransferState = EParadoxTransferEndpointState::Sending;
	TransferPhase = EParadoxTransferPhase::Preparing;
	CurrentTransferSubject = TransferSubject;
	CurrentTransferRequester = Requester;
	CurrentTransferPartner = Destination;
	ActiveTransferSource = this;
	CurrentOperationId = OperationId;
	bTransferCommitted = false;

	Destination->TransferState = EParadoxTransferEndpointState::Receiving;
	Destination->TransferPhase = EParadoxTransferPhase::Preparing;
	Destination->CurrentTransferSubject = TransferSubject;
	Destination->CurrentTransferRequester = Requester;
	Destination->CurrentTransferPartner = this;
	Destination->ActiveTransferSource = this;
	Destination->CurrentOperationId = OperationId;
	Destination->bTransferCommitted = false;

	TransferSubject->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleCurrentSubjectDestroyed);
	BroadcastStateTransition(PreviousSourceState, TransferState);
	Destination->BroadcastStateTransition(PreviousDestinationState, Destination->TransferState);
	NotifyInteractionAffordanceChanged();
	Destination->NotifyInteractionAffordanceChanged();
	return true;
}

bool AParadoxPairedTransferEndpoint::StartTransferOutPhase()
{
	if (!IsPairTransactionConsistent())
	{
		return false;
	}
	ApplyPairPhase(EParadoxTransferPhase::TransferOut);
	if (TransferOutCompletionMode == EParadoxTransferPhaseCompletionMode::Timed
		&& !SchedulePhaseCompletion(
			EParadoxTransferPhase::TransferOut,
			TransferOutDuration,
			CurrentOperationId))
	{
		return false;
	}
	const FParadoxTransferOperationContext Context = BuildCurrentContext();
	OnTransferOutStarted.Broadcast(Context);
	ReceiveTransferOutStarted(Context);
	LogDebugState(TEXT("TransferOutStarted"));
	DrawTransferDebug();
	return true;
}

bool AParadoxPairedTransferEndpoint::StartTransferInPhase()
{
	if (!IsPairTransactionConsistent())
	{
		return false;
	}
	ApplyPairPhase(EParadoxTransferPhase::TransferIn);
	AParadoxPairedTransferEndpoint* Destination = CurrentTransferPartner.Get();
	if (Destination->TransferInCompletionMode
			== EParadoxTransferPhaseCompletionMode::Timed
		&& !SchedulePhaseCompletion(
			EParadoxTransferPhase::TransferIn,
			Destination->TransferInDuration,
			CurrentOperationId))
	{
		return false;
	}
	const FParadoxTransferOperationContext Context = BuildCurrentContext();
	Destination->OnTransferInStarted.Broadcast(Context);
	Destination->ReceiveTransferInStarted(Context);
	Destination->LogDebugState(TEXT("TransferInStarted"));
	Destination->DrawTransferDebug();
	return true;
}

bool AParadoxPairedTransferEndpoint::SchedulePhaseCompletion(
	const EParadoxTransferPhase Phase,
	const float Duration,
	const FGuid& OperationId)
{
	UWorld* World = GetWorld();
	if (!World || !FMath::IsFinite(Duration) || Duration < 0.0f)
	{
		return false;
	}

	FTimerDelegate Delegate;
	FTimerHandle* TimerHandle = nullptr;
	if (Phase == EParadoxTransferPhase::TransferOut)
	{
		Delegate = FTimerDelegate::CreateUObject(this, &ThisClass::HandleTransferOutTimerElapsed, OperationId);
		TimerHandle = &TransferOutTimerHandle;
	}
	else if (Phase == EParadoxTransferPhase::TransferIn)
	{
		Delegate = FTimerDelegate::CreateUObject(this, &ThisClass::HandleTransferInTimerElapsed, OperationId);
		TimerHandle = &TransferInTimerHandle;
	}
	else
	{
		return false;
	}

	World->GetTimerManager().ClearTimer(*TimerHandle);
	if (Duration <= 0.0f)
	{
		*TimerHandle = World->GetTimerManager().SetTimerForNextTick(Delegate);
	}
	else
	{
		World->GetTimerManager().SetTimer(*TimerHandle, Delegate, Duration, false);
	}
	return TimerHandle->IsValid();
}

void AParadoxPairedTransferEndpoint::HandleTransferOutTimerElapsed(const FGuid ExpectedOperationId)
{
	CompleteTransferOut(ExpectedOperationId);
}

void AParadoxPairedTransferEndpoint::HandleTransferInTimerElapsed(const FGuid ExpectedOperationId)
{
	CompleteTransferIn(ExpectedOperationId);
}

bool AParadoxPairedTransferEndpoint::IsActiveSourceOperation(
	const FGuid& OperationId,
	const EParadoxTransferPhase ExpectedPhase) const
{
	return OperationId.IsValid()
		&& CurrentOperationId == OperationId
		&& ActiveTransferSource.Get() == this
		&& TransferState == EParadoxTransferEndpointState::Sending
		&& TransferPhase == ExpectedPhase;
}

bool AParadoxPairedTransferEndpoint::IsPairTransactionConsistent() const
{
	const AParadoxPairedTransferEndpoint* Destination = CurrentTransferPartner.Get();
	return IsValid(Destination)
		&& Destination != this
		&& LinkedEndpoint == Destination
		&& Destination->LinkedEndpoint == this
		&& TransferState == EParadoxTransferEndpointState::Sending
		&& Destination->TransferState == EParadoxTransferEndpointState::Receiving
		&& ActiveTransferSource.Get() == this
		&& Destination->ActiveTransferSource.Get() == this
		&& Destination->CurrentTransferPartner.Get() == this
		&& CurrentOperationId.IsValid()
		&& Destination->CurrentOperationId == CurrentOperationId
		&& Destination->CurrentTransferSubject == CurrentTransferSubject
		&& Destination->TransferPhase == TransferPhase;
}

bool AParadoxPairedTransferEndpoint::InternalCancelTransfer(
	EParadoxTransferCancellationReason Reason,
	const bool bNotifyObservers)
{
	if (TransferState == EParadoxTransferEndpointState::Receiving)
	{
		if (AParadoxPairedTransferEndpoint* Source = ActiveTransferSource.Get(true))
		{
			if (Source != this)
			{
				return Source->InternalCancelTransfer(Reason, bNotifyObservers);
			}
		}
	}
	if (TransferState != EParadoxTransferEndpointState::Sending || !CurrentOperationId.IsValid())
	{
		return false;
	}

	AParadoxPairedTransferEndpoint* Destination = CurrentTransferPartner.Get(true);
	if (Reason == EParadoxTransferCancellationReason::WorldStateRestore)
	{
		bWorldStateRestoreInProgress = true;
		if (Destination)
		{
			Destination->bWorldStateRestoreInProgress = true;
		}
	}

	const FParadoxTransferOperationContext Context = BuildCurrentContext();
	const bool bWasCommitted = bTransferCommitted;
	if (bWasCommitted && IsValid(Context.Subject)
		&& Reason != EParadoxTransferCancellationReason::SubjectDestroyed
		&& Reason != EParadoxTransferCancellationReason::EndpointDestroyed
		&& Reason != EParadoxTransferCancellationReason::EndPlay)
	{
		FString Diagnostic;
		if (!FinalizeTransferredSubject(Context, Diagnostic))
		{
			Reason = EParadoxTransferCancellationReason::FinalizationFailed;
			LastTransferDiagnostic = MoveTemp(Diagnostic);
		}
	}

	LastCancellationReason = Reason;
	if (Destination)
	{
		Destination->LastCancellationReason = Reason;
	}
	ReleasePairState();
	if (bNotifyObservers)
	{
		HandleTransferCancelled(Context, Reason, bWasCommitted);
		BroadcastPairCancelled(Context, Reason, bWasCommitted);
	}
	else
	{
		HandleTransferCancelled_Implementation(Context, Reason, bWasCommitted);
	}
	LogDebugState(TEXT("Cancelled"), StaticEnum<EParadoxTransferCancellationReason>()->GetNameStringByValue(static_cast<int64>(Reason)));
	return true;
}

void AParadoxPairedTransferEndpoint::ReleasePairState()
{
	AParadoxPairedTransferEndpoint* Destination = CurrentTransferPartner.Get(true);
	AActor* Subject = CurrentTransferSubject.Get();
	if (Subject)
	{
		Subject->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleCurrentSubjectDestroyed);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TransferOutTimerHandle);
		World->GetTimerManager().ClearTimer(TransferInTimerHandle);
	}

	const EParadoxTransferEndpointState PreviousSourceState = TransferState;
	const EParadoxTransferEndpointState PreviousDestinationState = Destination
		? Destination->TransferState
		: EParadoxTransferEndpointState::Idle;

	TransferState = EParadoxTransferEndpointState::Idle;
	TransferPhase = EParadoxTransferPhase::None;
	CurrentTransferSubject = nullptr;
	CurrentTransferRequester = nullptr;
	CurrentTransferPartner.Reset();
	ActiveTransferSource.Reset();
	CurrentOperationId.Invalidate();
	bTransferCommitted = false;

	if (Destination)
	{
		Destination->TransferState = EParadoxTransferEndpointState::Idle;
		Destination->TransferPhase = EParadoxTransferPhase::None;
		Destination->CurrentTransferSubject = nullptr;
		Destination->CurrentTransferRequester = nullptr;
		Destination->CurrentTransferPartner.Reset();
		Destination->ActiveTransferSource.Reset();
		Destination->CurrentOperationId.Invalidate();
		Destination->bTransferCommitted = false;
	}

	if (PreviousSourceState != EParadoxTransferEndpointState::Idle)
	{
		BroadcastStateTransition(PreviousSourceState, EParadoxTransferEndpointState::Idle);
	}
	NotifyInteractionAffordanceChanged();
	if (IsValid(Destination) && Destination->bRuntimeInitialized)
	{
		if (PreviousDestinationState != EParadoxTransferEndpointState::Idle)
		{
			Destination->BroadcastStateTransition(PreviousDestinationState, EParadoxTransferEndpointState::Idle);
		}
		Destination->NotifyInteractionAffordanceChanged();
	}
}

void AParadoxPairedTransferEndpoint::ApplyPairPhase(const EParadoxTransferPhase NewPhase)
{
	TransferPhase = NewPhase;
	if (AParadoxPairedTransferEndpoint* Destination = CurrentTransferPartner.Get())
	{
		Destination->TransferPhase = NewPhase;
	}
}

FParadoxTransferOperationContext AParadoxPairedTransferEndpoint::BuildCurrentContext() const
{
	const AParadoxPairedTransferEndpoint* Source = ActiveTransferSource.Get();
	if (!Source && TransferState == EParadoxTransferEndpointState::Sending)
	{
		Source = this;
	}

	FParadoxTransferOperationContext Context;
	Context.Source = const_cast<AParadoxPairedTransferEndpoint*>(Source);
	Context.Destination = Source ? Source->CurrentTransferPartner.Get() : nullptr;
	Context.Subject = IsValid(CurrentTransferSubject) ? CurrentTransferSubject.Get() : nullptr;
	Context.Requester = IsValid(CurrentTransferRequester) ? CurrentTransferRequester.Get() : nullptr;
	Context.OperationId = CurrentOperationId;
	return Context;
}

void AParadoxPairedTransferEndpoint::BroadcastPairAcquired(const FParadoxTransferOperationContext& Context)
{
	OnTransferRequested.Broadcast(Context);
	ReceiveTransferRequested(Context);
	if (IsValid(Context.Destination) && Context.Destination->bRuntimeInitialized)
	{
		Context.Destination->OnTransferRequested.Broadcast(Context);
		Context.Destination->ReceiveTransferRequested(Context);
	}
	LogDebugState(TEXT("Acquired"));
	DrawTransferDebug();
}

void AParadoxPairedTransferEndpoint::BroadcastPairCommitted(const FParadoxTransferOperationContext& Context)
{
	OnTransferCommitted.Broadcast(Context);
	ReceiveTransferCommitted(Context);
	if (IsValid(Context.Destination) && Context.Destination->bRuntimeInitialized)
	{
		Context.Destination->OnTransferCommitted.Broadcast(Context);
		Context.Destination->ReceiveTransferCommitted(Context);
	}
	LogDebugState(TEXT("Committed"));
	DrawTransferDebug();
}

void AParadoxPairedTransferEndpoint::BroadcastPairCompleted(const FParadoxTransferOperationContext& Context)
{
	OnTransferCompleted.Broadcast(Context);
	ReceiveTransferCompleted(Context);
	if (IsValid(Context.Destination) && Context.Destination->bRuntimeInitialized)
	{
		Context.Destination->OnTransferCompleted.Broadcast(Context);
		Context.Destination->ReceiveTransferCompleted(Context);
	}
	LogDebugState(TEXT("Completed"));
}

void AParadoxPairedTransferEndpoint::BroadcastPairCancelled(
	const FParadoxTransferOperationContext& Context,
	const EParadoxTransferCancellationReason Reason,
	const bool bWasCommitted)
{
	OnTransferCancelled.Broadcast(Context, Reason, bWasCommitted);
	ReceiveTransferCancelled(Context, Reason, bWasCommitted);
	if (IsValid(Context.Destination) && Context.Destination->bRuntimeInitialized)
	{
		Context.Destination->OnTransferCancelled.Broadcast(Context, Reason, bWasCommitted);
		Context.Destination->ReceiveTransferCancelled(Context, Reason, bWasCommitted);
	}
}

void AParadoxPairedTransferEndpoint::BroadcastStateTransition(
	const EParadoxTransferEndpointState PreviousState,
	const EParadoxTransferEndpointState NewState)
{
	OnTransferStateChanged.Broadcast(this, PreviousState, NewState);
}

void AParadoxPairedTransferEndpoint::NotifyInteractionAffordanceChanged()
{
	if (UParadoxInteractionComponent* Interaction = CachedInteractionComponent.Get())
	{
		Interaction->NotifyInteractionAffordanceChanged();
	}
}

void AParadoxPairedTransferEndpoint::BindLinkedEndpointObservers()
{
	UnbindLinkedEndpointObservers();
	if (IsValid(PuzzleReceiver))
	{
		OwnReceiverStateChangedHandle = PuzzleReceiver->OnReceiverStateChangedNative.AddUObject(
			this,
			&ThisClass::HandleLinkedReceiverStateChanged);
	}
	if (!IsValid(LinkedEndpoint))
	{
		return;
	}

	BoundLinkedEndpoint = LinkedEndpoint;
	LinkedEndpoint->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleLinkedEndpointDestroyed);
	if (IsValid(LinkedEndpoint->PuzzleReceiver))
	{
		BoundLinkedReceiver = LinkedEndpoint->PuzzleReceiver;
		LinkedReceiverStateChangedHandle = LinkedEndpoint->PuzzleReceiver->OnReceiverStateChangedNative.AddUObject(
			this,
			&ThisClass::HandleLinkedReceiverStateChanged);
	}
}

void AParadoxPairedTransferEndpoint::UnbindLinkedEndpointObservers()
{
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverStateChangedNative.Remove(OwnReceiverStateChangedHandle);
	}
	if (AParadoxPairedTransferEndpoint* Endpoint = BoundLinkedEndpoint.Get(true))
	{
		Endpoint->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleLinkedEndpointDestroyed);
	}
	if (UPuzzleReceiverComponent* Receiver = BoundLinkedReceiver.Get(true))
	{
		Receiver->OnReceiverStateChangedNative.Remove(LinkedReceiverStateChangedHandle);
	}
	BoundLinkedEndpoint.Reset();
	BoundLinkedReceiver.Reset();
	OwnReceiverStateChangedHandle.Reset();
	LinkedReceiverStateChangedHandle.Reset();
}

void AParadoxPairedTransferEndpoint::BindWorldStateObservers()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldStateRestoreStartedHandle = WorldState->OnRestoreStartedNative().AddUObject(
				this,
				&ThisClass::HandleWorldStateRestoreStarted);
			WorldStateRestoreCompletedHandle = WorldState->OnRestoreCompletedNative().AddUObject(
				this,
				&ThisClass::HandleWorldStateRestoreCompleted);
			WorldStateRestoreFailedHandle = WorldState->OnRestoreFailedNative().AddUObject(
				this,
				&ThisClass::HandleWorldStateRestoreFailed);
		}
	}
}

void AParadoxPairedTransferEndpoint::UnbindWorldStateObservers()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldState->OnRestoreStartedNative().Remove(WorldStateRestoreStartedHandle);
			WorldState->OnRestoreCompletedNative().Remove(WorldStateRestoreCompletedHandle);
			WorldState->OnRestoreFailedNative().Remove(WorldStateRestoreFailedHandle);
		}
	}
	WorldStateRestoreStartedHandle.Reset();
	WorldStateRestoreCompletedHandle.Reset();
	WorldStateRestoreFailedHandle.Reset();
}

void AParadoxPairedTransferEndpoint::RecordOperationResult(const FParadoxTransferOperationResult& Result)
{
	LastOperationStatus = Result.Status;
	LastTransferDiagnostic = Result.DiagnosticMessage;
	if (!Result.IsSuccess())
	{
		LogDebugState(TEXT("Rejected"), Result.DiagnosticMessage);
	}
}

void AParadoxPairedTransferEndpoint::LogDebugState(const TCHAR* EventName, const FString& Diagnostic) const
{
	if (!ShouldDrawTransferDebug())
	{
		return;
	}
	PARADOX_LOG_INFO(
		TEXT("PairedTransfer %s Endpoint=%s Receiver=%s State=%s Phase=%s Partner=%s Subject=%s Operation=%s Diagnostic=%s"),
		EventName,
		*GetPathName(),
		PuzzleReceiver && PuzzleReceiver->IsReceiverActive() ? TEXT("Active") : TEXT("Inactive"),
		*StaticEnum<EParadoxTransferEndpointState>()->GetNameStringByValue(static_cast<int64>(TransferState)),
		*StaticEnum<EParadoxTransferPhase>()->GetNameStringByValue(static_cast<int64>(TransferPhase)),
		*GetObjectDiagnosticName(CurrentTransferPartner.Get()),
		*GetObjectDiagnosticName(CurrentTransferSubject),
		*CurrentOperationId.ToString(),
		*Diagnostic);
}

void AParadoxPairedTransferEndpoint::DrawTransferDebug() const
{
#if ENABLE_DRAW_DEBUG
	if (!ShouldDrawTransferDebug() || !GetWorld())
	{
		return;
	}
	const FVector SourceLocation = GetTransferAnchorTransform().GetLocation();
	const FVector LabelLocation = SourceLocation + FVector::UpVector * DebugVerticalOffset;
	const FColor Color = TransferState == EParadoxTransferEndpointState::Idle ? FColor::Cyan : FColor::Yellow;
	DrawDebugString(
		GetWorld(),
		LabelLocation,
		FString::Printf(TEXT("%s / %s"),
			*StaticEnum<EParadoxTransferEndpointState>()->GetNameStringByValue(static_cast<int64>(TransferState)),
			*StaticEnum<EParadoxTransferPhase>()->GetNameStringByValue(static_cast<int64>(TransferPhase))),
		nullptr,
		Color,
		0.0f,
		true);
	if (IsValid(LinkedEndpoint))
	{
		DrawDebugDirectionalArrow(
			GetWorld(),
			SourceLocation,
			LinkedEndpoint->GetTransferAnchorTransform().GetLocation(),
			40.0f,
			Color,
			false,
			0.0f,
			0,
			2.0f);
	}
#endif
}

bool AParadoxPairedTransferEndpoint::ShouldDrawTransferDebug() const
{
	return bEnableDebug && IsParadoxPairedTransferDebugEnabled();
}

void AParadoxPairedTransferEndpoint::HandleCurrentSubjectDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == CurrentTransferSubject)
	{
		InternalCancelTransfer(EParadoxTransferCancellationReason::SubjectDestroyed, true);
	}
}

void AParadoxPairedTransferEndpoint::HandleLinkedEndpointDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor != BoundLinkedEndpoint.Get(true))
	{
		return;
	}
	if (IsTransferInProgress())
	{
		InternalCancelTransfer(EParadoxTransferCancellationReason::EndpointDestroyed, true);
	}
	NotifyInteractionAffordanceChanged();
}

void AParadoxPairedTransferEndpoint::HandleLinkedReceiverStateChanged(
	UPuzzleReceiverComponent* Receiver,
	const bool bIsActive)
{
	(void)Receiver;
	(void)bIsActive;
	NotifyInteractionAffordanceChanged();
	LogDebugState(TEXT("ReceiverStateChanged"));
	DrawTransferDebug();
}

void AParadoxPairedTransferEndpoint::HandleWorldStateRestoreStarted(
	const FWorldStateRestoreLifecycleContext& Context)
{
	(void)Context;
	bWorldStateRestoreInProgress = true;
	if (IsTransferInProgress())
	{
		InternalCancelTransfer(EParadoxTransferCancellationReason::WorldStateRestore, true);
	}
	NotifyInteractionAffordanceChanged();
}

void AParadoxPairedTransferEndpoint::HandleWorldStateRestoreCompleted(
	const FWorldStateRestoreResult& Result)
{
	(void)Result;
	bWorldStateRestoreInProgress = false;
	NotifyInteractionAffordanceChanged();
}

void AParadoxPairedTransferEndpoint::HandleWorldStateRestoreFailed(
	const FWorldStateRestoreResult& Result)
{
	(void)Result;
	bWorldStateRestoreInProgress = false;
	NotifyInteractionAffordanceChanged();
}

#undef LOCTEXT_NAMESPACE
