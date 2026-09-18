#include "Node/Presets/Math/ShineTextureModuloNode.h"

UShineTextureModuloNode::UShineTextureModuloNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureModuloNode", "ModuloTitle", "Modulo"),
        NSLOCTEXT("UShineTextureModuloNode", "ModuloSubtitle", "Returns the floating-point remainder of A divided by B."),
        FLinearColor(0.220f, 0.760f, 0.980f, 1.0f));
}

void UShineTextureModuloNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

