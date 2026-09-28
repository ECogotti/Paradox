#include "Puzzles/ParadoxTeleportGateInteractionAction.h"

#include "Characters/ParadoxCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayActionTags.h"
#include "Paradox.h"
#include "Puzzles/ParadoxTeleportGate.h"

namespace UE::Paradox::TeleportGateInteraction::Private
{
	constexpr float TunnelMoveTimeoutMultiplier = 2.0f;
	constexpr float MinimumTunnelMoveTimeout = 0.01f;

	FGameplayTag FailureTagForStatus(const EParadoxTransferOperationStatus Status)
	{
		switch (Status)
		{
		case EParadoxTransferOperationStatus::InvalidRequester:
		case EParadoxTransferOperationStatus::RequesterBlocked:
		case EParadoxTransferOperationStatus::InvalidSubject:
			return ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		case EParadoxTransferOperationStatus::MissingLinkedEndpoint:
		case EParadoxTransferOperationStatus::SelfLinkedEndpoint:
		case EParadoxTransferOperationStatus::NonReciprocalPair:
		case EParadoxTransferOperationStatus::IncompatibleEndpoint:
		case EParadoxTransferOperationStatus::DifferentWorld:
			return ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable;
		default:
			return ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
		}
	}

	FGameplayTag FailureTagForCancellation(const EParadoxTransferCancellationReason Reason)
	{
		switch (Reason)
		{
		case EParadoxTransferCancellationReason::SubjectDestroyed:
		case EParadoxTransferCancellationReason::EndpointDestroyed:
		case EParadoxTransferCancellationReason::EndPlay:
			return ParadoxGameplayTags::Result_Failure_Interaction_TargetUnavailable;
		default:
			return ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable;
		}
	}
}

bool UParadoxEnterTeleportGateInteractionAction::CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	AParadoxTeleportGate* Gate = Cast<AParadoxTeleportGate>(GetInteractionTarget());
	if (!Character || !Gate)
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		OutDiagnostic = TEXT("Enter requires a Paradox Character requester and Teleport Gate target.");
		return false;
	}
	const FParadoxTransferOperationResult Result =
		Gate->EvaluateEnter(Character, const_cast<ThisClass*>(this));
	if (Result.IsSuccess())
	{
		return true;
	}
	OutFailureReason =
		UE::Paradox::TeleportGateInteraction::Private::FailureTagForStatus(Result.Status);
	OutDiagnostic = Result.DiagnosticMessage;
	return false;
}

bool UParadoxEnterTeleportGateInteractionAction::IsInteractionExecutionPending_Implementation() const
{
	return ActiveOperationId.IsValid() && ActiveGate.IsValid();
}

void UParadoxEnterTeleportGateInteractionAction::ExecuteInteraction_Implementation()
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	AParadoxTeleportGate* Gate = Cast<AParadoxTeleportGate>(GetInteractionTarget());
	const FParadoxTransferOperationResult Result = Gate && Character
		? Gate->TryEnter(Character, this)
		: FParadoxTransferOperationResult();
	if (!Result.IsSuccess())
	{
		PARADOX_LOG_WARNING(
			TEXT("Teleport Gate Enter action '%s' could not acquire Gate '%s' for Character '%s': status=%s diagnostic=%s"),
			*GetNameSafe(this),
			*GetNameSafe(Gate),
			*GetNameSafe(Character),
			*UEnum::GetValueAsString(Result.Status),
			*Result.DiagnosticMessage);
		CompleteInteractionFailure(
			UE::Paradox::TeleportGateInteraction::Private::FailureTagForStatus(Result.Status),
			Result.DiagnosticMessage);
		return;
	}
	BindTransfer(*Gate, Result.OperationId);

	FVector IngressTarget = FVector::ZeroVector;
	FString Diagnostic;
	if (!Gate->GetActiveTunnelTarget(Result.OperationId, false, IngressTarget)
		|| !StartTunnelMovement(ETunnelMoveStage::Ingress, IngressTarget, Diagnostic))
	{
		FailTunnelTraversal(
			Diagnostic.IsEmpty()
				? TEXT("Teleport Gate could not start the forced ingress movement.")
				: Diagnostic);
	}
}

