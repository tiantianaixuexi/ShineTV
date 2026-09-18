#include "Node/Presets/Math/ShineTextureLerpNode.h"

UShineTextureLerpNode::UShineTextureLerpNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureLerpNode", "LerpTitle", "Lerp"),
        NSLOCTEXT("UShineTextureLerpNode", "LerpSubtitle", "Linearly interpolates between A and B using Alpha."),
        FLinearColor(0.200f, 0.720f, 0.980f, 1.0f));
}

void UShineTextureLerpNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Alpha"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

