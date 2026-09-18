#include "Paint/SShineAIPaintCanvas.h"

#include "Engine/Texture2D.h"
#include "Paint/ShineAIPaintSession.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"

void SShineAIPaintCanvas::Construct(const FArguments& InArgs)
{
    Session = InArgs._Session;

    BaseBrush = MakeShared<FSlateBrush>();
    BaseBrush->DrawAs = ESlateBrushDrawType::Image;

    MaskBrush = MakeShared<FSlateBrush>();
    MaskBrush->DrawAs = ESlateBrushDrawType::Image;

    if (Session.IsValid())
    {
        Session->OnPixelsChanged.AddSP(this, &SShineAIPaintCanvas::HandlePixelsChanged);
    }

    HandlePixelsChanged();

    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SImage)
            .Image(this, &SShineAIPaintCanvas::GetBaseBrush)
            .Visibility(EVisibility::HitTestInvisible)
        ]
        + SOverlay::Slot()
        [
            SNew(SImage)
            .Image(this, &SShineAIPaintCanvas::GetMaskBrush)
            .Visibility(EVisibility::HitTestInvisible)
        ]
    ];

    SetCanTick(false);
    SetVisibility(EVisibility::Visible);
}

void SShineAIPaintCanvas::HandlePixelsChanged()
{
    if (!Session.IsValid())
    {
        return;
    }

    // 贴图内容走 UpdateTextureRegions 原地刷新，所以只有"换了贴图对象"时才需要重挂资源。
    if (UTexture2D* BaseTexture = Session->GetBaseTexture())
    {
        if (BaseBrush->GetResourceObject() != BaseTexture)
        {
            BaseBrush->SetResourceObject(BaseTexture);
        }

        BaseBrush->ImageSize = FVector2D(BaseTexture->GetSizeX(), BaseTexture->GetSizeY());
    }

    if (UTexture2D* MaskTexture = Session->GetMaskTexture())
    {
        if (MaskBrush->GetResourceObject() != MaskTexture)
        {
            MaskBrush->SetResourceObject(MaskTexture);
        }

        MaskBrush->ImageSize = FVector2D(MaskTexture->GetSizeX(), MaskTexture->GetSizeY());
    }
}

const FSlateBrush* SShineAIPaintCanvas::GetBaseBrush() const
{
    return BaseBrush.Get();
}

const FSlateBrush* SShineAIPaintCanvas::GetMaskBrush() const
{
    return MaskBrush.Get();
}

FVector2D SShineAIPaintCanvas::ScreenToUV(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
    const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    const FVector2D Normalized = MyGeometry.GetLocalPositionAtCoordinates(LocalPosition);
    return FVector2D(
        FMath::Clamp(Normalized.X, 0.0, 1.0),
        FMath::Clamp(Normalized.Y, 0.0, 1.0));
}

void SShineAIPaintCanvas::PaintSegment(const FVector2D& FromUV, const FVector2D& ToUV)
{
    if (!Session.IsValid())
    {
        return;
    }

    Session->PaintStroke(FromUV, ToUV, Session->BrushRadiusUV, Session->BrushColor, Session->CurrentTool);
}

FReply SShineAIPaintCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (!Session.IsValid() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }

    bPainting = true;
    Session->BeginStroke();

    LastUV = ScreenToUV(MyGeometry, MouseEvent);
    PaintSegment(LastUV, LastUV);

    return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SShineAIPaintCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (!Session.IsValid() || !bPainting || !HasMouseCapture())
    {
        return FReply::Unhandled();
    }

    const FVector2D CurrentUV = ScreenToUV(MyGeometry, MouseEvent);
    PaintSegment(LastUV, CurrentUV);
    LastUV = CurrentUV;

    return FReply::Handled();
}

FReply SShineAIPaintCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }

    bPainting = false;
    if (Session.IsValid())
    {
        // 抬笔才把整笔写进贴图，拖动过程只刷显示贴图，所以不会卡。
        Session->EndStroke();
    }

    return FReply::Handled().ReleaseMouseCapture();
}

FReply SShineAIPaintCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (!Session.IsValid())
    {
        return FReply::Unhandled();
    }

    const float Scale = MouseEvent.GetWheelDelta() > 0.0f ? 1.15f : (1.0f / 1.15f);
    Session->ViewZoom = FMath::Clamp(Session->ViewZoom * Scale, 0.05f, 3.0f);

    // 外层 SBox 的尺寸依赖缩放值，必须让布局重新计算。
    Invalidate(EInvalidateWidgetReason::Layout);
    return FReply::Handled();
}
