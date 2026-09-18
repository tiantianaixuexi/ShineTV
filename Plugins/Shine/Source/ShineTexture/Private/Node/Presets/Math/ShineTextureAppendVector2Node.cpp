#include "Node/Presets/Math/ShineTextureAppendVector2Node.h"

UShineTextureAppendVector2Node::UShineTextureAppendVector2Node()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureAppendVector2Node", "AppendVector2Title", "Append 2"),
        NSLOCTEXT("UShineTextureAppendVector2Node", "AppendVector2Subtitle", "Builds a two-component vector from scalar inputs."),
        FLinearColor(0.180f, 0.830f, 0.900f, 1.0f));
}

void UShineTextureAppendVector2Node::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("X"));
    CreateNamedPin(EGPD_Input, ScalarPinCategory, TEXT("Y"));
    CreateNamedPin(EGPD_Output, Vector2PinCategory, TEXT("Result"));
}

