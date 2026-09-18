#pragma once

#include "CoreMinimal.h"
#include "Paint/ShineAIPaintTypes.h"
#include "UObject/Object.h"
#include "ShineAIPaintAsset.generated.h"

class UMaterialInterface;
class UShineComfyAsset;
class UTexture2D;

/**
 * AI 贴图资产。
 *
 * 一个自包含的「AI 画贴图」工作单元：指认要画的**贴图**（新建一张，或从材质的贴图参数里指认），
 * 指认几个网格体用于 3D 预览与在模型上涂画，再配一组 AI 参数。
 * 画出来的结果直接写回那张贴图本身，不往关卡里放任何东西。
 */
UCLASS(BlueprintType, meta = (DisplayName = "Shine AI 贴图"))
class SHINEAIPAINT_API UShineAIPaintAsset : public UObject
{
    GENERATED_BODY()

public:
    UShineAIPaintAsset();

    // ---------- 要画的贴图 ----------

    /** 被绘制的贴图。可以是新建的，也可以是材质里已有的那张。 */
    UPROPERTY(EditAnywhere, Category = "贴图")
    TObjectPtr<UTexture2D> TargetTexture;

    /**
     * 用来"读取材质的贴图"的材质。
     * 面板上的「扫描材质贴图」会列出它的贴图参数，选中哪张就把哪张设成 TargetTexture。
     */
    UPROPERTY(EditAnywhere, Category = "贴图")
    TObjectPtr<UMaterialInterface> SourceMaterial;

    /** 新建贴图时的分辨率（仅在「新建贴图」时使用）。 */
    UPROPERTY(EditAnywhere, Category = "贴图", meta = (ClampMin = "64", ClampMax = "4096"))
    int32 NewTextureSize = 1024;

    // ---------- 预览用网格体 ----------

    /** 参与 3D 预览 / 在模型上涂画的网格体（UStaticMesh / USkeletalMesh）。 */
    UPROPERTY(EditAnywhere, Category = "目标")
    TArray<TSoftObjectPtr<UObject>> MeshAssets;

    // ---------- AI ----------

    /** 指认的 ShineComfy 资产：勾选"跟随"时用它里面的服务地址。 */
    UPROPERTY(EditAnywhere, Category = "AI")
    TSoftObjectPtr<UShineComfyAsset> ComfyAsset;

    /** 是否用 ShineComfy 资产里的地址覆盖 Settings.ServiceUrl。 */
    UPROPERTY(EditAnywhere, Category = "AI")
    bool bUseComfyAssetUrl = true;

    /** AI 更新参数。 */
    UPROPERTY(EditAnywhere, Category = "AI")
    FShineAIPaintSettings Settings;

    // ---------- 遮罩 ----------

    /** 遮罩像素：0 = 不遮罩，255 = 完全遮罩（尺寸跟随 TargetTexture）。 */
    UPROPERTY()
    TArray<uint8> MaskPixels;

    /** 实际生效的服务地址（跟随 ShineComfy 资产时取它的 ComfyBaseUrl）。 */
    FString ResolveServiceUrl() const;

    /** 是否用的是 ShineComfy 资产里的地址。 */
    bool IsUsingComfyAssetUrl() const;

    /** 要画的贴图尺寸；没有贴图时返回 0。 */
    FIntPoint GetTargetSize() const;

    /** 遮罩缓存尺寸是否跟目标贴图一致；不一致会按需要重建。 */
    void EnsureMaskBuffer(int32 Width, int32 Height);

    /** 清空遮罩。 */
    void ClearMaskPixels();

    virtual void PostLoad() override;
};
