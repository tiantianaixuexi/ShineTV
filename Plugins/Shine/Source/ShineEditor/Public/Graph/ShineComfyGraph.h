#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Graph/ShineComfyGraphTypes.h"
#include "ShineComfyGraph.generated.h"

UCLASS()
class SHINEEDITOR_API UShineComfyGraph : public UEdGraph
{
    GENERATED_BODY()

public:
    FShineComfyGraphDocument BuildGraphDocument() const;
    FShineComfyExecutionPlan BuildExecutionPlan() const;
    FShineComfyExecutionResult ExecuteGraph() const;

    bool ExportGraphDefinitionToJson(FString& OutJson) const;
    bool ExportExecutionPlanToJson(FString& OutJson) const;
    bool ExportComfyPromptToJson(FString& OutJson, FString& OutErrorMessage) const;

    /** 从 JSON（Shine Graph JSON / ComfyUI workflow / API prompt）重建整张图。 */
    bool ImportGraphJson(const FString& JsonText, FString& OutErrorMessage);

    /**
     * 按参数名给图里所有节点设置文本参数，返回被改动的节点数量。
     *
     * 场景捕获完把新图接进图里就靠它：例如把所有节点的 ColorImage
     * 换成刚上传到 ComfyUI 的那张图。
     */
    int32 SetTextParameterOnNodesByName(FName ParameterName, const FString& NewValue);

    /**
     * 把生成结果图的本地路径写进图里所有"显示类"节点（Preview / MultiImageGallery），
     * 这些节点会把图直接画在自己的节点身体里。返回被更新的节点数量。
     */
    int32 SetResultImagesOnDisplayNodes(const TArray<FString>& ImagePaths, TArray<FString>& OutNodeTitles);
};