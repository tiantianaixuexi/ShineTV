using UnrealBuildTool;

public class ShineMCP : ModuleRules
{
    public ShineMCP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DeveloperSettings",
            "Json",
            "JsonUtilities",
            "Sockets",
            "Networking",
            "RenderCore",
            "ImageWriteQueue",
            "UnrealEd",
            "AssetRegistry",
            "AssetTools",
            "ShineEditor",
            "ShineHttp"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ApplicationCore",
            "EditorScriptingUtilities",
            "EditorSubsystem",
            "InputCore",
            "LevelEditor",
            "Projects",
            "PythonScriptPlugin",
            "Slate",
            "SlateCore",
            "ToolMenus"
        });
    }
}
