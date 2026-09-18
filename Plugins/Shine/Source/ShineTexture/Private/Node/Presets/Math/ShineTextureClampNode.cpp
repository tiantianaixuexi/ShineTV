#include "Node/Presets/Math/ShineTextureClampNode.h"

#include "Node/Slate/Presets/SShineTextureClampNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureClampNode::CreateVisualWidget()
{
    return SNew(SShineTextureClampNode, this);
}

UShineTextureClampNode::UShineTextureClampNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureClampNode", "ClampTitle", "限制 (Clamp)"),
        NSLOCTEXT("UShineTextureClampNode", "ClampSubtitle", "将输入值限制在指定的最小和最大范围内"),
        FLinearColor(0.420f, 0.820f, 0.420f, 1.0f));
}

float UShineTextureClampNode::GetMinValue() const
{
    return MinValue;
}

float UShineTextureClampNode::GetMaxValue() const
{
    return MaxValue;
}

void UShineTextureClampNode::SetMinValue(float InMinValue)
{
    const float NewMinValue = FMath::Clamp(InMinValue, 0.0f, MaxValue);
    if (FMath::IsNearlyEqual(MinValue, NewMinValue))
    {
        return;
    }

    Modify();
    MinValue = NewMinValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureClampNode::SetMaxValue(float InMaxValue)
{
    const float NewMaxValue = FMath::Clamp(InMaxValue, MinValue, 1.0f);
    if (FMath::IsNearlyEqual(MaxValue, NewMaxValue))
    {
        return;
    }

    Modify();
    MaxValue = NewMaxValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureClampNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

