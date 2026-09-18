#include "Node/Presets/Math/ShineTextureMinNode.h"

UShineTextureMinNode::UShineTextureMinNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureMinNode", "MinTitle", "Min"),
        NSLOCTEXT("UShineTextureMinNode", "MinSubtitle", "Returns the component-wise minimum of A and B."),
        FLinearColor(0.360f, 0.680f, 0.950f, 1.0f));
}

void UShineTextureMinNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

