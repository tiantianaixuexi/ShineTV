#include "ShineMCPTypes.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FShineMCPResult FShineMCPResult::OkJson(const TSharedPtr<FJsonObject>& JsonObject)
{
    if (!JsonObject.IsValid())
    {
        return Ok(TEXT("{}"));
    }

    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    return Ok(Text);
}

FShineMCPResult FShineMCPResult::FailJson(const TSharedPtr<FJsonObject>& JsonObject)
{
    FShineMCPResult Result = OkJson(JsonObject);
    Result.bSuccess = false;
    return Result;
}

namespace ShineMCPSchema
{
    TSharedPtr<FJsonObject> Object(const TArray<FString>& RequiredProperties)
    {
        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));

        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        Schema->SetObjectField(TEXT("properties"), Properties);

        if (RequiredProperties.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            for (const FString& Name : RequiredProperties)
            {
                RequiredArray.Add(MakeShared<FJsonValueString>(Name));
            }
            Schema->SetArrayField(TEXT("required"), RequiredArray);
        }

        return Schema;
    }

    static TSharedPtr<FJsonObject> GetProperties(const TSharedPtr<FJsonObject>& Schema)
    {
        const TSharedPtr<FJsonObject>* Properties = nullptr;
        if (Schema->TryGetObjectField(TEXT("properties"), Properties) && Properties && Properties->IsValid())
        {
            return *Properties;
        }

        TSharedPtr<FJsonObject> NewProperties = MakeShared<FJsonObject>();
        Schema->SetObjectField(TEXT("properties"), NewProperties);
        return NewProperties;
    }

    void AddString(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired, const FString& Default)
    {
        if (!Schema.IsValid())
        {
            return;
        }

        TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
        Property->SetStringField(TEXT("type"), TEXT("string"));
        Property->SetStringField(TEXT("description"), Description);
        if (!Default.IsEmpty())
        {
            Property->SetStringField(TEXT("default"), Default);
        }
        GetProperties(Schema)->SetObjectField(Name, Property);

        if (bRequired)
        {
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
            if (Schema->TryGetArrayField(TEXT("required"), Existing) && Existing)
            {
                RequiredArray = *Existing;
            }
            RequiredArray.Add(MakeShared<FJsonValueString>(Name));
            Schema->SetArrayField(TEXT("required"), RequiredArray);
        }
    }

    void AddStringArray(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired)
    {
        if (!Schema.IsValid())
        {
            return;
        }

        TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
        Items->SetStringField(TEXT("type"), TEXT("string"));

        TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
        Property->SetStringField(TEXT("type"), TEXT("array"));
        Property->SetStringField(TEXT("description"), Description);
        Property->SetObjectField(TEXT("items"), Items);
        GetProperties(Schema)->SetObjectField(Name, Property);

        if (bRequired)
        {
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
            if (Schema->TryGetArrayField(TEXT("required"), Existing) && Existing)
            {
                RequiredArray = *Existing;
            }
            RequiredArray.Add(MakeShared<FJsonValueString>(Name));
            Schema->SetArrayField(TEXT("required"), RequiredArray);
        }
    }

    void AddNumber(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired, double Default)
    {
        if (!Schema.IsValid())
        {
            return;
        }

        TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
        Property->SetStringField(TEXT("type"), TEXT("number"));
        Property->SetStringField(TEXT("description"), Description);
        Property->SetNumberField(TEXT("default"), Default);
        GetProperties(Schema)->SetObjectField(Name, Property);

        if (bRequired)
        {
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
            if (Schema->TryGetArrayField(TEXT("required"), Existing) && Existing)
            {
                RequiredArray = *Existing;
            }
            RequiredArray.Add(MakeShared<FJsonValueString>(Name));
            Schema->SetArrayField(TEXT("required"), RequiredArray);
        }
    }

    void AddInteger(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired, int32 Default)
    {
        if (!Schema.IsValid())
        {
            return;
        }

        TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
        Property->SetStringField(TEXT("type"), TEXT("integer"));
        Property->SetStringField(TEXT("description"), Description);
        Property->SetNumberField(TEXT("default"), Default);
        GetProperties(Schema)->SetObjectField(Name, Property);

        if (bRequired)
        {
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
            if (Schema->TryGetArrayField(TEXT("required"), Existing) && Existing)
            {
                RequiredArray = *Existing;
            }
            RequiredArray.Add(MakeShared<FJsonValueString>(Name));
            Schema->SetArrayField(TEXT("required"), RequiredArray);
        }
    }

    void AddBoolean(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired, bool Default)
    {
        if (!Schema.IsValid())
        {
            return;
        }

        TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
        Property->SetStringField(TEXT("type"), TEXT("boolean"));
        Property->SetStringField(TEXT("description"), Description);
        Property->SetBoolField(TEXT("default"), Default);
        GetProperties(Schema)->SetObjectField(Name, Property);

        if (bRequired)
        {
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
            if (Schema->TryGetArrayField(TEXT("required"), Existing) && Existing)
            {
                RequiredArray = *Existing;
            }
            RequiredArray.Add(MakeShared<FJsonValueString>(Name));
            Schema->SetArrayField(TEXT("required"), RequiredArray);
        }
    }
}
