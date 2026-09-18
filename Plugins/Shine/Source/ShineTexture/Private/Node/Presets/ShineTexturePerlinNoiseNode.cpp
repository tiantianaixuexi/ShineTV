#include "Node/Presets/ShineTexturePerlinNoiseNode.h"

#include "Node/Slate/Presets/SShineTexturePerlinNoiseNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTexturePerlinNoiseNode::CreateVisualWidget()
{
    return SNew(SShineTexturePerlinNoiseNode, this);
}

UShineTexturePerlinNoiseNode::UShineTexturePerlinNoiseNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTexturePerlinNoiseNode", "PerlinNoiseTitle", "柏林噪声 (Perlin Noise)"),
        NSLOCTEXT("UShineTexturePerlinNoiseNode", "PerlinNoiseSubtitle", "生成平滑的灰度柏林噪声图案。"),
        FLinearColor(0.380f, 0.720f, 0.520f, 1.0f));
}

int32 UShineTexturePerlinNoiseNode::GetScale() const
{
    return Scale;
}

float UShineTexturePerlinNoiseNode::GetDisorder() const
{
    return Disorder;
}

float UShineTexturePerlinNoiseNode::GetDisorderSpeed() const
{
    return DisorderSpeed;
}

FVector2D UShineTexturePerlinNoiseNode::GetTileOffset() const
{
    return TileOffset;
}

bool UShineTexturePerlinNoiseNode::GetNonSquareExpansion() const
{
    return bNonSquareExpansion;
}

void UShineTexturePerlinNoiseNode::SetScale(int32 InScale)
{
    const int32 NewScale = FMath::Clamp(InScale, 1, 256);
    if (Scale == NewScale)
    {
        return;
    }

    Modify();
    Scale = NewScale;
    NotifyNodeVisualsChanged();
}

void UShineTexturePerlinNoiseNode::SetDisorder(float InDisorder)
{
    const float NewDisorder = FMath::Clamp(InDisorder, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(Disorder, NewDisorder))
    {
        return;
    }

    Modify();
    Disorder = NewDisorder;
    NotifyNodeVisualsChanged();
}

void UShineTexturePerlinNoiseNode::SetDisorderSpeed(float InDisorderSpeed)
{
    const float NewDisorderSpeed = FMath::Max(0.0f, InDisorderSpeed);
    if (FMath::IsNearlyEqual(DisorderSpeed, NewDisorderSpeed))
    {
        return;
    }

    Modify();
    DisorderSpeed = NewDisorderSpeed;
    NotifyNodeVisualsChanged();
}

void UShineTexturePerlinNoiseNode::SetTileOffset(const FVector2D& InTileOffset)
{
    if (TileOffset.Equals(InTileOffset))
    {
        return;
    }

    Modify();
    TileOffset = InTileOffset;
    NotifyNodeVisualsChanged();
}

void UShineTexturePerlinNoiseNode::SetNonSquareExpansion(bool bInNonSquareExpansion)
{
    if (bNonSquareExpansion == bInNonSquareExpansion)
    {
        return;
    }

    Modify();
    bNonSquareExpansion = bInNonSquareExpansion;
    NotifyNodeVisualsChanged();
}

void UShineTexturePerlinNoiseNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("灰度 (Grayscale)"));
}
