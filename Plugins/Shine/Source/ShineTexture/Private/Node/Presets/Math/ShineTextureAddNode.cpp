#include "Node/Presets/Math/ShineTextureAddNode.h"

UShineTextureAddNode::UShineTextureAddNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureAddNode", "AddTitle", "Add"),
        NSLOCTEXT("UShineTextureAddNode", "AddSubtitle", "Adds two input values component-wise."),
        FLinearColor(0.150f, 0.680f, 0.900f, 1.0f));
}

void UShineTextureAddNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("A"));
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("B"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

