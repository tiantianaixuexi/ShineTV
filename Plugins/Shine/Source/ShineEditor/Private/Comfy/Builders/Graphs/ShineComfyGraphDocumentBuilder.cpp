#include "Comfy/Builders/Graphs/ShineComfyGraphDocumentBuilder.h"

#include "Comfy/Builders/Nodes/ShineComfyDirectorConfig.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Graph/Node/Comfy/ShineComfyGraphNode.h"
#include "Graph/Node/Display/ShineComfyMultiImageGraphNode.h"
#include "Graph/Node/Presets/ShineComfyDirectorGraphNode.h"
#include "Graph/Node/Presets/ShineComfyPreviewGraphNode.h"
#include "Graph/Node/Presets/ShineComfyPromptGraphNode.h"
#include "Graph/Node/Presets/ShineComfySamplerGraphNode.h"
#include "Graph/Node/ShineComfyGraphNodeBase.h"
#include "Graph/ShineComfyGraph.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    struct FJsonNodeRecord
    {
        FString Id;
        FGuid NodeGuid;
        FString Type;
        FVector2D Position = FVector2D::ZeroVector;
        TArray<FShineComfyNodeParameter> Parameters;
    };

    struct FJsonLinkRecord
    {
        FString SourceNodeId;
        FString SourcePin;
        FString TargetNodeId;
        FString TargetPin;
    };

    // ComfyUI 格式里的连线先记录槽位，等所有节点解析完再按定义解析成 pin 名。
    struct FDeferredLinkRecord
    {
        FString SourceNodeId;
        int32 SourceSlot = INDEX_NONE;
        FString SourcePin;
        FString TargetNodeId;
        int32 TargetSlot = INDEX_NONE;
        FString TargetPin;
    };

    bool EqualsIgnoreCase(const FString& A, const FString& B)
    {
        return A.Equals(B, ESearchCase::IgnoreCase);
    }

    TSharedPtr<FJsonValue> FindField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
    {
        if (!Object.IsValid())
        {
            return nullptr;
        }

        TSharedPtr<FJsonValue> Value = Object->TryGetField(FieldName);
        if (Value.IsValid())
        {
            return Value;
        }

        for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Object->Values)
        {
            if (EqualsIgnoreCase(Entry.Key, FieldName))
            {
                return Entry.Value;
            }
        }

        return nullptr;
    }

    bool GetObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, TSharedPtr<FJsonObject>& OutObject)
    {
        const TSharedPtr<FJsonValue> Value = FindField(Object, FieldName);
        OutObject = Value.IsValid() ? Value->AsObject() : nullptr;
        return OutObject.IsValid();
    }

    bool GetArrayField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const TArray<TSharedPtr<FJsonValue>>** OutArray)
    {
        const TSharedPtr<FJsonValue> Value = FindField(Object, FieldName);
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Value.IsValid() && Value->TryGetArray(Array))
        {
            *OutArray = Array;
            return true;
        }
        return false;
    }

    FString JsonValueToString(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value.IsValid())
        {
            return FString();
        }

        FString StringValue;
        if (Value->TryGetString(StringValue))
        {
            return StringValue;
        }

        double NumberValue = 0.0;
        if (Value->TryGetNumber(NumberValue))
        {
            if (FMath::IsNearlyEqual(NumberValue, FMath::RoundToZero(NumberValue), 1e-6))
            {
                return FString::FromInt(static_cast<int64>(NumberValue));
            }
            return FString::SanitizeFloat(NumberValue);
        }

        bool BoolValue = false;
        if (Value->TryGetBool(BoolValue))
        {
            return BoolValue ? TEXT("true") : TEXT("false");
        }

        return FString();
    }

    void AddStringParameter(FJsonNodeRecord& Record, const FString& Name, const FString& Value)
    {
        FShineComfyNodeParameter& Parameter = Record.Parameters.AddDefaulted_GetRef();
        Parameter.Name = FName(*Name);
        Parameter.Label = FText::FromString(Name);
        Parameter.Type = EShineComfyParameterType::Text;
        Parameter.StringValue = Value;
    }

    FVector2D ReadPosition(const TSharedPtr<FJsonObject>& NodeObject)
    {
        const TArray<TSharedPtr<FJsonValue>>* PositionArray = nullptr;
        if (GetArrayField(NodeObject, TEXT("position"), &PositionArray) || GetArrayField(NodeObject, TEXT("pos"), &PositionArray))
        {
            if (PositionArray->Num() >= 2)
            {
                return FVector2D((*PositionArray)[0]->AsNumber(), (*PositionArray)[1]->AsNumber());
            }
            return FVector2D::ZeroVector;
        }

        TSharedPtr<FJsonObject> PositionObject;
        if (GetObjectField(NodeObject, TEXT("position"), PositionObject) || GetObjectField(NodeObject, TEXT("pos"), PositionObject))
        {
            double X = 0.0;
            double Y = 0.0;
            if (!PositionObject->TryGetNumberField(TEXT("X"), X))
            {
                PositionObject->TryGetNumberField(TEXT("x"), X);
            }
            if (!PositionObject->TryGetNumberField(TEXT("Y"), Y))
            {
                PositionObject->TryGetNumberField(TEXT("y"), Y);
            }
            return FVector2D(X, Y);
        }

        return FVector2D::ZeroVector;
    }

    const FShineComfyNodeDefinition* FindDefinitionByType(const TArray<FShineComfyNodeDefinition>& Definitions, const FString& TypeName)
    {
        for (const FShineComfyNodeDefinition& Definition : Definitions)
        {
            if (EqualsIgnoreCase(Definition.NodeClassName, TypeName))
            {
                return &Definition;
            }
        }
        return nullptr;
    }

    FString ResolveComfyPinName(const TArray<FJsonNodeRecord>& Records, const TMap<FString, int32>& RecordIndexById,
        const TArray<FShineComfyNodeDefinition>& Definitions, const FString& NodeId, int32 Slot, bool bOutput)
    {
        const int32* RecordIndex = RecordIndexById.Find(NodeId);
        if (RecordIndex && Records.IsValidIndex(*RecordIndex))
        {
            const FShineComfyNodeDefinition* Definition = FindDefinitionByType(Definitions, Records[*RecordIndex].Type);
            if (Definition)
            {
                const TArray<FShineComfyNodePinDefinition>& Pins = bOutput ? Definition->OutputPins : Definition->InputPins;
                if (Pins.IsValidIndex(Slot))
                {
                    return Pins[Slot].Name;
                }
            }
        }

        return bOutput
            ? FString::Printf(TEXT("Output%d"), Slot + 1)
            : FString::Printf(TEXT("Input%d"), Slot + 1);
    }

    void ReadNodeParameters(const TSharedPtr<FJsonObject>& NodeObject, const TArray<FShineComfyNodeDefinition>& Definitions,
        FJsonNodeRecord& Record, TArray<FString>& OutWarnings)
    {
        // 对象形式的参数：{ "name": value }
        TSharedPtr<FJsonObject> ParameterObject;
        if (GetObjectField(NodeObject, TEXT("parameters"), ParameterObject) || GetObjectField(NodeObject, TEXT("params"), ParameterObject))
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : ParameterObject->Values)
            {
                AddStringParameter(Record, Entry.Key, JsonValueToString(Entry.Value));
            }
            return;
        }

        // 数组形式的参数：兼容 ExportGraphDefinitionToJson 的导出结果。
        const TArray<TSharedPtr<FJsonValue>>* ParameterArray = nullptr;
        if (GetArrayField(NodeObject, TEXT("parameters"), &ParameterArray))
        {
            for (const TSharedPtr<FJsonValue>& ParameterValue : *ParameterArray)
            {
                const TSharedPtr<FJsonObject> ParameterEntry = ParameterValue.IsValid() ? ParameterValue->AsObject() : nullptr;
                if (!ParameterEntry.IsValid())
                {
                    continue;
                }

                FString Name;
                if (!ParameterEntry->TryGetStringField(TEXT("Name"), Name))
                {
                    ParameterEntry->TryGetStringField(TEXT("name"), Name);
                }
                if (Name.IsEmpty())
                {
                    continue;
                }

                FString Value;
                if (!ParameterEntry->TryGetStringField(TEXT("StringValue"), Value) || Value.IsEmpty())
                {
                    double NumberValue = 0.0;
                    bool BoolValue = false;
                    if (ParameterEntry->TryGetNumberField(TEXT("IntValue"), NumberValue) && NumberValue != 0.0)
                    {
                        Value = FString::FromInt(static_cast<int32>(NumberValue));
                    }
                    else if (ParameterEntry->TryGetNumberField(TEXT("FloatValue"), NumberValue))
                    {
                        Value = FString::SanitizeFloat(NumberValue);
                    }
                    else if (ParameterEntry->TryGetBoolField(TEXT("bBoolValue"), BoolValue))
                    {
                        Value = BoolValue ? TEXT("true") : TEXT("false");
                    }
                }

                AddStringParameter(Record, Name, Value);
            }
            return;
        }

        // ComfyUI workflow 的 widgets_values：按定义参数顺序映射（跳过 control_after_generate 这类注入值）。
        const TSharedPtr<FJsonValue> WidgetsValue = FindField(NodeObject, TEXT("widgets_values"));
        if (!WidgetsValue.IsValid())
        {
            return;
        }

        const FShineComfyNodeDefinition* Definition = FindDefinitionByType(Definitions, Record.Type);
        if (!Definition)
        {
            OutWarnings.Add(FString::Printf(TEXT("节点 %s 找不到定义，widgets_values 已忽略。"), *Record.Type));
            return;
        }

        const TArray<TSharedPtr<FJsonValue>>* WidgetArray = nullptr;
        if (WidgetsValue->TryGetArray(WidgetArray))
        {
            static const FString ControlValues[] = { TEXT("fixed"), TEXT("increment"), TEXT("decrement"), TEXT("randomize") };

            int32 ParameterIndex = 0;
            for (const TSharedPtr<FJsonValue>& WidgetValue : *WidgetArray)
            {
                const FString WidgetString = JsonValueToString(WidgetValue);

                bool bIsControlValue = false;
                for (const FString& ControlValue : ControlValues)
                {
                    if (EqualsIgnoreCase(WidgetString, ControlValue))
                    {
                        bIsControlValue = true;
                        break;
                    }
                }
                if (bIsControlValue)
                {
                    continue;
                }

                if (!Definition->Parameters.IsValidIndex(ParameterIndex))
                {
                    break;
                }

                AddStringParameter(Record, Definition->Parameters[ParameterIndex].Name.ToString(), WidgetString);
                ++ParameterIndex;
            }
        }
        else
        {
            const TSharedPtr<FJsonObject> WidgetObject = WidgetsValue->AsObject();
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : WidgetObject->Values)
            {
                AddStringParameter(Record, Entry.Key, JsonValueToString(Entry.Value));
            }
        }
    }

    bool ParseEndpointValue(const TSharedPtr<FJsonValue>& Value, FString& OutNodeId, FString& OutPin)
    {
        if (!Value.IsValid())
        {
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* EndpointArray = nullptr;
        if (Value->TryGetArray(EndpointArray))
        {
            if (EndpointArray->Num() < 2)
            {
                return false;
            }

            OutNodeId = JsonValueToString((*EndpointArray)[0]);
            OutPin = JsonValueToString((*EndpointArray)[1]);
            return !OutNodeId.IsEmpty() && !OutPin.IsEmpty();
        }

        FString Endpoint;
        if (!Value->TryGetString(Endpoint))
        {
            return false;
        }

        const int32 SplitIndex = Endpoint.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (SplitIndex == INDEX_NONE || SplitIndex == 0 || SplitIndex == Endpoint.Len() - 1)
        {
            return false;
        }

        OutNodeId = Endpoint.Left(SplitIndex);
        OutPin = Endpoint.Mid(SplitIndex + 1);
        return true;
    }
}

