#include "Node/Presets/Math/ShineTextureSwizzle4Node.h"

#include "Node/Slate/Presets/SShineTextureSwizzle4Node.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureSwizzle4Node::CreateVisualWidget()
{
    return SNew(SShineTextureSwizzle4Node, this);
}

namespace
{
    int32 ClampSwizzleComponent(int32 InComponent)
    {
        return FMath::Clamp(InComponent, 0, 3);
    }
}

UShineTextureSwizzle4Node::UShineTextureSwizzle4Node()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureSwizzle4Node", "Swizzle4Title", "Swizzle 4"),
        NSLOCTEXT("UShineTextureSwizzle4Node", "Swizzle4Subtitle", "Reorders four components from the input. 0=X, 1=Y, 2=Z, 3=W."),
        FLinearColor(0.120f, 0.820f, 0.900f, 1.0f));
}

int32 UShineTextureSwizzle4Node::GetXComponent() const
{
    return XComponent;
}

int32 UShineTextureSwizzle4Node::GetYComponent() const
{
    return YComponent;
}

int32 UShineTextureSwizzle4Node::GetZComponent() const
{
    return ZComponent;
}

int32 UShineTextureSwizzle4Node::GetWComponent() const
{
    return WComponent;
}

void UShineTextureSwizzle4Node::SetXComponent(int32 InComponent)
{
    const int32 NewValue = ClampSwizzleComponent(InComponent);
    if (XComponent == NewValue)
    {
        return;
    }

    Modify();
    XComponent = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureSwizzle4Node::SetYComponent(int32 InComponent)
{
    const int32 NewValue = ClampSwizzleComponent(InComponent);
    if (YComponent == NewValue)
    {
        return;
    }

    Modify();
    YComponent = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureSwizzle4Node::SetZComponent(int32 InComponent)
{
    const int32 NewValue = ClampSwizzleComponent(InComponent);
    if (ZComponent == NewValue)
    {
        return;
    }

    Modify();
    ZComponent = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureSwizzle4Node::SetWComponent(int32 InComponent)
{
    const int32 NewValue = ClampSwizzleComponent(InComponent);
    if (WComponent == NewValue)
    {
        return;
    }

    Modify();
    WComponent = NewValue;
    NotifyNodeVisualsChanged();
}

void UShineTextureSwizzle4Node::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

