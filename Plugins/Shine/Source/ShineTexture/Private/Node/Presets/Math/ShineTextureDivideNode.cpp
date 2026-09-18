#include "Node/Presets/Math/ShineTextureDivideNode.h"

UShineTextureDivideNode::UShineTextureDivideNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureDivideNode", "DivideTitle", "Divide"),
        NSLOCTEXT("UShineTextureDivideNode", "DivideSubtitle", "Divides A by B component-wise."),
        FLinearColor(0.180f, 0.700f, 0.980f, 1.0f));
}

void UShineTextureDivideNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

