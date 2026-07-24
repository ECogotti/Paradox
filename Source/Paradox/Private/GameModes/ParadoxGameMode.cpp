// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameModes/ParadoxGameMode.h"

#include "Paradox.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"

AParadoxGameMode::AParadoxGameMode()
{
	TimeLoopComponent = CreateDefaultSubobject<UParadoxTimeLoopComponent>(TEXT("TimeLoopComponent"));
}

void AParadoxGameMode::StartPlay()
{
	Super::StartPlay();

	if (!TimeLoopComponent)
	{
		PARADOX_LOG_ERROR(
			TEXT("GameMode '%s' has no Paradox Time Loop Component."),
			*GetNameSafe(this));
		return;
	}

	const FParadoxTimeLoopOperationResult Result = TimeLoopComponent->InitializeTimeLoop();
	if (!Result.IsSuccess())
	{
		PARADOX_LOG_ERROR(
			TEXT("Time-loop startup failed for GameMode '%s': %s"),
			*GetNameSafe(this),
			*Result.DiagnosticMessage);
	}
}
