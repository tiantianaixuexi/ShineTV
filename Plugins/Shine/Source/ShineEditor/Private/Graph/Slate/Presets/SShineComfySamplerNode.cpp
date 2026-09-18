#include "Graph/Slate/Presets/SShineComfySamplerNode.h"

#include "Graph/Node/Presets/ShineComfySamplerGraphNode.h"

void SShineComfySamplerNode::Construct(const FArguments& InArgs, UShineComfySamplerGraphNode* InNode)
{
    SShineComfyGraphStandardNode::Construct(SShineComfyGraphStandardNode::FArguments(), InNode);
}