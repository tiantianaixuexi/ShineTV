#include "Node/Presets/ShineTextureBlurNode.h"

#include "Node/Slate/Presets/SShineTextureBlurNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureBlurNode::CreateVisualWidget()
{
    return SNew(SShineTextureBlurNode, this);
}

UShineTextureBlurNode::UShineTextureBlurNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureBlurNode", "BlurTitle", "模糊 (Blur)"),
        NSLOCTEXT("UShineTextureBlurNode", "BlurSubtitle", "在预览空间中应用盒式模糊。"),
        FLinearColor(0.420f, 0.580f, 0.980f, 1.0f));
}

float UShineTextureBlurNode::GetRadius() const
{
    return Radius;
}

void UShineTextureBlurNode::SetRadius(float InRadius)
{
    const float NewRadius = FMath::Clamp(InRadius, 0.0f, 32.0f);
    if (FMath::IsNearlyEqual(Radius, NewRadius))
    {
        return;
    }

    Modify();
    Radius = NewRadius;
    NotifyNodeVisualsChanged();
}

void UShineTextureBlurNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

