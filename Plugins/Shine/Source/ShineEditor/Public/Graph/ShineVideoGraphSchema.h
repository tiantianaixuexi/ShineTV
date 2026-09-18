#pragma once

#include "CoreMinimal.h"
#include "Graph/ShineComfyGraphSchema.h"
#include "ShineVideoGraphSchema.generated.h"

class FConnectionDrawingPolicy;
class UEdGraphPin;
struct FGraphActionListBuilderBase;

/**
 * 六类视频节点的调色板动作。
 *
 * 画布的右键菜单和左侧的节点面板都从这里取——两处各写一份列表的话，
 * "菜单里能建、面板里找不到"这种不一致迟早出现。
 */
namespace ShineVideoSchemaActions
{
    SHINEEDITOR_API void AppendNodeActions(FGraphActionListBuilderBase& OutAllActions);

    /** 节点面板用的分组名（与右键菜单保持一致）。 */
    SHINEEDITOR_API FText GetNodeCategory();
}

/**
 * 视频工作台的画布 schema。
 *
 * **刻意继承 `UShineComfyGraphSchema`**，不是为了复用那几条规则，而是因为引擎侧有两处
 * 写死的 `IsA<UShineComfyGraphSchema>()` 判断：
 *
 *   1. `FShineEditorModule` 里的 `FShineGraphPinFactory::CreatePin` —— 不满足就退回
 *      引擎默认 pin 控件（自绘连线、着色全丢）；
 *   2. `UShineComfyGraphNodeBase::CanCreateUnderSpecifiedSchema` —— 不满足则六类节点
 *      在画布上**建不出来**。
 *
 * 继承它这两处就自动成立，不必去改那两个共享文件（改了以后 Comfy 图那条线也得跟着回归）。
 * 需要覆盖的行为只有三个：右键菜单、连线规则、连线着色。
 */
UCLASS()
class SHINEEDITOR_API UShineVideoGraphSchema : public UShineComfyGraphSchema
{
    GENERATED_BODY()

public:
    virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
    virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
    virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const override;
    virtual EGraphType GetGraphType(const UEdGraph* TestEdGraph) const override;
};
