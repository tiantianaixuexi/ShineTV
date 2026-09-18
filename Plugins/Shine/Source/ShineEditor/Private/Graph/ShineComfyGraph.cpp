#include "Graph/ShineComfyGraph.h"

#include "Comfy/Builders/Graphs/ShineComfyGraphDocumentBuilder.h"
#include "Comfy/Builders/Nodes/ShineComfyDirectorConfig.h"
#include "Graph/Execution/ShineComfyExecutionEvaluator.h"
#include "Graph/Node/Comfy/ShineComfyGraphNode.h"
#include "Graph/Node/Display/ShineComfyMultiImageGraphNode.h"
#include "Graph/Node/Presets/ShineComfyDirectorGraphNode.h"
#include "Graph/Node/Presets/ShineComfyPreviewGraphNode.h"
#include "Graph/Node/Presets/ShineComfyPromptGraphNode.h"
#include "Graph/Node/Presets/ShineComfySamplerGraphNode.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/ShineComfySchemaActions.h"
#include "Graph/Slate/ShineImageZoom.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    const FString UnresolvedValue(TEXT("<unresolved>"));
    const FName PresetPrompt(TEXT("Prompt"));
    const FName PresetSampler(TEXT("Sampler"));
    const FName PresetPreview(TEXT("Preview"));
    const FName PresetGallery(TEXT("MultiImageGallery"));
    const FName PresetDirector(TEXT("Director"));

    struct FPromptOutputRef
    {
        FString NodeId;
        int32 OutputIndex = 0;
    };

    struct FSamplerExpansion
    {
        FString CheckpointLoaderId;
        FString PositivePromptId;
        FString NegativePromptId;
        FString EmptyLatentId;
        FString KSamplerId;
        FPromptOutputRef LatentRef;
        FPromptOutputRef VaeRef;
        FPromptOutputRef ClipRef;
        FPromptOutputRef ModelRef;
    };

    void AddOrUpdateNamedValue(TArray<FShineComfyNamedValue>& Values, const FString& Name, const FString& Value)
    {
        for (FShineComfyNamedValue& ExistingValue : Values)
        {
            if (ExistingValue.Name == Name)
            {
                ExistingValue.Value = Value;
                return;
            }
        }

        FShineComfyNamedValue& NewValue = Values.AddDefaulted_GetRef();
        NewValue.Name = Name;
        NewValue.Value = Value;
    }

    FString FindNamedValue(const TArray<FShineComfyNamedValue>& Values, const FString& Name, const FString& DefaultValue = FString())
    {
        for (const FShineComfyNamedValue& Value : Values)
        {
            if (Value.Name == Name)
            {
                return Value.Value;
            }
        }

        return DefaultValue;
    }

    TArray<FShineComfyNamedValue> ConvertParameters(const TArray<FShineComfyNodeParameter>& Parameters)
    {
        TArray<FShineComfyNamedValue> Result;
        Result.Reserve(Parameters.Num());

        for (const FShineComfyNodeParameter& Parameter : Parameters)
        {
            FShineComfyNamedValue& NamedValue = Result.AddDefaulted_GetRef();
            NamedValue.Name = Parameter.Name.ToString();
            NamedValue.Value = Parameter.ExportValueAsString();
        }

        return Result;
    }

    FString FindFinalImageValue(const TArray<FShineComfyExecutionNode>& OrderedNodes)
    {
        if (OrderedNodes.IsEmpty())
        {
            return FString();
        }

        const TArray<FShineComfyNamedValue>& FinalOutputs = OrderedNodes.Last().Outputs;
        FString FinalImageValue = FindNamedValue(FinalOutputs, TEXT("Image"));
        if (FinalImageValue.IsEmpty())
        {
            FinalImageValue = FindNamedValue(FinalOutputs, TEXT("Images"));
        }

        return FinalImageValue;
    }

    void AddParameterValueToPromptInputs(const FShineComfyNodeParameter& Parameter, const TSharedRef<FJsonObject>& InputsObject)
    {
        const FString FieldName = Parameter.Name.ToString();
        switch (Parameter.Type)
        {
        case EShineComfyParameterType::Float:
            InputsObject->SetNumberField(FieldName, Parameter.FloatValue);
            break;
        case EShineComfyParameterType::Integer:
            InputsObject->SetNumberField(FieldName, Parameter.IntValue);
            break;
        case EShineComfyParameterType::Boolean:
            InputsObject->SetBoolField(FieldName, Parameter.bBoolValue);
            break;
        default:
            InputsObject->SetStringField(FieldName, Parameter.StringValue);
            break;
        }
    }

    int32 GetOutputPinIndex(const UShineComfyGraphNodeBase* Node, const UEdGraphPin* OutputPin)
    {
        if (!Node || !OutputPin)
        {
            return INDEX_NONE;
        }

        int32 OutputPinIndex = 0;
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->Direction != EGPD_Output)
            {
                continue;
            }

            if (Pin == OutputPin)
            {
                return OutputPinIndex;
            }

            ++OutputPinIndex;
        }

        return INDEX_NONE;
    }

    FString MakeOutputKey(const UShineComfyGraphNodeBase* Node, const FName& PinName)
    {
        return Node ? FString::Printf(TEXT("%s:%s"), *Node->NodeGuid.ToString(EGuidFormats::Digits), *PinName.ToString()) : FString();
    }

    const UEdGraphPin* FindLinkedSourcePin(const UShineComfyGraphNodeBase* Node, const FName& InputPinName)
    {
        if (!Node)
        {
            return nullptr;
        }

        const UEdGraphPin* InputPin = Node->FindPin(InputPinName);
        return InputPin && InputPin->LinkedTo.Num() > 0 ? InputPin->LinkedTo[0] : nullptr;
    }

    const UShineComfyGraphNodeBase* GetSourceNode(const UEdGraphPin* SourcePin)
    {
        return SourcePin ? Cast<UShineComfyGraphNodeBase>(SourcePin->GetOwningNode()) : nullptr;
    }

    bool SetPromptLinkField(const FString& FieldName, const FPromptOutputRef& OutputRef, const TSharedRef<FJsonObject>& InputsObject)
    {
        if (OutputRef.NodeId.IsEmpty() || OutputRef.OutputIndex < 0)
        {
            return false;
        }

        TArray<TSharedPtr<FJsonValue>> LinkValue;
        LinkValue.Add(MakeShared<FJsonValueString>(OutputRef.NodeId));
        LinkValue.Add(MakeShared<FJsonValueNumber>(OutputRef.OutputIndex));
        InputsObject->SetArrayField(FieldName, LinkValue);
        return true;
    }

    FString GetTextParameterValue(const UShineComfyGraphNodeBase* Node, const FName& ParameterName, const FString& DefaultValue = FString())
    {
        const FShineComfyNodeParameter* Parameter = Node ? Node->FindParameter(ParameterName) : nullptr;
        return Parameter ? Parameter->StringValue : DefaultValue;
    }

    int32 GetIntParameterValue(const UShineComfyGraphNodeBase* Node, const FName& ParameterName, int32 DefaultValue)
    {
        const FShineComfyNodeParameter* Parameter = Node ? Node->FindParameter(ParameterName) : nullptr;
        return Parameter ? Parameter->IntValue : DefaultValue;
    }

    double GetFloatParameterValue(const UShineComfyGraphNodeBase* Node, const FName& ParameterName, double DefaultValue)
    {
        const FShineComfyNodeParameter* Parameter = Node ? Node->FindParameter(ParameterName) : nullptr;
        return Parameter ? Parameter->FloatValue : DefaultValue;
    }

    bool GetBoolParameterValue(const UShineComfyGraphNodeBase* Node, const FName& ParameterName, bool bDefaultValue)
    {
        const FShineComfyNodeParameter* Parameter = Node ? Node->FindParameter(ParameterName) : nullptr;
        return Parameter ? Parameter->bBoolValue : bDefaultValue;
    }

    TSharedRef<FJsonObject> AddPromptNode(TSharedRef<FJsonObject> PromptObject, const FString& NodeId, const FString& ClassType)
    {
        TSharedRef<FJsonObject> NodeObject = MakeShared<FJsonObject>();
        NodeObject->SetStringField(TEXT("class_type"), ClassType);
        NodeObject->SetObjectField(TEXT("inputs"), MakeShared<FJsonObject>());
        PromptObject->SetObjectField(NodeId, NodeObject);
        return NodeObject;
    }

    TSharedRef<FJsonObject> GetInputsObject(const TSharedRef<FJsonObject>& NodeObject)
    {
        const TSharedPtr<FJsonObject>* InputsObject = nullptr;
        if (NodeObject->TryGetObjectField(TEXT("inputs"), InputsObject) && InputsObject && InputsObject->IsValid())
        {
            return InputsObject->ToSharedRef();
        }

        TSharedRef<FJsonObject> NewInputsObject = MakeShared<FJsonObject>();
        NodeObject->SetObjectField(TEXT("inputs"), NewInputsObject);
        return NewInputsObject;
    }
}

