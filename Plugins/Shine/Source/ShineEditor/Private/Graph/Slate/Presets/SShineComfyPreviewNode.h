#pragma once

#include "CoreMinimal.h"
#include "Graph/Slate/Display/SShineComfyMultiImageNode.h"

class UShineComfyPreviewGraphNode;

/**
 * "Preview Image" 节点的外观。
 *
 * 直接复用 Gallery 那套节点身体：Preview 没有 Columns 参数，
 * 列数取 1，于是它就是"一张大图"——生成结果直接画在节点里。
 */
class SShineComfyPreviewNode : public SShineComfyMultiImageNode
{
public:
    SLATE_BEGIN_ARGS(SShineComfyPreviewNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfyPreviewGraphNode* InNode);
};
