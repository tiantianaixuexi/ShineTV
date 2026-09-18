using UnrealBuildTool;

public class ShineEditor : ModuleRules
{
    public ShineEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Comfy/ShineComfySocket.h 是对外公开的，里面直接用到了 IWebSocket，
        // 所以 WebSockets 必须放在 public，下游模块才拿得到它的头文件路径。
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "WebSockets"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ApplicationCore",
            "AssetRegistry",
            "AssetTools",
            "Core",
            "CoreUObject",
            "DeveloperSettings",
            "Engine",
            "EditorFramework",
            "GraphEditor",
            "HTTP",
            "ImageCore",
            "ImageWriteQueue",
            "InputCore",
            "Json",
            "JsonUtilities",
            "LevelEditor",
            // 视频工作台的节点要在画布上直接播 ComfyUI 出的 mp4（含音轨）：
            // MediaPlayer + MediaTexture 归 MediaAssets，跨平台的播放器辅助在 MediaUtils。
            "MediaAssets",
            "MediaUtils",
            // 视频项目的资产编辑器直接用 Details 面板编辑分镜表（UPROPERTY 天生就能编辑，
            // 自造表单只会做得更差），所以要依赖 PropertyEditor。
            "PropertyEditor",
            "RenderCore",
            "Projects",
            "ShineHttp",
            "ShineWebSocket",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
