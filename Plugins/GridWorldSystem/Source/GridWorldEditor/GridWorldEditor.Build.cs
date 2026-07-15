// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GridWorldEditor : ModuleRules
{
	public GridWorldEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GridWorld",
				"LevelEditor",
				"NavigationSystem",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd"
			});
	}
}
