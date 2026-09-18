using UnrealBuildTool;

public class ShineAIPaint : ModuleRules
{
    public ShineAIPaint(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ApplicationCore",
            "AssetRegistry",
            "AssetTools",
            "Core",
            "CoreUObject",
            "Engine",
            "HTTP",          // FGenericPlatformHttp::UrlEncode
            "ImageCore",
            "ImageWriteQueue",
            "InputCore",
            "Json",
            "JsonUtilities",
            "Projects",
            "RenderCore",
            "ShineEditor",   // UShineComfyAsset / FShineComfyClient / 共用的 ComfyUI 常连
            "ShineHttp",     // FShineHttpClient
            "UnrealEd",      // GEditor / USelection / AssetToolsModule
            "WebSockets"     // 常连 ComfyUI /ws，用事件代替轮询 /history
        });
    }
}
