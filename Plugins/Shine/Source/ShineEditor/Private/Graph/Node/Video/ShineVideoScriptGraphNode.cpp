#include "Graph/Node/Video/ShineVideoScriptGraphNode.h"

#include "Graph/ShineVideoGraphTypes.h"
#include "Graph/Slate/Video/SShineVideoNode.h"

using namespace ShineVideoScriptNode;

UShineVideoScriptGraphNode::UShineVideoScriptGraphNode()
{
    SetNodePresentation(
        FText::FromString(TEXT("剧本")),
        FText::FromString(TEXT("世界观 / 时期 / 场景 / 风格：作为全局设定注入它所连的分镜")),
        FLinearColor(0.42f, 0.36f, 0.72f, 1.0f));

    Parameters.Reset();

    AddTextParameter(TitleParameter, TEXT("标题"), TEXT("第一集"));
    AddTextParameter(PeriodParameter, TEXT("时期"), TEXT("现代都市，初秋"));
    AddTextParameter(SceneParameter, TEXT("场景"), TEXT("老式公寓的客厅，午后斜阳光"));
    AddTextParameter(StyleParameter, TEXT("风格（会拼进分镜提示词）"),
        TEXT("cinematic live-action, photorealistic, natural volumetric light, shallow depth of field, 35mm film grain"),
        /*bMultiLine=*/true);
    AddTextParameter(BodyParameter, TEXT("正文"), FString(), /*bMultiLine=*/true);
    AddTextParameter(ForeshadowParameter, TEXT("视觉伏笔"), FString(), /*bMultiLine=*/true);
}

FText UShineVideoScriptGraphNode::GetVideoNodeKind() const
{
    return FText::FromString(TEXT("剧本"));
}

FName UShineVideoScriptGraphNode::GetNodePreset() const
{
    return Preset;
}

TSharedPtr<SGraphNode> UShineVideoScriptGraphNode::CreateVisualWidget()
{
    return SNew(SShineVideoNode, this);
}

void UShineVideoScriptGraphNode::BuildNodePins()
{
    CreateNamedPin(EGPD_Output, ShineVideoPin::Script, OutputPin);
}
