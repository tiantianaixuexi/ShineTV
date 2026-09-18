#include "Node/Presets/ShineTextureChannelPackNode.h"

UShineTextureChannelPackNode::UShineTextureChannelPackNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureChannelPackNode", "ChannelPackTitle", "通道打包 (Channel Pack)"),
        NSLOCTEXT("UShineTextureChannelPackNode", "ChannelPackSubtitle", "从单独的通道输入构建 RGBA 输出。"),
        FLinearColor(0.960f, 0.420f, 0.680f, 1.0f));
}

float UShineTextureChannelPackNode::GetDefaultAlpha() const
{
    return DefaultAlpha;
}

void UShineTextureChannelPackNode::SetDefaultAlpha(float InDefaultAlpha)
{
    const float NewDefaultAlpha = FMath::Clamp(InDefaultAlpha, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(DefaultAlpha, NewDefaultAlpha))
    {
        return;
    }

    Modify();
    DefaultAlpha = NewDefaultAlpha;
    NotifyNodeVisualsChanged();
}

void UShineTextureChannelPackNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("R"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("G"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

