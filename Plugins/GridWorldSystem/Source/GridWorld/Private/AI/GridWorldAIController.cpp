// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/GridWorldAIController.h"

#include "AI/GridWorldPathFollowingComponent.h"

AGridWorldAIController::AGridWorldAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGridWorldPathFollowingComponent>(TEXT("PathFollowingComponent")))
{
}

void AGridWorldAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// UPathFollowingComponent normally observes the controller's new-Pawn delegate. Calling
	// this explicitly also covers possession before component registration and is idempotent.
	if (InPawn != nullptr && GetPawn() == InPawn)
	{
		if (UGridWorldPathFollowingComponent* GridPathFollowing = Cast<UGridWorldPathFollowingComponent>(GetPathFollowingComponent()))
		{
			GridPathFollowing->EnsurePawnOccupancy(InPawn);
		}
	}
}
