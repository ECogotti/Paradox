#pragma once

#include "Components/BoxComponent.h"
#include "Puzzles/ParadoxElevator.h"
#include "ParadoxElevatorTestTypes.generated.h"

UCLASS()
class AParadoxElevatorTestActor : public AParadoxElevator
{
	GENERATED_BODY()

public:
	bool RequestEndWithoutButtonForTest() { return RequestMoveTowardEnd(); }
	int32 ButtonPressedCount = 0;
	int32 ButtonReleasedCount = 0;
	int32 ButtonMovementCompletedCount = 0;
	bool bLastButtonCompletionPressed = false;

	int32 GetButtonPresentationEventCountForTest() const
	{
		return ButtonPressedCount + ButtonReleasedCount + ButtonMovementCompletedCount;
	}

protected:
	virtual void HandleButtonPressed_Implementation() override
	{
		Super::HandleButtonPressed_Implementation();
		++ButtonPressedCount;
	}

	virtual void HandleButtonReleased_Implementation() override
	{
		Super::HandleButtonReleased_Implementation();
		++ButtonReleasedCount;
	}

	virtual void HandleButtonMovementCompleted_Implementation(bool bIsPressed) override
	{
		Super::HandleButtonMovementCompleted_Implementation(bIsPressed);
		++ButtonMovementCompletedCount;
		bLastButtonCompletionPressed = bIsPressed;
	}
};

UCLASS()
class AParadoxElevatorTestOccupant : public AActor
{
	GENERATED_BODY()

public:
	AParadoxElevatorTestOccupant()
	{
		Root = CreateDefaultSubobject<UBoxComponent>(TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Root->InitBoxExtent(FVector(20.0f));
		Root->SetCollisionProfileName(TEXT("Trigger"));
		Root->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Root->SetGenerateOverlapEvents(true);
		SetRootComponent(Root);

		SecondComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("SecondComponent"));
		SecondComponent->SetupAttachment(Root);
		SecondComponent->SetMobility(EComponentMobility::Movable);
		SecondComponent->InitBoxExtent(FVector(15.0f));
		SecondComponent->SetCollisionProfileName(TEXT("Trigger"));
		SecondComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		SecondComponent->SetGenerateOverlapEvents(true);
	}

	UPROPERTY()
	TObjectPtr<UBoxComponent> Root = nullptr;

	UPROPERTY()
	TObjectPtr<UBoxComponent> SecondComponent = nullptr;
};
