// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Paradox : ModuleRules
{
	public Paradox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"CommonUI",
			"DeveloperSettings",
			"Engine",
			"AIModule",
			"FootstepSystem",
			"InputCore",
			"EnhancedInput",
			"GameplayActions",
			"GameplayActionsGridWorld",
			"GameplayTags",
			"GridWorld",
			"IntentReplay",
			"IntentReplayPerception",
			"LineOfSight",
			"NavigationSystem",
			"PerceptionKnowledge",
			"PhysicsCore",
			"ProceduralMeshComponent",
			"PuzzleSystem",
			"SmartObjectsModule",
			"EntityRelations",
			"WorldState",
			"TacticalPause",
			"Niagara",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore"
		});
		if (Target.bBuildEditor)
		{
			// Transient Blueprint composition tests; no editor dependency in game targets.
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "UMGEditor", "KismetCompiler", "NiagaraEditor", "AssetRegistry" });
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
