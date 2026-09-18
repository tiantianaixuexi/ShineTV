#include "Graph/Node/Video/ShineVideoImageGraphNode.h"

#include "Graph/ShineVideoGraphTypes.h"
#include "Graph/Slate/Video/SShineVideoImageNode.h"

using namespace ShineVideoImageNode;

UShineVideoImageGraphNode::UShineVideoImageGraphNode()
{
    SetNodePresentation(
        FText::FromString(TEXT("素材图片")),
        FText::FromString(TEXT("一张图（素材库 / 场景捕获）：接进分镜的参考图槽位")),
        FLinearColor(0.31f, 0.66f, 0.97f, 1.0f));

    Parameters.Reset();

    AddTextParameter(LabelParameter, TEXT("名称"), FString());
    AddTextParameter(ImagePathParameter, TEXT("图片路径"), FString());
}

FText UShineVideoImageGraphNode::GetVideoNodeKind() const
{
    return FText::FromString(TEXT("素材图片"));
}

FName UShineVideoImageGraphNode::GetNodePreset() const
{
    return Preset;
}

TSharedPtr<SGraphNode> UShineVideoImageGraphNode::CreateVisualWidget()
{
    return SNew(SShineVideoImageNode, this);
}

void UShineVideoImageGraphNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ShineVideoPin::Image, OutputPin);
}

FString UShineVideoImageGraphNode::GetImagePath() const
{
    const FShineComfyNodeParameter* Parameter = FindParameter(ImagePathParameter);
    return Parameter ? Parameter->StringValue.TrimStartAndEnd() : FString();
}
