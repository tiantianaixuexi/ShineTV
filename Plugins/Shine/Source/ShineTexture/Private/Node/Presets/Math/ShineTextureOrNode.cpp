#include "Node/Presets/Math/ShineTextureOrNode.h"

UShineTextureOrNode::UShineTextureOrNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureOrNode", "OrTitle", "Or"),
        NSLOCTEXT("UShineTextureOrNode", "OrSubtitle", "Outputs true when either boolean input is true."),
        FLinearColor(0.740f, 0.780f, 0.320f, 1.0f));
}

void UShineTextureOrNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, BoolPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, BoolPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, BoolPinCategory, TEXT("Result"));
}

