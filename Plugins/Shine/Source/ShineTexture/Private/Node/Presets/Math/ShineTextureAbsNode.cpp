#include "Node/Presets/Math/ShineTextureAbsNode.h"

UShineTextureAbsNode::UShineTextureAbsNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureAbsNode", "AbsTitle", "Abs"),
        NSLOCTEXT("UShineTextureAbsNode", "AbsSubtitle", "Returns the absolute value of each component."),
        FLinearColor(0.640f, 0.740f, 0.980f, 1.0f));
}

void UShineTextureAbsNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

