#pragma once

#include "CoreMinimal.h"
#include "Paint/ShineAIPaintTypes.h"

/** 一次 AI 更新贴图的结果。 */
struct FShineAIPaintResult
{
    bool bSuccess = false;
    FString ErrorMessage;

    /** 给 UI 显示的过程说明（上传/提交/轮询到第几步）。 */
    FString LogText;

    /** AI 返回的整张图（BGRA8）。 */
    TArray<FColor> Pixels;
    int32 Width = 0;
    int32 Height = 0;
};

/** 贴图编解码小工具，UI 的"导出"按钮也会用到。 */
namespace ShineAIPaintImage
{
    /** FColor(BGRA8) → PNG 字节。 */
    SHINEAIPAINT_API bool EncodePng(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutPngBytes);

    /** PNG 字节 → FColor(BGRA8)。 */
    SHINEAIPAINT_API bool DecodePng(const TArray<uint8>& PngBytes, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);

    /**
     * 遮罩图层 → 单通道灰度 PNG。
     * bWhiteIsEditable=true 时输出"白 = 可重绘"，即 inpaint 工作流期望的遮罩。
     */
    SHINEAIPAINT_API bool EncodeMaskPng(const TArray<uint8>& MaskPixels, int32 Width, int32 Height, bool bWhiteIsEditable, TArray<uint8>& OutPngBytes);

    SHINEAIPAINT_API bool SavePngToFile(const TArray<uint8>& PngBytes, const FString& FilePath);
}

/**
 * AI 贴图服务：把"当前画布 + 遮罩"发给 AI，拿回一张新图。
 *
 * 支持两种后端：
 *  - ComfyUI：/upload/image + /upload/mask → /prompt（内置 inpaint 工作流）
 *    → 常连的 /ws 用事件通知进度和完成（连不上才退回轮询 /history）→ /view 取图。
 *  - GenericHttp：POST JSON（base64 原图/遮罩）→ 返回 JSON（base64 结果图）。
 */
class SHINEAIPAINT_API FShineAIPaintService
{
public:
    using FCompletion = TFunction<void(FShineAIPaintResult&&)>;
    using FOnCheckpointsFetched = TFunction<void(bool bSuccess, const TArray<FString>& Checkpoints, const FString& ErrorMessage)>;
    using FProgress = TFunction<void(const FString& /*ProgressText*/)>;

    /**
     * 发起一次 AI 更新。
     * @param BasePixels  当前底色图层（BGRA8），尺寸必须是 Width*Height。
     * @param MaskPixels  遮罩图层（0=不遮罩，255=完全遮罩），尺寸必须是 Width*Height。
     * @param Progress    过程回调（走 WebSocket 的实时进度），可以传空。
     */
    static void RequestTexture(
        const TArray<FColor>& BasePixels,
        int32 Width,
        int32 Height,
        const TArray<uint8>& MaskPixels,
        const FShineAIPaintSettings& Settings,
        FProgress&& Progress,
        FCompletion&& Completion);

    /** 从 ComfyUI 拉取可用的 checkpoint 列表（/object_info/CheckpointLoaderSimple）。 */
    static void FetchCheckpoints(const FString& ServiceUrl, FOnCheckpointsFetched&& Completion);
};
