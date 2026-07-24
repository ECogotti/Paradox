using UnrealBuildTool;

public class IntentReplay : ModuleRules
{
	public IntentReplay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// GameplayActions is intentionally the only gameplay-system dependency. IntentReplay remains
		// independent from AI, Behavior Tree, GridWorld, editor modules, and project-specific code.
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"GameplayActions"
		});
	}
}
