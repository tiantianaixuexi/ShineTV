#include "Node/Presets/ShineTextureOutputNode.h"

#include "Asset/ShineTextureAsset.h"
#include "Node/ShineTextureNodeConstants.h"
#include "Node/Slate/Presets/SShineTextureOutputNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureOutputNode::CreateVisualWidget()
{
    return SNew(SShineTextureOutputNode, this);
}

UShineTextureOutputNode::UShineTextureOutputNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureOutputNode", "OutputTitle", "输出 (Output)"),
        NSLOCTEXT("UShineTextureOutputNode", "OutputSubtitle", "Marks the final surface preview target."),
        FLinearColor(0.250f, 0.620f, 0.980f, 1.0f));
}

FIntPoint UShineTextureOutputNode::GetInlinePreviewSize() const
{
    return FIntPoint(GetPreviewResolution(), GetPreviewResolution());
}

void UShineTextureOutputNode::SetInlinePreviewSize(FIntPoint InPreviewSize)
{
    SetPreviewResolution(FMath::Max(InPreviewSize.X, InPreviewSize.Y));
}

int32 UShineTextureOutputNode::GetPreviewResolution() const
{
    return PreviewResolution;
}

void UShineTextureOutputNode::SetPreviewResolution(int32 InPreviewResolution)
{
    const int32 NewPreviewResolution = FMath::Clamp(InPreviewResolution, 64, ShineTextureNodeConstants::MaxOutputPreviewResolution);
    if (PreviewResolution == NewPreviewResolution)
    {
        return;
    }

    Modify();
    PreviewResolution = NewPreviewResolution;
    NotifyNodeVisualsChanged();
}

int32 UShineTextureOutputNode::GetInlinePreviewResolution() const
{
    return GetPreviewResolution();
}

void UShineTextureOutputNode::SetInlinePreviewResolution(int32 InPreviewResolution)
{
    SetPreviewResolution(InPreviewResolution);
}

UTexture* UShineTextureOutputNode::GetInlinePreviewTexture() const
{
    if (const UShineTextureAsset* Asset = GetGraph() ? GetGraph()->GetTypedOuter<UShineTextureAsset>() : nullptr)
    {
        return Asset->GetPreviewTexture();
    }

    return nullptr;
}

UTextureRenderTarget2D* UShineTextureOutputNode::GetOrCreateInlinePreviewRenderTarget(FIntPoint Size)
{
    if (UShineTextureAsset* Asset = GetGraph() ? GetGraph()->GetTypedOuter<UShineTextureAsset>() : nullptr)
    {
        return Asset->GetOrCreatePreviewRenderTarget(Size);
    }

    return nullptr;
}

void UShineTextureOutputNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Surface"));
}

