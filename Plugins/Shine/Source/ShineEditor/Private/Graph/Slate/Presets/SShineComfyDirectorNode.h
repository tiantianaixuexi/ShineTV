#pragma once

#include "CoreMinimal.h"
#include "Graph/Slate/SShineComfyGraphStandardNode.h"

class UShineComfyDirectorGraphNode;

/**
 * 导演台 Slate UI。
 * 直接复用标准节点 UI（参数控件由 Parameters 数组自动生成），
 * 如需自定义布局可在此扩展。
 */
class SShineComfyDirectorNode : public SShineComfyGraphStandardNode
{
public:
    SLATE_BEGIN_ARGS(SShineComfyDirectorNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfyDirectorGraphNode* InNode);
};
