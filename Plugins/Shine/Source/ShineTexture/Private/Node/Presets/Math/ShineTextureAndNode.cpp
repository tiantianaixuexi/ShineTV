#include "Node/Presets/Math/ShineTextureAndNode.h"

UShineTextureAndNode::UShineTextureAndNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureAndNode", "AndTitle", "And"),
        NSLOCTEXT("UShineTextureAndNode", "AndSubtitle", "Outputs true when both boolean inputs are true."),
        FLinearColor(0.720f, 0.760f, 0.300f, 1.0f));
}

void UShineTextureAndNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, BoolPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, BoolPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, BoolPinCategory, TEXT("Result"));
}

