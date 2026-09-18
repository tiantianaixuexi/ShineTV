#include "Node/Presets/Math/ShineTextureOneMinusNode.h"

UShineTextureOneMinusNode::UShineTextureOneMinusNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureOneMinusNode", "OneMinusTitle", "One Minus"),
        NSLOCTEXT("UShineTextureOneMinusNode", "OneMinusSubtitle", "Computes one minus the input value for each component."),
        FLinearColor(0.500f, 0.860f, 0.980f, 1.0f));
}

void UShineTextureOneMinusNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