void UParadoxEnterTeleportGateInteractionAction::OnActionPaused_Implementation()
{
	if (TunnelMoveTimeoutHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().PauseTimer(TunnelMoveTimeoutHandle);
		}
	}
	Super::OnActionPaused_Implementation();
}

void UParadoxEnterTeleportGateInteractionAction::OnActionResumed_Implementation()
{
	Super::OnActionResumed_Implementation();
	if (TunnelMoveTimeoutHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().UnPauseTimer(TunnelMoveTimeoutHandle);
		}
	}
}

void UParadoxEnterTeleportGateInteractionAction::OnActionTick_Implementation(
	const float DeltaSeconds)
{
	Super::OnActionTick_Implementation(DeltaSeconds);
	if (ActiveTunnelMoveStage == ETunnelMoveStage::None)
	{
		return;
	}

	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	if (!Character || !ActiveGate.IsValid() || !ActiveOperationId.IsValid())
	{
		FailTunnelTraversal(
			TEXT("Teleport Gate transaction disappeared during forced tunnel movement."));
		return;
	}

	const float AcceptanceRadius = FMath::Max(
		0.1f,
		ActiveGate->TunnelTraversalAcceptanceRadius);
	const FVector CharacterLocation = Character->GetActorLocation();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		FailTunnelTraversal(
			TEXT("Character Movement disappeared during forced tunnel movement."));
		return;
	}
	FVector HorizontalOffset = ActiveTunnelMoveGoal - CharacterLocation;
	HorizontalOffset.Z = 0.0f;
	if (HorizontalOffset.SizeSquared()
		<= FMath::Square(AcceptanceRadius))
	{
		const ETunnelMoveStage CompletedStage = ActiveTunnelMoveStage;
		ReleaseTunnelMovement();
		HandleTunnelStageSucceeded(CompletedStage);
		return;
	}

	const FVector MoveDirection = HorizontalOffset.GetSafeNormal();
	if (!MoveDirection.IsNearlyZero())
	{
		Character->AddMovementInput(MoveDirection, 1.0f, true);
	}
}

void UParadoxEnterTeleportGateInteractionAction::OnActionCleanup_Implementation()
{
	AParadoxTeleportGate* Gate = ActiveGate.Get();
	const FGuid OperationId = ActiveOperationId;
	bCleaningUpTransfer = true;
	ReleaseTunnelMovement();
	UnbindTransfer();
	if (Gate && OperationId.IsValid())
	{
		Gate->CancelTransfer(OperationId);
	}
	bCleaningUpTransfer = false;
	Super::OnActionCleanup_Implementation();
}

