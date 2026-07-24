using UnrealBuildTool;

public class WorldStateEditor : ModuleRules
{
	public WorldStateEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// All authoring dependencies stay private so none of them leak into the WorldState runtime module.
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph",
				"Core",
				"CoreUObject",
				"Engine",
				"Kismet",
				"KismetCompiler",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"WorldState"
			});
	}
}
