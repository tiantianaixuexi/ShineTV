#include "Node/Presets/Math/ShineTextureNotNode.h"

UShineTextureNotNode::UShineTextureNotNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureNotNode", "NotTitle", "Not"),
        NSLOCTEXT("UShineTextureNotNode", "NotSubtitle", "Outputs the inverse of the boolean input."),
        FLinearColor(0.700f, 0.740f, 0.280f, 1.0f));
}

void UShineTextureNotNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, BoolPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, BoolPinCategory, TEXT("Result"));
}

