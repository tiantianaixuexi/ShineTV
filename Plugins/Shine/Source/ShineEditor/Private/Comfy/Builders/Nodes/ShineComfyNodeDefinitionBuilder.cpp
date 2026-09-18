#include "Comfy/Builders/Nodes/ShineComfyNodeDefinitionBuilder.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    FString NormalizeLookupKey(const FString& Value)
    {
        return Value.ToLower().Replace(TEXT("-"), TEXT("_")).Replace(TEXT(" "), TEXT("_"));
    }

    FString ResolveModelFolderForParameter(const FString& ParameterName)
    {
        const FString Normalized = NormalizeLookupKey(ParameterName);
        if (Normalized == TEXT("ckpt_name") || Normalized == TEXT("checkpoint_name") || Normalized == TEXT("model"))
        {
            return TEXT("checkpoints");
        }

        if (Normalized == TEXT("vae_name"))
        {
            return TEXT("vae");
        }

        if (Normalized == TEXT("control_net_name") || Normalized == TEXT("controlnet_name"))
        {
            return TEXT("controlnet");
        }

        if (Normalized == TEXT("clip_name"))
        {
            return TEXT("clip");
        }

        if (Normalized == TEXT("lora_name"))
        {
            return TEXT("loras");
        }

        return FString();
    }

    FString BuildPinCategory(const FString& TypeName)
    {
        if (TypeName.Equals(TEXT("CONDITIONING"), ESearchCase::IgnoreCase))
        {
            return TEXT("Shine.Conditioning");
        }

        if (TypeName.Equals(TEXT("LATENT"), ESearchCase::IgnoreCase))
        {
            return TEXT("Shine.Latent");
        }

        if (TypeName.Equals(TEXT("IMAGE"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("MASK"), ESearchCase::IgnoreCase))
        {
            return TEXT("Shine.Image");
        }

        FString Sanitized = TypeName;
        for (TCHAR& Character : Sanitized)
        {
            if (!FChar::IsAlnum(Character))
            {
                Character = TCHAR('_');
            }
        }

        return FString::Printf(TEXT("Shine.Comfy.%s"), *Sanitized);
    }

    bool TryParseBoolean(const TSharedPtr<FJsonObject>& ConfigObject, bool& OutValue)
    {
        return ConfigObject.IsValid() && ConfigObject->TryGetBoolField(TEXT("default"), OutValue);
    }

    bool TryParseInteger(const TSharedPtr<FJsonObject>& ConfigObject, int32& OutValue)
    {
        double NumericValue = 0.0;
        if (!ConfigObject.IsValid() || !ConfigObject->TryGetNumberField(TEXT("default"), NumericValue))
        {
            return false;
        }

        OutValue = static_cast<int32>(FMath::RoundToInt(NumericValue));
        return true;
    }

    bool TryParseFloat(const TSharedPtr<FJsonObject>& ConfigObject, double& OutValue)
    {
        return ConfigObject.IsValid() && ConfigObject->TryGetNumberField(TEXT("default"), OutValue);
    }

    FString GetDisplayLabel(const FString& Name)
    {
        return Name.Replace(TEXT("_"), TEXT(" "));
    }

    bool IsPrimitiveParameterType(const FString& TypeName)
    {
        return TypeName.Equals(TEXT("STRING"), ESearchCase::IgnoreCase)
            || TypeName.Equals(TEXT("INT"), ESearchCase::IgnoreCase)
            || TypeName.Equals(TEXT("FLOAT"), ESearchCase::IgnoreCase)
            || TypeName.Equals(TEXT("BOOLEAN"), ESearchCase::IgnoreCase);
    }

    FShineComfyNodeParameterDefinition MakeParameterDefinition(const FString& ParameterName, const FString& TypeName, const TArray<TSharedPtr<FJsonValue>>& RawDefinition)
    {
        FShineComfyNodeParameterDefinition ParameterDefinition;
        ParameterDefinition.Name = FName(*ParameterName);
        ParameterDefinition.Label = FText::FromString(GetDisplayLabel(ParameterName));
        ParameterDefinition.OptionsSourceName = ResolveModelFolderForParameter(ParameterName);
        if (!ParameterDefinition.OptionsSourceName.IsEmpty())
        {
            ParameterDefinition.OptionsSource = EShineComfyParameterOptionsSource::ModelFolder;
        }

        const TSharedPtr<FJsonObject> ConfigObject = RawDefinition.Num() > 1 ? RawDefinition[1]->AsObject() : nullptr;

        if (TypeName.Equals(TEXT("INT"), ESearchCase::IgnoreCase))
        {
            ParameterDefinition.Type = EShineComfyParameterType::Integer;
            TryParseInteger(ConfigObject, ParameterDefinition.IntValue);
            return ParameterDefinition;
        }

        if (TypeName.Equals(TEXT("FLOAT"), ESearchCase::IgnoreCase))
        {
            ParameterDefinition.Type = EShineComfyParameterType::Float;
            TryParseFloat(ConfigObject, ParameterDefinition.FloatValue);
            return ParameterDefinition;
        }

        if (TypeName.Equals(TEXT("BOOLEAN"), ESearchCase::IgnoreCase))
        {
            ParameterDefinition.Type = EShineComfyParameterType::Boolean;
            TryParseBoolean(ConfigObject, ParameterDefinition.bBoolValue);
            return ParameterDefinition;
        }

        ParameterDefinition.Type = EShineComfyParameterType::Text;
        if (ConfigObject.IsValid())
        {
            ConfigObject->TryGetStringField(TEXT("default"), ParameterDefinition.StringValue);
        }

        return ParameterDefinition;
    }

    FShineComfyNodeParameterDefinition MakeChoiceParameterDefinition(const FString& ParameterName, const TArray<TSharedPtr<FJsonValue>>& Choices)
    {
        FShineComfyNodeParameterDefinition ParameterDefinition;
        ParameterDefinition.Name = FName(*ParameterName);
        ParameterDefinition.Label = FText::FromString(GetDisplayLabel(ParameterName));
        ParameterDefinition.Type = EShineComfyParameterType::Text;
        ParameterDefinition.StringOptions.Reserve(Choices.Num());
        ParameterDefinition.OptionsSourceName = ResolveModelFolderForParameter(ParameterName);
        ParameterDefinition.OptionsSource = ParameterDefinition.OptionsSourceName.IsEmpty()
            ? EShineComfyParameterOptionsSource::StaticChoices
            : EShineComfyParameterOptionsSource::ModelFolder;

        for (const TSharedPtr<FJsonValue>& ChoiceValue : Choices)
        {
            FString Choice;
            if (ChoiceValue.IsValid() && ChoiceValue->TryGetString(Choice))
            {
                ParameterDefinition.StringOptions.Add(Choice);
                ParameterDefinition.StringValue = Choice;
            }
        }

        return ParameterDefinition;
    }

    bool TryAppendInputDefinition(const FString& InputName, const TSharedPtr<FJsonValue>& RawValue, bool bIsOptional, FShineComfyNodeDefinition& InOutDefinition)
    {
        const TArray<TSharedPtr<FJsonValue>>* RawArray = nullptr;
        if (!RawValue.IsValid() || !RawValue->TryGetArray(RawArray) || !RawArray || RawArray->Num() == 0)
        {
            return false;
        }

        const TSharedPtr<FJsonValue>& TypeValue = (*RawArray)[0];

        const TArray<TSharedPtr<FJsonValue>>* ChoiceArray = nullptr;
        if (TypeValue.IsValid() && TypeValue->TryGetArray(ChoiceArray) && ChoiceArray)
        {
            InOutDefinition.Parameters.Add(MakeChoiceParameterDefinition(InputName, *ChoiceArray));
            return true;
        }

        FString TypeName;
        if (!TypeValue.IsValid() || !TypeValue->TryGetString(TypeName))
        {
            return false;
        }

        if (IsPrimitiveParameterType(TypeName))
        {
            InOutDefinition.Parameters.Add(MakeParameterDefinition(InputName, TypeName, *RawArray));
            return true;
        }

        FShineComfyNodePinDefinition& PinDefinition = InOutDefinition.InputPins.AddDefaulted_GetRef();
        PinDefinition.Name = InputName;
        PinDefinition.TypeName = TypeName;
        PinDefinition.PinCategory = BuildPinCategory(TypeName);
        PinDefinition.bIsOptional = bIsOptional;
        return true;
    }
}

