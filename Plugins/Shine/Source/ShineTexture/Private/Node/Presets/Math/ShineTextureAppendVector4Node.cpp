#include "Node/Presets/Math/ShineTextureAppendVector4Node.h"

UShineTextureAppendVector4Node::UShineTextureAppendVector4Node()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureAppendVector4Node", "AppendVector4Title", "Append 4"),
        NSLOCTEXT("UShineTextureAppendVector4Node", "AppendVector4Subtitle", "Builds a four-component value from four scalar inputs."),
        FLinearColor(0.180f, 0.760f, 0.900f, 1.0f));
}

void UShineTextureAppendVector4Node::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("X"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Y"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Z"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("W"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

