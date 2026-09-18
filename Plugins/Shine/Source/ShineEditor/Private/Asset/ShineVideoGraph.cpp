#include "Asset/ShineVideoGraph.h"

#include "Asset/ShineVideoProject.h"
#include "Comfy/ShineVideoGraphCompiler.h"
#include "EdGraph/EdGraphNode.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/Node/Video/ShineVideoGroupGraphNode.h"

UShineComfyGraphNodeBase* UShineVideoGraph::CreateNodeAt(TSubclassOf<UEdGraphNode> NodeClass, const FVector2D& Position, bool bSelectNewNode)
{
    if (!NodeClass)
    {
        return nullptr;
    }

    UShineComfyGraphNodeBase* NewNode = NewObject<UShineComfyGraphNodeBase>(this, NodeClass);
    if (!NewNode)
    {
        return nullptr;
    }

    // 顺序照抄 FShineComfySchemaAction_NewNode::PerformAction——先入图，再给 Guid / 落位 / 建 pin。
    // 把 AddNode 挪到 AllocateDefaultPins 之后就出过"节点在图上但没有 pin"的问题。
    AddNode(NewNode, true, bSelectNewNode);
    NewNode->SetFlags(RF_Transactional);
    NewNode->CreateNewGuid();
    NewNode->PostPlacedNewNode();
    NewNode->NodePosX = FMath::RoundToInt(Position.X);
    NewNode->NodePosY = FMath::RoundToInt(Position.Y);
    NewNode->AllocateDefaultPins();

    return NewNode;
}

void UShineVideoGraph::GetVideoGroupNodesInOrder(TArray<UShineVideoGroupGraphNode*>& OutNodes) const
{
    OutNodes.Reset();

    for (UEdGraphNode* GraphNode : Nodes)
    {
        if (UShineVideoGroupGraphNode* GroupNode = Cast<UShineVideoGroupGraphNode>(GraphNode))
        {
            OutNodes.Add(GroupNode);
        }
    }

    OutNodes.Sort([](const UShineVideoGroupGraphNode& Left, const UShineVideoGroupGraphNode& Right)
    {
        // 画布坐标是 float，但节点位置是整数；先按 Y 分行（同一行容差 120），行内按 X。
        // 这样"把两个视频组并排放在同一行"不会因为 Y 差几个像素而顺序乱跳。
        constexpr int32 RowTolerance = 120;
        if (FMath::Abs(Left.NodePosY - Right.NodePosY) > RowTolerance)
        {
            return Left.NodePosY < Right.NodePosY;
        }

        return Left.NodePosX < Right.NodePosX;
    });
}

bool UShineVideoGraph::CompileToProject(UShineVideoProject& OutProject, FString& OutErrorMessage, TArray<FString>& OutWarnings, bool bApplyToProject) const
{
    const FShineVideoGraphCompileResult Result = FShineVideoGraphCompiler::Compile(*this);

    OutErrorMessage = Result.ErrorMessage;
    OutWarnings = Result.Warnings;

    if (!Result.bSuccess)
    {
        return false;
    }

    if (bApplyToProject)
    {
        FShineVideoGraphCompiler::ApplyToProject(Result, OutProject);
    }

    return true;
}
