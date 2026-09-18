#pragma once

#include "CoreMinimal.h"
#include "Paint/ShineAIPaintTypes.h"
#include "UObject/StrongObjectPtr.h"

class UShineAIPaintAsset;
class UTexture2D;

/**
 * AI 贴图绘制会话。
 *
 * 一个打开着的「Shine AI 贴图」资产对应一个会话：
 *  - 绘制的目标就是资产里那张 **UTexture2D**（TargetTexture），尺寸也由它决定；
 *  - BasePixels / MaskPixels 是编辑期的 CPU 工作副本；
 *  - 画布与预览用一张临时贴图做实时显示（快，不走贴图重建）；
 *  - SaveToAsset() 才把像素写回 TargetTexture 的源数据（会重建贴图）。
 * 全程不往关卡里放东西，也不改关卡里的材质。
 */
class SHINEAIPAINT_API FShineAIPaintSession : public TSharedFromThis<FShineAIPaintSession>
{
public:
    FShineAIPaintSession(UShineAIPaintAsset* InAsset);

    /** 为某个 AI 贴图资产创建会话（每个打开着的资产编辑器一个）。 */
    static TSharedPtr<FShineAIPaintSession> CreateForAsset(UShineAIPaintAsset* InAsset);

    UShineAIPaintAsset* GetAsset() const { return Asset.Get(); }

    /** 被绘制的贴图（可能与资产的 TargetTexture 一致；没有则为空）。 */
    UTexture2D* GetTargetTexture() const;

    /** 有没有可以画的贴图。 */
    bool HasPaintTarget() const { return Width > 0 && Height > 0 && GetTargetTexture() != nullptr; }

    /** 没有可画贴图时的原因说明。 */
    const FString& GetTargetTextureIssue() const { return TargetTextureIssue; }

    /** 目标贴图能不能原地更新（未压缩 BGRA8/RGBA8）。 */
    bool CanUpdateTargetInPlace() const { return bCanUpdateTargetInPlace; }

    /** 关于"贴图什么时候真正更新"的说明，给 UI 显示。 */
    const FString& GetTargetUpdateHint() const { return TargetUpdateHint; }

    /** 一笔开始（鼠标按下）。 */
    void BeginStroke();

    /**
     * 一笔结束（鼠标抬起）。
     * 能原地更新就把这一笔直接写进目标贴图（不改源结构、不重建）；
     * 压缩格式的贴图原地写不进去，只能等"写入贴图"/保存时走源数据 + 重建。
     */
    void EndStroke();

    /** 从资产读入贴图像素 / 参数 / 遮罩 / 网格列表（打开编辑器、或换了贴图后调用）。 */
    void LoadFromAsset();

    /** 把参数、遮罩、网格列表与**像素**写回资产（会重建被画的贴图）。 */
    void SaveToAsset();

    // ---------- 目标网格体（只用于 3D 预览和在模型上涂画） ----------
    static bool IsSupportedMeshAsset(const UObject* Object);
    int32 AddTargetsFromSelection(bool bClearExisting);
    void RemoveTarget(int32 Index);
    void ClearTargets();
    const TArray<FShineAIPaintTarget>& GetTargets() const { return Targets; }
    FString BuildTargetsSummary() const;

    // ---------- 图层数据 ----------
    int32 GetWidth() const { return Width; }
    int32 GetHeight() const { return Height; }
    const TArray<FColor>& GetBasePixels() const { return BasePixels; }
    const TArray<uint8>& GetMaskPixels() const { return MaskPixels; }
    bool IsMaskEmpty() const { return MaskPaintedCount <= 0; }

    /** 把底色图层重置为纯色，并记录为"擦除还原目标"。 */
    void ResetBasePixels(const FColor& FillColor = FColor(190, 190, 196, 255));

    /** 清空遮罩图层。 */
    void ClearMask();

    /** 显示用底色贴图（临时贴图，实时刷新）。 */
    UTexture2D* GetBaseTexture();

    /** 显示用遮罩叠加贴图。 */
    UTexture2D* GetMaskTexture();

    // ---------- 绘制 ----------
    void PaintStroke(const FVector2D& FromUV, const FVector2D& ToUV, float InBrushRadiusUV, const FLinearColor& Color, EShineAIPaintTool Tool);
    void PaintStrokeWrapped(const FVector2D& FromUV, const FVector2D& ToUV, float InBrushRadiusUV, const FLinearColor& Color, EShineAIPaintTool Tool);
    void FillAll(const FLinearColor& Color);

    // ---------- AI ----------
    bool ApplyAIPixels(const TArray<FColor>& InPixels, int32 InWidth, int32 InHeight, FString& OutErrorMessage);
    bool RequestAIUpdate(FString& OutErrorMessage);

    // ---------- 动态更新 ----------

    /**
     * 动态更新：每画完一笔（抬笔）自动重跑一次 AI。
     *
     * 只在"这一笔真的改了像素"时才触发，且同一时刻只允许一个请求在飞；
     * 等待期间又画了新的，会把旧结果丢掉、按最新画布重跑一次，不会排队堆请求。
     */
    bool IsLiveAIUpdateEnabled() const { return bLiveAIUpdate; }
    void SetLiveAIUpdateEnabled(bool bEnabled);

    /** 画布内容版本号：底色 / 遮罩真的被改动一次就 +1。 */
    int32 GetPaintRevision() const { return PaintRevision; }

