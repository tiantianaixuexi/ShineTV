#pragma once

#include "CoreMinimal.h"
#include "Comfy/ShineComfyTypes.h"
#include "Graph/Node/ShineComfyGraphPresetNode.h"
#include "ShineComfyDirectorGraphNode.generated.h"

class SGraphNode;

/**
 * JSON 驱动的导演台节点。
 *
 * 参数、Pin、外观全部来自 DirectorNode.json 配置文件，
 * 导出时按 JSON 中的 expansion 模板展开为 ComfyUI 节点。
 */
UCLASS()
class SHINEEDITOR_API UShineComfyDirectorGraphNode : public UShineComfyGraphPresetNode
{
    GENERATED_BODY()

public:
    UShineComfyDirectorGraphNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    /**
     * 节点参数表是序列化在资产里的：DirectorNode.json 后来新增的参数（例如
     * EnableImageGen / EnableVideoGen）在老资产里并不存在。
     * 加载时按当前配置补齐/刷新，避免"缺参数 → 条件不成立 → 导出的 prompt 少一整块甚至为空"。
     */
    virtual void PostLoad() override;

    /**
     * 用 ComfyUI 的模型库填充下拉框选项：
     * 参数在 DirectorNode.json 里声明了 optionsSource（例如 checkpoints / controlnet）时，
     * 把这个目录下真实存在的模型名灌进 StringOptions，节点上就会渲染成下拉框。
     */
    void ApplyModelLibrary(const TArray<FShineComfyModelFolder>& ModelFolders);

protected:
    virtual void BuildNodePins() override;

private:
    void LoadConfig();

    /** 把当前 DirectorNode.json 里新增/改动的参数合并进这个节点（保留节点上已有的取值）。 */
    void MergeConfigParameters();
};
