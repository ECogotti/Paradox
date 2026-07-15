// Copyright Epic Games, Inc. All Rights Reserved.

#include "PuzzleSystem.h"

#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "FPuzzleSystemModule"

DEFINE_LOG_CATEGORY(LogPuzzleSystem);

static TAutoConsoleVariable<int32> CVarPuzzleSystemDebug(
	TEXT("PuzzleSystem.Debug"),
	0,
	TEXT("Enables verbose PuzzleSystem runtime logging when non-zero."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarPuzzleSystemDebugVisual(
	TEXT("PuzzleSystem.Debug.Visual"),
	1,
	TEXT("Allows PuzzleSystem visual debug drawing when non-zero. Actors must also enable local debug."),
	ECVF_Default);

bool IsPuzzleSystemDebugEnabled()
{
	return CVarPuzzleSystemDebug.GetValueOnAnyThread() != 0;
}

bool IsPuzzleSystemDebugVisualEnabled()
{
	return CVarPuzzleSystemDebugVisual.GetValueOnAnyThread() != 0;
}

void FPuzzleSystemModule::StartupModule()
{
}

void FPuzzleSystemModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FPuzzleSystemModule, PuzzleSystem)
