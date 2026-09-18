#pragma once

#include "CoreMinimal.h"
#include "Comfy/ShineComfyTypes.h"
#include "EdGraph/EdGraphSchema.h"
#include "Templates/SubclassOf.h"

class UShineComfyGraphNodeBase;
class UEdGraph;
struct FGraphActionListBuilderBase;

struct FShineComfySchemaAction_NewNode final : public FEdGraphSchemaAction
{
    FShineComfySchemaAction_NewNode();
    FShineComfySchemaAction_NewNode(const FText& InCategory, const FText& InMenuDescription, const FText& InToolTip, TSubclassOf<UShineComfyGraphNodeBase> InNodeClass);
    FShineComfySchemaAction_NewNode(const FShineComfyNodeDefinition& InNodeDefinition);

    virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, FVector2D Location, bool bSelectNewNode) override;

    TSubclassOf<UShineComfyGraphNodeBase> NodeClass;
    bool bSpawnComfyNode = false;
    FShineComfyNodeDefinition ComfyNodeDefinition;
};

namespace ShineComfySchemaActions
{
    void AppendBuiltInNodeActions(FGraphActionListBuilderBase& OutAllActions);
    void AppendDynamicNodeActions(const UEdGraph* Graph, FGraphActionListBuilderBase& OutAllActions);
    TSharedPtr<FEdGraphSchemaAction> MakeComfyNodeAction(const FShineComfyNodeDefinition& NodeDefinition);
    void SetDynamicNodeDefinitions(const UEdGraph* Graph, const TArray<FShineComfyNodeDefinition>& NodeDefinitions);
    const TArray<FShineComfyNodeDefinition>* GetDynamicNodeDefinitions(const UEdGraph* Graph);
}