#include "Graph/Slate/Presets/SShineComfyPromptNode.h"

#include "Graph/Node/Presets/ShineComfyPromptGraphNode.h"

void SShineComfyPromptNode::Construct(const FArguments& InArgs, UShineComfyPromptGraphNode* InNode)
{
    SShineComfyGraphStandardNode::Construct(SShineComfyGraphStandardNode::FArguments(), InNode);
}