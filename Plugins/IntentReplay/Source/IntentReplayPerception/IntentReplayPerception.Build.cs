using UnrealBuildTool;

public class IntentReplayPerception : ModuleRules
{
	public IntentReplayPerception(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"IntentReplay",
			"PerceptionKnowledge"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"DeveloperSettings",
			"GameplayActions"
		});
	}
}
