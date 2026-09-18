#pragma once

#include "CoreMinimal.h"
#include "Graph/Slate/SShineComfyGraphStandardNode.h"

class UShineComfyPromptGraphNode;

class SShineComfyPromptNode : public SShineComfyGraphStandardNode
{
public:
    SLATE_BEGIN_ARGS(SShineComfyPromptNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UShineComfyPromptGraphNode* InNode);
};