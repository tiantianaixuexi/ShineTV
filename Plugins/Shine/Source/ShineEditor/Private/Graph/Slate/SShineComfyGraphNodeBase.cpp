#include "Graph/Slate/SShineComfyGraphNodeBase.h"

#include "Graph/Node/ShineComfyGraphNodeBase.h"

void SShineComfyGraphNodeBase::ConstructBase(UShineComfyGraphNodeBase* InNode)
{
    GraphNode = InNode;
}

void SShineComfyGraphNodeBase::ResetNodeContainers()
{
    InputPins.Empty();
    OutputPins.Empty();
    LeftNodeBox.Reset();
    ParameterBox.Reset();
    RightNodeBox.Reset();
}

const UShineComfyGraphNodeBase* SShineComfyGraphNodeBase::GetShineNode() const
{
    return Cast<UShineComfyGraphNodeBase>(GraphNode);
}