FShineComfyGraphDocument UShineComfyGraph::BuildGraphDocument() const
{
    FShineComfyGraphDocument Document;

    for (UEdGraphNode* GraphNode : Nodes)
    {
        const UShineComfyGraphNodeBase* ShineNode = Cast<UShineComfyGraphNodeBase>(GraphNode);
        if (!ShineNode)
        {
            continue;
        }

        FShineComfySerializedNode& SerializedNode = Document.Nodes.AddDefaulted_GetRef();
        SerializedNode.NodeId = ShineNode->NodeGuid;
        SerializedNode.Title = ShineNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
        SerializedNode.Preset = ShineNode->GetNodePreset().ToString();
        SerializedNode.Position = FVector2D(ShineNode->NodePosX, ShineNode->NodePosY);
        SerializedNode.Parameters = ShineNode->GetParameters();

        for (const UEdGraphPin* Pin : ShineNode->Pins)
        {
            if (!Pin || Pin->Direction != EGPD_Output)
            {
                continue;
            }

            for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                const UShineComfyGraphNodeBase* LinkedNode = LinkedPin ? Cast<UShineComfyGraphNodeBase>(LinkedPin->GetOwningNode()) : nullptr;
                if (!LinkedNode)
                {
                    continue;
                }

                FShineComfySerializedLink& Link = Document.Links.AddDefaulted_GetRef();
                Link.SourceNodeId = ShineNode->NodeGuid;
                Link.SourcePin = Pin->PinName.ToString();
                Link.TargetNodeId = LinkedNode->NodeGuid;
                Link.TargetPin = LinkedPin->PinName.ToString();
            }
        }
    }

    return Document;
}

