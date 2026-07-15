// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class AGridNavigationData;

/** Editor module entry point for GridWorldSystem. */
class FGridWorldEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Registers Level Editor commands and toolbar/menu entries without a custom window. */
	void RegisterMenus();
	/** Builds every Grid nav data instance in the current editor World. */
	void BuildAll();
	/** Rebuilds regions intersecting the current editor selection. */
	void RebuildSelected();
	/** Clears generated GridWorld data from the current level. */
	void ClearAll();
	/** Runs bounds/topology validation and reports results through notifications/logging. */
	void ValidateAll();
	/** Logs cell data under the current editor selection/cursor context. */
	void InspectSelectedCell();
	/** Selects the configured GridWorld Supported Agent for navigation workflows. */
	void UseGridWorldAgent();
	/** Toggles one AGridNavigationData debug member across editor navigation instances. */
	void ToggleDebugFlag(bool AGridNavigationData::* Flag);
	/** @return True when the supplied debug member is enabled on the active Grid nav data. */
	bool IsDebugFlagEnabled(bool AGridNavigationData::* Flag) const;
};
