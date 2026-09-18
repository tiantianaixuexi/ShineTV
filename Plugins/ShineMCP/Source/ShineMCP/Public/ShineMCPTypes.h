#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

/**
 * MCP 工具的统一返回体。统一用 JSON 文本返回，便于 MCP、Python、蓝图三处复用。
 */
struct SHINEMCP_API FShineMCPResult
{
    bool bSuccess = true;
    /** 给模型看的纯文本 / JSON 文本。 */
    FString Text;

    static FShineMCPResult Ok(const FString& InText)
    {
        FShineMCPResult Result;
        Result.bSuccess = true;
        Result.Text = InText;
        return Result;
    }

    static FShineMCPResult Fail(const FString& InError)
    {
        FShineMCPResult Result;
        Result.bSuccess = false;
        Result.Text = InError;
        return Result;
    }

    static FShineMCPResult OkJson(const TSharedPtr<FJsonObject>& JsonObject);
    static FShineMCPResult FailJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/**
 * 一个 MCP 工具定义。
 */
struct SHINEMCP_API FShineMCPTool
{
    FString Name;
    FString Title;
    FString Description;
    /** JSON Schema（object），描述该工具的入参。 */
    TSharedPtr<FJsonObject> InputSchema;
    /** 处理函数，返回值文本 + 是否出错。 */
    TFunction<FShineMCPResult(const TSharedPtr<FJsonObject>& Arguments)> Handler;
};

/** 便捷构造 JSON Schema 的小工具。 */
namespace ShineMCPSchema
{
    TSharedPtr<FJsonObject> Object(const TArray<FString>& RequiredProperties = TArray<FString>());
    void AddString(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false, const FString& Default = FString());
    void AddStringArray(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false);
    void AddNumber(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false, double Default = 0.0);
    void AddInteger(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false, int32 Default = 0);
    void AddBoolean(const TSharedPtr<FJsonObject>& Schema, const FString& Name, const FString& Description, bool bRequired = false, bool Default = false);
}
