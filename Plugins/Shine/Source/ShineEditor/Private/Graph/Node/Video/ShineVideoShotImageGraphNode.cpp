#include "Graph/Node/Video/ShineVideoShotImageGraphNode.h"

#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/Video/ShineVideoImageGraphNode.h"
#include "Graph/ShineVideoGraphTypes.h"
#include "Graph/Slate/Video/SShineVideoImageNode.h"

using namespace ShineVideoShotImageNode;

UShineVideoShotImageGraphNode::UShineVideoShotImageGraphNode()
{
    SetNodePresentation(
        FText::FromString(TEXT("分镜图")),
        FText::FromString(TEXT("这个分镜最终定下来的那一张图：视频拿它当参考 / 首帧")),
        FLinearColor(0.83f, 0.42f, 0.64f, 1.0f));

    Parameters.Reset();

    AddTextParameter(TitleParameter, TEXT("名称"), FString());
    AddTextParameter(ImagePathParameter, TEXT("图片路径（手填引用素材）"), FString());
}

FText UShineVideoShotImageGraphNode::GetVideoNodeKind() const
{
    return FText::FromString(TEXT("分镜图"));
}

FName UShineVideoShotImageGraphNode::GetNodePreset() const
{
    return Preset;
}

TSharedPtr<SGraphNode> UShineVideoShotImageGraphNode::CreateVisualWidget()
{
    return SNew(SShineVideoImageNode, this);
}

void UShineVideoShotImageGraphNode::BuildNodePins()
{
    // 输入顺序刻意是"分镜在前、图片在后"：这个节点的主语义是"某个分镜定下来的图"，
    // 直接引用素材只是绕开上游出图的捷径。
    CreateNamedPin(EGPD_Input, ShineVideoPin::Shot, ShotPin);
    CreateNamedPin(EGPD_Input, ShineVideoPin::Image, ImagePin);
    CreateNamedPin(EGPD_Output, ShineVideoPin::Image, OutputPin);
}

FString UShineVideoShotImageGraphNode::GetImagePath() const
{
    return ResolveImagePath(0);
}

FString UShineVideoShotImageGraphNode::ResolveImagePath(int32 Depth) const
{
    if (Depth > 8)
    {
        // 环：两个分镜图互接。返回空，让编译器去报"这一级还没有图"，
        // 比在这里无限递归崩掉好得多。
        return FString();
    }

    // 1. 已经生成出来的产物最优先：它就是"这一级定下来的那张图"。
    if (const int32 ResultCount = GetResultImageCount())
    {
        const FString Produced = GetResultImagePath(ResultCount - 1);
        if (!Produced.TrimStartAndEnd().IsEmpty())
        {
            return Produced.TrimStartAndEnd();
        }
    }

    // 2. 手填的路径（直接引用素材）。
    const FShineComfyNodeParameter* Parameter = FindParameter(ImagePathParameter);
    if (Parameter && !Parameter->StringValue.TrimStartAndEnd().IsEmpty())
    {
        return Parameter->StringValue.TrimStartAndEnd();
    }

    // 3. 最后才跟着线往上找（只沿着 `图片` 输入）。
    //    局部变量不能叫 ImagePin：本文件有 `using namespace ShineVideoShotImageNode;`，
    //    同名会触发 C4459（UE 把它当错误）。
    const UEdGraphPin* ImageSourceLink = FindPin(ImagePin);
    if (ImageSourceLink && ImageSourceLink->LinkedTo.Num() > 0)
    {
        const UEdGraphNode* SourceNode = ImageSourceLink->LinkedTo[0]->GetOwningNode();
        if (const UShineVideoShotImageGraphNode* OtherShotImage = Cast<UShineVideoShotImageGraphNode>(SourceNode))
        {
            return OtherShotImage->ResolveImagePath(Depth + 1);
        }

        if (const UShineVideoImageGraphNode* ImageNode = Cast<UShineVideoImageGraphNode>(SourceNode))
        {
            return ImageNode->GetImagePath();
        }
    }

    return FString();
}
