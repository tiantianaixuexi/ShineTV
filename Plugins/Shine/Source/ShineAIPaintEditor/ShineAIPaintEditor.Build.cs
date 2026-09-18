using UnrealBuildTool;

public class ShineAIPaintEditor : ModuleRules
{
    public ShineAIPaintEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ApplicationCore",
            "AssetRegistry",
            "AssetTools",
            "Core",
            "CoreUObject",
            "EditorFramework",
            "Engine",
            "ImageCore",
            "InputCore",
            "LevelEditor",
            "Projects",
            "PropertyEditor",   // SObjectPropertyEntryBox（资产挑选框）
            "RenderCore",
            "ShineAIPaint",   // 逻辑层
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
