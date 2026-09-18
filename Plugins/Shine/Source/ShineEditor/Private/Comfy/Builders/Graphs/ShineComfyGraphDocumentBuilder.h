#pragma once

#include "CoreMinimal.h"
#include "Comfy/ShineComfyTypes.h"
#include "Graph/ShineComfyGraphTypes.h"

class UShineComfyGraph;

/**
 * 数据驱动建图：把一份 JSON 文档重建为一张 Shine Comfy 图（节点 + 参数 + 连线），
 * 避免在代码里为每个节点写死创建逻辑。
 *
 * 支持三种 JSON 格式（自动识别）：
 *  1. Shine Graph JSON：
 *     {
 *       "nodes": [
 *         { "id": "ckpt", "type": "CheckpointLoaderSimple", "position": [0, 0],
 *           "parameters": { "ckpt_name": "model.safetensors" } },
 *         { "id": "prompt", "type": "Prompt", "parameters": { "PositivePrompt": "...", "NegativePrompt": "..." } }
 *       ],
 *       "links": [
 *         { "from": "ckpt:MODEL", "to": "ksampler:model" }
 *       ]
 *     }
 *     - "type" 可以是内置预设名（Prompt / Sampler / Preview / MultiImageGallery）
 *       或动态 Comfy 节点类名（需先从 ComfyUI 拉取过节点定义）。
 *     - links 也支持数组形式：["ckpt", "MODEL", "ksampler", "model"]。
 *  2. ComfyUI workflow JSON（前端导出格式）：nodes/links 数组，links 为
 *     [linkId, originId, originSlot, targetId, targetSlot, type]，widget 值按定义顺序映射。
 *  3. ComfyUI API prompt JSON：{ "1": { "class_type": "KSampler", "inputs": { "model": ["5", 0], ... } } }。
 *
 * 同时兼容 ExportGraphDefinitionToJson 导出的文档（字段大小写不敏感）。
 */
class FShineComfyGraphDocumentBuilder
{
public:
    /** 解析 JSON 文本为图文档（节点 ID 若不是 GUID 会自动生成稳定映射）。 */
    static bool ParseGraphJson(const FString& JsonText, const TArray<FShineComfyNodeDefinition>& DynamicDefinitions,
        FShineComfyGraphDocument& OutDocument, FString& OutErrorMessage);

    /** 清空并按文档重建图（生成节点、写入参数、按名字连线）。 */
    static bool ApplyGraphDocument(UShineComfyGraph* Graph, const FShineComfyGraphDocument& Document,
        const TArray<FShineComfyNodeDefinition>& DynamicDefinitions, FString& OutErrorMessage);

    /** 一步到位：解析并应用到图上。 */
    static bool BuildGraphFromJson(UShineComfyGraph* Graph, const FString& JsonText,
        const TArray<FShineComfyNodeDefinition>& DynamicDefinitions, FString& OutErrorMessage);
};
