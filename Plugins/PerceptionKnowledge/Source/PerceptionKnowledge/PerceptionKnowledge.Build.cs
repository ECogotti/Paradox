using UnrealBuildTool;

public class PerceptionKnowledge : ModuleRules
{
	public PerceptionKnowledge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"GameplayTags",
			"DeveloperSettings"
		});
	}
}
