#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

/** 插件内部公用的小工具。 */
namespace ShineMCPJson
{
    inline TSharedPtr<FJsonObject> NewObject()
    {
        return MakeShared<FJsonObject>();
    }

    inline FString ToText(const TSharedPtr<FJsonObject>& JsonObject)
    {
        if (!JsonObject.IsValid())
        {
            return TEXT("{}");
        }

        FString Text;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
        return Text;
    }

    inline bool Parse(const FString& Text, TSharedPtr<FJsonObject>& OutObject)
    {
        if (Text.IsEmpty())
        {
            return false;
        }

        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
    }

    inline TSharedPtr<FJsonObject> Success()
    {
        TSharedPtr<FJsonObject> Object = NewObject();
        Object->SetBoolField(TEXT("success"), true);
        return Object;
    }

    inline TSharedPtr<FJsonObject> Error(const FString& Message)
    {
        TSharedPtr<FJsonObject> Object = NewObject();
        Object->SetBoolField(TEXT("success"), false);
        Object->SetStringField(TEXT("error"), Message);
        return Object;
    }

    inline FString GetString(const TSharedPtr<FJsonObject>& Object, const FString& Field, const FString& Default = FString())
    {
        FString Value;
        if (Object.IsValid() && Object->TryGetStringField(Field, Value))
        {
            return Value;
        }
        return Default;
    }

    inline double GetNumber(const TSharedPtr<FJsonObject>& Object, const FString& Field, double Default = 0.0)
    {
        double Value = Default;
        if (Object.IsValid())
        {
            Object->TryGetNumberField(Field, Value);
        }
        return Value;
    }

    inline int32 GetInt(const TSharedPtr<FJsonObject>& Object, const FString& Field, int32 Default = 0)
    {
        return static_cast<int32>(GetNumber(Object, Field, static_cast<double>(Default)));
    }

    inline bool GetBool(const TSharedPtr<FJsonObject>& Object, const FString& Field, bool Default = false)
    {
        bool Value = Default;
        if (Object.IsValid())
        {
            Object->TryGetBoolField(Field, Value);
        }
        return Value;
    }
}
