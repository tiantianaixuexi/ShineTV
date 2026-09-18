#pragma once

#include "CoreMinimal.h"
#include "Comfy/ShineComfyTypes.h"
#include "Graph/Node/ShineComfyGraphPresetNode.h"
#include "ShineComfyGraphNode.generated.h"

UCLASS()
class SHINEEDITOR_API UShineComfyGraphNode : public UShineComfyGraphPresetNode
{
    GENERATED_BODY()

public:
    UShineComfyGraphNode();

    void ConfigureFromDefinition(const FShineComfyNodeDefinition& InDefinition);
    void ApplyModelLibrary(const TArray<FShineComfyModelFolder>& ModelFolders);

    virtual FText GetTooltipText() const override;

protected:
    virtual void BuildNodePins() override;

private:
    UPROPERTY()
    FShineComfyNodeDefinition NodeDefinition;
};
