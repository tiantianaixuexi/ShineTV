#include "Node/Presets/Math/ShineTextureFracNode.h"

UShineTextureFracNode::UShineTextureFracNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureFracNode", "FracTitle", "Frac"),
        NSLOCTEXT("UShineTextureFracNode", "FracSubtitle", "Keeps only the fractional part of each component."),
        FLinearColor(0.540f, 0.860f, 0.980f, 1.0f));
}

void UShineTextureFracNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