bool UParadoxEnterTeleportGateInteractionAction::StartTunnelMovement(
	const ETunnelMoveStage Stage,
	const FVector& GoalLocation,
	FString& OutDiagnostic)
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetInteractionRequester());
	AParadoxTeleportGate* Gate = ActiveGate.Get();
	UCharacterMovementComponent* Movement = Character
		? Character->GetCharacterMovement()
		: nullptr;
	UWorld* World = GetWorld();
	if (!Character || !Gate || !Movement || !World
		|| Stage == ETunnelMoveStage::None || GoalLocation.ContainsNaN())
	{
		OutDiagnostic = TEXT(
			"Forced tunnel traversal requires a Character, Character Movement Component, World, and valid goal.");
		return false;
	}
	AParadoxTeleportGate* LinkedGate = Stage == ETunnelMoveStage::Ingress
		? Cast<AParadoxTeleportGate>(Gate->GetLinkedEndpoint())
		: nullptr;
	if (Stage == ETunnelMoveStage::Ingress && !LinkedGate)
	{
		OutDiagnostic = TEXT(
			"Forced tunnel ingress requires the acquired linked Teleport Gate.");
		return false;
	}

	ReleaseTunnelMovement();
	if (Stage == ETunnelMoveStage::Ingress)
	{
		Gate->SetTransitNavigationBlocking(true);
		LinkedGate->SetTransitNavigationBlocking(true);
	}
	ActiveTunnelMoveStage = Stage;
	ActiveTunnelMoveGoal = GoalLocation;
	const float AcceptanceRadius = FMath::Max(0.1f, Gate->TunnelTraversalAcceptanceRadius);
	const float Distance = FVector::Dist2D(
		GoalLocation,
		Character->GetActorLocation());
	if (Distance <= AcceptanceRadius)
	{
		ReleaseTunnelMovement();
		HandleTunnelStageSucceeded(Stage);
		OutDiagnostic.Reset();
		return true;
	}

	const float MaxWalkSpeed = Movement->MaxWalkSpeed;
	ActiveTunnelMoveTimeoutSeconds = MaxWalkSpeed > KINDA_SMALL_NUMBER
		? FMath::Max(
			UE::Paradox::TeleportGateInteraction::Private::MinimumTunnelMoveTimeout,
			UE::Paradox::TeleportGateInteraction::Private::TunnelMoveTimeoutMultiplier
				* Distance / MaxWalkSpeed)
		: UE::Paradox::TeleportGateInteraction::Private::MinimumTunnelMoveTimeout;
	World->GetTimerManager().SetTimer(
		TunnelMoveTimeoutHandle,
		this,
		&ThisClass::HandleTunnelMoveTimedOut,
		ActiveTunnelMoveTimeoutSeconds,
		false);
	SetActionTickEnabled(true);
	OutDiagnostic.Reset();
	return true;
}

void UParadoxEnterTeleportGateInteractionAction::ReleaseTunnelMovement()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TunnelMoveTimeoutHandle);
	}
	else
	{
		TunnelMoveTimeoutHandle.Invalidate();
	}
	ActiveTunnelMoveStage = ETunnelMoveStage::None;
	ActiveTunnelMoveGoal = FVector::ZeroVector;
	ActiveTunnelMoveTimeoutSeconds = 0.0f;
	SetActionTickEnabled(false);
}

void UParadoxEnterTeleportGateInteractionAction::HandleTunnelMoveTimedOut()
{
	if (ActiveTunnelMoveStage == ETunnelMoveStage::None)
	{
		return;
	}

	const ETunnelMoveStage CompletedStage = ActiveTunnelMoveStage;
	const FVector GoalLocation = ActiveTunnelMoveGoal;
	const AParadoxCharacter* Character =
		Cast<AParadoxCharacter>(GetInteractionRequester());
	const float DistanceRemaining = Character
		? FVector::Distance(Character->GetActorLocation(), GoalLocation)
		: -1.0f;
	ReleaseTunnelMovement();
	PARADOX_LOG_WARNING(
		TEXT("Teleport Gate action '%s' forced %s completion after its movement timeout; remaining distance=%.2f."),
		*GetNameSafe(this),
		CompletedStage == ETunnelMoveStage::Ingress ? TEXT("ingress") : TEXT("egress"),
		DistanceRemaining);
	HandleTunnelStageSucceeded(CompletedStage);
}

void UParadoxEnterTeleportGateInteractionAction::HandleTunnelStageSucceeded(
	const ETunnelMoveStage Stage)
{
	AParadoxTeleportGate* Gate = ActiveGate.Get();
	const FGuid OperationId = ActiveOperationId;
	if (!Gate || !OperationId.IsValid())
	{
		FailTunnelTraversal(TEXT("Teleport Gate transaction disappeared during tunnel traversal."));
		return;
	}

	if (Stage == ETunnelMoveStage::Ingress)
	{
		if (!Gate->CompleteTransferOut(OperationId))
		{
			if (ActiveOperationId.IsValid())
			{
				FailTunnelTraversal(TEXT("Teleport Gate rejected Transfer-Out completion after ingress."));
			}
			return;
		}
		if (!ActiveOperationId.IsValid() || ActiveGate.Get() != Gate)
		{
			return;
		}
		FVector EgressTarget = FVector::ZeroVector;
		FString Diagnostic;
		if (!Gate->GetActiveTunnelTarget(OperationId, true, EgressTarget)
			|| !StartTunnelMovement(ETunnelMoveStage::Egress, EgressTarget, Diagnostic))
		{
			FailTunnelTraversal(
				Diagnostic.IsEmpty()
					? TEXT("Teleport Gate could not start the forced egress movement.")
					: Diagnostic);
		}
		return;
	}

	if (Stage == ETunnelMoveStage::Egress)
	{
		if (!Gate->CompleteTransferIn(OperationId) && ActiveOperationId.IsValid())
		{
			FailTunnelTraversal(TEXT("Teleport Gate rejected Transfer-In completion after egress."));
		}
		return;
	}

	FailTunnelTraversal(TEXT("Teleport Gate completed an unknown tunnel movement stage."));
}

