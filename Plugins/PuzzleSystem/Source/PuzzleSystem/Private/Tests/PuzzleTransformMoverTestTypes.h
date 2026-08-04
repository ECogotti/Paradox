#pragma once

#include "CoreMinimal.h"
#include "Activators/PuzzleTransformMover.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "PuzzleTransformMoverTestTypes.generated.h"

/** Concrete native fixture for the abstract transform-mover template. */
UCLASS()
class APuzzleTransformMoverTestActor : public APuzzleTransformMover
{
	GENERATED_BODY()

public:
	APuzzleTransformMoverTestActor()
	{
		TestMovedComponent = CreateDefaultSubobject<USceneComponent>(TEXT("TestMovedComponent"));
		TestMovedComponent->SetupAttachment(BillboardRoot);
		TestMovedComponent->SetMobility(EComponentMobility::Movable);

		ReplacementComponent = CreateDefaultSubobject<USceneComponent>(TEXT("ReplacementComponent"));
		ReplacementComponent->SetupAttachment(BillboardRoot);
		ReplacementComponent->SetMobility(EComponentMobility::Movable);

		DefaultMovedComponent.ComponentProperty = GET_MEMBER_NAME_CHECKED(APuzzleTransformMoverTestActor, TestMovedComponent);
	}

	UPROPERTY(VisibleDefaultsOnly)
	TObjectPtr<USceneComponent> TestMovedComponent = nullptr;

	UPROPERTY(VisibleDefaultsOnly)
	TObjectPtr<USceneComponent> ReplacementComponent = nullptr;

	bool InitializeForTest()
	{
		return InitializePuzzleTransformMover();
	}

	bool RequestStartForTest()
	{
		return RequestMoveTowardStart();
	}

	bool RequestEndForTest()
	{
		return RequestMoveTowardEnd();
	}

	int32 MovementStartedHookCount = 0;
	int32 MovementResumedHookCount = 0;
	int32 MovementReversedHookCount = 0;
	int32 MovementPausedHookCount = 0;
	int32 MovementUpdatedHookCount = 0;
	int32 ReachedStartHookCount = 0;
	int32 ReachedEndHookCount = 0;
	int32 MovedComponentChangedHookCount = 0;
	int32 MoverResetHookCount = 0;

protected:
	virtual void HandleMovementStarted_Implementation() override
	{
		++MovementStartedHookCount;
	}

	virtual void HandleMovementResumed_Implementation() override
	{
		++MovementResumedHookCount;
	}

	virtual void HandleMovementReversed_Implementation() override
	{
		++MovementReversedHookCount;
	}

	virtual void HandleMovementPaused_Implementation() override
	{
		++MovementPausedHookCount;
	}

	virtual void HandleMovementUpdated_Implementation(float CurrentMovementAlpha, float CurrentEasedAlpha) override
	{
		++MovementUpdatedHookCount;
	}

	virtual void HandleReachedStart_Implementation() override
	{
		++ReachedStartHookCount;
	}

	virtual void HandleReachedEnd_Implementation() override
	{
		++ReachedEndHookCount;
	}

	virtual void HandleMovedComponentChanged_Implementation() override
	{
		++MovedComponentChangedHookCount;
	}

	virtual void HandleMoverReset_Implementation() override
	{
		++MoverResetHookCount;
	}
};

/** Delegate observer that also verifies native/BlueprintNativeEvent hooks run before multicast delegates. */
UCLASS()
class UPuzzleTransformMoverTestObserver : public UObject
{
	GENERATED_BODY()

public:
	int32 MovementStartedCount = 0;
	int32 MovementResumedCount = 0;
	int32 MovementReversedCount = 0;
	int32 MovementPausedCount = 0;
	int32 ReachedStartCount = 0;
	int32 ReachedEndCount = 0;
	int32 MovedComponentChangedCount = 0;
	int32 MoverResetCount = 0;
	bool bHooksPrecededDelegates = true;

	UFUNCTION()
	void HandleMovementStarted(APuzzleTransformMover* Mover)
	{
		const APuzzleTransformMoverTestActor* TestMover = Cast<APuzzleTransformMoverTestActor>(Mover);
		bHooksPrecededDelegates &= TestMover && TestMover->MovementStartedHookCount == MovementStartedCount + 1;
		++MovementStartedCount;
	}

	UFUNCTION()
	void HandleMovementResumed(APuzzleTransformMover* Mover)
	{
		const APuzzleTransformMoverTestActor* TestMover = Cast<APuzzleTransformMoverTestActor>(Mover);
		bHooksPrecededDelegates &= TestMover && TestMover->MovementResumedHookCount == MovementResumedCount + 1;
		++MovementResumedCount;
	}

	UFUNCTION()
	void HandleMovementReversed(APuzzleTransformMover* Mover)
	{
		const APuzzleTransformMoverTestActor* TestMover = Cast<APuzzleTransformMoverTestActor>(Mover);
		bHooksPrecededDelegates &= TestMover && TestMover->MovementReversedHookCount == MovementReversedCount + 1;
		++MovementReversedCount;
	}

	UFUNCTION()
	void HandleMovementPaused(APuzzleTransformMover* Mover)
	{
		const APuzzleTransformMoverTestActor* TestMover = Cast<APuzzleTransformMoverTestActor>(Mover);
		bHooksPrecededDelegates &= TestMover && TestMover->MovementPausedHookCount == MovementPausedCount + 1;
		++MovementPausedCount;
	}

	UFUNCTION()
	void HandleReachedStart(APuzzleTransformMover* Mover)
	{
		const APuzzleTransformMoverTestActor* TestMover = Cast<APuzzleTransformMoverTestActor>(Mover);
		bHooksPrecededDelegates &= TestMover && TestMover->ReachedStartHookCount == ReachedStartCount + 1;
		++ReachedStartCount;
	}

	UFUNCTION()
	void HandleReachedEnd(APuzzleTransformMover* Mover)
	{
		const APuzzleTransformMoverTestActor* TestMover = Cast<APuzzleTransformMoverTestActor>(Mover);
		bHooksPrecededDelegates &= TestMover && TestMover->ReachedEndHookCount == ReachedEndCount + 1;
		++ReachedEndCount;
	}

	UFUNCTION()
	void HandleMovedComponentChanged(APuzzleTransformMover* Mover)
	{
		const APuzzleTransformMoverTestActor* TestMover = Cast<APuzzleTransformMoverTestActor>(Mover);
		bHooksPrecededDelegates &= TestMover && TestMover->MovedComponentChangedHookCount == MovedComponentChangedCount + 1;
		++MovedComponentChangedCount;
	}

	UFUNCTION()
	void HandleMoverReset(APuzzleTransformMover* Mover)
	{
		const APuzzleTransformMoverTestActor* TestMover = Cast<APuzzleTransformMoverTestActor>(Mover);
		bHooksPrecededDelegates &= TestMover && TestMover->MoverResetHookCount == MoverResetCount + 1;
		++MoverResetCount;
	}
};
