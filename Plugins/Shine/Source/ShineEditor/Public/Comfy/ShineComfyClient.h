#pragma once

#include "CoreMinimal.h"
#include "Comfy/ShineComfyTypes.h"

class FJsonObject;

class SHINEEDITOR_API FShineComfyClient
{
public:
    using FOnModelsFetched = TFunction<void(FShineComfyModelsResult&&)>;
    using FOnNodesFetched = TFunction<void(FShineComfyNodesResult&&)>;
    using FOnQueueFetched = TFunction<void(FShineComfyQueueResult&&)>;
    using FOnPromptSubmitted = TFunction<void(FShineComfyPromptSubmitResult&&)>;
    using FOnQueueOperationCompleted = TFunction<void(FShineComfyQueueOperationResult&&)>;
    using FOnHistoryFetched = TFunction<void(FShineComfyHistoryResult&&)>;
    using FOnUploadCompleted = TFunction<void(FShineComfyUploadResult&&)>;
    using FOnMediaDownloaded = TFunction<void(FShineComfyMediaDownloadResult&&)>;
    using FOnFreeVramCompleted = TFunction<void(FShineComfyFreeVramResult&&)>;
    using FOnSystemStatsFetched = TFunction<void(FShineComfySystemStatsResult&&)>;

    static FString NormalizeBaseUrl(const FString& BaseUrl);
    static FString BuildApiUrl(const FString& BaseUrl, const FString& RelativePath);
    static FString BuildWebSocketUrl(const FString& BaseUrl, const FString& ClientId);

    static void FetchModels(const FString& BaseUrl, FOnModelsFetched&& Callback);
    static void FetchModels(const FString& BaseUrl, const FShineComfyFetchOptions& Options, FOnModelsFetched&& Callback);
    static void FetchNodes(const FString& BaseUrl, FOnNodesFetched&& Callback);
    static void FetchNodes(const FString& BaseUrl, const FShineComfyFetchOptions& Options, FOnNodesFetched&& Callback);
    static void FetchQueue(const FString& BaseUrl, FOnQueueFetched&& Callback);
    static void FetchHistory(const FString& BaseUrl, int32 MaxItems, FOnHistoryFetched&& Callback);

    /**
     * 只查一个 prompt 的历史。
     *
     * 为什么必须有这个：/history 不支持按 id 过滤，拉整份再筛在队列很脏时既慢又容易
     * 筛错；而且**执行事件里根本拿不到视频产出** —— FShineComfySocket 的 executed
     * 事件只带了 output.images[0] 那一个字段，SaveVideo 的产物虽然也在 "images" 键里，
     * 但 socket 侧只取了第一个且没做分类。所以"某次任务到底产出了什么"只能靠这里回读。
     *
     * 返回的 Entries 最多一条（该 id 存在时）。
     */
    static void FetchHistoryForPrompt(const FString& BaseUrl, const FString& PromptId, FOnHistoryFetched&& Callback);

    /**
     * 从 /view 抓一份媒体（图片 / 视频 / 音频）到本地。
     *
     * 收编原先散在 SShineMainPanel 里的那段 /view 下载：同一条通道、同一套错误处理，
     * 且不再限于图片。目标目录不存在时自动创建。
     * 走的是二进制 GET，所以直接用 FHttpModule —— FShineHttpClient 只封装了 JSON。
     */
    static void DownloadMedia(const FString& BaseUrl, const FShineComfyHistoryMedia& Media, const FString& DestinationPath, FOnMediaDownloaded&& Callback);

    /**
     * POST /api/free：卸载 ComfyUI 的模型缓存、释放显存。
     *
     * H3 的底座 + Qwen3-VL-32B 文本编码器会同时常驻，显存不足时 ComfyUI 不报错而是
     * 开始换页、表现为越来越慢直到卡死。任务执行器每段生成前调这个，再等空闲显存
     * 达到阈值才提交。
     */
    static void FreeVram(const FString& BaseUrl, FOnFreeVramCompleted&& Callback);

    /**
     * GET /api/system_stats：读整卡空闲显存。
     *
     * 显存门禁的两个动作必须成对、且顺序固定：**先 FreeVram 卸载，再轮询这个接口**。
     * 反过来（先轮询再卸载）会永远等不到——Dynamic VRAM 的取向就是尽量占满显存，
     * 模型还没卸的时候空闲显存读数本来就低。
     */
    static void FetchSystemStats(const FString& BaseUrl, FOnSystemStatsFetched&& Callback);

    static void SubmitPrompt(const FString& BaseUrl, const FShineComfyPromptSubmitRequest& Request, FOnPromptSubmitted&& Callback);
    static void UploadImage(const FString& BaseUrl, const FString& FileName, const TArray<uint8>& PngBytes, bool bIsMask, FOnUploadCompleted&& Callback);
    static void DeleteQueueItems(const FString& BaseUrl, const TArray<FString>& PromptIds, FOnQueueOperationCompleted&& Callback);
    static void InterruptPrompt(const FString& BaseUrl, const FString& PromptId, FOnQueueOperationCompleted&& Callback);
    static void ApplyModelLibraryToNodeDefinitions(TArray<FShineComfyNodeDefinition>& NodeDefinitions, const TArray<FShineComfyModelFolder>& ModelFolders);

    /** 按扩展名判定媒体种类。视频和图片会同处 "images" 键，只能靠这个分。 */
    static EShineComfyMediaKind ClassifyMediaKind(const FString& FileName);

private:
    static bool ParseNodeDefinitions(const TSharedPtr<FJsonObject>& RootObject, TArray<FShineComfyNodeDefinition>& OutNodes, FString& OutErrorMessage);
};
