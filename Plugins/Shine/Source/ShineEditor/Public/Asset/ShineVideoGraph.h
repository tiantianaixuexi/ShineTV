#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "ShineVideoGraph.generated.h"

class UShineVideoProject;

/**
 * 视频工作台的节点画布。
 *
 * 它挂在 `UShineVideoProject` 上（Instanced），是**创作期的图**：剧本、角色、素材图片、
 * 分镜、分镜图、视频组六类节点 + 它们之间的连线。它本身不生成任何东西，编译一遍之后
 * 变成项目里的 `Shots` 分镜表，再交给 `FShineMiniMaxH3WorkflowBuilder`（纯函数）
 * 编译成 ComfyUI 的 API 图、由 `FShineVideoTaskRunner` 提交。
 *
 * 为什么不做成"图直接出 ComfyUI API 图"：`FShineMiniMaxH3WorkflowBuilder` 已经与 P0
 * 探针图逐字段对拍过（`Scripts/H3/probe_ref2va.json` / `probe_chain2.json`），
 * 中间再插一层编译只会让"图和探针图不一致"的排查变难。这里只负责把画布翻译成
 * 分镜表，剩下的事一步都不重做。
 */
UCLASS()
class SHINEEDITOR_API UShineVideoGraph : public UEdGraph
{
    GENERATED_BODY()

public:
    /**
     * 在画布上建一个节点并把它放到指定位置。
     *
     * 顺序照抄 `FShineComfySchemaAction_NewNode::PerformAction`：先 AddNode，
     * 再给 Guid / PostPlacedNewNode / 位置 / AllocateDefaultPins。这三步的顺序换过
     * 就会出"节点没有 pin"或者"撤销之后位置错乱"这类问题。
     *
     * @param NodeClass 必须是 `UShineComfyGraphNodeBase` 的子类。
     */
    class UShineComfyGraphNodeBase* CreateNodeAt(TSubclassOf<UEdGraphNode> NodeClass, const FVector2D& Position, bool bSelectNewNode = false);

    /**
     * 图里所有视频组节点，按**画布位置**从上到下、从左到右排序。
     *
     * 这个顺序就是段序：多个视频组时，用户在画布上排在前面的先跑。
     * 之所以用位置而不是 Nodes 数组下标：Nodes 的下标取决于建节点/加载包的顺序，
     * 用户拖动节点不该改变段序，但"谁在上面"是用户能看见、也能控制的。
     */
    void GetVideoGroupNodesInOrder(TArray<class UShineVideoGroupGraphNode*>& OutNodes) const;

    /**
     * 把画布编译成项目分镜表（只改 `Shots`，模型/输出等设置原样不动）。
     *
     * 纯函数：不读磁盘、不碰网络。分镜图的产物路径是从节点的结果字段里读的，
     * 那些字段由任务执行器回填。
     *
     * @param bApplyToProject true = 真的写进 OutProject；false = 只试算一遍（校验用）。
     */
    bool CompileToProject(UShineVideoProject& OutProject, FString& OutErrorMessage, TArray<FString>& OutWarnings, bool bApplyToProject = true) const;
};
