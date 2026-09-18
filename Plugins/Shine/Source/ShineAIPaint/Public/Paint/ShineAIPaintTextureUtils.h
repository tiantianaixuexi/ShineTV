#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UTexture2D;

/**
 * AI 贴图用到的贴图工具。
 *
 * 负责：新建一张可绘制的贴图资产、读写贴图源像素、从材质里把贴图指认出来。
 * 所有写入都会走 FTextureSource + UpdateResource，所以改的是"那张贴图本身"。
 */
namespace ShineAIPaintTextureUtils
{
    /** 贴图能不能拿来画（必须是 2D、源数据有效、非 HDR/非压缩源）。 */
    SHINEAIPAINT_API bool IsPaintable(UTexture2D* Texture, FString& OutReason);

    /**
     * 新建一张可绘制的贴图资产（BGRA8 源、单 mip、可读可画），并立即落盘。
     * @param PackagePath  例如 /Game/ShineAIPaint
     * @param BaseName     资产基础名，重名会自动加数字后缀
     */
    SHINEAIPAINT_API UTexture2D* CreatePaintableTextureAsset(
        const FString& PackagePath,
        const FString& BaseName,
        int32 Size,
        const FColor& FillColor,
        FString& OutErrorMessage);

    /** 读取贴图源像素，统一成 BGRA8 / FColor。 */
    SHINEAIPAINT_API bool ReadTexturePixels(
        UTexture2D* Texture,
        TArray<FColor>& OutPixels,
        int32& OutWidth,
        int32& OutHeight,
        FString& OutErrorMessage);

    /**
     * 贴图能不能"原地更新"——也就是不改源数据、不重建资源，直接把像素写进已建好的 GPU 贴图。
     * 只有平台格式是未压缩 BGRA8/RGBA8、且尺寸一致时才行；压缩贴图（BC1/BC7…）里直接写
     * BGRA8 会把数据写坏，所以必须走源数据 + 重建。
     */
    SHINEAIPAINT_API bool SupportsInPlaceUpdate(UTexture2D* Texture, FString& OutReason);

    /**
     * 原地更新贴图（快、不重建）：
     *   关掉流式 → UpdateTextureRegions 直接改 GPU 贴图 → 同步 CPU 源数据（便宜）。
     * 涂画过程中用这个，材质/缩略图会立刻跟着变。
     * 平台格式是压缩格式时返回 false，请改用 WriteTexturePixels。
     */
    SHINEAIPAINT_API bool UpdateTextureInPlace(
        UTexture2D* Texture,
        const TArray<FColor>& Pixels,
        int32 Width,
        int32 Height,
        FString& OutErrorMessage);

    /** 只把像素同步进贴图的 CPU 源数据（不刷新资源、不重建）。 */
    SHINEAIPAINT_API bool SyncTextureSource(
        UTexture2D* Texture,
        const TArray<FColor>& Pixels,
        int32 Width,
        int32 Height,
        FString& OutErrorMessage);

    /** 把 BGRA8 像素写回源数据并刷新资源（会重建贴图）；保存 / AI 出图后用这个落定。 */
    SHINEAIPAINT_API bool WriteTexturePixels(
        UTexture2D* Texture,
        const TArray<FColor>& Pixels,
        int32 Width,
        int32 Height,
        FString& OutErrorMessage);

    /** 从材质里收集贴图参数：参数名 + 对应的贴图。 */
    SHINEAIPAINT_API void CollectMaterialTextures(
        UMaterialInterface* Material,
        TArray<FName>& OutParameterNames,
        TArray<UTexture2D*>& OutTextures);
}
