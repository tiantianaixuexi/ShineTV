#include "Graph/ShineTextureGraph.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/Guid.h"
#include "Node/ShineTextureGraphNodeBase.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/NoExportTypes.h"

namespace
{
    constexpr int32 ShineTextureGraphJsonVersion = 1;

    TSharedRef<FJsonObject> MakeVector2Object(const FVector2D& Value)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("x"), Value.X);
        Object->SetNumberField(TEXT("y"), Value.Y);
        return Object;
    }

    TSharedRef<FJsonObject> MakeIntPointObject(const FIntPoint& Value)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("x"), Value.X);
        Object->SetNumberField(TEXT("y"), Value.Y);
        return Object;
    }

    TSharedRef<FJsonObject> MakeLinearColorObject(const FLinearColor& Value)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("r"), Value.R);
        Object->SetNumberField(TEXT("g"), Value.G);
        Object->SetNumberField(TEXT("b"), Value.B);
        Object->SetNumberField(TEXT("a"), Value.A);
        return Object;
    }

    bool SerializeSupportedProperty(const FProperty* Property, const void* ValuePtr, TSharedRef<FJsonObject>& OutJsonObject)
    {
        if (!Property || !ValuePtr)
        {
            return false;
        }

        if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            OutJsonObject->SetNumberField(Property->GetName(), FloatProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            OutJsonObject->SetNumberField(Property->GetName(), DoubleProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
        {
            OutJsonObject->SetNumberField(Property->GetName(), IntProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        if (const FInt64Property* Int64Property = CastField<FInt64Property>(Property))
        {
            OutJsonObject->SetNumberField(Property->GetName(), static_cast<double>(Int64Property->GetPropertyValue(ValuePtr)));
            return true;
        }

        if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            OutJsonObject->SetBoolField(Property->GetName(), BoolProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            OutJsonObject->SetNumberField(Property->GetName(), ByteProperty->GetPropertyValue(ValuePtr));
            return true;
        }

        if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
            {
                OutJsonObject->SetObjectField(Property->GetName(), MakeVector2Object(*static_cast<const FVector2D*>(ValuePtr)));
                return true;
            }

            if (StructProperty->Struct == TBaseStructure<FIntPoint>::Get())
            {
                OutJsonObject->SetObjectField(Property->GetName(), MakeIntPointObject(*static_cast<const FIntPoint*>(ValuePtr)));
                return true;
            }

            if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
            {
                OutJsonObject->SetObjectField(Property->GetName(), MakeLinearColorObject(*static_cast<const FLinearColor*>(ValuePtr)));
                return true;
            }
        }

        return false;
    }

    bool DeserializeSupportedProperty(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& JsonValue)
    {
        if (!Property || !ValuePtr || !JsonValue.IsValid())
        {
            return false;
        }

        if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
        {
            FloatProperty->SetPropertyValue(ValuePtr, static_cast<float>(JsonValue->AsNumber()));
            return true;
        }

        if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
        {
            DoubleProperty->SetPropertyValue(ValuePtr, JsonValue->AsNumber());
            return true;
        }

        if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
        {
            IntProperty->SetPropertyValue(ValuePtr, static_cast<int32>(JsonValue->AsNumber()));
            return true;
        }

        if (FInt64Property* Int64Property = CastField<FInt64Property>(Property))
        {
            Int64Property->SetPropertyValue(ValuePtr, static_cast<int64>(JsonValue->AsNumber()));
            return true;
        }

        if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
        {
            BoolProperty->SetPropertyValue(ValuePtr, JsonValue->AsBool());
            return true;
        }

        if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            ByteProperty->SetPropertyValue(ValuePtr, static_cast<uint8>(JsonValue->AsNumber()));
            return true;
        }

        if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            const TSharedPtr<FJsonObject>* StructObject = nullptr;
            if (JsonValue->TryGetObject(StructObject) && StructObject && StructObject->IsValid())
            {
                if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
                {
                    FVector2D& Value = *static_cast<FVector2D*>(ValuePtr);
                    Value.X = static_cast<float>((*StructObject)->GetNumberField(TEXT("x")));
                    Value.Y = static_cast<float>((*StructObject)->GetNumberField(TEXT("y")));
                    return true;
                }

                if (StructProperty->Struct == TBaseStructure<FIntPoint>::Get())
                {
                    FIntPoint& Value = *static_cast<FIntPoint*>(ValuePtr);
                    Value.X = static_cast<int32>((*StructObject)->GetNumberField(TEXT("x")));
                    Value.Y = static_cast<int32>((*StructObject)->GetNumberField(TEXT("y")));
                    return true;
                }

                if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
                {
                    FLinearColor& Value = *static_cast<FLinearColor*>(ValuePtr);
                    Value.R = static_cast<float>((*StructObject)->GetNumberField(TEXT("r")));
                    Value.G = static_cast<float>((*StructObject)->GetNumberField(TEXT("g")));
                    Value.B = static_cast<float>((*StructObject)->GetNumberField(TEXT("b")));
                    Value.A = static_cast<float>((*StructObject)->GetNumberField(TEXT("a")));
                    return true;
                }
            }
        }

        return false;
    }

    bool ShouldSerializeNodeProperty(const FProperty* Property)
    {
        if (!Property)
        {
            return false;
        }

        const UClass* OwnerClass = Property->GetOwnerClass();
        if (!OwnerClass || !OwnerClass->IsChildOf(UShineTextureGraphNodeBase::StaticClass()))
        {
            return false;
        }

        if (OwnerClass == UShineTextureGraphNodeBase::StaticClass())
        {
            static const FName AllowedBaseProperties[] =
            {
                FName(TEXT("bInlinePreviewExpanded")),
                FName(TEXT("InlinePreviewSize")),
                FName(TEXT("PreviewDisplayMode")),
            };

            for (const FName& AllowedName : AllowedBaseProperties)
            {
                if (Property->GetFName() == AllowedName)
                {
                    return true;
                }
            }

            return false;
        }

        if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            return false;
        }

        return true;
    }
}

