#include "Node/Presets/Math/ShineTextureMaskNode.h"

#include "Node/Slate/Presets/SShineTextureMaskNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureMaskNode::CreateVisualWidget()
{
    return SNew(SShineTextureMaskNode, this);
}

UShineTextureMaskNode::UShineTextureMaskNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureMaskNode", "MaskTitle", "Mask"),
        NSLOCTEXT("UShineTextureMaskNode", "MaskSubtitle", "Keeps selected components and zeros out the rest."),
        FLinearColor(0.160f, 0.840f, 0.920f, 1.0f));
}

bool UShineTextureMaskNode::GetMaskX() const
{
    return bMaskX;
}

bool UShineTextureMaskNode::GetMaskY() const
{
    return bMaskY;
}

bool UShineTextureMaskNode::GetMaskZ() const
{
    return bMaskZ;
}

bool UShineTextureMaskNode::GetMaskW() const
{
    return bMaskW;
}

void UShineTextureMaskNode::SetMaskX(bool bInMaskX)
{
    if (bMaskX == bInMaskX)
    {
        return;
    }

    Modify();
    bMaskX = bInMaskX;
    NotifyNodeVisualsChanged();
}

void UShineTextureMaskNode::SetMaskY(bool bInMaskY)
{
    if (bMaskY == bInMaskY)
    {
        return;
    }

    Modify();
    bMaskY = bInMaskY;
    NotifyNodeVisualsChanged();
}

void UShineTextureMaskNode::SetMaskZ(bool bInMaskZ)
{
    if (bMaskZ == bInMaskZ)
    {
        return;
    }

    Modify();
    bMaskZ = bInMaskZ;
    NotifyNodeVisualsChanged();
}

void UShineTextureMaskNode::SetMaskW(bool bInMaskW)
{
    if (bMaskW == bInMaskW)
    {
        return;
    }

    Modify();
    bMaskW = bInMaskW;
    NotifyNodeVisualsChanged();
}

void UShineTextureMaskNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

