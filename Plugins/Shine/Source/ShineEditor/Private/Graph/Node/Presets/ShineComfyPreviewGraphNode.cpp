#include "Graph/Node/Presets/ShineComfyPreviewGraphNode.h"

#include "Graph/Slate/Presets/SShineComfyPreviewNode.h"

namespace ShineComfyPreviewNode
{
    const FName Preset(TEXT("Preview"));
    const FName LatentPin(TEXT("Latent"));
    const FName ImagePin(TEXT("Image"));
    const FName LatentCategory(TEXT("Shine.Latent"));
    const FName ImageCategory(TEXT("Shine.Image"));
}

UShineComfyPreviewGraphNode::UShineComfyPreviewGraphNode()
{
    SetPresetName(ShineComfyPreviewNode::Preset);
    SetNodePresentation(
        FText::FromString(TEXT("Preview Image")),
        FText::FromString(TEXT("Render the generated image to the output viewer")),
        FLinearColor(0.22f, 0.56f, 0.91f, 1.0f));

    ResetPresetParameters();
    AddTextParameter(TEXT("OutputLabel"), TEXT("Output Label"), TEXT("Preview Output"));
    AddBoolParameter(TEXT("AutoRefresh"), TEXT("Auto Refresh"), true);
}

TSharedPtr<SGraphNode> UShineComfyPreviewGraphNode::CreateVisualWidget()
{
    return SNew(SShineComfyPreviewNode, this);
}

void UShineComfyPreviewGraphNode::BuildNodePins()
{
    // Latent：接 Sampler 预设（内部走 VAE 解码）。
    CreateNamedPin(EGPD_Input, ShineComfyPreviewNode::LatentCategory, ShineComfyPreviewNode::LatentPin);
    // Image：直接接已经是图片的输出（例如导演台的 Image），省掉一次解码。
    CreateNamedPin(EGPD_Input, ShineComfyPreviewNode::ImageCategory, ShineComfyPreviewNode::ImagePin);
    CreateNamedPin(EGPD_Output, ShineComfyPreviewNode::ImageCategory, ShineComfyPreviewNode::ImagePin);
}