    /**
     * 动态更新：如果开关开着、画布确实有变化、且没有请求在跑，就发起一次 AI 更新。
     *
     * 抬笔会自动调；"填充底色 / 清空遮罩 / 重置画布"这类会改像素的按钮也调一下。
     * 有变化但已经有请求在跑时不丢弃：只记一个"待发"标记，
     * 等当前这次结果回来后再发一次（队列一个一个传）。
     */
    void MaybeRequestLiveAIUpdate();

    /**
     * 常连 WebSocket 推过来的实时进度文本。
     *
     * 只读字段，不广播通知 —— 进度每一步都广播会把整个 UI 刷爆，
     * 面板那边用拉取式的 lambda 读它就够了。
     */
    const FString& GetLiveProgress() const { return LiveProgressText; }

    const FString& GetLastStatus() const { return LastStatus; }
    void SetLastStatus(const FString& InStatus);

    // ---------- 通知 ----------
    DECLARE_MULTICAST_DELEGATE(FShineAIPaintSessionSignal);
    FShineAIPaintSessionSignal OnPixelsChanged;
    FShineAIPaintSessionSignal OnStructureChanged;

    /** 可编辑参数（与资产通过 LoadFromAsset / SaveToAsset 同步）。 */
    FShineAIPaintSettings Settings;

    float BrushRadiusUV = 0.035f;
    FLinearColor BrushColor = FLinearColor(0.85f, 0.42f, 0.22f, 1.0f);
    EShineAIPaintTool CurrentTool = EShineAIPaintTool::Paint;
    float ViewZoom = 0.5f;

    /**
     * 是否在 3D 模型上直接涂画。
     *
     * 默认关：开着的时候左键会被涂画吃掉，而 UE 视口里左键拖拽就是旋转视角，
     * 用户会以为是"视角坏了"。要涂模型得先在视口页签里勾上。
     */
    bool bPaintOnModel = false;

    // ---------- 预览灯光（纯运行时，不写进资产） ----------

    /** 预览场景的灯光列表；由「放置」面板增删，视口负责同步成实际组件。 */
    TArray<TSharedPtr<FShineAIPaintPreviewLight>> PreviewLights;

    /** 环境光（天光）强度。 */
    float PreviewSkyIntensity = 1.6f;

    /** 第一次用到时填一盏默认主光。 */
    void EnsurePreviewLightsInitialized()
    {
        if (PreviewLights.Num() > 0)
        {
            return;
        }

        TSharedPtr<FShineAIPaintPreviewLight> Sun = MakeShared<FShineAIPaintPreviewLight>();
        Sun->Name = TEXT("主光");
        Sun->Type = EShineAIPaintLightType::Directional;
        Sun->Location = FVector(0.0f, 0.0f, 400.0f);
        // 和预览相机默认的右前上方视角配合，保证看到的是受光面。
        Sun->Rotation = FRotator(-30.0f, 60.0f, 0.0f);
        Sun->Intensity = 5.0f;
        PreviewLights.Add(Sun);
    }

    bool IsBusy() const { return bBusy; }
    void SetBusy(bool bInBusy);

private:
    void RebuildPixelBuffers();
    void UploadBaseTexture();
    void UploadMaskTexture();

    /** 打一笔。返回 true 表示这一笔真的改动了像素（至少一个像素值变了）。 */
    bool StampBrush(const FVector2D& FromUV, const FVector2D& ToUV, float InBrushRadiusUV, const FLinearColor& Color, EShineAIPaintTool Tool);
    void FinalizeStroke(EShineAIPaintTool Tool, bool bChanged);

    /** 画布内容真的变了：版本号 +1。只在确实改动了像素时调用。 */
    void MarkPixelsChanged() { ++PaintRevision; }
    void SyncTargetsToAsset();
    void AddTargetUnique(UObject* MeshAsset);
    FShineAIPaintSettings BuildEffectiveSettings() const;

    TWeakObjectPtr<UShineAIPaintAsset> Asset;

    TArray<FShineAIPaintTarget> Targets;

    /** 底色图层工作副本（BGRA8）。 */
    TArray<FColor> BasePixels;

    /** 初始底色快照，擦除时还原到这里。 */
    TArray<FColor> InitialPixels;

    /** 遮罩图层，0 = 不遮罩，255 = 完全遮罩。 */
    TArray<uint8> MaskPixels;

    int32 Width = 0;
    int32 Height = 0;
    int32 MaskPaintedCount = 0;

    bool bBusy = false;
    bool bBaseTextureDirty = true;
    bool bMaskTextureDirty = true;

    /** 动态更新开关（纯运行时，不写进资产）。 */
    bool bLiveAIUpdate = false;

    /** 画布内容版本号：底色 / 遮罩每次真的被改动就 +1。 */
    int32 PaintRevision = 0;

    /** 已经被"请求意图"认领过的画布版本：同一个版本不会重复排队。 */
    int32 AccountedAIRevision = -1;

    /** 有变化但当时有请求在跑：等这次回来后再补发一次。 */
    bool bLiveRequestPending = false;

    /** WebSocket 推过来的实时进度文本（拉取式，不广播）。 */
    FString LiveProgressText;

    /** 目标贴图是否支持原地更新（决定涂改是不是实时进贴图）。 */
    bool bCanUpdateTargetInPlace = false;

    /** 当前是否在一笔当中。 */
    bool bStrokeActive = false;

    FString LastStatus;
    FString TargetTextureIssue;
    FString TargetUpdateHint;

    TStrongObjectPtr<UTexture2D> BaseTexture;
    TStrongObjectPtr<UTexture2D> MaskTexture;
};
