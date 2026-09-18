#include "Node/Presets/Math/ShineTextureToIntNode.h"

UShineTextureToIntNode::UShineTextureToIntNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureToIntNode", "ToIntTitle", "到整数"),
        NSLOCTEXT("UShineTextureToIntNode", "ToIntSubtitle", "Quantizes a numeric input to an integer-style value."),
        FLinearColor(0.78f, 0.72f, 0.42f, 1.0f));
}

void UShineTextureToIntNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, IntPinCategory, TEXT("Result"));
}

