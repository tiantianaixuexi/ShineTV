#include "Node/Presets/Math/ShineTextureSubtractNode.h"

UShineTextureSubtractNode::UShineTextureSubtractNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureSubtractNode", "SubtractTitle", "Subtract"),
        NSLOCTEXT("UShineTextureSubtractNode", "SubtractSubtitle", "Subtracts B from A component-wise."),
        FLinearColor(0.180f, 0.780f, 0.900f, 1.0f));
}

void UShineTextureSubtractNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

