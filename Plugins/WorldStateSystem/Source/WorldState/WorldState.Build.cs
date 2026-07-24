using UnrealBuildTool;

public class WorldState : ModuleRules
{
	public WorldState(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Keep the runtime portable: reflection, world/Actor APIs and core containers are its only module needs.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			});
	}
}
