#pragma once

#include "Components/LightComponent.h"
#include "CoreMinimal.h"
#include "Paint/ShineAIPaintTypes.h"
#include "SEditorViewport.h"
#include "UObject/StrongObjectPtr.h"

class FPreviewScene;
class FShineAIPaintMeshPicker;
class FShineAIPaintSession;
class FShineAIPaintViewportClient;
class UPrimitiveComponent;
class UTexture2D;

/**
 * 目标网格体的 3D 预览视口（独立于关卡视口，用的是 FPreviewScene）。
 *
 * 除了显示，它还负责"在模型上涂画"：
 *   左键拖动 → 射线打到网格 → 取命中点 UV → 写进会话的贴图；
 *   右键拖动 / 中键拖动 / Alt+左键 → 仍然交给引擎做转视角和升降。
 */
class SShineAIPaintPreviewViewport : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SShineAIPaintPreviewViewport) {}
        SLATE_ARGUMENT(TSharedPtr<FShineAIPaintSession>, Session)
    SLATE_END_ARGS()

    virtual ~SShineAIPaintPreviewViewport() override;

    void Construct(const FArguments& InArgs);

    /** 视口要等真正被创建出来（有 Viewport 对象）才能算相机；这里补一次延迟对焦。 */
    virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

    // ---- 3D 涂画：由视口客户端转发进来 ----

    /** 当前是否允许在模型上涂画。 */
    bool IsModelPaintingEnabled() const;

    /** 左键按下。返回 true 表示这次输入已被涂画消费。 */
    bool HandlePaintButtonPressed();

    /** 拖动中继续落笔。 */
    void HandlePaintMoved();

    /** 左键松开，结束一笔。 */
    void HandlePaintReleased();

protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void BindCommands() override;

private:
    void HandleStructureChanged();
    void RebuildPreviewMeshes();
    void RebuildMeshPicker();

    /**
     * 把上一次建出来的预览 Actor 从预览场景里真的删掉。
     *
     * 只清 SpawnedComponents 是不够的：那只是我们自己的引用，
     * Actor 还留在场景里，换网格时新旧模型会叠在一起。
     */
    void DestroyPreviewActors();
    void ApplyPaintedTextureToPreview();
    void FocusCameraOnMeshes();

    /** 把会话里的灯光列表同步成预览场景里的真实灯光组件（增 / 删 / 改）。 */
    void SyncPreviewLights();

    /** 把光标位置的射线打到网格上取回 UV；没打中返回 false。 */
    bool PickUVUnderCursor(FVector2D& OutUV);

    TSharedPtr<FShineAIPaintSession> Session;
    TSharedPtr<FShineAIPaintMeshPicker> MeshPicker;

    /** 预览场景里被加进来的网格组件（强引用防止被 GC）。 */
    TArray<TStrongObjectPtr<UPrimitiveComponent>> SpawnedComponents;

    FBox PreviewBounds = FBox(ForceInit);

    /** 上一次构建预览用的目标签名，用来跳过"目标没变"的重建。 */
    TArray<FWeakObjectPtr> BuiltTargetSignature;
    bool bHasBuiltPreview = false;

    /** 相机对焦要等视口真正创建完成，先挂起、拿到 Viewport 后再执行一次。 */
    bool bPendingCameraFocus = false;

    /** 一盏灯"上次写进场景的值"，用来跳过重复设置（每帧都设灯光很贵）。 */
    struct FAppliedLight
    {
        TSharedPtr<FShineAIPaintPreviewLight> Source;
        TWeakObjectPtr<class ULightComponent> Component;
        bool bEnabled = true;
        float Intensity = -1.0f;
        FLinearColor Color = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
        FVector Location = FVector(FLT_MAX);
        FRotator Rotation = FRotator(FLT_MAX, 0.0f, 0.0f);
    };

    TArray<FAppliedLight> AppliedLights;

    /** 上一次写进场景的天光强度。 */
    float AppliedSkyIntensity = -1.0f;

    /** 上一次接进预览材质的贴图，用来跳过重复设置贴图参数。 */
    UTexture2D* LastAppliedPreviewTexture = nullptr;

    /** 上一笔落点的 UV，用来把笔画连成连续的线。 */
    FVector2D LastPaintUV = FVector2D::ZeroVector;
    bool bStrokeActive = false;

    TSharedPtr<FPreviewScene> PreviewScene;
    TSharedPtr<FShineAIPaintViewportClient> ViewportClient;
};
