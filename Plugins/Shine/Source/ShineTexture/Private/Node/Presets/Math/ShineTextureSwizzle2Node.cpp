#include "Node/Presets/Math/ShineTextureSwizzle2Node.h"

#include "Node/Slate/Presets/SShineTextureSwizzle2Node.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureSwizzle2Node::CreateVisualWidget()
{
    return SNew(SShineTextureSwizzle2Node, this);
}

UShineTextureSwizzle2Node::UShineTextureSwizzle2Node()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureSwizzle2Node", "Swizzle2Title", "Swizzle 2"),
        NSLOCTEXT("UShineTextureSwizzle2Node", "Swizzle2Subtitle", "Extracts two components from the input. 0=X, 1=Y, 2=Z, 3=W."),
        FLinearColor(0.160f, 0.860f, 0.820f, 1.0f));
}

int32 UShineTextureSwizzle2Node::GetFirstComponent() const
{
    return FirstComponent;
}

int32 UShineTextureSwizzle2Node::GetSecondComponent() const
{
    return SecondComponent;
}

void UShineTextureSwizzle2Node::SetFirstComponent(int32 InComponent)
{
    const int32 NewValue = FMath::Clamp(InComponent, 0, 3);
    if (FirstComponent == NewValue)
    {
        return;
    }

    Modify();
    FirstComponent = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureSwizzle2Node::SetSecondComponent(int32 InComponent)
{
    const int32 NewValue = FMath::Clamp(InComponent, 0, 3);
    if (SecondComponent == NewValue)
    {
        return;
    }

    Modify();
    SecondComponent = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureSwizzle2Node::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, Vector2PinCategory, TEXT("Result"));
}

