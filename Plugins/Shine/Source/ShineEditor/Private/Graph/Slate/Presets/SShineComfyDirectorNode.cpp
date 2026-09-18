#include "Graph/Slate/Presets/SShineComfyDirectorNode.h"

#include "Graph/Node/Presets/ShineComfyDirectorGraphNode.h"

void SShineComfyDirectorNode::Construct(const FArguments& InArgs, UShineComfyDirectorGraphNode* InNode)
{
    SShineComfyGraphStandardNode::Construct(SShineComfyGraphStandardNode::FArguments(), InNode);
}
