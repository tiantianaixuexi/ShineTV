using UnrealBuildTool;

public class ShineTools : ModuleRules
{
	public ShineTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"DynamicMesh",
			"EditorFramework",
			"Engine",
			"GeometryCore",
			"GeometryFramework",
			"ImageWriteQueue",
			"InputCore",
			"LevelEditor",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd"
		});
	}
}
