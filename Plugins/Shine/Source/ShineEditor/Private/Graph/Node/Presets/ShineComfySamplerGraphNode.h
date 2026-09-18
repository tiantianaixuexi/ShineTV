#pragma once

#include "Comfy/ShineComfyTypes.h"
#include "CoreMinimal.h"
#include "Graph/Node/ShineComfyGraphPresetNode.h"
#include "ShineComfySamplerGraphNode.generated.h"

class SGraphNode;

UCLASS()
class SHINEEDITOR_API UShineComfySamplerGraphNode : public UShineComfyGraphPresetNode
{
    GENERATED_BODY()

public:
    UShineComfySamplerGraphNode();
    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
    void ApplyModelLibrary(const TArray<FShineComfyModelFolder>& ModelFolders);

protected:
    virtual void BuildNodePins() override;
};