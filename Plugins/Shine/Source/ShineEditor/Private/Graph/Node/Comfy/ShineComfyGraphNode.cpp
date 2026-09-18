#include "Graph/Node/Comfy/ShineComfyGraphNode.h"

#include "Comfy/ShineComfyClient.h"

namespace
{
    EShineComfyParameterType ResolveParameterType(EShineComfyParameterType ParameterType)
    {
        switch (ParameterType)
        {
        case EShineComfyParameterType::Float:
            return EShineComfyParameterType::Float;
        case EShineComfyParameterType::Integer:
            return EShineComfyParameterType::Integer;
        case EShineComfyParameterType::Boolean:
            return EShineComfyParameterType::Boolean;
        default:
            return EShineComfyParameterType::Text;
        }
    }

    FLinearColor ResolveAccentColor(bool bIsApiNode)
    {
        return bIsApiNode
            ? FLinearColor(0.70f, 0.30f, 0.20f, 1.0f)
            : FLinearColor(0.20f, 0.49f, 0.80f, 1.0f);
    }

    void CopyParameterDefinitionsToNodeParameters(const TArray<FShineComfyNodeParameterDefinition>& ParameterDefinitions, TArray<FShineComfyNodeParameter>& OutParameters)
    {
        OutParameters.Reset();
        OutParameters.Reserve(ParameterDefinitions.Num());

        for (const FShineComfyNodeParameterDefinition& ParameterDefinition : ParameterDefinitions)
        {
            FShineComfyNodeParameter& Parameter = OutParameters.AddDefaulted_GetRef();
            Parameter.Name = ParameterDefinition.Name;
            Parameter.Label = ParameterDefinition.Label;
            Parameter.Type = ResolveParameterType(ParameterDefinition.Type);
            Parameter.StringValue = ParameterDefinition.StringValue;
            Parameter.FloatValue = ParameterDefinition.FloatValue;
            Parameter.IntValue = ParameterDefinition.IntValue;
            Parameter.bBoolValue = ParameterDefinition.bBoolValue;
            Parameter.StringOptions = ParameterDefinition.StringOptions;
            Parameter.OptionsSourceName = ParameterDefinition.OptionsSourceName;
        }
    }
}

UShineComfyGraphNode::UShineComfyGraphNode()
{
    SetPresetName(TEXT("Comfy.Dynamic"));
    SetNodePresentation(
        FText::FromString(TEXT("Comfy Node")),
        FText::FromString(TEXT("Drag a node from the left Comfy palette to configure it.")),
        ResolveAccentColor(false));
}

void UShineComfyGraphNode::ConfigureFromDefinition(const FShineComfyNodeDefinition& InDefinition)
{
    NodeDefinition = InDefinition;
    SetPresetName(FName(*NodeDefinition.NodeClassName));
    SetNodePresentation(
        FText::FromString(NodeDefinition.DisplayName.IsEmpty() ? NodeDefinition.NodeClassName : NodeDefinition.DisplayName),
        FText::FromString(NodeDefinition.Description.IsEmpty() ? NodeDefinition.Category : NodeDefinition.Description),
        ResolveAccentColor(NodeDefinition.bIsApiNode));

    CopyParameterDefinitionsToNodeParameters(NodeDefinition.Parameters, Parameters);
}

void UShineComfyGraphNode::ApplyModelLibrary(const TArray<FShineComfyModelFolder>& ModelFolders)
{
    if (NodeDefinition.NodeClassName.IsEmpty())
    {
        return;
    }

    TArray<FShineComfyNodeDefinition> SingleNodeDefinitions;
    SingleNodeDefinitions.Add(NodeDefinition);
    FShineComfyClient::ApplyModelLibraryToNodeDefinitions(SingleNodeDefinitions, ModelFolders);

    if (SingleNodeDefinitions.Num() > 0)
    {
        NodeDefinition = MoveTemp(SingleNodeDefinitions[0]);
        CopyParameterDefinitionsToNodeParameters(NodeDefinition.Parameters, Parameters);
        RefreshNodeAfterParameterChange();
    }
}

FText UShineComfyGraphNode::GetTooltipText() const
{
    if (NodeDefinition.NodeClassName.IsEmpty())
    {
        return UShineComfyGraphPresetNode::GetTooltipText();
    }

    const FString Description = NodeDefinition.Description.IsEmpty() ? NodeDefinition.Category : NodeDefinition.Description;
    return FText::FromString(FString::Printf(TEXT("%s\nClass: %s"), *Description, *NodeDefinition.NodeClassName));
}

void UShineComfyGraphNode::BuildNodePins()
{
    for (const FShineComfyNodePinDefinition& InputPin : NodeDefinition.InputPins)
    {
        CreateNamedPin(EGPD_Input, FName(*InputPin.PinCategory), FName(*InputPin.Name));
    }

    for (const FShineComfyNodePinDefinition& OutputPin : NodeDefinition.OutputPins)
    {
        CreateNamedPin(EGPD_Output, FName(*OutputPin.PinCategory), FName(*OutputPin.Name));
    }
}
