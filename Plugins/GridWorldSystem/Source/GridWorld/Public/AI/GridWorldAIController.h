// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AIController.h"

#include "GridWorldAIController.generated.h"

/** AI controller configured with GridWorld's optional precise cell-center path following. */
UCLASS(Blueprintable, meta = (DisplayName = "GridWorld AI Controller"))
class GRIDWORLD_API AGridWorldAIController : public AAIController
{
	GENERATED_BODY()

public:
	AGridWorldAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Publishes this controller's Pawn as non-blocking runtime occupancy for optional agent-aware filters. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Grid World|Dynamic Agents")
	bool bAutoRegisterPawnOccupancy = true;

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
