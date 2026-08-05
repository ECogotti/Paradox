#pragma once

#include "Components/BoxComponent.h"
#include "Puzzles/ParadoxVerticalBarrier.h"
#include "ParadoxVerticalBarrierTestTypes.generated.h"

UCLASS()
class AParadoxVerticalBarrierTestActor : public AParadoxVerticalBarrier
{
	GENERATED_BODY()

public:
	bool RequestEndForTest() { return RequestMoveTowardEnd(); }
	bool RequestStartForTest() { return RequestMoveTowardStart(); }

	void SimulateBeginOverlap(AActor* Actor, UPrimitiveComponent* Component)
	{
		const FHitResult EmptySweep;
		HandlePassageBeginOverlap(
			PassageOccupancyVolume,
			Actor,
			Component,
			INDEX_NONE,
			false,
			EmptySweep);
	}

	void SimulateEndOverlap(AActor* Actor, UPrimitiveComponent* Component)
	{
		HandlePassageEndOverlap(PassageOccupancyVolume, Actor, Component, INDEX_NONE);
	}
};

/** Movable two-component fixture for distinct-Actor occupancy and attachment restoration. */
UCLASS()
class AParadoxVerticalBarrierTestOccupant : public AActor
{
	GENERATED_BODY()

public:
	AParadoxVerticalBarrierTestOccupant()
	{
		Root = CreateDefaultSubobject<UBoxComponent>(TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetRootComponent(Root);

		SecondComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("SecondComponent"));
		SecondComponent->SetupAttachment(Root);
		SecondComponent->SetMobility(EComponentMobility::Movable);
		SecondComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	void EnablePhysicalOverlap()
	{
		for (UBoxComponent* Component : { Root.Get(), SecondComponent.Get() })
		{
			Component->SetCollisionProfileName(TEXT("Trigger"));
			Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Component->SetGenerateOverlapEvents(true);
			Component->SetBoxExtent(FVector(20.0f));
		}
	}

	UPROPERTY()
	TObjectPtr<UBoxComponent> Root = nullptr;

	UPROPERTY()
	TObjectPtr<UBoxComponent> SecondComponent = nullptr;
};
