#include "Node/Presets/ShineTextureTransform2DNode.h"

#include "Node/Slate/Presets/SShineTextureTransform2DNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureTransform2DNode::CreateVisualWidget()
{
    return SNew(SShineTextureTransform2DNode, this);
}

UShineTextureTransform2DNode::UShineTextureTransform2DNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureTransform2DNode", "Transform2DTitle", "2D 变换 (Transformation 2D)"),
        NSLOCTEXT("UShineTextureTransform2DNode", "Transform2DSubtitle", "对输入图像应用偏移、缩放、旋转和平铺。"),
        FLinearColor(0.330f, 0.760f, 0.620f, 1.0f));
}

FVector2D UShineTextureTransform2DNode::GetOffset() const
{
    return Offset;
}

FVector2D UShineTextureTransform2DNode::GetScale() const
{
    return Scale;
}

float UShineTextureTransform2DNode::GetRotationDegrees() const
{
    return RotationDegrees;
}

bool UShineTextureTransform2DNode::GetTilingEnabled() const
{
    return bTilingEnabled;
}

void UShineTextureTransform2DNode::SetOffset(FVector2D InOffset)
{
    if (Offset.Equals(InOffset))
    {
        return;
    }

    Modify();
    Offset = InOffset;
    NotifyNodeVisualsChanged();
}

void UShineTextureTransform2DNode::SetScale(FVector2D InScale)
{
    const FVector2D NewScale(FMath::Max(0.01, InScale.X), FMath::Max(0.01, InScale.Y));
    if (Scale.Equals(NewScale))
    {
        return;
    }

    Modify();
    Scale = NewScale;
    NotifyNodeVisualsChanged();
}

void UShineTextureTransform2DNode::SetRotationDegrees(float InRotationDegrees)
{
    const float NewRotationDegrees = FMath::Clamp(InRotationDegrees, -180.0f, 180.0f);
    if (FMath::IsNearlyEqual(RotationDegrees, NewRotationDegrees))
    {
        return;
    }

    Modify();
    RotationDegrees = NewRotationDegrees;
    NotifyNodeVisualsChanged();
}

void UShineTextureTransform2DNode::SetTilingEnabled(bool bInTilingEnabled)
{
    if (bTilingEnabled == bInTilingEnabled)
    {
        return;
    }

    Modify();
    bTilingEnabled = bInTilingEnabled;
    NotifyNodeVisualsChanged();
}

void UShineTextureTransform2DNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

