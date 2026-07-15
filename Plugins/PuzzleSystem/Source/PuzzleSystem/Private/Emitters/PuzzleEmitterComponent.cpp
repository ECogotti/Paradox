#include "Emitters/PuzzleEmitterComponent.h"

#include "PuzzleSystem.h"
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
