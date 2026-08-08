using UnrealBuildTool;

public class ParadoxMaterialExpressions : ModuleRules
{
    public ParadoxMaterialExpressions(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
            });
    }
}
