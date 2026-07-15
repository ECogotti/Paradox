// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PuzzleSystem : ModuleRules
{
	public PuzzleSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags"
			}
			);
	}
}
