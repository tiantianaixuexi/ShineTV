#include "Node/Presets/Math/ShineTextureSelectNode.h"

UShineTextureSelectNode::UShineTextureSelectNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureSelectNode", "SelectTitle", "Select"),
        NSLOCTEXT("UShineTextureSelectNode", "SelectSubtitle", "Outputs True when the condition is set, otherwise outputs False."),
        FLinearColor(0.260f, 0.820f, 0.520f, 1.0f));
}

void UShineTextureSelectNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, BoolPinCategory, TEXT("Condition"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("True"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("False"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

