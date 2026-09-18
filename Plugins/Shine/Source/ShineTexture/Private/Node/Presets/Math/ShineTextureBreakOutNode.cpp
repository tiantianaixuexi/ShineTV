#include "Node/Presets/Math/ShineTextureBreakOutNode.h"

UShineTextureBreakOutNode::UShineTextureBreakOutNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureBreakOutNode", "BreakOutTitle", "Break Out"),
        NSLOCTEXT("UShineTextureBreakOutNode", "BreakOutSubtitle", "Splits the input into X, Y, Z, and W scalar outputs."),
        FLinearColor(0.140f, 0.860f, 0.860f, 1.0f));
}

void UShineTextureBreakOutNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ScalarPinCategory, TEXT("X"));
    CreateNamedPin(EGPD_Output, ScalarPinCategory, TEXT("Y"));
    CreateNamedPin(EGPD_Output, ScalarPinCategory, TEXT("Z"));
    CreateNamedPin(EGPD_Output, ScalarPinCategory, TEXT("W"));
}

