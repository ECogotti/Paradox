// Copyright Epic Games, Inc. All Rights Reserved.

#include "GridWorldModule.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogGridWorld);

static TAutoConsoleVariable<int32> CVarGridWorldVisualDebug(
	TEXT("GridWorld.Debug.Visual"),
	1,
	TEXT("Globally enables GridWorld visual debugging. Local Navigation Data drawing must also be enabled."),
	ECVF_Default);

bool IsGridWorldVisualDebugEnabled()
{
	return CVarGridWorldVisualDebug.GetValueOnAnyThread() != 0;
}

void FGridWorldModule::StartupModule()
{
	GRIDWORLD_LOG_INFO("GridWorld runtime module started.");
}

void FGridWorldModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FGridWorldModule, GridWorld)
