#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Paint/ShineAIPaintTypes.h"
#include "Widgets/SCompoundWidget.h"

class FShineAIPaintSession;
class UTexture2D;

/**
 * 「视口」页签：3D 预览 + 视口上浮着的一排涂画工具。
 */
class SShineAIPaintViewportTab : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SShineAIPaintViewportTab) {}
        SLATE_ARGUMENT(TSharedPtr<FShineAIPaintSession>, Session)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedRef<SWidget> BuildToolStrip();

    TSharedPtr<FShineAIPaintSession> Session;
};

/**
 * 「画布」页签：2D 贴图画布，带缩放 / 适应 / 填充 / 导出。
 */
class SShineAIPaintCanvasTab : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SShineAIPaintCanvasTab) {}
        SLATE_ARGUMENT(TSharedPtr<FShineAIPaintSession>, Session)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedRef<SWidget> BuildHeader();

    void RefreshCanvasLayout();
    void HandleCanvasAreaResized(const FVector2D& NewSize);
    void FitCanvasToArea();

    /** 让整张贴图刚好放得进当前区域的缩放系数。 */
    float ComputeFitZoom() const;

    /**
     * 把缩放压回"放得下"的范围。
     *
     * 画布尺寸 = 贴图尺寸 × 缩放；一旦超出可用区域就会被外层裁掉，
     * 看起来就像图片比例不对（其实是中间被切了一条）。
     */
    void ClampCanvasZoomToArea();

    FReply HandleFillBase();
    FReply HandleClearMask();
    FReply HandleResetCanvas();
    FReply HandleExportPng();

    TSharedPtr<FShineAIPaintSession> Session;

    TSharedPtr<SBox> CanvasSizeBox;

    /** 画布区域当前尺寸（由 SShineAIPaintCanvasHost 上报）。 */
    FVector2D CanvasAreaSize = FVector2D::ZeroVector;

    /** 已经自动适应过缩放的贴图；换了目标贴图会重新适应一次。 */
    const UTexture2D* AutoFittedTexture = nullptr;
};

/**
 * 「放置」页签：预览场景里的灯光。
 *
 * 仿放置 Actor 面板：左边一列灯，右边（下面）是选中灯的属性；可以增删。
 */
class SShineAIPaintPlaceActorsTab : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SShineAIPaintPlaceActorsTab) {}
        SLATE_ARGUMENT(TSharedPtr<FShineAIPaintSession>, Session)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    TSharedRef<SWidget> BuildToolbar();
    TSharedRef<SWidget> BuildLightProperties();

    void RefreshLightList();
    void RefreshLightProperties();

    FReply HandleAddLight(EShineAIPaintLightType Type);
    FReply HandleRemoveSelectedLight();

    TSharedPtr<FShineAIPaintSession> Session;

    TSharedPtr<SVerticalBox> LightListBox;

    /** 属性区的容器；选中灯变化时整块重建。 */
    TSharedPtr<SVerticalBox> LightPropertiesBox;

    /** 当前选中的灯；属性区跟着它走。 */
    TSharedPtr<FShineAIPaintPreviewLight> SelectedLight;
};
