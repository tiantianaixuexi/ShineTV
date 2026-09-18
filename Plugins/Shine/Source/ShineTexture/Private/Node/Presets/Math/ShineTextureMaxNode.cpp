#include "Node/Presets/Math/ShineTextureMaxNode.h"

UShineTextureMaxNode::UShineTextureMaxNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureMaxNode", "MaxTitle", "Max"),
        NSLOCTEXT("UShineTextureMaxNode", "MaxSubtitle", "Returns the component-wise maximum of A and B."),
        FLinearColor(0.450f, 0.720f, 0.980f, 1.0f));
}

void UShineTextureMaxNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

