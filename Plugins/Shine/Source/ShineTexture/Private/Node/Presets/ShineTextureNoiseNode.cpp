#include "Node/Presets/ShineTextureNoiseNode.h"

#include "Node/Slate/Presets/SShineTextureNoiseNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureNoiseNode::CreateVisualWidget()
{
    return SNew(SShineTextureNoiseNode, this);
}

UShineTextureNoiseNode::UShineTextureNoiseNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureNoiseNode", "NoiseTitle", "噪波 (Noise)"),
        NSLOCTEXT("UShineTextureNoiseNode", "NoiseSubtitle", "生成程序化的灰度噪波场。"),
        FLinearColor(0.450f, 0.520f, 0.980f, 1.0f));
}

float UShineTextureNoiseNode::GetScale() const
{
    return Scale;
}

int32 UShineTextureNoiseNode::GetOctaves() const
{
    return Octaves;
}

void UShineTextureNoiseNode::SetScale(float InScale)
{
    const float NewScale = FMath::Max(1.0f, InScale);
    if (FMath::IsNearlyEqual(Scale, NewScale))
    {
        return;
    }

    Modify();
    Scale = NewScale;
    NotifyNodeVisualsChanged();
}

void UShineTextureNoiseNode::SetOctaves(int32 InOctaves)
{
    const int32 NewOctaves = FMath::Clamp(InOctaves, 1, 8);
    if (Octaves == NewOctaves)
    {
        return;
    }

    Modify();
    Octaves = NewOctaves;
    NotifyNodeVisualsChanged();
}

void UShineTextureNoiseNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("颜色 (Color)"));
}

