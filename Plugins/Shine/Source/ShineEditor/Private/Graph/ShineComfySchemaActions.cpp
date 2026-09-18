#include "Graph/ShineComfySchemaActions.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/Comfy/ShineComfyGraphNode.h"
#include "Graph/Node/Display/ShineComfyMultiImageGraphNode.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/Node/Presets/ShineComfyDirectorGraphNode.h"
#include "Graph/Node/Presets/ShineComfyPreviewGraphNode.h"
#include "Graph/Node/Presets/ShineComfyPromptGraphNode.h"
#include "Graph/Node/Presets/ShineComfySamplerGraphNode.h"

namespace
{
    TMap<const UEdGraph*, TArray<FShineComfyNodeDefinition>> GDynamicNodeDefinitionsByGraph;

    FString BuildCategoryPath(const FString& Category)
    {
        FString SanitizedCategory = Category.TrimStartAndEnd();
        SanitizedCategory.ReplaceInline(TEXT("\\"), TEXT("|"));
        SanitizedCategory.ReplaceInline(TEXT("/"), TEXT("|"));
        while (SanitizedCategory.Contains(TEXT("||")))
        {
            SanitizedCategory.ReplaceInline(TEXT("||"), TEXT("|"));
        }
        while (SanitizedCategory.StartsWith(TEXT("|")))
        {
            SanitizedCategory.RightChopInline(1, EAllowShrinking::No);
        }
        while (SanitizedCategory.EndsWith(TEXT("|")))
        {
            SanitizedCategory.LeftChopInline(1, EAllowShrinking::No);
        }

        return SanitizedCategory.IsEmpty()
            ? TEXT("Comfy Nodes")
            : FString::Printf(TEXT("Comfy Nodes|%s"), *SanitizedCategory);
    }
}

FShineComfySchemaAction_NewNode::FShineComfySchemaAction_NewNode()
    : FEdGraphSchemaAction()
{
}

FShineComfySchemaAction_NewNode::FShineComfySchemaAction_NewNode(const FText& InCategory, const FText& InMenuDescription, const FText& InToolTip, TSubclassOf<UShineComfyGraphNodeBase> InNodeClass)
    : FEdGraphSchemaAction(InCategory, InMenuDescription, InToolTip, 0)
    , NodeClass(InNodeClass)
{
}

FShineComfySchemaAction_NewNode::FShineComfySchemaAction_NewNode(const FShineComfyNodeDefinition& InNodeDefinition)
    : FEdGraphSchemaAction(
        FText::FromString(BuildCategoryPath(InNodeDefinition.Category)),
        FText::FromString(InNodeDefinition.DisplayName.IsEmpty() ? InNodeDefinition.NodeClassName : InNodeDefinition.DisplayName),
        FText::FromString(InNodeDefinition.Description.IsEmpty() ? InNodeDefinition.NodeClassName : InNodeDefinition.Description),
        0)
    , NodeClass(UShineComfyGraphNode::StaticClass())
    , bSpawnComfyNode(true)
    , ComfyNodeDefinition(InNodeDefinition)
{
}

UEdGraphNode* FShineComfySchemaAction_NewNode::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, FVector2D Location, bool bSelectNewNode)
{
    if (!ParentGraph || !NodeClass)
    {
        return nullptr;
    }

    UShineComfyGraphNodeBase* NewNode = NewObject<UShineComfyGraphNodeBase>(ParentGraph, NodeClass);
    if (bSpawnComfyNode)
    {
        if (UShineComfyGraphNode* ComfyNode = Cast<UShineComfyGraphNode>(NewNode))
        {
            ComfyNode->ConfigureFromDefinition(ComfyNodeDefinition);
        }
    }

    ParentGraph->Modify();
    ParentGraph->AddNode(NewNode, true, bSelectNewNode);

    NewNode->SetFlags(RF_Transactional);
    NewNode->CreateNewGuid();
    NewNode->PostPlacedNewNode();
    NewNode->NodePosX = FMath::RoundToInt(Location.X);
    NewNode->NodePosY = FMath::RoundToInt(Location.Y);
    NewNode->AllocateDefaultPins();
    NewNode->AutowireNewNode(FromPin);

    return NewNode;
}

namespace ShineComfySchemaActions
{
    void AppendBuiltInNodeActions(FGraphActionListBuilderBase& OutAllActions)
    {
        OutAllActions.AddAction(MakeShared<FShineComfySchemaAction_NewNode>(
            FText::FromString(TEXT("Shine Nodes")),
            FText::FromString(TEXT("Prompt Encoder")),
            FText::FromString(TEXT("Create a prompt encoder node with positive and negative outputs.")),
            UShineComfyPromptGraphNode::StaticClass()));

        OutAllActions.AddAction(MakeShared<FShineComfySchemaAction_NewNode>(
            FText::FromString(TEXT("Shine Nodes")),
            FText::FromString(TEXT("KSampler")),
            FText::FromString(TEXT("Create a sampler node that consumes conditioning and emits latent data.")),
            UShineComfySamplerGraphNode::StaticClass()));

        OutAllActions.AddAction(MakeShared<FShineComfySchemaAction_NewNode>(
            FText::FromString(TEXT("Shine Nodes")),
            FText::FromString(TEXT("Preview Image")),
            FText::FromString(TEXT("Create an image preview node for the graph output.")),
            UShineComfyPreviewGraphNode::StaticClass()));

        OutAllActions.AddAction(MakeShared<FShineComfySchemaAction_NewNode>(
            FText::FromString(TEXT("Shine Nodes")),
            FText::FromString(TEXT("Multi Image Gallery")),
            FText::FromString(TEXT("Create a gallery node that displays multiple images in a tiled custom Slate layout.")),
            UShineComfyMultiImageGraphNode::StaticClass()));

        OutAllActions.AddAction(MakeShared<FShineComfySchemaAction_NewNode>(
            FText::FromString(TEXT("Shine Nodes")),
            FText::FromString(TEXT("导演台")),
            FText::FromString(TEXT("JSON 驱动的导演台节点，编排场景、镜头、光照并自动展开为 ComfyUI 工作流。")),
            UShineComfyDirectorGraphNode::StaticClass()));
    }

    void AppendDynamicNodeActions(const UEdGraph* Graph, FGraphActionListBuilderBase& OutAllActions)
    {
        if (!Graph)
        {
            return;
        }

        const TArray<FShineComfyNodeDefinition>* NodeDefinitions = GDynamicNodeDefinitionsByGraph.Find(Graph);
        if (!NodeDefinitions)
        {
            return;
        }

        for (const FShineComfyNodeDefinition& NodeDefinition : *NodeDefinitions)
        {
            OutAllActions.AddAction(MakeComfyNodeAction(NodeDefinition));
        }
    }

    TSharedPtr<FEdGraphSchemaAction> MakeComfyNodeAction(const FShineComfyNodeDefinition& NodeDefinition)
    {
        return MakeShared<FShineComfySchemaAction_NewNode>(NodeDefinition);
    }

    void SetDynamicNodeDefinitions(const UEdGraph* Graph, const TArray<FShineComfyNodeDefinition>& NodeDefinitions)
    {
        if (!Graph)
        {
            return;
        }

        GDynamicNodeDefinitionsByGraph.FindOrAdd(Graph) = NodeDefinitions;
    }

    const TArray<FShineComfyNodeDefinition>* GetDynamicNodeDefinitions(const UEdGraph* Graph)
    {
        return Graph ? GDynamicNodeDefinitionsByGraph.Find(Graph) : nullptr;
    }
}