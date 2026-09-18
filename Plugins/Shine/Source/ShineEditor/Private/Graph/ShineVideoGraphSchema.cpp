#include "Graph/ShineVideoGraphSchema.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/Video/ShineVideoCharacterGraphNode.h"
#include "Graph/Node/Video/ShineVideoGroupGraphNode.h"
#include "Graph/Node/Video/ShineVideoImageGraphNode.h"
#include "Graph/Node/Video/ShineVideoScriptGraphNode.h"
#include "Graph/Node/Video/ShineVideoShotGraphNode.h"
#include "Graph/Node/Video/ShineVideoShotImageGraphNode.h"
#include "Graph/ShineComfyConnectionDrawingPolicy.h"
#include "Graph/ShineComfySchemaActions.h"
#include "Graph/ShineVideoGraphTypes.h"
#include "ToolMenu.h"

namespace
{
    /** 六类节点的顺序刻意与 PLAN 里的数据流一致（剧本/角色/图片 → 分镜 → 分镜图 → 视频组）。 */
    void AppendVideoNodeActions(FGraphActionListBuilderBase& OutAllActions)
    {
        auto AddAction = [&OutAllActions](const FString& Label, const FString& Tooltip, TSubclassOf<UShineComfyGraphNodeBase> NodeClass)
        {
            OutAllActions.AddAction(MakeShared<FShineComfySchemaAction_NewNode>(
                ShineVideoSchemaActions::GetNodeCategory(),
                FText::FromString(Label),
                FText::FromString(Tooltip),
                NodeClass));
        };

        AddAction(TEXT("剧本"),
            TEXT("世界观 / 时期 / 场景 / 风格：接到分镜的「剧本」输入，会拼进该分镜的提示词。"),
            UShineVideoScriptGraphNode::StaticClass());

        AddAction(TEXT("角色"),
            TEXT("引用一个角色资产；接进分镜的参考图槽位后会展开成它的整份参考图序列。"),
            UShineVideoCharacterGraphNode::StaticClass());

        AddAction(TEXT("素材图片"),
            TEXT("一张图（素材库 / 场景捕获）。"),
            UShineVideoImageGraphNode::StaticClass());

        AddAction(TEXT("分镜"),
            TEXT("提示词 + 采样参数；左侧 9 个槽位就是 <Picture 1..9>。"),
            UShineVideoShotGraphNode::StaticClass());

        AddAction(TEXT("分镜图"),
            TEXT("某个分镜定下来的那一张图：上游出图，或直接引用素材。"),
            UShineVideoShotImageGraphNode::StaticClass());

        AddAction(TEXT("视频组"),
            TEXT("#1..#N 按顺序出视频，段内可直接播 mp4；把上一段输出接回本段的「链」就是链式。"),
            UShineVideoGroupGraphNode::StaticClass());
    }
}

namespace ShineVideoSchemaActions
{
    FText GetNodeCategory()
    {
        return FText::FromString(TEXT("视频工作台"));
    }

    void AppendNodeActions(FGraphActionListBuilderBase& OutAllActions)
    {
        AppendVideoNodeActions(OutAllActions);
    }
}

void UShineVideoGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
    // 刻意不调 Super：父类的菜单是"从 ComfyUI 拉的节点定义"，和视频域的六类节点
    // 混在一个菜单里只会让人选错。
    ShineVideoSchemaActions::AppendNodeActions(ContextMenuBuilder);
}

const FPinConnectionResponse UShineVideoGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
    if (!A || !B)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::FromString(TEXT("pin 无效")));
    }

    if (A == B)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::FromString(TEXT("不能接自己")));
    }

    if (A->Direction == B->Direction)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::FromString(TEXT("请把输出接到输入")));
    }

    const UEdGraphPin* OutputPin = (A->Direction == EGPD_Output) ? A : B;
    const UEdGraphPin* InputPin = (A->Direction == EGPD_Output) ? B : A;

    if (OutputPin->GetOwningNode() == InputPin->GetOwningNode())
    {
        // 全图唯一放行的同节点连线：视频组把"上一段的输出"接回"本段的链"。
        // 这是 PLAN 3.2 第 2 条要求的效果——链式必须是一根看得见的线，不是勾选框。
        if (UShineVideoGroupGraphNode::IsChainLink(OutputPin, InputPin))
        {
            return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::FromString(TEXT("接成链式")));
        }

        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::FromString(TEXT("同一节点内的两个 pin 不能互连")));
    }

    // 一个输入只收一根线：段序、编号顺序都靠"哪个槽位接了什么"来确定，
    // 允许多接会让"顺序"变成二义。
    if (InputPin->LinkedTo.Num() > 0)
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::FromString(TEXT("这个输入已经接了线，先断开")));
    }

    if (!ShineVideoPin::CanConnect(OutputPin->PinType.PinCategory.ToString(), InputPin->PinType.PinCategory.ToString()))
    {
        return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FText::FromString(TEXT("这两类 pin 不能接")));
    }

    return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, FText::FromString(TEXT("接上")));
}

FConnectionDrawingPolicy* UShineVideoGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
    // 复用 Comfy 图那套自绘连线（它按 pin 类别着色，新类别已经在里面补过色）。
    return new FShineComfyConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements);
}

EGraphType UShineVideoGraphSchema::GetGraphType(const UEdGraph* TestEdGraph) const
{
    return GT_Function;
}
