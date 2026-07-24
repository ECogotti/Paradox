// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ParadoxGameMode.generated.h"

class UParadoxTimeLoopComponent;

/**
 *  Simple Game Mode for a top-down perspective game
 *  Sets the default gameplay framework classes
 *  Check the Blueprint derived class for the set values
 */
UCLASS(abstract)
class AParadoxGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor */
	AParadoxGameMode();

	virtual void StartPlay() override;

	UFUNCTION(BlueprintPure, Category = "Paradox|Time Loop")
	UParadoxTimeLoopComponent* GetTimeLoopComponent() const { return TimeLoopComponent; }

private:
	/** Unique authority for recording, reset and clone reconstruction in this GameMode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Time Loop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxTimeLoopComponent> TimeLoopComponent;
};



