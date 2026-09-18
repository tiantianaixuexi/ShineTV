using UnrealBuildTool;

public class ShineHttp : ModuleRules
{
    public ShineHttp(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "HTTP",
            "Json"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "JsonUtilities"
        });
    }
}