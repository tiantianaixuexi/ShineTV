#include "Node/Presets/Math/ShineTextureFloorNode.h"

UShineTextureFloorNode::UShineTextureFloorNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureFloorNode", "FloorTitle", "Floor"),
        NSLOCTEXT("UShineTextureFloorNode", "FloorSubtitle", "Rounds each component down to the nearest integer."),
        FLinearColor(0.620f, 0.820f, 0.980f, 1.0f));
}

void UShineTextureFloorNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

