#include "Emitters/PuzzleEmitterComponent.h"

#include "PuzzleSystem.h"
#include "Controllers/PuzzleController.h"
#include "Signals/PuzzleSignalPayload.h"

UPuzzleEmitterComponent::UPuzzleEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPuzzleEmitterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BroadcastInvalidated();
	Super::EndPlay(EndPlayReason);
}

void UPuzzleEmitterComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	BroadcastInvalidated();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UPuzzleEmitterComponent::SetSignalState(FGameplayTag SignalTag, bool bNewActive, UPuzzleSignalPayload* Payload)
{
	if (!SignalTag.IsValid())
	{
		PUZZLESYSTEM_LOG_WARNING("Emitter '%s' ignored invalid signal tag.", *GetNameSafe(this));
		return false;
	}

	FPuzzleSignalState* ExistingState = SignalStates.Find(SignalTag);
	if (ExistingState && ExistingState->bIsValid && ExistingState->bIsActive == bNewActive && ExistingState->Payload == Payload)
	{
		return false;
	}

	const int64 PreviousRevision = ExistingState ? ExistingState->Revision : 0;

	FPuzzleSignalState NewState;
	NewState.bIsValid = true;
	NewState.bIsActive = bNewActive;
	NewState.Payload = Payload;
	NewState.Revision = PreviousRevision + 1;

	SignalStates.Add(SignalTag, NewState);
	BroadcastSignalChanged(SignalTag, NewState);
	return true;
}

bool UPuzzleEmitterComponent::RepublishSignal(FGameplayTag SignalTag)
{
	if (!SignalTag.IsValid())
	{
		PUZZLESYSTEM_LOG_WARNING("Emitter '%s' cannot republish an invalid signal tag.", *GetNameSafe(this));
		return false;
	}

	FPuzzleSignalState* ExistingState = SignalStates.Find(SignalTag);
	if (!ExistingState || !ExistingState->bIsValid)
	{
		PUZZLESYSTEM_LOG_WARNING("Emitter '%s' cannot republish missing signal '%s'.", *GetNameSafe(this), *SignalTag.ToString());
		return false;
	}

	++ExistingState->Revision;
	BroadcastSignalChanged(SignalTag, *ExistingState);
	return true;
}

bool UPuzzleEmitterComponent::TryGetSignalState(FGameplayTag SignalTag, FPuzzleSignalState& OutSignalState) const
{
	const FPuzzleSignalState* ExistingState = SignalStates.Find(SignalTag);
	if (!ExistingState || !ExistingState->bIsValid)
	{
		OutSignalState = FPuzzleSignalState();
		return false;
	}

	OutSignalState = *ExistingState;
	return true;
}

const TMap<FGameplayTag, FPuzzleSignalState>& UPuzzleEmitterComponent::GetSignalStates() const
{
	return SignalStates;
}

bool UPuzzleEmitterComponent::IsInvalidated(FGameplayTag SignalTag) const
{
	if (bHasBroadcastInvalidated) { return true; }
	bool bHasConsumer = false;
	for (const auto& ControllerEntry : ControllerGateAdmissions)
	{
		if (!ControllerEntry.Key.IsValid()) { continue; }
		for (const auto& Admission : ControllerEntry.Value)
		{
			if (SignalTag.IsValid() && Admission.Key != SignalTag) { continue; }
			bHasConsumer = true;
			if (Admission.Value) { return false; }
		}
	}
	return bHasConsumer;
}

void UPuzzleEmitterComponent::SetControllerGateAdmissions(APuzzleController* Controller, const TMap<FGameplayTag, bool>& Admissions)
{
	if (bHasBroadcastInvalidated || !IsValid(Controller)) { return; }
	ControllerGateAdmissions.Add(Controller, Admissions);
	NotifyGateInvalidationChanges();
}

void UPuzzleEmitterComponent::RemoveControllerGateAdmissions(APuzzleController* Controller)
{
	if (ControllerGateAdmissions.Remove(Controller) > 0) { NotifyGateInvalidationChanges(); }
}

void UPuzzleEmitterComponent::NotifyGateInvalidationChanges()
{
	if (bHasBroadcastInvalidated) { return; }
	if (bNotifyingGateInvalidation) { bGateNotificationRequested = true; return; }
	TGuardValue<bool> Guard(bNotifyingGateInvalidation, true);
	do
	{
		bGateNotificationRequested = false;
		TSet<FGameplayTag> Signals;
		for (const auto& Previous : LastSignalInvalidations) { Signals.Add(Previous.Key); }
		for (auto It = ControllerGateAdmissions.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid()) { It.RemoveCurrent(); continue; }
			for (const auto& Admission : It.Value()) { Signals.Add(Admission.Key); }
		}
		TMap<FGameplayTag, bool> Notifications;
		for (FGameplayTag Signal : Signals)
		{
			const bool bInvalidated = IsInvalidated(Signal);
			const bool bPrevious = LastSignalInvalidations.FindRef(Signal);
			LastSignalInvalidations.Add(Signal, bInvalidated);
			if (bPrevious != bInvalidated) { Notifications.Add(Signal, bInvalidated); }
		}
		// State is committed before callbacks; reentrant gate changes are reconciled in another pass.
		for (const auto& Notification : Notifications)
		{
			if (bHasBroadcastInvalidated) { break; }
			if (IsInvalidated(Notification.Key) != Notification.Value) { continue; }
			OnGateInvalidationChangedNative.Broadcast(this, Notification.Key, Notification.Value);
			if (!bHasBroadcastInvalidated && IsInvalidated(Notification.Key) == Notification.Value)
			{
				OnGateInvalidationChanged.Broadcast(this, Notification.Key, Notification.Value);
			}
		}
	}
	while (bGateNotificationRequested && !bHasBroadcastInvalidated);
}

void UPuzzleEmitterComponent::BroadcastInvalidated()
{
	if (bHasBroadcastInvalidated)
	{
		return;
	}

	bHasBroadcastInvalidated = true;
	OnEmitterInvalidatedNative.Broadcast(this);
	OnEmitterInvalidated.Broadcast(this);
}

void UPuzzleEmitterComponent::BroadcastSignalChanged(FGameplayTag SignalTag, const FPuzzleSignalState& SignalState)
{
	if (IsPuzzleSystemDebugEnabled())
	{
		PUZZLESYSTEM_LOG_INFO(
			"Emitter '%s' published '%s': Active=%s Revision=%lld Payload=%s.",
			*GetNameSafe(this),
			*SignalTag.ToString(),
			SignalState.bIsActive ? TEXT("true") : TEXT("false"),
			SignalState.Revision,
			*GetNameSafe(SignalState.Payload ? SignalState.Payload->GetClass() : nullptr));
	}

	OnSignalChangedNative.Broadcast(this, SignalTag, SignalState);
	OnSignalChanged.Broadcast(this, SignalTag, SignalState);
}
