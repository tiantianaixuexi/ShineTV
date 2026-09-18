#include "Node/Presets/ShineTextureNormalBlendNode.h"

UShineTextureNormalBlendNode::UShineTextureNormalBlendNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureNormalBlendNode", "NormalBlendTitle", "法线混合 (Normal Blend)"),
        NSLOCTEXT("UShineTextureNormalBlendNode", "NormalBlendSubtitle", "Combines base and detail normals."),
        FLinearColor(0.700f, 0.620f, 0.980f, 1.0f));
}

float UShineTextureNormalBlendNode::GetDetailStrength() const
{
    return DetailStrength;
}

void UShineTextureNormalBlendNode::SetDetailStrength(float InStrength)
{
    const float NewStrength = FMath::Clamp(InStrength, 0.0f, 2.0f);
    if (FMath::IsNearlyEqual(DetailStrength, NewStrength))
    {
        return;
    }

    Modify();
    DetailStrength = NewStrength;
    NotifyNodeVisualsChanged();
}

void UShineTextureNormalBlendNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Base"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Detail"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

