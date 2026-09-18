#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FShineAIPaintSession;

/**
 * 2D 贴图画布。
 *
 * 显示底色图层，并在上面叠加半透明的遮罩图层。左键拖动 = 用当前工具涂画，
 * 中间键拖动 = 平移视图，滚轮 = 缩放（缩放值由外部通过 SetZoom 控制）。
 */
class SShineAIPaintCanvas : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SShineAIPaintCanvas) {}
        SLATE_ARGUMENT(TSharedPtr<FShineAIPaintSession>, Session)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

    void HandlePixelsChanged();
    FVector2D ScreenToUV(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const;
    void PaintSegment(const FVector2D& FromUV, const FVector2D& ToUV);

    const FSlateBrush* GetBaseBrush() const;
    const FSlateBrush* GetMaskBrush() const;

    TSharedPtr<FShineAIPaintSession> Session;
    TSharedPtr<FSlateBrush> BaseBrush;
    TSharedPtr<FSlateBrush> MaskBrush;

    bool bPainting = false;
    FVector2D LastUV = FVector2D::ZeroVector;
};
