#include "Node/Presets/ShineTextureSolidColorNode.h"

#include "Node/Slate/Presets/SShineTextureSolidColorNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureSolidColorNode::CreateVisualWidget()
{
    return SNew(SShineTextureSolidColorNode, this);
}

UShineTextureSolidColorNode::UShineTextureSolidColorNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureSolidColorNode", "SolidColorTitle", "纯色 (Solid Color)"),
        NSLOCTEXT("UShineTextureSolidColorNode", "SolidColorSubtitle", "Produces a constant RGBA color swatch."),
        FLinearColor(0.920f, 0.420f, 0.220f, 1.0f));
}

FLinearColor UShineTextureSolidColorNode::GetColorValue() const
{
    return ColorValue;
}

void UShineTextureSolidColorNode::SetColorValue(FLinearColor InColorValue)
{
    if (ColorValue.Equals(InColorValue))
    {
        return;
    }

    Modify();
    ColorValue = InColorValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureSolidColorNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("颜色 (Color)"));
}

