#include "Comfy/Builders/Nodes/ShineComfyDirectorConfig.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    EShineComfyParameterType ParseParameterType(const FString& TypeStr)
    {
        if (TypeStr.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
        {
            return EShineComfyParameterType::Float;
        }
        if (TypeStr.Equals(TEXT("Integer"), ESearchCase::IgnoreCase))
        {
            return EShineComfyParameterType::Integer;
        }
        if (TypeStr.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
        {
            return EShineComfyParameterType::Boolean;
        }
        return EShineComfyParameterType::Text;
    }

    bool ParseParameter(const TSharedPtr<FJsonObject>& ParamObj, FShineComfyNodeParameter& OutParam)
    {
        if (!ParamObj.IsValid())
        {
            return false;
        }

        FString Name;
        if (!ParamObj->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            return false;
        }

        OutParam.Name = FName(*Name);

        FString Label;
        if (ParamObj->TryGetStringField(TEXT("label"), Label))
        {
            OutParam.Label = FText::FromString(Label);
        }
        else
        {
            OutParam.Label = FText::FromString(Name);
        }

        FString TypeStr;
        ParamObj->TryGetStringField(TEXT("type"), TypeStr);
        OutParam.Type = ParseParameterType(TypeStr);

        switch (OutParam.Type)
        {
        case EShineComfyParameterType::Float:
            {
                double DefaultVal = 0.0;
                ParamObj->TryGetNumberField(TEXT("default"), DefaultVal);
                OutParam.FloatValue = DefaultVal;
            }
            break;
        case EShineComfyParameterType::Integer:
            {
                int32 DefaultVal = 0;
                ParamObj->TryGetNumberField(TEXT("default"), DefaultVal);
                OutParam.IntValue = DefaultVal;
            }
            break;
        case EShineComfyParameterType::Boolean:
            {
                bool bDefaultVal = false;
                ParamObj->TryGetBoolField(TEXT("default"), bDefaultVal);
                OutParam.bBoolValue = bDefaultVal;
            }
            break;
        default:
            {
                FString DefaultVal;
                ParamObj->TryGetStringField(TEXT("default"), DefaultVal);
                OutParam.StringValue = DefaultVal;
            }
            break;
        }

        // optionsSource：下拉框的数据来源。填模型目录名（如 checkpoints / controlnet），
        // 面板刷新模型库时会用 ComfyUI 里的真实模型列表填满这个参数的下拉选项。
        FString OptionsSource;
        if (ParamObj->TryGetStringField(TEXT("optionsSource"), OptionsSource))
        {
            OutParam.OptionsSourceName = OptionsSource;
        }

        // options 数组（写死的下拉选项）
        const TArray<TSharedPtr<FJsonValue>>* OptionsArray = nullptr;
        if (ParamObj->TryGetArrayField(TEXT("options"), OptionsArray) && OptionsArray)
        {
            for (const TSharedPtr<FJsonValue>& OptVal : *OptionsArray)
            {
                FString OptStr;
                if (OptVal.IsValid() && OptVal->TryGetString(OptStr))
                {
                    OutParam.StringOptions.Add(OptStr);
                }
            }
        }

        return true;
    }

    bool ParsePinDef(const TSharedPtr<FJsonObject>& PinObj, FShineComfyDirectorPinDef& OutPin)
    {
        if (!PinObj.IsValid())
        {
            return false;
        }

        if (!PinObj->TryGetStringField(TEXT("name"), OutPin.Name) || OutPin.Name.IsEmpty())
        {
            return false;
        }

        if (!PinObj->TryGetStringField(TEXT("category"), OutPin.Category) || OutPin.Category.IsEmpty())
        {
            OutPin.Category = TEXT("Shine.Any");
        }

        return true;
    }

    bool ParseExpansion(const TSharedPtr<FJsonObject>& ExpansionObj, FShineComfyDirectorExpansion& OutExpansion)
    {
        if (!ExpansionObj.IsValid())
        {
            return true; // expansion 可选
        }

        // nodes 数组
        const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
        if (ExpansionObj->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray)
        {
            for (const TSharedPtr<FJsonValue>& NodeVal : *NodesArray)
            {
                const TSharedPtr<FJsonObject>* NodeObj = nullptr;
                if (!NodeVal.IsValid() || !NodeVal->TryGetObject(NodeObj) || !NodeObj || !NodeObj->IsValid())
                {
                    continue;
                }

                FShineComfyDirectorExpansionNode ExpNode;
                if (!(*NodeObj)->TryGetStringField(TEXT("id"), ExpNode.Id) || ExpNode.Id.IsEmpty())
                {
                    continue;
                }
                if (!(*NodeObj)->TryGetStringField(TEXT("classType"), ExpNode.ClassType) || ExpNode.ClassType.IsEmpty())
                {
                    continue;
                }

                // inputs 可以是任意 JSON 对象，保存引用
                const TSharedPtr<FJsonObject>* InputsObj = nullptr;
                if ((*NodeObj)->TryGetObjectField(TEXT("inputs"), InputsObj) && InputsObj && InputsObj->IsValid())
                {
                    ExpNode.InputsJson = *InputsObj;
                }

                // condition 可选条件
                const TSharedPtr<FJsonObject>* ConditionObj = nullptr;
                if ((*NodeObj)->TryGetObjectField(TEXT("condition"), ConditionObj) && ConditionObj && ConditionObj->IsValid())
                {
                    ExpNode.ConditionJson = *ConditionObj;
                }

                OutExpansion.Nodes.Add(MoveTemp(ExpNode));
            }
        }

        // outputs 映射
        const TSharedPtr<FJsonObject>* OutputsObj = nullptr;
        if (ExpansionObj->TryGetObjectField(TEXT("outputs"), OutputsObj) && OutputsObj && OutputsObj->IsValid())
        {
            for (const auto& Pair : (*OutputsObj)->Values)
            {
                // 值格式: ["nodeId", outputIndex]
                const TArray<TSharedPtr<FJsonValue>>* LinkArray = nullptr;
                if (Pair.Value.IsValid() && Pair.Value->TryGetArray(LinkArray) && LinkArray && LinkArray->Num() >= 2)
                {
                    FShineComfyDirectorExpansionOutput ExpOutput;
                    ExpOutput.PinName = Pair.Key;
                    (*LinkArray)[0]->TryGetString(ExpOutput.TargetNodeId);
                    double Idx = 0.0;
                    (*LinkArray)[1]->TryGetNumber(Idx);
                    ExpOutput.OutputIndex = static_cast<int32>(Idx);
                    OutExpansion.Outputs.Add(MoveTemp(ExpOutput));
                }
            }
        }

        return true;
    }
}

bool FShineComfyDirectorConfig::ParseFromJson(const FString& JsonText, FShineComfyDirectorConfig& OutConfig, FString& OutError)
{
    TSharedPtr<FJsonObject> RootObj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
    {
        OutError = TEXT("导演台 JSON 解析失败");
        return false;
    }

    OutConfig = FShineComfyDirectorConfig();

    // preset / displayName / description
    RootObj->TryGetStringField(TEXT("preset"), OutConfig.Preset);
    RootObj->TryGetStringField(TEXT("displayName"), OutConfig.DisplayName);
    RootObj->TryGetStringField(TEXT("description"), OutConfig.Description);

    // accentColor: [r, g, b] 或 [r, g, b, a]
    const TArray<TSharedPtr<FJsonValue>>* ColorArray = nullptr;
    if (RootObj->TryGetArrayField(TEXT("accentColor"), ColorArray) && ColorArray && ColorArray->Num() >= 3)
    {
        double R = 0.5, G = 0.25, B = 0.75, A = 1.0;
        (*ColorArray)[0]->TryGetNumber(R);
        (*ColorArray)[1]->TryGetNumber(G);
        (*ColorArray)[2]->TryGetNumber(B);
        if (ColorArray->Num() >= 4)
        {
            (*ColorArray)[3]->TryGetNumber(A);
        }
        OutConfig.AccentColor = FLinearColor(R, G, B, A);
    }

    // parameters
    const TArray<TSharedPtr<FJsonValue>>* ParamsArray = nullptr;
    if (RootObj->TryGetArrayField(TEXT("parameters"), ParamsArray) && ParamsArray)
    {
        for (const TSharedPtr<FJsonValue>& ParamVal : *ParamsArray)
        {
            const TSharedPtr<FJsonObject>* ParamObj = nullptr;
            if (ParamVal.IsValid() && ParamVal->TryGetObject(ParamObj) && ParamObj && ParamObj->IsValid())
            {
                FShineComfyNodeParameter Param;
                if (ParseParameter(*ParamObj, Param))
                {
                    OutConfig.Parameters.Add(MoveTemp(Param));
                }
            }
        }
    }

    // inputPins
    const TArray<TSharedPtr<FJsonValue>>* InputPinsArray = nullptr;
    if (RootObj->TryGetArrayField(TEXT("inputPins"), InputPinsArray) && InputPinsArray)
    {
        for (const TSharedPtr<FJsonValue>& PinVal : *InputPinsArray)
        {
            const TSharedPtr<FJsonObject>* PinObj = nullptr;
            if (PinVal.IsValid() && PinVal->TryGetObject(PinObj) && PinObj && PinObj->IsValid())
            {
                FShineComfyDirectorPinDef Pin;
                if (ParsePinDef(*PinObj, Pin))
                {
                    OutConfig.InputPins.Add(MoveTemp(Pin));
                }
            }
        }
    }

    // outputPins
    const TArray<TSharedPtr<FJsonValue>>* OutputPinsArray = nullptr;
    if (RootObj->TryGetArrayField(TEXT("outputPins"), OutputPinsArray) && OutputPinsArray)
    {
        for (const TSharedPtr<FJsonValue>& PinVal : *OutputPinsArray)
        {
            const TSharedPtr<FJsonObject>* PinObj = nullptr;
            if (PinVal.IsValid() && PinVal->TryGetObject(PinObj) && PinObj && PinObj->IsValid())
            {
                FShineComfyDirectorPinDef Pin;
                if (ParsePinDef(*PinObj, Pin))
                {
                    OutConfig.OutputPins.Add(MoveTemp(Pin));
                }
            }
        }
    }

    // expansion
    const TSharedPtr<FJsonObject>* ExpansionObj = nullptr;
    if (RootObj->TryGetObjectField(TEXT("expansion"), ExpansionObj) && ExpansionObj && ExpansionObj->IsValid())
    {
        ParseExpansion(*ExpansionObj, OutConfig.Expansion);
    }

    OutConfig.bIsValid = true;
    return true;
}

bool FShineComfyDirectorConfig::LoadFromFile(const FString& FileName, FShineComfyDirectorConfig& OutConfig, FString& OutError)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Shine"));
    if (!Plugin.IsValid())
    {
        OutError = TEXT("找不到 Shine 插件");
        return false;
    }

    const FString FilePath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), FileName);
    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
    {
        OutError = FString::Printf(TEXT("读取文件失败: %s"), *FilePath);
        return false;
    }

    return ParseFromJson(JsonText, OutConfig, OutError);
}

const FShineComfyDirectorConfig& FShineComfyDirectorConfig::Get()
{
    static FShineComfyDirectorConfig CachedConfig;
    static bool bLoaded = false;

    if (!bLoaded)
    {
        bLoaded = true;
        FString Error;
        if (!LoadFromFile(TEXT("DirectorNode.json"), CachedConfig, Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("Director config load failed: %s"), *Error);
        }
    }

    return CachedConfig;
}
