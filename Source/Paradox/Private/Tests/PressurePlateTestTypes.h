#pragma once

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Puzzles/PressurePlate.h"
#include "PressurePlateTestTypes.generated.h"

/** Concrete fixture exposing protected pressure-plate overlap boundaries to native automation. */
UCLASS()
class APressurePlateTestActor : public APressurePlate
{
	GENERATED_BODY()

public:
	void SimulateBeginOverlap(AActor* OtherActor, UPrimitiveComponent* OtherComponent)
	{
		const FHitResult EmptySweep;
		HandleOccupancyBeginOverlap(
			OccupancyVolume,
			OtherActor,
			OtherComponent,
			INDEX_NONE,
			false,
			EmptySweep);
	}

	void SimulateEndOverlap(AActor* OtherActor, UPrimitiveComponent* OtherComponent)
	{
		HandleOccupancyEndOverlap(
			OccupancyVolume,
			OtherActor,
			OtherComponent,
			INDEX_NONE);
	}

	int32 ConfirmedPressCount = 0;
	int32 ConfirmedReleaseCount = 0;
	int32 OccupantAcceptedCount = 0;
	int32 OccupantReleasedCount = 0;
	int32 OccupantReplacedCount = 0;
	int32 MovementStartedCount = 0;
	int32 MovementCompletedCount = 0;

protected:
	virtual void HandleInputPressed_Implementation() override
	{
		Super::HandleInputPressed_Implementation();
		++ConfirmedPressCount;
	}

	virtual void HandleInputReleased_Implementation() override
	{
		Super::HandleInputReleased_Implementation();
		++ConfirmedReleaseCount;
	}

	virtual void HandleOccupantAccepted_Implementation(AActor* OccupantActor) override
	{
		Super::HandleOccupantAccepted_Implementation(OccupantActor);
		++OccupantAcceptedCount;
	}

	virtual void HandleOccupantReleased_Implementation(AActor* OccupantActor) override
	{
		Super::HandleOccupantReleased_Implementation(OccupantActor);
		++OccupantReleasedCount;
	}

	virtual void HandleOccupantReplaced_Implementation(AActor* PreviousOccupant, AActor* NewOccupant) override
	{
		Super::HandleOccupantReplaced_Implementation(PreviousOccupant, NewOccupant);
		++OccupantReplacedCount;
	}

	virtual void HandlePlateMovementStarted_Implementation(bool bMovingDown) override
	{
		Super::HandlePlateMovementStarted_Implementation(bMovingDown);
		++MovementStartedCount;
	}

	virtual void HandlePlateMovementCompleted_Implementation(bool bIsPressed) override
	{
		Super::HandlePlateMovementCompleted_Implementation(bIsPressed);
		++MovementCompletedCount;
	}
};

/** Two-component Actor used to verify physical overlap, single ownership, and ALL Actor Tag semantics. */
UCLASS()
class APressurePlateTestOccupant : public AActor
{
	GENERATED_BODY()

public:
	APressurePlateTestOccupant()
	{
		SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
		SetRootComponent(SceneRoot);

		FirstComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("FirstComponent"));
		FirstComponent->SetupAttachment(SceneRoot);
		FirstComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		SecondComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("SecondComponent"));
		SecondComponent->SetupAttachment(SceneRoot);
		SecondComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	/** Enables one real query overlap for initialization/reset reconciliation tests. */
	void EnablePhysicalOverlap()
	{
		FirstComponent->SetCollisionProfileName(TEXT("Trigger"));
		FirstComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		FirstComponent->SetGenerateOverlapEvents(true);
		FirstComponent->InitBoxExtent(FVector(20.0f));
	}

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY()
	TObjectPtr<UBoxComponent> FirstComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UBoxComponent> SecondComponent = nullptr;

};

/** Pickupable fixture proving that world-presence collision can drive a real overlap detector. */
UCLASS()
class APressurePlateTestPickupable : public AParadoxPickupableActor
{
	GENERATED_BODY()

public:
	APressurePlateTestPickupable()
	{
		bUseAuthoredWorldCollision = true;
		WorldCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("WorldCollision"));
		WorldCollision->SetupAttachment(GetRootComponent());
		WorldCollision->InitBoxExtent(FVector(20.0f));
		WorldCollision->SetCollisionObjectType(ECC_WorldStatic);
		WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		WorldCollision->SetCollisionResponseToAllChannels(ECR_Block);
		WorldCollision->SetGenerateOverlapEvents(false);
	}

	UPROPERTY()
	TObjectPtr<UBoxComponent> WorldCollision = nullptr;
};
