using UnrealBuildTool;

public class TacticalPause : ModuleRules
{
	public TacticalPause(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public types expose UMG and developer settings to consuming runtime modules.
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"CommonUI",
			"UMG"
		});
	}
}
