#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "ShineComfyMultiImageGraphNode.generated.h"

class SGraphNode;

UCLASS()
class SHINEEDITOR_API UShineComfyMultiImageGraphNode : public UShineComfyGraphNodeBase
{
    GENERATED_BODY()

public:
    UShineComfyMultiImageGraphNode();

    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
    virtual FName GetNodePreset() const override;

protected:
    virtual void BuildNodePins() override;
    virtual bool ShouldRefreshGraphOnParameterChanged(const FShineComfyNodeParameter& Parameter) const override;
};