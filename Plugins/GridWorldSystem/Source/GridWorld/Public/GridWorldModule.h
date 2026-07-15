// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

GRIDWORLD_API DECLARE_LOG_CATEGORY_EXTERN(LogGridWorld, Log, All);

/** Writes an informational message to the module-owned GridWorld log category. */
#define GRIDWORLD_LOG_INFO(Format, ...) UE_LOG(LogGridWorld, Log, TEXT(Format), ##__VA_ARGS__)

/** Writes a recoverable GridWorld configuration or runtime warning. */
#define GRIDWORLD_LOG_WARNING(Format, ...) UE_LOG(LogGridWorld, Warning, TEXT(Format), ##__VA_ARGS__)

/** Writes a GridWorld operation or configuration error. */
#define GRIDWORLD_LOG_ERROR(Format, ...) UE_LOG(LogGridWorld, Error, TEXT(Format), ##__VA_ARGS__)

/** Returns whether GridWorld visual debugging is globally enabled. */
GRIDWORLD_API bool IsGridWorldVisualDebugEnabled();

/** Runtime module entry point for GridWorldSystem. */
class FGridWorldModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
