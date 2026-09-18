#include "Graph/Node/Display/ShineComfyMultiImageGraphNode.h"

#include "Graph/Slate/Display/SShineComfyMultiImageNode.h"

namespace ShineComfyMultiImageNode
{
    const FName Preset(TEXT("MultiImageGallery"));
    const FName LatentPin(TEXT("Latent"));
    const FName ImagesPin(TEXT("Images"));
    const FName LatentCategory(TEXT("Shine.Latent"));
    const FName ImageCategory(TEXT("Shine.Image"));
    const FName ImageCountParameter(TEXT("ImageCount"));
    const FName ColumnsParameter(TEXT("Columns"));
}

UShineComfyMultiImageGraphNode::UShineComfyMultiImageGraphNode()
{
    SetNodePresentation(
        FText::FromString(TEXT("Multi Image Gallery")),
        FText::FromString(TEXT("Display a configurable grid of generated images inside the node body")),
        FLinearColor(0.77f, 0.31f, 0.27f, 1.0f));

    Parameters.Reset();

    FShineComfyNodeParameter& CaptionParameter = Parameters.AddDefaulted_GetRef();
    CaptionParameter.Name = TEXT("Caption");
    CaptionParameter.Label = FText::FromString(TEXT("Caption"));
    CaptionParameter.Type = EShineComfyParameterType::Text;
    CaptionParameter.StringValue = TEXT("Gallery Preview");

    FShineComfyNodeParameter& ImageCountParameter = Parameters.AddDefaulted_GetRef();
    ImageCountParameter.Name = ShineComfyMultiImageNode::ImageCountParameter;
    ImageCountParameter.Label = FText::FromString(TEXT("Image Count"));
    ImageCountParameter.Type = EShineComfyParameterType::Integer;
    ImageCountParameter.IntValue = 6;

    FShineComfyNodeParameter& ColumnsParameter = Parameters.AddDefaulted_GetRef();
    ColumnsParameter.Name = ShineComfyMultiImageNode::ColumnsParameter;
    ColumnsParameter.Label = FText::FromString(TEXT("Columns"));
    ColumnsParameter.Type = EShineComfyParameterType::Integer;
    ColumnsParameter.IntValue = 3;

    FShineComfyNodeParameter& FrameNumbersParameter = Parameters.AddDefaulted_GetRef();
    FrameNumbersParameter.Name = TEXT("ShowFrameNumbers");
    FrameNumbersParameter.Label = FText::FromString(TEXT("Show Frame Numbers"));
    FrameNumbersParameter.Type = EShineComfyParameterType::Boolean;
    FrameNumbersParameter.bBoolValue = true;
}

TSharedPtr<SGraphNode> UShineComfyMultiImageGraphNode::CreateVisualWidget()
{
    return SNew(SShineComfyMultiImageNode, this);
}

FName UShineComfyMultiImageGraphNode::GetNodePreset() const
{
    return ShineComfyMultiImageNode::Preset;
}

void UShineComfyMultiImageGraphNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Input, ShineComfyMultiImageNode::LatentCategory, ShineComfyMultiImageNode::LatentPin);
    // 也可以直接接图片输出（例如导演台的 Image）。
    CreateNamedPin(EGPD_Input, ShineComfyMultiImageNode::ImageCategory, ShineComfyMultiImageNode::ImagesPin);
    CreateNamedPin(EGPD_Output, ShineComfyMultiImageNode::ImageCategory, ShineComfyMultiImageNode::ImagesPin);
}

bool UShineComfyMultiImageGraphNode::ShouldRefreshGraphOnParameterChanged(const FShineComfyNodeParameter& Parameter) const
{
    if (Parameter.Name == ShineComfyMultiImageNode::ImageCountParameter || Parameter.Name == ShineComfyMultiImageNode::ColumnsParameter)
    {
        return true;
    }

    return UShineComfyGraphNodeBase::ShouldRefreshGraphOnParameterChanged(Parameter);
}