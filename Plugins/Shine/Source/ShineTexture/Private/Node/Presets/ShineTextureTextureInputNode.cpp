#include "Node/Presets/ShineTextureTextureInputNode.h"

#include "Engine/Texture2D.h"
#include "Node/Slate/Presets/SShineTextureTextureInputNode.h"
#include "SGraphNode.h"

TSharedPtr<SGraphNode> UShineTextureTextureInputNode::CreateVisualWidget()
{
    return SNew(SShineTextureTextureInputNode, this);
}

UShineTextureTextureInputNode::UShineTextureTextureInputNode()
{
    SetNodePresentation(
        NSLOCTEXT("UShineTextureTextureInputNode", "TextureInputTitle", "贴图输入 (Texture Input)"),
        NSLOCTEXT("UShineTextureTextureInputNode", "TextureInputSubtitle", "Outputs a selected texture asset as a color source."),
        FLinearColor(0.340f, 0.620f, 0.940f, 1.0f));
}

UTexture2D* UShineTextureTextureInputNode::GetSourceTexture() const
{
    return SourceTexture;
}

void UShineTextureTextureInputNode::SetSourceTexture(UTexture2D* InSourceTexture)
{
    if (SourceTexture == InSourceTexture)
    {
        return;
    }

    Modify();
    SourceTexture = InSourceTexture;
    NotifyNodeVisualsChanged();
}

UTexture* UShineTextureTextureInputNode::GetInlinePreviewTexture() const
{
    return SourceTexture ? SourceTexture : Super::GetInlinePreviewTexture();
}

void UShineTextureTextureInputNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ColorPinCategory, TEXT("颜色 (Color)"));
}
