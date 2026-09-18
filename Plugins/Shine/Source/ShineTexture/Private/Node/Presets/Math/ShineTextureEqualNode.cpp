#include "Node/Presets/Math/ShineTextureEqualNode.h"

UShineTextureEqualNode::UShineTextureEqualNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureEqualNode", "EqualTitle", "Equal"),
        NSLOCTEXT("UShineTextureEqualNode", "EqualSubtitle", "Compares the X or R components of A and B and outputs a boolean mask."),
        FLinearColor(0.620f, 0.760f, 0.240f, 1.0f));
}

void UShineTextureEqualNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, BoolPinCategory, TEXT("Result"));
}

