using UnrealBuildTool;

public class ShineTextureEditor : ModuleRules
{
    public ShineTextureEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AppFramework",
            "AssetRegistry",
            "ApplicationCore",
            "AssetTools",
            "Core",
            "CoreUObject",
            "DesktopPlatform",
            "EditorFramework",
            "Engine",
            "GraphEditor",
            "InputCore",
            "Json",
            "JsonUtilities",
            "Projects",
            "PropertyEditor",
            "RenderCore",
            "RHI",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
