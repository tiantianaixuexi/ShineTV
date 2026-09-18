#include "Node/Presets/Math/ShineTextureToFloatNode.h"

UShineTextureToFloatNode::UShineTextureToFloatNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureToFloatNode", "ToFloatTitle", "到浮点"),
        NSLOCTEXT("UShineTextureToFloatNode", "ToFloatSubtitle", "Converts an integer-style value to a scalar float."),
        FLinearColor(0.78f, 0.72f, 0.42f, 1.0f));
}

void UShineTextureToFloatNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, IntPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ScalarPinCategory, TEXT("Result"));
}

