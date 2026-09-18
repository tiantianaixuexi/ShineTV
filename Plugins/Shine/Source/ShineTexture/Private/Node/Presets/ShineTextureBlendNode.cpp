#include "Node/Presets/ShineTextureBlendNode.h"

#include "Node/Slate/Presets/SShineTextureBlendNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureBlendNode::CreateVisualWidget()
{
    return SNew(SShineTextureBlendNode, this);
}

UShineTextureBlendNode::UShineTextureBlendNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureBlendNode", "BlendTitle", "混合 (Blend)"),
        NSLOCTEXT("UShineTextureBlendNode", "BlendSubtitle", "线性混合两个输入的颜色。"),
        FLinearColor(0.980f, 0.730f, 0.200f, 1.0f));
}

float UShineTextureBlendNode::GetBlendFactor() const
{
    return BlendFactor;
}

void UShineTextureBlendNode::SetBlendFactor(float InBlendFactor)
{
    const float NewBlendFactor = FMath::Clamp(InBlendFactor, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(BlendFactor, NewBlendFactor))
    {
        return;
    }

    Modify();
    BlendFactor = NewBlendFactor;
    NotifyNodeVisualsChanged();
}

void UShineTextureBlendNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