bool FShineComfyNodeDefinitionBuilder::ParseNodeDefinitions(const TSharedPtr<FJsonObject>& RootObject, TArray<FShineComfyNodeDefinition>& OutNodes, FString& OutErrorMessage)
{
    if (!RootObject.IsValid())
    {
        OutErrorMessage = TEXT("ComfyUI 节点定义解析失败。");
        return false;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : RootObject->Values)
    {
        const TSharedPtr<FJsonObject> NodeObject = Entry.Value.IsValid() ? Entry.Value->AsObject() : nullptr;
        if (!NodeObject.IsValid())
        {
            continue;
        }

        FShineComfyNodeDefinition Definition;
        Definition.NodeClassName = Entry.Key;
        if (!NodeObject->TryGetStringField(TEXT("display_name"), Definition.DisplayName) || Definition.DisplayName.IsEmpty())
        {
            Definition.DisplayName = Entry.Key;
        }

        NodeObject->TryGetStringField(TEXT("category"), Definition.Category);
        NodeObject->TryGetStringField(TEXT("description"), Definition.Description);
        NodeObject->TryGetBoolField(TEXT("api_node"), Definition.bIsApiNode);

        if (NodeObject->HasTypedField<EJson::Object>(TEXT("input")))
        {
            const TSharedPtr<FJsonObject> InputObject = NodeObject->GetObjectField(TEXT("input"));
            const TSharedPtr<FJsonObject> RequiredObject = InputObject.IsValid() && InputObject->HasTypedField<EJson::Object>(TEXT("required"))
                ? InputObject->GetObjectField(TEXT("required"))
                : nullptr;
            const TSharedPtr<FJsonObject> OptionalObject = InputObject.IsValid() && InputObject->HasTypedField<EJson::Object>(TEXT("optional"))
                ? InputObject->GetObjectField(TEXT("optional"))
                : nullptr;
            ParseInputSection(RequiredObject, false, Definition);
            ParseInputSection(OptionalObject, true, Definition);
        }

        const TArray<TSharedPtr<FJsonValue>>* OutputTypes = nullptr;
        if (NodeObject->TryGetArrayField(TEXT("output"), OutputTypes) && OutputTypes)
        {
            const TArray<TSharedPtr<FJsonValue>>* OutputNames = nullptr;
            NodeObject->TryGetArrayField(TEXT("output_name"), OutputNames);

            for (int32 OutputIndex = 0; OutputIndex < OutputTypes->Num(); ++OutputIndex)
            {
                FString OutputType;
                if (!(*OutputTypes)[OutputIndex].IsValid() || !(*OutputTypes)[OutputIndex]->TryGetString(OutputType))
                {
                    continue;
                }

                FString OutputName = FString::Printf(TEXT("Output%d"), OutputIndex + 1);
                if (OutputNames && OutputNames->IsValidIndex(OutputIndex))
                {
                    (*OutputNames)[OutputIndex]->TryGetString(OutputName);
                }

                FShineComfyNodePinDefinition& OutputPin = Definition.OutputPins.AddDefaulted_GetRef();
                OutputPin.Name = OutputName;
                OutputPin.TypeName = OutputType;
                OutputPin.PinCategory = BuildPinCategory(OutputType);
            }
        }

        OutNodes.Add(MoveTemp(Definition));
    }

    OutNodes.Sort([](const FShineComfyNodeDefinition& Left, const FShineComfyNodeDefinition& Right)
    {
        if (Left.Category == Right.Category)
        {
            return Left.DisplayName < Right.DisplayName;
        }

        return Left.Category < Right.Category;
    });
    return true;
}

bool FShineComfyNodeDefinitionBuilder::ParseInputSection(const TSharedPtr<FJsonObject>& InputSection, bool bIsOptional, FShineComfyNodeDefinition& InOutDefinition)
{
    if (!InputSection.IsValid())
    {
        return false;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& InputEntry : InputSection->Values)
    {
        TryAppendInputDefinition(InputEntry.Key, InputEntry.Value, bIsOptional, InOutDefinition);
    }

    return true;
}