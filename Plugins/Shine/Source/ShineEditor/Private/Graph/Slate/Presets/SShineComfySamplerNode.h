#pragma once

#include "CoreMinimal.h"
#include "Graph/Slate/SShineComfyGraphStandardNode.h"

class UShineComfySamplerGraphNode;

class SShineComfySamplerNode : public SShineComfyGraphStandardNode
{
public:
    SLATE_BEGIN_ARGS(SShineComfySamplerNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfySamplerGraphNode* InNode);
};