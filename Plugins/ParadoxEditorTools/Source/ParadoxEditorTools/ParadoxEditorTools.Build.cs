using UnrealBuildTool;

public class ParadoxEditorTools : ModuleRules
{
    public ParadoxEditorTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "AssetRegistry",
                "ContentBrowser",
                "CoreUObject",
                "Engine",
                "MeshDescription",
                "Slate",
                "SlateCore",
                "StaticMeshDescription",
                "ToolMenus",
                "UnrealEd",
                "InputCore",
            });
    }
}
