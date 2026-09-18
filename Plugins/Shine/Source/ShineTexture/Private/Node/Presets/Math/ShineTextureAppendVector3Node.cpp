#include "Node/Presets/Math/ShineTextureAppendVector3Node.h"

UShineTextureAppendVector3Node::UShineTextureAppendVector3Node()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureAppendVector3Node", "AppendVector3Title", "Append 3"),
        NSLOCTEXT("UShineTextureAppendVector3Node", "AppendVector3Subtitle", "Builds a three-component vector from scalar inputs."),
        FLinearColor(0.180f, 0.800f, 0.900f, 1.0f));
}

void UShineTextureAppendVector3Node::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("X"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Y"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Z"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}
