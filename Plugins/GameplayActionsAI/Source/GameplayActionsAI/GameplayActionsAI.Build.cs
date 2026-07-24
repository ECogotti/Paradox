using UnrealBuildTool;

public class GameplayActionsAI : ModuleRules
{
	public GameplayActionsAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"GameplayActions",
			"GameplayStateTreeModule",
			"GameplayTags",
			"StateTreeModule"
		});
	}
}
