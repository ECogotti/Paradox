#pragma once

#include "CoreMinimal.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Signals/PuzzleSignalPayload.h"
#include "PuzzleSystemTestTypes.generated.h"

/** Test payload used to verify UObject payload publication and republish revision behavior. */
UCLASS()
class UPuzzleTestSignalPayload : public UPuzzleSignalPayload
{
	GENERATED_BODY()

public:
	/** Arbitrary value used by payload-related tests. */
	UPROPERTY()
	int32 Value = 0;
};

/** Test observer used to verify receiver Blueprint-style delegates from C++. */
UCLASS()
class UPuzzleReceiverTestObserver : public UObject
{
	GENERATED_BODY()

public:
	/** Number of receiver state changes observed. */
	UPROPERTY()
	int32 StateChangedCount = 0;

	/** Last state value received from the receiver delegate. */
	UPROPERTY()
	bool bLastActive = false;

	/**
	 * Records a receiver state change notification.
	 *
	 * @param Receiver Receiver component that fired the delegate.
	 * @param bIsActive New effective receiver state.
	 */
	UFUNCTION()
	void HandleReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bIsActive)
	{
		++StateChangedCount;
		bLastActive = bIsActive;
	}
};

/** Test receiver that publishes an emitter signal during activation to exercise reentrant controller evaluation. */
UCLASS(ClassGroup = (Puzzle))
class UPuzzleReentrantReceiverComponent : public UPuzzleReceiverComponent
{
	GENERATED_BODY()

public:
	/** Emitter that receives the chained signal publication. */
	UPROPERTY()
	TObjectPtr<UPuzzleEmitterComponent> EmitterToPublish = nullptr;

	/** Signal tag published when this receiver activates. */
	UPROPERTY()
	FGameplayTag SignalTagToPublish;

	/** Number of times activation caused a chained publication. */
	UPROPERTY()
	int32 PublishCount = 0;

protected:
	/** Publishes a chained signal after base receiver activation behavior. */
	virtual void HandleReceiverActivated() override
	{
		Super::HandleReceiverActivated();

		++PublishCount;
		if (EmitterToPublish && SignalTagToPublish.IsValid())
		{
			EmitterToPublish->SetSignalState(SignalTagToPublish, true, nullptr);
		}
	}
};
