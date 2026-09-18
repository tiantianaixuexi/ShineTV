#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/ShineComfyGraphPresetNode.h"
#include "ShineComfyPromptGraphNode.generated.h"

class SGraphNode;

UCLASS()
class SHINEEDITOR_API UShineComfyPromptGraphNode : public UShineComfyGraphPresetNode
{
    GENERATED_BODY()

public:
    UShineComfyPromptGraphNode();
    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

protected:
    virtual void BuildNodePins() override;
};