#include "Node/Presets/Math/ShineTextureGreaterNode.h"

UShineTextureGreaterNode::UShineTextureGreaterNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureGreaterNode", "GreaterTitle", "Greater"),
        NSLOCTEXT("UShineTextureGreaterNode", "GreaterSubtitle", "Outputs true when the scalar value of A is greater than B."),
        FLinearColor(0.720f, 0.700f, 0.250f, 1.0f));
}

void UShineTextureGreaterNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, BoolPinCategory, TEXT("Result"));
}