void UParadoxEnterTeleportGateInteractionAction::FailTunnelTraversal(
	const FString& DiagnosticMessage)
{
	ReleaseTunnelMovement();
	PendingTunnelFailureDiagnostic = DiagnosticMessage;
	AParadoxTeleportGate* Gate = ActiveGate.Get();
	const FGuid OperationId = ActiveOperationId;
	if (Gate && OperationId.IsValid())
	{
		Gate->CancelTransfer(OperationId);
		if (!ActiveOperationId.IsValid())
		{
			return;
		}
	}
	UnbindTransfer();
	CompleteInteractionFailure(
		ParadoxGameplayTags::Result_Failure_Interaction_EffectUnavailable,
		DiagnosticMessage);
	PendingTunnelFailureDiagnostic.Reset();
}

void UParadoxEnterTeleportGateInteractionAction::BindTransfer(
	AParadoxTeleportGate& Gate,
	const FGuid& OperationId)
{
	UnbindTransfer();
	ActiveGate = &Gate;
	ActiveOperationId = OperationId;
	Gate.OnTransferCompleted.AddUniqueDynamic(this, &ThisClass::HandleTransferCompleted);
	Gate.OnTransferCancelled.AddUniqueDynamic(this, &ThisClass::HandleTransferCancelled);
}

void UParadoxEnterTeleportGateInteractionAction::UnbindTransfer()
{
	if (AParadoxTeleportGate* Gate = ActiveGate.Get())
	{
		Gate->OnTransferCompleted.RemoveDynamic(this, &ThisClass::HandleTransferCompleted);
		Gate->OnTransferCancelled.RemoveDynamic(this, &ThisClass::HandleTransferCancelled);
	}
	ActiveGate.Reset();
	ActiveOperationId.Invalidate();
}

void UParadoxEnterTeleportGateInteractionAction::HandleTransferCompleted(
	const FParadoxTransferOperationContext& Context)
{
	if (bCleaningUpTransfer
		|| Context.Source != ActiveGate.Get()
		|| Context.Subject != GetInteractionRequester()
		|| Context.OperationId != ActiveOperationId)
	{
		return;
	}
	ReleaseTunnelMovement();
	PendingTunnelFailureDiagnostic.Reset();
	UnbindTransfer();
	CompleteInteractionSuccess(
		GameplayActionTags::Result_Success,
		TEXT("Teleport Gate transfer completed through Transfer-In."));
}

void UParadoxEnterTeleportGateInteractionAction::HandleTransferCancelled(
	const FParadoxTransferOperationContext& Context,
	const EParadoxTransferCancellationReason Reason,
	const bool bWasCommitted)
{
	(void)bWasCommitted;
	if (bCleaningUpTransfer
		|| Context.Source != ActiveGate.Get()
		|| Context.OperationId != ActiveOperationId)
	{
		return;
	}
	ReleaseTunnelMovement();
	const FString Diagnostic = PendingTunnelFailureDiagnostic.IsEmpty()
		? FString::Printf(
			TEXT("Teleport Gate transfer was cancelled (%s)."),
			*UEnum::GetValueAsString(Reason))
		: PendingTunnelFailureDiagnostic;
	PendingTunnelFailureDiagnostic.Reset();
	UnbindTransfer();
	CompleteInteractionFailure(
		UE::Paradox::TeleportGateInteraction::Private::FailureTagForCancellation(Reason),
		Diagnostic);
}
