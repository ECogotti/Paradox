using UnrealBuildTool;

public class FootstepSystem : ModuleRules
{
	public FootstepSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PhysicsCore",
			"Niagara"
		});
	}
}
