#include "Node/Presets/Math/ShineTextureMultiplyNode.h"

UShineTextureMultiplyNode::UShineTextureMultiplyNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureMultiplyNode", "MultiplyTitle", "正片叠底 (Multiply)"),
        NSLOCTEXT("UShineTextureMultiplyNode", "MultiplySubtitle", "Multiplies two incoming color values."),
        FLinearColor(0.930f, 0.520f, 0.190f, 1.0f));
}

void UShineTextureMultiplyNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

