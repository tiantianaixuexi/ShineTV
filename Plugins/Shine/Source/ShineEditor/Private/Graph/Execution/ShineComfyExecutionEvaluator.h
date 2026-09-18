#pragma once

#include "CoreMinimal.h"
#include "Graph/ShineComfyGraphTypes.h"

namespace ShineComfyExecutionEvaluator
{
    void EvaluateNode(const FShineComfySerializedNode& SerializedNode, FShineComfyExecutionNode& ExecutionNode);
}