#include "Receivers/PuzzleReceiverComponent.h"

#include "Controllers/PuzzleController.h"
#include "Graph/PuzzleGraphSubsystem.h"
#include "PuzzleSystem.h"

UPuzzleReceiverComponent::UPuzzleReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPuzzleReceiverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BroadcastInvalidated();
	ControllerRequests.Empty();
	bActivationPrerequisitesSatisfied = false;
	bManualActivationRequested = false;
	bReconciliationRequested = false;
	if (bIsReceiverActive)
	{
		bIsReceiverActive = false;
		BroadcastReceiverStateChanged(false);
	}
	OnReceiverStateChangedNative.Clear();
	OnReceiverActivationPrerequisitesChangedNative.Clear();

	Super::EndPlay(EndPlayReason);
}

void UPuzzleReceiverComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	BroadcastInvalidated();
	ControllerRequests.Empty();
	bActivationPrerequisitesSatisfied = false;
	bManualActivationRequested = false;
	bIsReceiverActive = false;
	bReconciliationRequested = false;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UPuzzleReceiverComponent::SetControllerRequest(APuzzleController* SourceController, bool bRequestedActive)
{
	if (bHasBroadcastInvalidated || !IsValid(SourceController))
	{
		PUZZLESYSTEM_LOG_WARNING("Receiver '%s' ignored request from invalid Controller.", *GetNameSafe(this));
		return false;
	}

	if (bRequestedActive)
	{
		ControllerRequests.FindOrAdd(SourceController) = true;
	}
	else
	{
		ControllerRequests.Remove(SourceController);
	}

	return RecomputeEffectiveState();
}

bool UPuzzleReceiverComponent::RemoveControllerRequest(APuzzleController* SourceController)
{
	if (bHasBroadcastInvalidated || !SourceController)
	{
		return false;
	}

	const int32 RemovedCount = ControllerRequests.Remove(SourceController);
	if (RemovedCount <= 0)
	{
		return false;
	}

	return RecomputeEffectiveState();
}

FPuzzleReceiverActivationCommandResult UPuzzleReceiverComponent::RequestManualActivation()
{
	if (bHasBroadcastInvalidated || !IsValid(this))
	{
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::ReceiverUnavailable,
			TEXT("The Receiver is ending play or has been destroyed."));
	}
	if (ActivationMode != EPuzzleReceiverActivationMode::Manual)
	{
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::NotInManualMode,
			TEXT("Manual activation is unavailable because this Receiver uses Automatic mode."));
	}
	if (bIsReconcilingState)
	{
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::SupersededDuringNotification,
			TEXT("The Receiver is already reconciling a synchronous state notification."));
	}

	const bool bStateChangedBeforeRequest = RecomputeEffectiveState();
	if (!bActivationPrerequisitesSatisfied)
	{
		if (bStateChangedBeforeRequest)
		{
			NotifyGraphStateChanged();
		}
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::PrerequisitesNotSatisfied,
			TEXT("No valid Controller currently requests this Receiver active."));
	}
	if (bManualActivationRequested && bIsReceiverActive)
	{
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::AlreadyInRequestedState,
			TEXT("The Receiver is already manually active."));
	}

	bManualActivationRequested = true;
	RecomputeEffectiveState();
	NotifyGraphStateChanged();

	if (bManualActivationRequested && bIsReceiverActive)
	{
		if (IsPuzzleSystemDebugEnabled())
		{
			PUZZLESYSTEM_LOG_INFO("Receiver '%s' accepted manual activation.", *GetNameSafe(this));
		}
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::Applied,
			TEXT("Manual activation was applied."));
	}
	if (!bActivationPrerequisitesSatisfied)
	{
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::PrerequisitesNotSatisfied,
			TEXT("Activation prerequisites became unavailable during the synchronous notification chain."));
	}
	return MakeActivationCommandResult(
		EPuzzleReceiverActivationCommandStatus::SupersededDuringNotification,
		TEXT("Another synchronous notification superseded the manual activation request."));
}

