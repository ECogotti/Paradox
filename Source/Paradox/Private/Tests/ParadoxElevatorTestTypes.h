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
