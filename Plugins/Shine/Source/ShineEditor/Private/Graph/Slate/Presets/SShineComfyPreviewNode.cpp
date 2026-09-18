#include "Graph/Slate/Presets/SShineComfyPreviewNode.h"

#include "Graph/Node/Presets/ShineComfyPreviewGraphNode.h"

void SShineComfyPreviewNode::Construct(const FArguments& InArgs, UShineComfyPreviewGraphNode* InNode)
{
    SShineComfyMultiImageNode::Construct(SShineComfyMultiImageNode::FArguments(), InNode);
}
