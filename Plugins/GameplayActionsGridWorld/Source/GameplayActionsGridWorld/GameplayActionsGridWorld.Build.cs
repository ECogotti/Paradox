using UnrealBuildTool;

public class GameplayActionsGridWorld : ModuleRules
{
	public GameplayActionsGridWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"GameplayActions",
			"GameplayTags",
			"GameplayTasks",
			"GridWorld",
			"NavigationSystem"
		});
	}
}