FShineComfyExecutionPlan UShineComfyGraph::BuildExecutionPlan() const
{
    const FShineComfyGraphDocument Document = BuildGraphDocument();
    FShineComfyExecutionPlan Plan;

    TMap<FGuid, int32> NodeIndexById;
    TArray<TArray<FShineComfySerializedLink>> IncomingLinks;
    TArray<TArray<FShineComfySerializedLink>> OutgoingLinks;
    TArray<int32> InDegree;

    IncomingLinks.SetNum(Document.Nodes.Num());
    OutgoingLinks.SetNum(Document.Nodes.Num());
    InDegree.Init(0, Document.Nodes.Num());

    for (int32 NodeIndex = 0; NodeIndex < Document.Nodes.Num(); ++NodeIndex)
    {
        NodeIndexById.Add(Document.Nodes[NodeIndex].NodeId, NodeIndex);
    }

    for (const FShineComfySerializedLink& Link : Document.Links)
    {
        const int32* SourceIndex = NodeIndexById.Find(Link.SourceNodeId);
        const int32* TargetIndex = NodeIndexById.Find(Link.TargetNodeId);
        if (!SourceIndex || !TargetIndex)
        {
            continue;
        }

        OutgoingLinks[*SourceIndex].Add(Link);
        IncomingLinks[*TargetIndex].Add(Link);
        ++InDegree[*TargetIndex];

        FShineComfyExecutionEdge& Edge = Plan.Edges.AddDefaulted_GetRef();
        Edge.SourceNodeId = Link.SourceNodeId;
        Edge.SourcePin = Link.SourcePin;
        Edge.TargetNodeId = Link.TargetNodeId;
        Edge.TargetPin = Link.TargetPin;
    }

    TArray<int32> ReadyNodes;
    for (int32 NodeIndex = 0; NodeIndex < InDegree.Num(); ++NodeIndex)
    {
        if (InDegree[NodeIndex] == 0)
        {
            ReadyNodes.Add(NodeIndex);
        }
    }

    TArray<int32> OrderedNodeIndices;
    while (!ReadyNodes.IsEmpty())
    {
        const int32 NodeIndex = ReadyNodes[0];
        ReadyNodes.RemoveAt(0);
        OrderedNodeIndices.Add(NodeIndex);

        for (const FShineComfySerializedLink& Link : OutgoingLinks[NodeIndex])
        {
            const int32* TargetIndex = NodeIndexById.Find(Link.TargetNodeId);
            if (!TargetIndex)
            {
                continue;
            }

            --InDegree[*TargetIndex];
            if (InDegree[*TargetIndex] == 0)
            {
                ReadyNodes.Add(*TargetIndex);
            }
        }
    }

    Plan.bHasCycle = OrderedNodeIndices.Num() != Document.Nodes.Num();
    if (Plan.bHasCycle)
    {
        Plan.StatusMessage = TEXT("Graph contains a cycle and cannot be scheduled as a DAG.");
        return Plan;
    }

    TMap<FGuid, TArray<FShineComfyNamedValue>> OutputValuesByNode;

    for (const int32 OrderedNodeIndex : OrderedNodeIndices)
    {
        const FShineComfySerializedNode& SerializedNode = Document.Nodes[OrderedNodeIndex];
        FShineComfyExecutionNode& ExecutionNode = Plan.OrderedNodes.AddDefaulted_GetRef();
        ExecutionNode.NodeId = SerializedNode.NodeId;
        ExecutionNode.Title = SerializedNode.Title;
        ExecutionNode.Preset = SerializedNode.Preset;
        ExecutionNode.Parameters = ConvertParameters(SerializedNode.Parameters);

        for (const FShineComfySerializedLink& Link : IncomingLinks[OrderedNodeIndex])
        {
            const TArray<FShineComfyNamedValue>* SourceOutputs = OutputValuesByNode.Find(Link.SourceNodeId);
            const FString ResolvedValue = SourceOutputs ? FindNamedValue(*SourceOutputs, Link.SourcePin, UnresolvedValue) : UnresolvedValue;
            AddOrUpdateNamedValue(ExecutionNode.Inputs, Link.TargetPin, ResolvedValue);
        }

        ShineComfyExecutionEvaluator::EvaluateNode(SerializedNode, ExecutionNode);
        OutputValuesByNode.Add(SerializedNode.NodeId, ExecutionNode.Outputs);
    }

    Plan.StatusMessage = FString::Printf(TEXT("Built executable DAG with %d nodes and %d links."), Plan.OrderedNodes.Num(), Plan.Edges.Num());
    return Plan;
}

FShineComfyExecutionResult UShineComfyGraph::ExecuteGraph() const
{
    const FShineComfyExecutionPlan Plan = BuildExecutionPlan();
    FShineComfyExecutionResult Result;

    Result.bSuccess = !Plan.bHasCycle;
    Result.ExecutedNodes = Plan.OrderedNodes;

    if (Plan.bHasCycle)
    {
        Result.Summary = Plan.StatusMessage;
        return Result;
    }

    const FString FinalImageValue = FindFinalImageValue(Plan.OrderedNodes);
    Result.Summary = FinalImageValue.IsEmpty()
        ? Plan.StatusMessage
        : FString::Printf(TEXT("Execution finished. Final preview: %s"), *FinalImageValue);
    return Result;
}

