#include "Node/Presets/ShineTexturePreviewNode.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Node/Slate/Presets/SShineTexturePreviewNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTexturePreviewNode::CreateVisualWidget()
{
    return SNew(SShineTexturePreviewNode, this);
}

UShineTexturePreviewNode::UShineTexturePreviewNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTexturePreviewNode", "PreviewTitle", "预览 (Preview)"),
        NSLOCTEXT("UShineTexturePreviewNode", "PreviewSubtitle", "Shows an intermediate texture result."),
        FLinearColor(0.220f, 0.740f, 0.980f, 1.0f));
}

int32 UShineTexturePreviewNode::GetPreviewResolution() const
{
    return PreviewResolution;
}

void UShineTexturePreviewNode::SetPreviewResolution(int32 InPreviewResolution)
{
    const int32 NewPreviewResolution = FMath::Clamp(InPreviewResolution, 64, 1024);
    if (PreviewResolution == NewPreviewResolution)
    {
        return;
    }

    Modify();
    PreviewResolution = NewPreviewResolution;
    NotifyNodeVisualsChanged();
}

UTexture* UShineTexturePreviewNode::GetPreviewTexture() const
{
    return PreviewTexture;
}

UTextureRenderTarget2D* UShineTexturePreviewNode::GetOrCreatePreviewRenderTarget(FIntPoint Size)
{
    const FIntPoint TextureSize(FMath::Max(16, Size.X), FMath::Max(16, Size.Y));
    if (!PreviewTexture || PreviewTexture->SizeX != TextureSize.X || PreviewTexture->SizeY != TextureSize.Y)
    {
        PreviewTexture = NewObject<UTextureRenderTarget2D>(this, TEXT("ShineTextureNodePreview"), RF_Transient);
        PreviewTexture->NeverStream = true;
        PreviewTexture->Filter = TF_Bilinear;
        PreviewTexture->ClearColor = FLinearColor::Black;
        PreviewTexture->bAutoGenerateMips = false;
        PreviewTexture->bCanCreateUAV = true;
        PreviewTexture->RenderTargetFormat = RTF_RGBA8_SRGB;
        PreviewTexture->InitCustomFormat(TextureSize.X, TextureSize.Y, PF_B8G8R8A8, true);
        PreviewTexture->UpdateResourceImmediate(true);
    }

    return PreviewTexture;
}

UTextureRenderTarget2D* UShineTexturePreviewNode::GetOrCreatePreviewRenderTarget(int32 Size)
{
    return GetOrCreatePreviewRenderTarget(FIntPoint(Size, Size));
}

FIntPoint UShineTexturePreviewNode::GetInlinePreviewSize() const
{
    return FIntPoint(GetPreviewResolution(), GetPreviewResolution());
}

void UShineTexturePreviewNode::SetInlinePreviewSize(FIntPoint InPreviewSize)
{
    SetPreviewResolution(FMath::Max(InPreviewSize.X, InPreviewSize.Y));
}

int32 UShineTexturePreviewNode::GetInlinePreviewResolution() const
{
    return GetPreviewResolution();
}

void UShineTexturePreviewNode::SetInlinePreviewResolution(int32 InPreviewResolution)
{
    SetPreviewResolution(InPreviewResolution);
}

UTexture* UShineTexturePreviewNode::GetInlinePreviewTexture() const
{
    return GetPreviewTexture();
}

UTextureRenderTarget2D* UShineTexturePreviewNode::GetOrCreateInlinePreviewRenderTarget(FIntPoint Size)
{
    return GetOrCreatePreviewRenderTarget(Size);
}

void UShineTexturePreviewNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ColorPinCategory, TEXT("Input"));
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("Result"));
}

