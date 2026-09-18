#include "ShineMCPToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FShineMCPToolRegistry& FShineMCPToolRegistry::Get()
{
    static FShineMCPToolRegistry Instance;
    return Instance;
}

void FShineMCPToolRegistry::RegisterTool(const FShineMCPTool& Tool)
{
    if (Tool.Name.IsEmpty())
    {
        return;
    }

    // 同名工具覆盖旧的，方便热重载时幂等注册。
    for (int32 Index = Tools.Num() - 1; Index >= 0; --Index)
    {
        if (Tools[Index].Name == Tool.Name)
        {
            Tools.RemoveAt(Index);
        }
    }

    Tools.Add(Tool);
}

void FShineMCPToolRegistry::UnregisterTool(const FString& ToolName)
{
    Tools.RemoveAll([&ToolName](const FShineMCPTool& Tool) { return Tool.Name == ToolName; });
}

void FShineMCPToolRegistry::Clear()
{
    Tools.Reset();
}

const FShineMCPTool* FShineMCPToolRegistry::FindTool(const FString& ToolName) const
{
    return Tools.FindByPredicate([&ToolName](const FShineMCPTool& Tool) { return Tool.Name == ToolName; });
}

TSharedPtr<FJsonObject> FShineMCPToolRegistry::BuildToolsListJson() const
{
    TArray<TSharedPtr<FJsonValue>> ToolArray;
    for (const FShineMCPTool& Tool : Tools)
    {
        TSharedPtr<FJsonObject> ToolObject = MakeShared<FJsonObject>();
        ToolObject->SetStringField(TEXT("name"), Tool.Name);
        ToolObject->SetStringField(TEXT("title"), Tool.Title.IsEmpty() ? Tool.Name : Tool.Title);
        ToolObject->SetStringField(TEXT("description"), Tool.Description);
        ToolObject->SetObjectField(TEXT("inputSchema"), Tool.InputSchema.IsValid() ? Tool.InputSchema : ShineMCPSchema::Object());
        ToolArray.Add(MakeShared<FJsonValueObject>(ToolObject));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("tools"), ToolArray);
    return Root;
}

FShineMCPResult FShineMCPToolRegistry::CallTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, bool& bOutFound)
{
    const FShineMCPTool* Tool = FindTool(ToolName);
    if (!Tool)
    {
        bOutFound = false;
        return FShineMCPResult::Fail(FString::Printf(TEXT("未知工具: %s"), *ToolName));
    }

    bOutFound = true;

    if (!Tool->Handler)
    {
        return FShineMCPResult::Fail(FString::Printf(TEXT("工具 %s 未实现处理函数。"), *ToolName));
    }

    TSharedPtr<FJsonObject> SafeArguments = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
    return Tool->Handler(SafeArguments);
}