bool FShineComfyGraphDocumentBuilder::ParseGraphJson(const FString& JsonText,
    const TArray<FShineComfyNodeDefinition>& DynamicDefinitions, FShineComfyGraphDocument& OutDocument, FString& OutErrorMessage)
{
    OutDocument = FShineComfyGraphDocument();
    OutErrorMessage.Reset();

    TSharedPtr<FJsonObject> RootObject;
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
        {
            OutErrorMessage = TEXT("JSON 解析失败，请检查格式。");
            return false;
        }
    }

    TArray<FJsonNodeRecord> Records;
    TArray<FJsonLinkRecord> Links;
    TArray<FDeferredLinkRecord> DeferredLinks;
    TArray<FString> Warnings;
    TMap<FString, int32> RecordIndexById;

    auto RegisterRecord = [&Records, &RecordIndexById](FJsonNodeRecord&& Record)
    {
        const int32 RecordIndex = Records.Num();
        RecordIndexById.Add(Record.Id, RecordIndex);

        FGuid ParsedGuid;
        Record.NodeGuid = FGuid::Parse(Record.Id, ParsedGuid) ? ParsedGuid : FGuid::NewGuid();
        Records.Add(MoveTemp(Record));
    };

    // ---- 解析节点 ----
    const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
    if (GetArrayField(RootObject, TEXT("nodes"), &NodesArray))
    {
        int32 NumericIdFallback = 1;
        for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
        {
            const TSharedPtr<FJsonObject> NodeObject = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
            if (!NodeObject.IsValid())
            {
                continue;
            }

            FJsonNodeRecord Record;
            const TSharedPtr<FJsonValue> IdValue = FindField(NodeObject, TEXT("id"));
            Record.Id = IdValue.IsValid() ? JsonValueToString(IdValue) : FString();
            if (Record.Id.IsEmpty())
            {
                Record.Id = FString::FromInt(NumericIdFallback++);
            }

            FString NodeType;
            const TSharedPtr<FJsonValue> TypeValue = FindField(NodeObject, TEXT("type"));
            if (TypeValue.IsValid())
            {
                TypeValue->TryGetString(NodeType);
            }
            if (NodeType.IsEmpty())
            {
                OutErrorMessage = FString::Printf(TEXT("节点 %s 缺少 type 字段。"), *Record.Id);
                return false;
            }
            Record.Type = NodeType;

            Record.Position = ReadPosition(NodeObject);
            ReadNodeParameters(NodeObject, DynamicDefinitions, Record, Warnings);
            RegisterRecord(MoveTemp(Record));
        }
    }
    else
    {
        // ComfyUI API prompt 格式：{ "id": { "class_type": ..., "inputs": ... } }
        bool bFoundApiNode = false;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : RootObject->Values)
        {
            const TSharedPtr<FJsonObject> NodeObject = Entry.Value.IsValid() ? Entry.Value->AsObject() : nullptr;
            if (!NodeObject.IsValid())
            {
                continue;
            }

            FString ClassType;
            const TSharedPtr<FJsonValue> ClassTypeValue = FindField(NodeObject, TEXT("class_type"));
            if (!ClassTypeValue.IsValid() || !ClassTypeValue->TryGetString(ClassType))
            {
                continue;
            }

            bFoundApiNode = true;

            FJsonNodeRecord Record;
            Record.Id = Entry.Key;
            Record.Type = ClassType;
            Record.Position = ReadPosition(NodeObject);

            TSharedPtr<FJsonObject> InputsObject;
            if (GetObjectField(NodeObject, TEXT("inputs"), InputsObject))
            {
                for (const TPair<FString, TSharedPtr<FJsonValue>>& InputEntry : InputsObject->Values)
                {
                    const TArray<TSharedPtr<FJsonValue>>* LinkArray = nullptr;
                    if (InputEntry.Value.IsValid() && InputEntry.Value->TryGetArray(LinkArray)
                        && LinkArray->Num() >= 2 && (*LinkArray)[1]->Type == EJson::Number)
                    {
                        FDeferredLinkRecord& DeferredLink = DeferredLinks.AddDefaulted_GetRef();
                        DeferredLink.SourceNodeId = JsonValueToString((*LinkArray)[0]);
                        DeferredLink.SourceSlot = static_cast<int32>((*LinkArray)[1]->AsNumber());
                        DeferredLink.TargetNodeId = Record.Id;
                        DeferredLink.TargetPin = InputEntry.Key;
                        continue;
                    }

                    AddStringParameter(Record, InputEntry.Key, JsonValueToString(InputEntry.Value));
                }
            }

            RegisterRecord(MoveTemp(Record));
        }

        if (!bFoundApiNode)
        {
            OutErrorMessage = TEXT("JSON 中找不到 nodes 数组，也不是 ComfyUI API prompt 格式。");
            return false;
        }
    }

    // ---- 解析连线 ----
    const TArray<TSharedPtr<FJsonValue>>* LinksArray = nullptr;
    if (GetArrayField(RootObject, TEXT("links"), &LinksArray))
    {
        for (const TSharedPtr<FJsonValue>& LinkValue : *LinksArray)
        {
            const TArray<TSharedPtr<FJsonValue>>* LinkArray = nullptr;
            if (LinkValue.IsValid() && LinkValue->TryGetArray(LinkArray))
            {
                // ComfyUI workflow: [linkId, originId, originSlot, targetId, targetSlot, type]
                if (LinkArray->Num() < 5)
                {
                    continue;
                }

                FDeferredLinkRecord& DeferredLink = DeferredLinks.AddDefaulted_GetRef();
                DeferredLink.SourceNodeId = JsonValueToString((*LinkArray)[1]);
                DeferredLink.SourceSlot = static_cast<int32>((*LinkArray)[2]->AsNumber());
                DeferredLink.TargetNodeId = JsonValueToString((*LinkArray)[3]);
                DeferredLink.TargetSlot = static_cast<int32>((*LinkArray)[4]->AsNumber());
                continue;
            }

            const TSharedPtr<FJsonObject> LinkObject = LinkValue.IsValid() ? LinkValue->AsObject() : nullptr;
            if (!LinkObject.IsValid())
            {
                continue;
            }

            FJsonLinkRecord Link;
            TSharedPtr<FJsonValue> FromValue = FindField(LinkObject, TEXT("from"));
            if (!FromValue.IsValid())
            {
                FromValue = FindField(LinkObject, TEXT("source"));
            }
            TSharedPtr<FJsonValue> ToValue = FindField(LinkObject, TEXT("to"));
            if (!ToValue.IsValid())
            {
                ToValue = FindField(LinkObject, TEXT("target"));
            }

            if (!FromValue.IsValid() || !ToValue.IsValid() || !ParseEndpointValue(FromValue, Link.SourceNodeId, Link.SourcePin)
                || !ParseEndpointValue(ToValue, Link.TargetNodeId, Link.TargetPin))
            {
                Warnings.Add(TEXT("一条连线格式无效，已跳过。"));
                continue;
            }

            Links.Add(MoveTemp(Link));
        }
    }

    // ---- 解析 ComfyUI 槽位连线 ----
    for (const FDeferredLinkRecord& DeferredLink : DeferredLinks)
    {
        FJsonLinkRecord Link;
        Link.SourceNodeId = DeferredLink.SourceNodeId;
        Link.SourcePin = DeferredLink.SourcePin.IsEmpty()
            ? ResolveComfyPinName(Records, RecordIndexById, DynamicDefinitions, DeferredLink.SourceNodeId, DeferredLink.SourceSlot, true)
            : DeferredLink.SourcePin;
        Link.TargetNodeId = DeferredLink.TargetNodeId;
        Link.TargetPin = DeferredLink.TargetPin.IsEmpty()
            ? ResolveComfyPinName(Records, RecordIndexById, DynamicDefinitions, DeferredLink.TargetNodeId, DeferredLink.TargetSlot, false)
            : DeferredLink.TargetPin;
        Links.Add(MoveTemp(Link));
    }

    // ---- 组装文档 ----
    for (const FJsonNodeRecord& Record : Records)
    {
        FShineComfySerializedNode& SerializedNode = OutDocument.Nodes.AddDefaulted_GetRef();
        SerializedNode.NodeId = Record.NodeGuid;
        SerializedNode.Preset = Record.Type;
        SerializedNode.Position = Record.Position;
        SerializedNode.Parameters = Record.Parameters;
    }

    for (const FJsonLinkRecord& Link : Links)
    {
        const int32* SourceIndex = RecordIndexById.Find(Link.SourceNodeId);
        const int32* TargetIndex = RecordIndexById.Find(Link.TargetNodeId);
        if (!SourceIndex || !TargetIndex)
        {
            Warnings.Add(FString::Printf(TEXT("连线 %s -> %s 引用了未知节点，已跳过。"), *Link.SourceNodeId, *Link.TargetNodeId));
            continue;
        }

        FShineComfySerializedLink& SerializedLink = OutDocument.Links.AddDefaulted_GetRef();
        SerializedLink.SourceNodeId = Records[*SourceIndex].NodeGuid;
        SerializedLink.SourcePin = Link.SourcePin;
        SerializedLink.TargetNodeId = Records[*TargetIndex].NodeGuid;
        SerializedLink.TargetPin = Link.TargetPin;
    }

    if (Warnings.Num() > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ShineComfyGraphDocumentBuilder: %s"), *FString::Join(Warnings, TEXT(" | ")));
    }

    return true;
}

