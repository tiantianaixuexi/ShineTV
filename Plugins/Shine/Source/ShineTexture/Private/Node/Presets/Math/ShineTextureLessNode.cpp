#include "Node/Presets/Math/ShineTextureLessNode.h"

UShineTextureLessNode::UShineTextureLessNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureLessNode", "LessTitle", "Less"),
        NSLOCTEXT("UShineTextureLessNode", "LessSubtitle", "Outputs true when the scalar value of A is less than B."),
        FLinearColor(0.660f, 0.720f, 0.240f, 1.0f));
}

void UShineTextureLessNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, BoolPinCategory, TEXT("Result"));
}

