#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShineComfyBridgeLibrary.generated.h"

/**
 * ComfyUI 桥接：给 UE 侧提供「同步、可脚本化」的 ComfyUI 调用与
 * Shine Comfy 图资产操作。
 *
 * 与插件里已有的 FShineComfyClient（异步、UI 驱动）互补：
 * 这里的函数是阻塞式的、立即返回结果的，专门服务于 MCP 工具与 Python 自动化脚本。
 *
 * 所有函数都返回 JSON 文本，形如 {"success":true,...} 或 {"success":false,"error":"..."}。
 */
UCLASS()
class SHINEMCP_API UShineComfyBridgeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------- 连接

    /** 本地 HTTP 探活（不依赖 ComfyUI），用于确认 MCP 服务本身可用。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString PingComfy(const FString& BaseUrl);

    /** 拉取 /object_info/<ClassType>，用于确认某个节点类是否存在。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString GetComfyObjectInfo(const FString& BaseUrl, const FString& ClassType);

    /** 列出当前 ComfyUI 可用的 checkpoint 名称（从 CheckpointLoaderSimple 的 object_info 里取）。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString ListCheckpoints(const FString& BaseUrl);

    // ------------------------------------------------------------ 图片与任务

    /** 上传本地图片到 ComfyUI input 目录，返回 {"name": ...}。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString UploadImage(const FString& BaseUrl, const FString& FilePath, const FString& Subfolder, bool bOverwrite = true);

    /** 提交 workflow（prompt JSON 文本），返回 prompt_id。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString SubmitPrompt(const FString& BaseUrl, const FString& PromptJson, const FString& ClientId);

    /** 查询队列状态（running / pending 数量）。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString GetQueueStatus(const FString& BaseUrl);

    /**
     * 查询某个 prompt 的执行结果。
     * 返回 {"status":"pending|running|done|error", "images":[{"filename","subfolder","type"}]}。
     */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString GetPromptResult(const FString& BaseUrl, const FString& PromptId);

    /** 下载 ComfyUI 输出图片到本地路径。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString DownloadImage(const FString& BaseUrl, const FString& Filename, const FString& Subfolder, const FString& Type, const FString& SavePath);

    /** 中断当前执行。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString Interrupt(const FString& BaseUrl);

    // ------------------------------------------------------- Shine Comfy 图资产

    /** 新建一个 Shine Comfy 图资产（UShineComfyAsset）并返回资产路径。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString CreateComfyGraphAsset(const FString& PackagePath, const FString& AssetName, const FString& ComfyBaseUrl);

    /** 用 JSON 重建资产内的图（支持 Shine Graph JSON / ComfyUI workflow / API prompt）。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString BuildComfyGraphFromJson(const FString& AssetPath, const FString& GraphJson);

    /** 把资产内的图导出成可提交给 ComfyUI 的 prompt JSON。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString ExportComfyPromptJson(const FString& AssetPath);

    /** 导出图的可读定义（节点/连线），便于调试。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString ExportComfyGraphDefinition(const FString& AssetPath);

    /** 在编辑器中打开资产。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString OpenAsset(const FString& AssetPath);

    // -------------------------------------------------------------- 结果回读

    /** 把磁盘上的图片导入为 UTexture2D 资产，便于在编辑器里直接查看生成结果。 */
    UFUNCTION(BlueprintCallable, Category = "Shine|Comfy")
    static FString ImportTextureFromFile(const FString& SourceFile, const FString& PackagePath, const FString& AssetName);
};
