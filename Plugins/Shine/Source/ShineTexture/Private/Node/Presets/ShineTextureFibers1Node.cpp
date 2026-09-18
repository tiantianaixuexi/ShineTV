#include "Node/Presets/ShineTextureFibers1Node.h"

#include "Node/Slate/Presets/SShineTextureFibers1Node.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureFibers1Node::CreateVisualWidget()
{
    return SNew(SShineTextureFibers1Node, this);
}

UShineTextureFibers1Node::UShineTextureFibers1Node()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureFibers1Node", "Fibers1Title", "纤维 1 (Fibers 1)"),
        NSLOCTEXT("UShineTextureFibers1Node", "Fibers1Subtitle", "生成用于布料、网格和绳索状细节的简单纤维图案。"),
        FLinearColor(0.800f, 0.620f, 0.330f, 1.0f));
}

int32 UShineTextureFibers1Node::GetTiling() const
{
    return Tiling;
}

bool UShineTextureFibers1Node::GetNonSquareExpansion() const
{
    return bNonSquareExpansion;
}

void UShineTextureFibers1Node::SetTiling(int32 InTiling)
{
    const int32 NewTiling = FMath::Clamp(InTiling, 1, 16);
    if (Tiling == NewTiling)
    {
        return;
    }

    Modify();
    Tiling = NewTiling;
    NotifyNodeVisualsChanged();
}

void UShineTextureFibers1Node::SetNonSquareExpansion(bool bInNonSquareExpansion)
{
    if (bNonSquareExpansion == bInNonSquareExpansion)
    {
        return;
    }

    Modify();
    bNonSquareExpansion = bInNonSquareExpansion;
    NotifyNodeVisualsChanged();
}

void UShineTextureFibers1Node::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("颜色 (Color)"));
}

