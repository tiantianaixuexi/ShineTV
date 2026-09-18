#include "Graph/Node/Presets/ShineComfyPromptGraphNode.h"

#include "Graph/Slate/Presets/SShineComfyPromptNode.h"

namespace ShineComfyPromptNode
{
    const FName Preset(TEXT("Prompt"));
    const FName ConditioningPin(TEXT("Conditioning"));
    const FName NegativePin(TEXT("Negative"));
    const FName ConditioningCategory(TEXT("Shine.Conditioning"));
}

UShineComfyPromptGraphNode::UShineComfyPromptGraphNode()
{
    SetPresetName(ShineComfyPromptNode::Preset);
    SetNodePresentation(
        FText::FromString(TEXT("Prompt Encoder")),
        FText::FromString(TEXT("Generate positive and negative conditioning vectors")),
        FLinearColor(0.19f, 0.58f, 0.36f, 1.0f));

    ResetPresetParameters();
    AddTextParameter(TEXT("PositivePrompt"), TEXT("Positive Prompt"), TEXT("cinematic portrait, rim light, volumetric fog"));
    AddTextParameter(TEXT("NegativePrompt"), TEXT("Negative Prompt"), TEXT("lowres, blurry, extra fingers"));
}

TSharedPtr<SGraphNode> UShineComfyPromptGraphNode::CreateVisualWidget()
{
    return SNew(SShineComfyPromptNode, this);
}

void UShineComfyPromptGraphNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ShineComfyPromptNode::ConditioningCategory, ShineComfyPromptNode::ConditioningPin);
    CreateNamedPin(EGPD_Output, ShineComfyPromptNode::ConditioningCategory, ShineComfyPromptNode::NegativePin);
}