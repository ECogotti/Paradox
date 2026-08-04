#pragma once

#include "CoreMinimal.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Emitters/PuzzleSwitch.h"
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

/** Concrete automation-test child for the abstract reusable switch template. */
UCLASS()
class APuzzleSwitchTestActor : public APuzzleSwitch
{
	GENERATED_BODY()

public:
	/** Establishes runtime state without depending on a test world's BeginPlay dispatch. */
	void InitializeForTest()
	{
		InitializePuzzleSwitch();
	}

	int32 InputPressedHookCount = 0;
	int32 InputReleasedHookCount = 0;
	int32 PressDelayStartedHookCount = 0;
	int32 PressDelayCancelledHookCount = 0;
	int32 PressDelayCompletedHookCount = 0;
	int32 ReleaseDelayStartedHookCount = 0;
	int32 ReleaseDelayCancelledHookCount = 0;
	int32 ReleaseDelayCompletedHookCount = 0;
	int32 SwitchActivatedHookCount = 0;
	int32 SwitchDeactivatedHookCount = 0;
	int32 SwitchResetHookCount = 0;

protected:
	virtual void HandleInputPressed_Implementation() override
	{
		++InputPressedHookCount;
	}

	virtual void HandleInputReleased_Implementation() override
	{
		++InputReleasedHookCount;
	}

	virtual void HandlePressDelayStarted_Implementation() override
	{
		++PressDelayStartedHookCount;
	}

	virtual void HandlePressDelayCancelled_Implementation() override
	{
		++PressDelayCancelledHookCount;
	}

	virtual void HandlePressDelayCompleted_Implementation() override
	{
		++PressDelayCompletedHookCount;
	}

	virtual void HandleReleaseDelayStarted_Implementation() override
	{
		++ReleaseDelayStartedHookCount;
	}

	virtual void HandleReleaseDelayCancelled_Implementation() override
	{
		++ReleaseDelayCancelledHookCount;
	}

	virtual void HandleReleaseDelayCompleted_Implementation() override
	{
		++ReleaseDelayCompletedHookCount;
	}

	virtual void HandleSwitchActivated_Implementation() override
	{
		++SwitchActivatedHookCount;
	}

	virtual void HandleSwitchDeactivated_Implementation() override
	{
		++SwitchDeactivatedHookCount;
	}

	virtual void HandleSwitchReset_Implementation() override
	{
		++SwitchResetHookCount;
	}
};

/** Records switch transition delegates so their edge and delay semantics can be asserted. */
UCLASS()
class UPuzzleSwitchTestObserver : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 PressedCount = 0;

	UPROPERTY()
	int32 ReleasedCount = 0;

	UPROPERTY()
	int32 PressDelayStartedCount = 0;

	UPROPERTY()
	int32 PressDelayCancelledCount = 0;

	UPROPERTY()
	int32 PressDelayCompletedCount = 0;

	UPROPERTY()
	int32 ReleaseDelayStartedCount = 0;

	UPROPERTY()
	int32 ReleaseDelayCancelledCount = 0;

	UPROPERTY()
	int32 ReleaseDelayCompletedCount = 0;

	UPROPERTY()
	int32 ActivatedCount = 0;

	UPROPERTY()
	int32 DeactivatedCount = 0;

	UPROPERTY()
	int32 ResetCount = 0;

	UPROPERTY()
	bool bHooksPrecededDelegates = true;

	UFUNCTION()
	void HandlePressed(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->InputPressedHookCount == PressedCount + 1;
		++PressedCount;
	}

	UFUNCTION()
	void HandleReleased(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->InputReleasedHookCount == ReleasedCount + 1;
		++ReleasedCount;
	}

	UFUNCTION()
	void HandlePressDelayStarted(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->PressDelayStartedHookCount == PressDelayStartedCount + 1;
		++PressDelayStartedCount;
	}

	UFUNCTION()
	void HandlePressDelayCancelled(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->PressDelayCancelledHookCount == PressDelayCancelledCount + 1;
		++PressDelayCancelledCount;
	}

	UFUNCTION()
	void HandlePressDelayCompleted(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->PressDelayCompletedHookCount == PressDelayCompletedCount + 1;
		++PressDelayCompletedCount;
	}

	UFUNCTION()
	void HandleReleaseDelayStarted(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->ReleaseDelayStartedHookCount == ReleaseDelayStartedCount + 1;
		++ReleaseDelayStartedCount;
	}

	UFUNCTION()
	void HandleReleaseDelayCancelled(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->ReleaseDelayCancelledHookCount == ReleaseDelayCancelledCount + 1;
		++ReleaseDelayCancelledCount;
	}

	UFUNCTION()
	void HandleReleaseDelayCompleted(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->ReleaseDelayCompletedHookCount == ReleaseDelayCompletedCount + 1;
		++ReleaseDelayCompletedCount;
	}

	UFUNCTION()
	void HandleActivated(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->SwitchActivatedHookCount == ActivatedCount + 1;
		++ActivatedCount;
	}

	UFUNCTION()
	void HandleDeactivated(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->SwitchDeactivatedHookCount == DeactivatedCount + 1;
		++DeactivatedCount;
	}

	UFUNCTION()
	void HandleReset(APuzzleSwitch* PuzzleSwitch)
	{
		const APuzzleSwitchTestActor* TestSwitch = Cast<APuzzleSwitchTestActor>(PuzzleSwitch);
		bHooksPrecededDelegates &= TestSwitch && TestSwitch->SwitchResetHookCount == ResetCount + 1;
		++ResetCount;
	}
};
