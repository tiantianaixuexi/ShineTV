#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class UShineComfyGraphNodeBase;

class SShineComfyGraphNodeBase : public SGraphNode
{
protected:
    void ConstructBase(UShineComfyGraphNodeBase* InNode);
    void ResetNodeContainers();
    const UShineComfyGraphNodeBase* GetShineNode() const;

    TSharedPtr<SVerticalBox> ParameterBox;
};