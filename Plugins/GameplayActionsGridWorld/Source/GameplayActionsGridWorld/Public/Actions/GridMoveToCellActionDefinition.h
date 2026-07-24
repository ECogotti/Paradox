#pragma once

#include "CoreMinimal.h"
#include "Actions/GameplayActionDefinition.h"
#include "GridMoveToCellActionDefinition.generated.h"

namespace GridMoveToCellActionParameters
{
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName PathSource;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName InjectedPath;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName GoalLocation;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName GoalActor;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName AcceptanceRadius;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName StopOnOverlap;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName AcceptPartialPath;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName UsePathfinding;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName LockAILogic;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName TrackMovingGoal;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName RequireNavigableEndLocation;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName FilterClass;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName AllowStrafe;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName GoalContentionPolicy;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName MaxAlternativeSearchRadius;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName AdditionalGoalSeparation;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName AutoRegisterPawnOccupancy;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName GoalAvailabilityTimeout;
	GAMEPLAYACTIONSGRIDWORLD_API extern const FName GoalWaitWarningInterval;
}

/**
 * Native, ready-to-author Definition for UGridMoveToCellAction.
 *
 * Its constructor owns the fixed parameter schema and safe scheduling defaults. Data Asset instances
 * may tune values and priority, but runtime requests still receive isolated copies through the core.
 */
UCLASS(BlueprintType)
class GAMEPLAYACTIONSGRIDWORLD_API UGridMoveToCellActionDefinition
	: public UGameplayActionDefinition
{
	GENERATED_BODY()

public:
	UGridMoveToCellActionDefinition();
	virtual void PostLoad() override;
};