bool UShineComfyGraph::ExportGraphDefinitionToJson(FString& OutJson) const
{
    return FJsonObjectConverter::UStructToJsonObjectString(BuildGraphDocument(), OutJson);
}

bool UShineComfyGraph::ImportGraphJson(const FString& JsonText, FString& OutErrorMessage)
{
    const TArray<FShineComfyNodeDefinition>* DynamicDefinitions = ShineComfySchemaActions::GetDynamicNodeDefinitions(this);
    static const TArray<FShineComfyNodeDefinition> EmptyDefinitions;
    return FShineComfyGraphDocumentBuilder::BuildGraphFromJson(this, JsonText,
        DynamicDefinitions ? *DynamicDefinitions : EmptyDefinitions, OutErrorMessage);
}

int32 UShineComfyGraph::SetTextParameterOnNodesByName(FName ParameterName, const FString& NewValue)
{
    int32 ChangedCount = 0;
    for (UEdGraphNode* GraphNode : Nodes)
    {
        UShineComfyGraphNodeBase* ShineNode = Cast<UShineComfyGraphNodeBase>(GraphNode);
        if (!ShineNode)
        {
            continue;
        }

        if (!ShineNode->FindParameter(ParameterName))
        {
            continue;
        }

        if (ShineNode->SetTextParameter(ParameterName, NewValue))
        {
            ++ChangedCount;
        }
    }

    return ChangedCount;
}