bool FShineComfyGraphDocumentBuilder::ApplyGraphDocument(UShineComfyGraph* Graph, const FShineComfyGraphDocument& Document,
    const TArray<FShineComfyNodeDefinition>& DynamicDefinitions, FString& OutErrorMessage)
{
    if (!Graph)
    {
        OutErrorMessage = TEXT("目标图为空。");
        return false;
    }

    Graph->Modify();

    TArray<UEdGraphNode*> OldNodes(Graph->Nodes);
    for (UEdGraphNode* OldNode : OldNodes)
    {
        Graph->RemoveNode(OldNode, true);
    }

    TMap<FGuid, UShineComfyGraphNodeBase*> SpawnedNodes;
    for (const FShineComfySerializedNode& SerializedNode : Document.Nodes)
    {
        FShineComfyNodeDefinition Definition;
        bool bDynamicNode = false;
        UClass* NodeClass = nullptr;

        if (EqualsIgnoreCase(SerializedNode.Preset, TEXT("Prompt")))
        {
            NodeClass = UShineComfyPromptGraphNode::StaticClass();
        }
        else if (EqualsIgnoreCase(SerializedNode.Preset, TEXT("Sampler")))
        {
            NodeClass = UShineComfySamplerGraphNode::StaticClass();
        }
        else if (EqualsIgnoreCase(SerializedNode.Preset, TEXT("Preview")))
        {
            NodeClass = UShineComfyPreviewGraphNode::StaticClass();
        }
        else if (EqualsIgnoreCase(SerializedNode.Preset, TEXT("MultiImageGallery")))
        {
            NodeClass = UShineComfyMultiImageGraphNode::StaticClass();
        }
        else if (EqualsIgnoreCase(SerializedNode.Preset, TEXT("Director")) ||
            EqualsIgnoreCase(SerializedNode.Preset, FShineComfyDirectorConfig::Get().Preset))
        {
            // 导演台是 JSON 驱动的预设节点：预设名来自 Resources/DirectorNode.json 的 "preset" 字段，
            // 所以这里同时接受 "Director" 与配置里声明的名字。
            NodeClass = UShineComfyDirectorGraphNode::StaticClass();
        }
        else if (const FShineComfyNodeDefinition* FoundDefinition = FindDefinitionByType(DynamicDefinitions, SerializedNode.Preset))
        {
            Definition = *FoundDefinition;
            bDynamicNode = true;
            NodeClass = UShineComfyGraphNode::StaticClass();
        }

        if (!NodeClass)
        {
            OutErrorMessage = FString::Printf(
                TEXT("未知节点类型 %s。请先在面板中刷新 Comfy 节点列表，再导入包含该类型的 JSON。"),
                *SerializedNode.Preset);
            return false;
        }

        UShineComfyGraphNodeBase* NewNode = NewObject<UShineComfyGraphNodeBase>(Graph, NodeClass);
        if (bDynamicNode)
        {
            if (UShineComfyGraphNode* ComfyNode = Cast<UShineComfyGraphNode>(NewNode))
            {
                ComfyNode->ConfigureFromDefinition(Definition);
            }
        }

        Graph->AddNode(NewNode, false, false);
        NewNode->SetFlags(RF_Transactional);
        NewNode->CreateNewGuid();
        NewNode->PostPlacedNewNode();
        NewNode->NodePosX = FMath::RoundToInt(SerializedNode.Position.X);
        NewNode->NodePosY = FMath::RoundToInt(SerializedNode.Position.Y);
        NewNode->AllocateDefaultPins();

        for (const FShineComfyNodeParameter& Parameter : SerializedNode.Parameters)
        {
            const FShineComfyNodeParameter* Target = NewNode->FindParameter(Parameter.Name);
            if (!Target)
            {
                continue;
            }

            const FString RawValue = Parameter.ExportValueAsString();
            switch (Target->Type)
            {
            case EShineComfyParameterType::Float:
                NewNode->SetFloatParameter(Parameter.Name, FCString::Atod(*RawValue));
                break;
            case EShineComfyParameterType::Integer:
                NewNode->SetIntegerParameter(Parameter.Name, FCString::Atoi(*RawValue));
                break;
            case EShineComfyParameterType::Boolean:
                NewNode->SetBoolParameter(Parameter.Name, EqualsIgnoreCase(RawValue, TEXT("true")) || RawValue == TEXT("1"));
                break;
            default:
                NewNode->SetTextParameter(Parameter.Name, RawValue);
                break;
            }
        }

        SpawnedNodes.Add(SerializedNode.NodeId, NewNode);
    }

    bool bAllLinksCreated = true;
    for (const FShineComfySerializedLink& Link : Document.Links)
    {
        UShineComfyGraphNodeBase** SourceNode = SpawnedNodes.Find(Link.SourceNodeId);
        UShineComfyGraphNodeBase** TargetNode = SpawnedNodes.Find(Link.TargetNodeId);
        if (!SourceNode || !TargetNode)
        {
            bAllLinksCreated = false;
            continue;
        }

        UEdGraphPin* OutputPin = (*SourceNode)->FindPin(FName(*Link.SourcePin), EGPD_Output);
        UEdGraphPin* InputPin = (*TargetNode)->FindPin(FName(*Link.TargetPin), EGPD_Input);
        if (!OutputPin || !InputPin || !Graph->GetSchema()->TryCreateConnection(OutputPin, InputPin))
        {
            bAllLinksCreated = false;
            UE_LOG(LogTemp, Warning,
                TEXT("ShineComfyGraphDocumentBuilder: 连线失败 %s:%s -> %s:%s"),
                *Link.SourceNodeId.ToString(), *Link.SourcePin, *Link.TargetNodeId.ToString(), *Link.TargetPin);
        }
    }

    Graph->NotifyGraphChanged();

    if (!bAllLinksCreated)
    {
        OutErrorMessage = TEXT("部分连线无法创建，详情见输出日志（节点已成功创建）。");
    }
    return true;
}

bool FShineComfyGraphDocumentBuilder::BuildGraphFromJson(UShineComfyGraph* Graph, const FString& JsonText,
    const TArray<FShineComfyNodeDefinition>& DynamicDefinitions, FString& OutErrorMessage)
{
    FShineComfyGraphDocument Document;
    if (!ParseGraphJson(JsonText, DynamicDefinitions, Document, OutErrorMessage))
    {
        return false;
    }

    return ApplyGraphDocument(Graph, Document, DynamicDefinitions, OutErrorMessage);
}