FPuzzleReceiverActivationCommandResult UPuzzleReceiverComponent::RequestManualDeactivation()
{
	if (bHasBroadcastInvalidated || !IsValid(this))
	{
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::ReceiverUnavailable,
			TEXT("The Receiver is ending play or has been destroyed."));
	}
	if (ActivationMode != EPuzzleReceiverActivationMode::Manual)
	{
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::NotInManualMode,
			TEXT("Manual deactivation is unavailable because this Receiver uses Automatic mode."));
	}
	if (bIsReconcilingState)
	{
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::SupersededDuringNotification,
			TEXT("The Receiver is already reconciling a synchronous state notification."));
	}

	const bool bStateChangedBeforeRequest = RecomputeEffectiveState();
	if (!bManualActivationRequested && !bIsReceiverActive)
	{
		if (bStateChangedBeforeRequest)
		{
			NotifyGraphStateChanged();
		}
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::AlreadyInRequestedState,
			TEXT("The Receiver is already manually inactive."));
	}

	bManualActivationRequested = false;
	RecomputeEffectiveState();
	NotifyGraphStateChanged();

	if (!bManualActivationRequested && !bIsReceiverActive)
	{
		if (IsPuzzleSystemDebugEnabled())
		{
			PUZZLESYSTEM_LOG_INFO("Receiver '%s' accepted manual deactivation.", *GetNameSafe(this));
		}
		return MakeActivationCommandResult(
			EPuzzleReceiverActivationCommandStatus::Applied,
			TEXT("Manual deactivation was applied."));
	}
	return MakeActivationCommandResult(
		EPuzzleReceiverActivationCommandStatus::SupersededDuringNotification,
		TEXT("Another synchronous notification superseded the manual deactivation request."));
}

bool UPuzzleReceiverComponent::CanRequestManualActivation() const
{
	return !bHasBroadcastInvalidated
		&& ActivationMode == EPuzzleReceiverActivationMode::Manual
		&& !bIsReconcilingState
		&& bActivationPrerequisitesSatisfied
		&& !bManualActivationRequested;
}

bool UPuzzleReceiverComponent::IsReceiverActive() const
{
	return bIsReceiverActive;
}

int32 UPuzzleReceiverComponent::GetActiveRequestCount() const
{
	int32 ActiveCount = 0;
	for (const TPair<TWeakObjectPtr<APuzzleController>, bool>& Request : ControllerRequests)
	{
		if (Request.Key.IsValid() && Request.Value)
		{
			++ActiveCount;
		}
	}

	return ActiveCount;
}

void UPuzzleReceiverComponent::GetRequestingControllers(TArray<APuzzleController*>& OutControllers) const
{
	OutControllers.Reset();
	for (const TPair<TWeakObjectPtr<APuzzleController>, bool>& Request : ControllerRequests)
	{
		if (Request.Key.IsValid() && Request.Value)
		{
			OutControllers.Add(Request.Key.Get());
		}
	}
}

void UPuzzleReceiverComponent::HandleReceiverStateChanged(bool bNewActive)
{
}

void UPuzzleReceiverComponent::HandleReceiverActivated()
{
}

void UPuzzleReceiverComponent::HandleReceiverDeactivated()
{
}

void UPuzzleReceiverComponent::HandleReceiverActivationPrerequisitesChanged(bool bPrerequisitesSatisfied)
{
}

