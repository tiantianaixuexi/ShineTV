#include "Node/Presets/ShineTextureUvNode.h"

UShineTextureUvNode::UShineTextureUvNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureUvNode", "UvTitle", "UV"),
        NSLOCTEXT("UShineTextureUvNode", "UvSubtitle", "Generates normalized texture coordinates."),
        FLinearColor(0.280f, 0.850f, 0.900f, 1.0f));
}

void UShineTextureUvNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, Vector2PinCategory, TEXT("UV"));
}

