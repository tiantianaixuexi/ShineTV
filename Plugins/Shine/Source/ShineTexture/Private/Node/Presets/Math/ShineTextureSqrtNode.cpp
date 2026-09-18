#include "Node/Presets/Math/ShineTextureSqrtNode.h"

UShineTextureSqrtNode::UShineTextureSqrtNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureSqrtNode", "SqrtTitle", "Sqrt"),
        NSLOCTEXT("UShineTextureSqrtNode", "SqrtSubtitle", "Applies square root to each component."),
        FLinearColor(0.640f, 0.820f, 0.980f, 1.0f));
}

void UShineTextureSqrtNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