bool UPuzzleReceiverComponent::RecomputeEffectiveState()
{
	if (bIsReconcilingState)
	{
		bReconciliationRequested = true;
		return false;
	}

	bIsReconcilingState = true;
	bool bAnyObservableStateChanged = false;
	do
	{
		bReconciliationRequested = false;
		bool bNewPrerequisitesSatisfied = false;
		for (TMap<TWeakObjectPtr<APuzzleController>, bool>::TIterator It(ControllerRequests); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
				continue;
			}
			bNewPrerequisitesSatisfied |= It.Value();
		}

		bool bNewManualActivationRequested = bManualActivationRequested;
		if (ActivationMode != EPuzzleReceiverActivationMode::Manual
			|| !bNewPrerequisitesSatisfied)
		{
			bNewManualActivationRequested = false;
		}

		const bool bNewActive = ActivationMode == EPuzzleReceiverActivationMode::Automatic
			? bNewPrerequisitesSatisfied
			: bNewPrerequisitesSatisfied && bNewManualActivationRequested;
		const bool bPrerequisitesChanged =
			bNewPrerequisitesSatisfied != bActivationPrerequisitesSatisfied;
		const bool bManualRequestChanged =
			bNewManualActivationRequested != bManualActivationRequested;
		const bool bEffectiveStateChanged = bNewActive != bIsReceiverActive;

		bActivationPrerequisitesSatisfied = bNewPrerequisitesSatisfied;
		bManualActivationRequested = bNewManualActivationRequested;
		bIsReceiverActive = bNewActive;
		bAnyObservableStateChanged |=
			bPrerequisitesChanged || bManualRequestChanged || bEffectiveStateChanged;

		if (bPrerequisitesChanged)
		{
			BroadcastActivationPrerequisitesChanged(bActivationPrerequisitesSatisfied);
		}
		if (bEffectiveStateChanged)
		{
			BroadcastReceiverStateChanged(bIsReceiverActive);
		}
	}
	while (bReconciliationRequested);
	bIsReconcilingState = false;
	return bAnyObservableStateChanged;
}

void UPuzzleReceiverComponent::BroadcastReceiverStateChanged(bool bNewActive)
{
	if (IsPuzzleSystemDebugEnabled())
	{
		PUZZLESYSTEM_LOG_INFO(
			"Receiver '%s' changed state: Active=%s ActiveRequests=%d.",
			*GetNameSafe(this),
			bNewActive ? TEXT("true") : TEXT("false"),
			GetActiveRequestCount());
	}

	HandleReceiverStateChanged(bNewActive);

	if (bNewActive)
	{
		HandleReceiverActivated();
	}
	else
	{
		HandleReceiverDeactivated();
	}

	OnReceiverStateChangedNative.Broadcast(this, bNewActive);

	if (bNewActive)
	{
		OnReceiverActivated.Broadcast(this);
	}
	else
	{
		OnReceiverDeactivated.Broadcast(this);
	}

	OnReceiverStateChanged.Broadcast(this, bNewActive);
}

void UPuzzleReceiverComponent::BroadcastActivationPrerequisitesChanged(
	const bool bPrerequisitesSatisfied)
{
	if (IsPuzzleSystemDebugEnabled())
	{
		PUZZLESYSTEM_LOG_INFO(
			"Receiver '%s' prerequisites changed: Satisfied=%s ActiveRequests=%d Mode=%s.",
			*GetNameSafe(this),
			bPrerequisitesSatisfied ? TEXT("true") : TEXT("false"),
			GetActiveRequestCount(),
			ActivationMode == EPuzzleReceiverActivationMode::Automatic
				? TEXT("Automatic")
				: TEXT("Manual"));
	}

	HandleReceiverActivationPrerequisitesChanged(bPrerequisitesSatisfied);
	OnReceiverActivationPrerequisitesChangedNative.Broadcast(this, bPrerequisitesSatisfied);
	OnReceiverActivationPrerequisitesChanged.Broadcast(this, bPrerequisitesSatisfied);
}

FPuzzleReceiverActivationCommandResult UPuzzleReceiverComponent::MakeActivationCommandResult(
	const EPuzzleReceiverActivationCommandStatus Status,
	const FString& DiagnosticMessage) const
{
	FPuzzleReceiverActivationCommandResult Result;
	Result.Status = Status;
	Result.bPrerequisitesSatisfied = bActivationPrerequisitesSatisfied;
	Result.bManualActivationRequested = bManualActivationRequested;
	Result.bReceiverActive = bIsReceiverActive;
	Result.DiagnosticMessage = DiagnosticMessage;
	return Result;
}

void UPuzzleReceiverComponent::NotifyGraphStateChanged()
{
	if (UWorld* World = GetWorld())
	{
		if (UPuzzleGraphSubsystem* GraphSubsystem = World->GetSubsystem<UPuzzleGraphSubsystem>())
		{
			GraphSubsystem->RefreshReceiverState(this);
		}
	}
}

void UPuzzleReceiverComponent::BroadcastInvalidated()
{
	if (bHasBroadcastInvalidated)
	{
		return;
	}

	bHasBroadcastInvalidated = true;
	OnReceiverInvalidatedNative.Broadcast(this);
}