bool UShineTextureGraph::ExportGraphToJson(FString& OutJson) const
{
    TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField(TEXT("format"), TEXT("ShineTextureGraph"));
    RootObject->SetNumberField(TEXT("version"), ShineTextureGraphJsonVersion);

    TArray<TSharedPtr<FJsonValue>> NodeValues;
    TArray<TSharedPtr<FJsonValue>> LinkValues;

    for (const UEdGraphNode* GraphNode : Nodes)
    {
        const UShineTextureGraphNodeBase* Node = Cast<UShineTextureGraphNodeBase>(GraphNode);
        if (!Node)
        {
            continue;
        }

        TSharedRef<FJsonObject> NodeObject = MakeShared<FJsonObject>();
        NodeObject->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
        NodeObject->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
        NodeObject->SetNumberField(TEXT("x"), Node->NodePosX);
        NodeObject->SetNumberField(TEXT("y"), Node->NodePosY);

        TSharedRef<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> PropertyIt(Node->GetClass(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
        {
            const FProperty* Property = *PropertyIt;
            if (!ShouldSerializeNodeProperty(Property))
            {
                continue;
            }

            SerializeSupportedProperty(Property, Property->ContainerPtrToValuePtr<void>(Node), PropertyObject);
        }

        NodeObject->SetObjectField(TEXT("properties"), PropertyObject);
        NodeValues.Add(MakeShared<FJsonValueObject>(NodeObject));

        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->Direction != EGPD_Output)
            {
                continue;
            }

            for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                const UShineTextureGraphNodeBase* LinkedNode = LinkedPin ? Cast<UShineTextureGraphNodeBase>(LinkedPin->GetOwningNode()) : nullptr;
                if (!LinkedNode)
                {
                    continue;
                }

                TSharedRef<FJsonObject> LinkObject = MakeShared<FJsonObject>();
                LinkObject->SetStringField(TEXT("fromNode"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
                LinkObject->SetStringField(TEXT("fromPin"), Pin->PinName.ToString());
                LinkObject->SetStringField(TEXT("toNode"), LinkedNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
                LinkObject->SetStringField(TEXT("toPin"), LinkedPin->PinName.ToString());
                LinkValues.Add(MakeShared<FJsonValueObject>(LinkObject));
            }
        }
    }

    RootObject->SetArrayField(TEXT("nodes"), NodeValues);
    RootObject->SetArrayField(TEXT("links"), LinkValues);

    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    return FJsonSerializer::Serialize(RootObject, Writer);
}

bool UShineTextureGraph::ImportGraphFromJson(const FString& InJson, FString* OutErrorMessage)
{
    auto SetError = [OutErrorMessage](const TCHAR* Message)
    {
        if (OutErrorMessage)
        {
            *OutErrorMessage = Message;
        }
    };

    TSharedPtr<FJsonObject> RootObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        SetError(TEXT("Invalid JSON payload."));
        return false;
    }

    if (RootObject->GetStringField(TEXT("format")) != TEXT("ShineTextureGraph"))
    {
        SetError(TEXT("Unsupported graph format."));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("nodes"), NodeValues) || !NodeValues)
    {
        SetError(TEXT("Missing node array."));
        return false;
    }

    Modify();

    TArray<UShineTextureGraphNodeBase*> ExistingNodes;
    for (UEdGraphNode* GraphNode : Nodes)
    {
        if (UShineTextureGraphNodeBase* Node = Cast<UShineTextureGraphNodeBase>(GraphNode))
        {
            ExistingNodes.Add(Node);
        }
    }

    for (UShineTextureGraphNodeBase* ExistingNode : ExistingNodes)
    {
        ExistingNode->Modify();
        ExistingNode->DestroyNode();
    }

    TMap<FGuid, UShineTextureGraphNodeBase*> NodesByGuid;

    for (const TSharedPtr<FJsonValue>& NodeValue : *NodeValues)
    {
        const TSharedPtr<FJsonObject>* NodeObject = nullptr;
        if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObject) || !NodeObject || !NodeObject->IsValid())
        {
            continue;
        }

        const FString ClassPath = (*NodeObject)->GetStringField(TEXT("class"));
        UClass* NodeClass = LoadObject<UClass>(nullptr, *ClassPath);
        if (!NodeClass || !NodeClass->IsChildOf(UShineTextureGraphNodeBase::StaticClass()))
        {
            SetError(TEXT("Failed to resolve node class while importing graph."));
            return false;
        }

        UShineTextureGraphNodeBase* NewNode = NewObject<UShineTextureGraphNodeBase>(this, NodeClass, NAME_None, RF_Transactional);
        AddNode(NewNode, true, false);
        NewNode->SetFlags(RF_Transactional);
        NewNode->CreateNewGuid();
        NewNode->NodePosX = (*NodeObject)->GetNumberField(TEXT("x"));
        NewNode->NodePosY = (*NodeObject)->GetNumberField(TEXT("y"));
        NewNode->PostPlacedNewNode();
        NewNode->AllocateDefaultPins();

        const TSharedPtr<FJsonObject>* PropertyObject = nullptr;
        if ((*NodeObject)->TryGetObjectField(TEXT("properties"), PropertyObject) && PropertyObject && PropertyObject->IsValid())
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertyObject)->Values)
            {
                if (FProperty* Property = FindFProperty<FProperty>(NodeClass, *Pair.Key))
                {
                    DeserializeSupportedProperty(Property, Property->ContainerPtrToValuePtr<void>(NewNode), Pair.Value);
                }
            }
        }

        FGuid NodeId;
        FGuid::Parse((*NodeObject)->GetStringField(TEXT("nodeId")), NodeId);
        if (!NodeId.IsValid())
        {
            NodeId = NewNode->NodeGuid;
        }
        NewNode->NodeGuid = NodeId;
        NodesByGuid.Add(NodeId, NewNode);
    }

    const TArray<TSharedPtr<FJsonValue>>* LinkValues = nullptr;
    if (RootObject->TryGetArrayField(TEXT("links"), LinkValues) && LinkValues)
    {
        for (const TSharedPtr<FJsonValue>& LinkValue : *LinkValues)
        {
            const TSharedPtr<FJsonObject>* LinkObject = nullptr;
            if (!LinkValue.IsValid() || !LinkValue->TryGetObject(LinkObject) || !LinkObject || !LinkObject->IsValid())
            {
                continue;
            }

            FGuid FromNodeId;
            FGuid ToNodeId;
            FGuid::Parse((*LinkObject)->GetStringField(TEXT("fromNode")), FromNodeId);
            FGuid::Parse((*LinkObject)->GetStringField(TEXT("toNode")), ToNodeId);

            UShineTextureGraphNodeBase* const* FromNodePtr = NodesByGuid.Find(FromNodeId);
            UShineTextureGraphNodeBase* const* ToNodePtr = NodesByGuid.Find(ToNodeId);
            if (!FromNodePtr || !ToNodePtr)
            {
                continue;
            }

            UEdGraphPin* FromPin = (*FromNodePtr)->FindPin((*LinkObject)->GetStringField(TEXT("fromPin")));
            UEdGraphPin* ToPin = (*ToNodePtr)->FindPin((*LinkObject)->GetStringField(TEXT("toPin")));
            if (!FromPin || !ToPin)
            {
                continue;
            }

            FromPin->MakeLinkTo(ToPin);
        }
    }

    NotifyGraphChanged();
    return true;
}
