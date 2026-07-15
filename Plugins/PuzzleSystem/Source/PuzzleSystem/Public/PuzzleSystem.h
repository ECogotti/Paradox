// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPuzzleSystem, Log, All);

/** Writes an informational message to the module-owned PuzzleSystem log category. */
#define PUZZLESYSTEM_LOG_INFO(Format, ...) UE_LOG(LogPuzzleSystem, Log, TEXT(Format), ##__VA_ARGS__)

/** Writes a recoverable configuration/runtime warning to the module-owned PuzzleSystem log category. */
#define PUZZLESYSTEM_LOG_WARNING(Format, ...) UE_LOG(LogPuzzleSystem, Warning, TEXT(Format), ##__VA_ARGS__)

/** Writes a non-recoverable operation/configuration error to the module-owned PuzzleSystem log category. */
#define PUZZLESYSTEM_LOG_ERROR(Format, ...) UE_LOG(LogPuzzleSystem, Error, TEXT(Format), ##__VA_ARGS__)

/**
 * Returns whether optional verbose runtime logging is enabled for PuzzleSystem.
 *
 * @return True when the `PuzzleSystem.Debug` console variable is non-zero.
 */
PUZZLESYSTEM_API bool IsPuzzleSystemDebugEnabled();

/**
 * Returns whether module-level visual debug drawing is allowed.
 *
 * @return True when the `PuzzleSystem.Debug.Visual` console variable is non-zero.
 */
PUZZLESYSTEM_API bool IsPuzzleSystemDebugVisualEnabled();

/** Runtime module entry point for the PuzzleSystem plugin. */
class FPuzzleSystemModule : public IModuleInterface
{
public:

	/** Called when Unreal loads the PuzzleSystem module. */
	virtual void StartupModule() override;

	/** Called before Unreal unloads the PuzzleSystem module. */
	virtual void ShutdownModule() override;
};
