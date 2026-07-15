// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GridWorld : ModuleRules
{
	public GridWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AIModule",
				"GameplayStateTreeModule",
				"GameplayTasks",
				"NavigationSystem",
				"StateTreeModule"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RenderCore"
			});
	}
}
