#include "Receivers/PuzzleReceiverComponent.h"

#include "Controllers/PuzzleController.h"
#include "PuzzleSystem.h"

UPuzzleReceiverComponent::UPuzzleReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPuzzleReceiverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ControllerRequests.Empty();
	if (bIsReceiverActive)
	{
		BroadcastReceiverStateChanged(false);
	}
	OnReceiverStateChangedNative.Clear();

	Super::EndPlay(EndPlayReason);
}

bool UPuzzleReceiverComponent::SetControllerRequest(APuzzleController* SourceController, bool bRequestedActive)
{
	if (!IsValid(SourceController))
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
	if (!SourceController)
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

bool UPuzzleReceiverComponent::RecomputeEffectiveState()
{
	bool bNewActive = false;

	for (TMap<TWeakObjectPtr<APuzzleController>, bool>::TIterator It(ControllerRequests); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		if (It.Value())
		{
			bNewActive = true;
		}
	}

	if (bNewActive == bIsReceiverActive)
	{
		return false;
	}

	BroadcastReceiverStateChanged(bNewActive);
	return true;
}

void UPuzzleReceiverComponent::BroadcastReceiverStateChanged(bool bNewActive)
{
	bIsReceiverActive = bNewActive;

	if (IsPuzzleSystemDebugEnabled())
	{
		PUZZLESYSTEM_LOG_INFO(
			"Receiver '%s' changed state: Active=%s ActiveRequests=%d.",
			*GetNameSafe(this),
			bIsReceiverActive ? TEXT("true") : TEXT("false"),
			GetActiveRequestCount());
	}

	HandleReceiverStateChanged(bIsReceiverActive);

	if (bIsReceiverActive)
	{
		HandleReceiverActivated();
	}
	else
	{
		HandleReceiverDeactivated();
	}

	OnReceiverStateChangedNative.Broadcast(this, bIsReceiverActive);

	if (bIsReceiverActive)
	{
		OnReceiverActivated.Broadcast(this);
	}
	else
	{
		OnReceiverDeactivated.Broadcast(this);
	}

	OnReceiverStateChanged.Broadcast(this, bIsReceiverActive);
}
