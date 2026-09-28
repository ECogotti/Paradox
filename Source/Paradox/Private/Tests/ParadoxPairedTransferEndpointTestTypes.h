#pragma once

#include "Components/SceneComponent.h"
#include "Puzzles/ParadoxPairedTransferEndpoint.h"
#include "ParadoxPairedTransferEndpointTestTypes.generated.h"

/** Concrete automation fixture that exposes the protected async completion boundary. */
UCLASS()
class AParadoxPairedTransferTestEndpoint : public AParadoxPairedTransferEndpoint
{
	GENERATED_BODY()

public:
	bool CompleteTransferOutForTest(const FGuid OperationId)
	{
		return CompleteTransferOut(OperationId);
	}

	bool CompleteTransferInForTest(const FGuid OperationId)
	{
		return CompleteTransferIn(OperationId);
	}
};

/** Generic movable Actor accepted by the base endpoint policy. */
UCLASS()
class AParadoxPairedTransferTestSubject : public AActor
{
	GENERATED_BODY()

public:
	AParadoxPairedTransferTestSubject()
	{
		MovableRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MovableRoot"));
		MovableRoot->SetMobility(EComponentMobility::Movable);
		SetRootComponent(MovableRoot);
	}

	UPROPERTY()
	TObjectPtr<USceneComponent> MovableRoot = nullptr;
};