int32 UShineComfyGraph::SetResultImagesOnDisplayNodes(const TArray<FString>& ImagePaths, TArray<FString>& OutNodeTitles)
{
    if (ImagePaths.Num() == 0)
    {
        return 0;
    }

    int32 UpdatedNodeCount = 0;
    for (UEdGraphNode* GraphNode : Nodes)
    {
        UShineComfyGraphNodeBase* ShineNode = Cast<UShineComfyGraphNodeBase>(GraphNode);
        if (!ShineNode)
        {
            continue;
        }

        const FName Preset = ShineNode->GetNodePreset();
        if (Preset == PresetPreview || Preset == PresetGallery)
        {
            ShineNode->SetResultImagePaths(ImagePaths);
            OutNodeTitles.Add(ShineNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
            ++UpdatedNodeCount;
        }
    }

    // 已经打开的"放大"窗口也要跟着更新，不能停在上一次的结果上。
    ShineImageZoom::RefreshOpenWindows(ImagePaths);

    return UpdatedNodeCount;
}

bool UShineComfyGraph::ExportExecutionPlanToJson(FString& OutJson) const
{
    return FJsonObjectConverter::UStructToJsonObjectString(BuildExecutionPlan(), OutJson);
}

bool UShineComfyGraph::ExportComfyPromptToJson(FString& OutJson, FString& OutErrorMessage) const
{
    OutJson.Reset();
    OutErrorMessage.Reset();

    TArray<const UShineComfyGraphNodeBase*> OrderedNodes;
    TMap<const UEdGraphNode*, FString> DynamicNodeIds;
    TMap<FString, FPromptOutputRef> OutputRefs;
    TMap<const UShineComfyGraphNodeBase*, FSamplerExpansion> SamplerExpansions;

    int32 NextGeneratedNodeId = 1;
    auto AllocateNodeId = [&NextGeneratedNodeId]()
    {
        return FString::FromInt(NextGeneratedNodeId++);
    };

    for (UEdGraphNode* GraphNode : Nodes)
    {
        const UShineComfyGraphNodeBase* ShineNode = Cast<UShineComfyGraphNodeBase>(GraphNode);
        if (!ShineNode)
        {
            continue;
        }

        OrderedNodes.Add(ShineNode);

        if (const UShineComfyGraphNode* ComfyNode = Cast<UShineComfyGraphNode>(ShineNode))
        {
            const FString NodeId = AllocateNodeId();
            DynamicNodeIds.Add(GraphNode, NodeId);

            int32 OutputPinIndex = 0;
            for (const UEdGraphPin* Pin : ComfyNode->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output)
                {
                    continue;
                }

                OutputRefs.Add(MakeOutputKey(ComfyNode, Pin->PinName), { NodeId, OutputPinIndex++ });
            }
            continue;
        }

        if (ShineNode->GetNodePreset() == PresetSampler)
        {
            FSamplerExpansion& Expansion = SamplerExpansions.Add(ShineNode);
            Expansion.CheckpointLoaderId = AllocateNodeId();
            Expansion.PositivePromptId = AllocateNodeId();
            Expansion.NegativePromptId = AllocateNodeId();
            Expansion.EmptyLatentId = AllocateNodeId();
            Expansion.KSamplerId = AllocateNodeId();
            Expansion.ModelRef = { Expansion.CheckpointLoaderId, 0 };
            Expansion.ClipRef = { Expansion.CheckpointLoaderId, 1 };
            Expansion.VaeRef = { Expansion.CheckpointLoaderId, 2 };
            Expansion.LatentRef = { Expansion.KSamplerId, 0 };
            OutputRefs.Add(MakeOutputKey(ShineNode, TEXT("Latent")), Expansion.LatentRef);
        }
    }

    if (OrderedNodes.IsEmpty())
    {
        OutErrorMessage = TEXT("当前图里没有可提交到 ComfyUI 的节点。");
        return false;
    }

    TSharedRef<FJsonObject> PromptObject = MakeShared<FJsonObject>();
    for (const UShineComfyGraphNodeBase* ShineNode : OrderedNodes)
    {
        const UShineComfyGraphNode* ComfyNode = Cast<UShineComfyGraphNode>(ShineNode);
        if (ComfyNode)
        {
            const FString* NodeId = DynamicNodeIds.Find(ShineNode);
            if (!NodeId)
            {
                OutErrorMessage = TEXT("生成 ComfyUI prompt 失败：动态节点 ID 映射缺失。");
                return false;
            }

            TSharedRef<FJsonObject> NodeObject = AddPromptNode(PromptObject, *NodeId, ComfyNode->GetNodePreset().ToString());
            TSharedRef<FJsonObject> InputsObject = GetInputsObject(NodeObject);
            for (const FShineComfyNodeParameter& Parameter : ComfyNode->GetParameters())
            {
                AddParameterValueToPromptInputs(Parameter, InputsObject);
            }

            for (const UEdGraphPin* InputPin : ComfyNode->Pins)
            {
                if (!InputPin || InputPin->Direction != EGPD_Input || InputPin->LinkedTo.Num() == 0)
                {
                    continue;
                }

                const UEdGraphPin* SourcePin = InputPin->LinkedTo[0];
                const UShineComfyGraphNodeBase* SourceNode = GetSourceNode(SourcePin);
                const FPromptOutputRef* SourceRef = SourceNode ? OutputRefs.Find(MakeOutputKey(SourceNode, SourcePin->PinName)) : nullptr;
                if (!SourceRef || !SetPromptLinkField(InputPin->PinName.ToString(), *SourceRef, InputsObject))
                {
                    OutErrorMessage = FString::Printf(TEXT("节点 %s 的连线无法转换成 ComfyUI 输入。"), *ComfyNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
                    return false;
                }
            }

            continue;
        }

        if (ShineNode->GetNodePreset() == PresetPrompt)
        {
            continue;
        }

        if (ShineNode->GetNodePreset() == PresetSampler)
        {
            const FSamplerExpansion* Expansion = SamplerExpansions.Find(ShineNode);
            if (!Expansion)
            {
                OutErrorMessage = TEXT("Sampler 预设节点展开失败。" );
                return false;
            }

            const FString CheckpointName = GetTextParameterValue(ShineNode, TEXT("Checkpoint"));
            if (CheckpointName.IsEmpty())
            {
                OutErrorMessage = FString::Printf(TEXT("节点 %s 缺少 Checkpoint，先刷新模型库或手动选择模型后再提交。"), *ShineNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
                return false;
            }

            const int32 Width = GetIntParameterValue(ShineNode, TEXT("Width"), 1024);
            const int32 Height = GetIntParameterValue(ShineNode, TEXT("Height"), 1024);
            const int32 Steps = GetIntParameterValue(ShineNode, TEXT("Steps"), 20);
            const double CfgScale = GetFloatParameterValue(ShineNode, TEXT("CfgScale"), 7.0);
            const FString SamplerName = GetTextParameterValue(ShineNode, TEXT("SamplerName"), TEXT("euler"));
            const FString SchedulerName = GetTextParameterValue(ShineNode, TEXT("Scheduler"), TEXT("normal"));
            const double Denoise = GetFloatParameterValue(ShineNode, TEXT("Denoise"), 1.0);
            const bool bUseFixedSeed = GetBoolParameterValue(ShineNode, TEXT("UseFixedSeed"), true);
            const int32 SeedValue = GetIntParameterValue(ShineNode, TEXT("Seed"), 42);

            TSharedRef<FJsonObject> LoaderObject = AddPromptNode(PromptObject, Expansion->CheckpointLoaderId, TEXT("CheckpointLoaderSimple"));
            GetInputsObject(LoaderObject)->SetStringField(TEXT("ckpt_name"), CheckpointName);

            const UEdGraphPin* PositiveSourcePin = FindLinkedSourcePin(ShineNode, TEXT("Conditioning"));
            const UShineComfyGraphNodeBase* PositiveSourceNode = GetSourceNode(PositiveSourcePin);
            const UEdGraphPin* NegativeSourcePin = FindLinkedSourcePin(ShineNode, TEXT("Negative"));
            const UShineComfyGraphNodeBase* NegativeSourceNode = GetSourceNode(NegativeSourcePin);

            FPromptOutputRef PositiveConditioningRef;
            FPromptOutputRef NegativeConditioningRef;

            if (PositiveSourceNode && PositiveSourceNode->GetNodePreset() == PresetPrompt)
            {
                TSharedRef<FJsonObject> PositiveObject = AddPromptNode(PromptObject, Expansion->PositivePromptId, TEXT("CLIPTextEncode"));
                TSharedRef<FJsonObject> PositiveInputs = GetInputsObject(PositiveObject);
                PositiveInputs->SetStringField(TEXT("text"), GetTextParameterValue(PositiveSourceNode, TEXT("PositivePrompt")));
                SetPromptLinkField(TEXT("clip"), Expansion->ClipRef, PositiveInputs);
                PositiveConditioningRef = { Expansion->PositivePromptId, 0 };
            }
            else if (PositiveSourcePin)
            {
                const FPromptOutputRef* ExistingRef = OutputRefs.Find(MakeOutputKey(PositiveSourceNode, PositiveSourcePin->PinName));
                if (!ExistingRef)
                {
                    OutErrorMessage = TEXT("Sampler 的正向 Conditioning 输入无法解析。" );
                    return false;
                }
                PositiveConditioningRef = *ExistingRef;
            }
            else
            {
                OutErrorMessage = TEXT("Sampler 缺少正向 Conditioning 输入。" );
                return false;
            }

            if (NegativeSourceNode && NegativeSourceNode->GetNodePreset() == PresetPrompt)
            {
                TSharedRef<FJsonObject> NegativeObject = AddPromptNode(PromptObject, Expansion->NegativePromptId, TEXT("CLIPTextEncode"));
                TSharedRef<FJsonObject> NegativeInputs = GetInputsObject(NegativeObject);
                NegativeInputs->SetStringField(TEXT("text"), GetTextParameterValue(NegativeSourceNode, TEXT("NegativePrompt")));
                SetPromptLinkField(TEXT("clip"), Expansion->ClipRef, NegativeInputs);
                NegativeConditioningRef = { Expansion->NegativePromptId, 0 };
            }
            else if (NegativeSourcePin)
            {
                const FPromptOutputRef* ExistingRef = OutputRefs.Find(MakeOutputKey(NegativeSourceNode, NegativeSourcePin->PinName));
                if (!ExistingRef)
                {
                    OutErrorMessage = TEXT("Sampler 的反向 Conditioning 输入无法解析。" );
                    return false;
                }
                NegativeConditioningRef = *ExistingRef;
            }
            else
            {
                OutErrorMessage = TEXT("Sampler 缺少反向 Conditioning 输入。" );
                return false;
            }

            TSharedRef<FJsonObject> LatentObject = AddPromptNode(PromptObject, Expansion->EmptyLatentId, TEXT("EmptyLatentImage"));
            TSharedRef<FJsonObject> LatentInputs = GetInputsObject(LatentObject);
            LatentInputs->SetNumberField(TEXT("width"), Width);
            LatentInputs->SetNumberField(TEXT("height"), Height);
            LatentInputs->SetNumberField(TEXT("batch_size"), 1);

            TSharedRef<FJsonObject> SamplerObject = AddPromptNode(PromptObject, Expansion->KSamplerId, TEXT("KSampler"));
            TSharedRef<FJsonObject> SamplerInputs = GetInputsObject(SamplerObject);
            SetPromptLinkField(TEXT("model"), Expansion->ModelRef, SamplerInputs);
            SetPromptLinkField(TEXT("positive"), PositiveConditioningRef, SamplerInputs);
            SetPromptLinkField(TEXT("negative"), NegativeConditioningRef, SamplerInputs);
            SetPromptLinkField(TEXT("latent_image"), { Expansion->EmptyLatentId, 0 }, SamplerInputs);
            SamplerInputs->SetNumberField(TEXT("steps"), Steps);
            SamplerInputs->SetNumberField(TEXT("cfg"), CfgScale);
            SamplerInputs->SetStringField(TEXT("sampler_name"), SamplerName);
            SamplerInputs->SetStringField(TEXT("scheduler"), SchedulerName);
            SamplerInputs->SetNumberField(TEXT("denoise"), Denoise);

            const UEdGraphPin* SeedSourcePin = FindLinkedSourcePin(ShineNode, TEXT("Seed"));
            const UShineComfyGraphNodeBase* SeedSourceNode = GetSourceNode(SeedSourcePin);
            const FPromptOutputRef* SeedRef = SeedSourceNode && SeedSourcePin ? OutputRefs.Find(MakeOutputKey(SeedSourceNode, SeedSourcePin->PinName)) : nullptr;
            if (SeedRef)
            {
                SetPromptLinkField(TEXT("seed"), *SeedRef, SamplerInputs);
            }
            else
            {
                const int32 FinalSeed = bUseFixedSeed ? SeedValue : static_cast<int32>(FDateTime::UtcNow().GetTicks() & 0x7fffffff);
                SamplerInputs->SetNumberField(TEXT("seed"), FinalSeed);
            }

            continue;
        }

        if (ShineNode->GetNodePreset() == PresetPreview || ShineNode->GetNodePreset() == PresetGallery)
        {
            // (1) 直接接了"图片"输出（例如导演台的 Image）→ 直接挂一个 PreviewImage，不用再解码。
            const UEdGraphPin* ImageSourcePin = FindLinkedSourcePin(ShineNode, TEXT("Image"));
            if (!ImageSourcePin)
            {
                ImageSourcePin = FindLinkedSourcePin(ShineNode, TEXT("Images"));
            }

            const UShineComfyGraphNodeBase* ImageSourceNode = GetSourceNode(ImageSourcePin);
            const FPromptOutputRef* ImageRef = ImageSourceNode && ImageSourcePin
                ? OutputRefs.Find(MakeOutputKey(ImageSourceNode, ImageSourcePin->PinName))
                : nullptr;

            if (ImageRef)
            {
                const FString PreviewNodeId = AllocateNodeId();
                TSharedRef<FJsonObject> PreviewObject = AddPromptNode(PromptObject, PreviewNodeId, TEXT("PreviewImage"));
                SetPromptLinkField(TEXT("images"), *ImageRef, GetInputsObject(PreviewObject));
                continue;
            }

            // (2) 接了 Sampler 的 Latent → 解码后再预览。
            const UEdGraphPin* SourcePin = FindLinkedSourcePin(ShineNode, TEXT("Latent"));
            const UShineComfyGraphNodeBase* SourceNode = GetSourceNode(SourcePin);
            const FPromptOutputRef* LatentRef = SourceNode && SourcePin ? OutputRefs.Find(MakeOutputKey(SourceNode, SourcePin->PinName)) : nullptr;
            const FSamplerExpansion* SourceSampler = SourceNode ? SamplerExpansions.Find(SourceNode) : nullptr;
            if (!LatentRef || !SourceSampler)
            {
                // (3) 什么都没接：当成"纯显示节点"——结果图由编辑器侧（队列历史）塞进节点里直接画出来，
                //     它不参与 prompt 转换，也不该让整张图导出失败。
                continue;
            }

            const FString VaeDecodeId = AllocateNodeId();
            const FString OutputNodeId = AllocateNodeId();
            TSharedRef<FJsonObject> DecodeObject = AddPromptNode(PromptObject, VaeDecodeId, TEXT("VAEDecode"));
            TSharedRef<FJsonObject> DecodeInputs = GetInputsObject(DecodeObject);
            SetPromptLinkField(TEXT("samples"), *LatentRef, DecodeInputs);
            SetPromptLinkField(TEXT("vae"), SourceSampler->VaeRef, DecodeInputs);

            TSharedRef<FJsonObject> OutputObject = AddPromptNode(PromptObject, OutputNodeId, TEXT("PreviewImage"));
            SetPromptLinkField(TEXT("images"), { VaeDecodeId, 0 }, GetInputsObject(OutputObject));
            continue;
        }

        if (ShineNode->GetNodePreset() == PresetDirector)
        {
            const FShineComfyDirectorConfig& DirectorConfig = FShineComfyDirectorConfig::Get();
            if (!DirectorConfig.bIsValid)
            {
                OutErrorMessage = TEXT("导演台配置加载失败，请检查 Resources/DirectorNode.json。");
                return false;
            }

            // 查找图中的 Sampler 以获取 CheckpointLoader 引用（用于 $checkpoint 占位符）
            const FSamplerExpansion* FoundSamplerExpansion = nullptr;
            for (const auto& Pair : SamplerExpansions)
            {
                FoundSamplerExpansion = &Pair.Value;
                break;
            }

            // 参数查找辅助
            auto GetDirectorParamBool = [&](const FString& ParamName) -> bool
            {
                const FShineComfyNodeParameter* Param = ShineNode->FindParameter(FName(*ParamName));
                return Param ? Param->bBoolValue : false;
            };

            // 展开模板中的节点 ID → 实际分配的 ComfyUI 节点 ID
            TMap<FString, FString> ExpansionIdToPromptId;

            // 判断节点是否应被包含（基于 condition 字段）
            auto ShouldIncludeExpansionNode = [&](const FShineComfyDirectorExpansionNode& ExpNode) -> bool
            {
                if (!ExpNode.ConditionJson.IsValid())
                {
                    return true; // 无条件，始终包含
                }

                // {"$paramTrue": "ParamName"}
                FString ParamName;
                if (ExpNode.ConditionJson->TryGetStringField(TEXT("$paramTrue"), ParamName))
                {
                    return GetDirectorParamBool(ParamName);
                }

                return true;
            };

            // 第一遍：分配节点 ID 并创建节点对象
            for (const FShineComfyDirectorExpansionNode& ExpNode : DirectorConfig.Expansion.Nodes)
            {
                if (!ShouldIncludeExpansionNode(ExpNode))
                {
                    continue;
                }

                const FString PromptNodeId = AllocateNodeId();
                ExpansionIdToPromptId.Add(ExpNode.Id, PromptNodeId);
                AddPromptNode(PromptObject, PromptNodeId, ExpNode.ClassType);
            }

            // 第二遍：解析 inputs，替换占位符
            for (const FShineComfyDirectorExpansionNode& ExpNode : DirectorConfig.Expansion.Nodes)
            {
                const FString* PromptNodeId = ExpansionIdToPromptId.Find(ExpNode.Id);
                if (!PromptNodeId)
                {
                    continue;
                }

                const TSharedPtr<FJsonObject>* FoundNodeObj = nullptr;
                if (!PromptObject->TryGetObjectField(*PromptNodeId, FoundNodeObj) || !FoundNodeObj || !FoundNodeObj->IsValid())
                {
                    continue;
                }

                TSharedRef<FJsonObject> InputsObject = GetInputsObject(FoundNodeObj->ToSharedRef());

                if (!ExpNode.InputsJson.IsValid())
                {
                    continue;
                }

                for (const auto& InputPair : ExpNode.InputsJson->Values)
                {
                    const FString FieldName(InputPair.Key);
                    const TSharedPtr<FJsonValue>& FieldValue = InputPair.Value;

                    if (!FieldValue.IsValid())
                    {
                        continue;
                    }

                    // 引用类型: {"$checkpoint": "clip"} / {"$paramTrue": "EnableNegative"}
                    const TSharedPtr<FJsonObject>* ObjVal = nullptr;
                    if (FieldValue->Type == EJson::Object && FieldValue->TryGetObject(ObjVal) && ObjVal && ObjVal->IsValid())
                    {
                        const TSharedPtr<FJsonObject>& RefObj = *ObjVal;

                        // $checkpoint 引用
                        FString CheckpointPin;
                        if (RefObj->TryGetStringField(TEXT("$checkpoint"), CheckpointPin))
                        {
                            if (!FoundSamplerExpansion)
                            {
                                OutErrorMessage = TEXT("导演台需要图中存在 Sampler 节点（用于提供 CheckpointLoader 的 CLIP/Model/VAE 引用）。");
                                return false;
                            }

                            FPromptOutputRef Ref;
                            if (CheckpointPin.Equals(TEXT("model"), ESearchCase::IgnoreCase))
                            {
                                Ref = FoundSamplerExpansion->ModelRef;
                            }
                            else if (CheckpointPin.Equals(TEXT("clip"), ESearchCase::IgnoreCase))
                            {
                                Ref = FoundSamplerExpansion->ClipRef;
                            }
                            else if (CheckpointPin.Equals(TEXT("vae"), ESearchCase::IgnoreCase))
                            {
                                Ref = FoundSamplerExpansion->VaeRef;
                            }

                            if (!Ref.NodeId.IsEmpty())
                            {
                                SetPromptLinkField(FieldName, Ref, InputsObject);
                            }
                            continue;
                        }

                        // $link 引用: {"$link": ["expNodeId", outputIndex]}
                        const TArray<TSharedPtr<FJsonValue>>* LinkArr = nullptr;
                        if (RefObj->TryGetArrayField(TEXT("$link"), LinkArr) && LinkArr && LinkArr->Num() >= 2)
                        {
                            FString RefExpId;
                            double RefIdx = 0;
                            (*LinkArr)[0]->TryGetString(RefExpId);
                            (*LinkArr)[1]->TryGetNumber(RefIdx);

                            const FString* RefPromptId = ExpansionIdToPromptId.Find(RefExpId);
                            if (RefPromptId)
                            {
                                SetPromptLinkField(FieldName, { *RefPromptId, static_cast<int32>(RefIdx) }, InputsObject);
                            }
                            continue;
                        }

                        // 未知对象引用，跳过
                        continue;
                    }

                    // 字符串类型：可能包含 {ParamName} 占位符
                    if (FieldValue->Type == EJson::String)
                    {
                        FString StrValue;
                        FieldValue->TryGetString(StrValue);

                        // 替换 {ParamName} 占位符
                        for (const FShineComfyNodeParameter& Param : DirectorConfig.Parameters)
                        {
                            const FString Placeholder = FString::Printf(TEXT("{%s}"), *Param.Name.ToString());
                            if (!StrValue.Contains(Placeholder))
                            {
                                continue;
                            }

                            // 关键：必须取「节点上当前的值」。
                            // 以前这里直接用了 DirectorConfig.Parameters 的默认值，导致在节点上改图片/
                            // 提示词/种子（或者脚本灌参数）都不会生效，导出的 prompt 永远是 JSON 默认值，
                            // 于是每次生成的结果都一模一样。
                            // 节点上没有这个参数（老资产）或值为空时，才退回配置默认值。
                            const FShineComfyNodeParameter* NodeParam = ShineNode->FindParameter(Param.Name);
                            const FString NodeValue = NodeParam ? NodeParam->ExportValueAsString() : FString();
                            const FString ParamValue = NodeValue.IsEmpty() ? Param.ExportValueAsString() : NodeValue;

                            StrValue.ReplaceInline(*Placeholder, *ParamValue);
                        }

                        InputsObject->SetStringField(FieldName, StrValue);
                        continue;
                    }

                    // 数字类型
                    if (FieldValue->Type == EJson::Number)
                    {
                        double NumValue = 0;
                        FieldValue->TryGetNumber(NumValue);
                        InputsObject->SetNumberField(FieldName, NumValue);
                        continue;
                    }

                    // 布尔类型
                    if (FieldValue->Type == EJson::Boolean)
                    {
                        bool bVal = false;
                        FieldValue->TryGetBool(bVal);
                        InputsObject->SetBoolField(FieldName, bVal);
                        continue;
                    }
                }
            }

            // 注册输出引用，供下游节点连接
            for (const FShineComfyDirectorExpansionOutput& ExpOutput : DirectorConfig.Expansion.Outputs)
            {
                const FString* PromptId = ExpansionIdToPromptId.Find(ExpOutput.TargetNodeId);
                if (PromptId)
                {
                    OutputRefs.Add(MakeOutputKey(ShineNode, FName(*ExpOutput.PinName)), { *PromptId, ExpOutput.OutputIndex });
                }
            }

            continue;
        }

        OutErrorMessage = FString::Printf(TEXT("节点 %s 目前还不能直接转换成可提交的 ComfyUI 工作流。"), *ShineNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
        return false;
    }

    // ComfyUI 只接受"含输出节点"的 prompt。一个节点都没展开出来时，
    // 必须在这里就把原因说清楚 —— 否则 ComfyUI 只会回一句 prompt_no_outputs，很难定位。
    if (PromptObject->Values.IsEmpty())
    {
        OutErrorMessage = TEXT("这张图没有展开出任何 ComfyUI 节点：请检查导演台节点的 "
            "「启用图片生成 / 启用视频生成」两个开关，至少打开一个（两者互斥）。");
        return false;
    }

    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    return FJsonSerializer::Serialize(PromptObject, Writer);
}