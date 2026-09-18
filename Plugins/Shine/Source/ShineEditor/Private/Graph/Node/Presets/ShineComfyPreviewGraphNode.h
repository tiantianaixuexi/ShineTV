#pragma once

#include "CoreMinimal.h"
#include "Graph/Node/ShineComfyGraphPresetNode.h"
#include "ShineComfyPreviewGraphNode.generated.h"

class SGraphNode;

UCLASS()
class SHINEEDITOR_API UShineComfyPreviewGraphNode : public UShineComfyGraphPresetNode
{
    GENERATED_BODY()

public:
    UShineComfyPreviewGraphNode();
    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

protected:
    virtual void BuildNodePins() override;
};