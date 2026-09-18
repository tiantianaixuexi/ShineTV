#pragma once

#include "CoreMinimal.h"
#include "ShineMCPTypes.h"

/**
 * MCP 工具注册表（进程单例）。
 *
 * 各功能模块在启动时把自己的工具注册进来，服务端只负责协议层。
 */
class SHINEMCP_API FShineMCPToolRegistry
{
public:
    static FShineMCPToolRegistry& Get();

    void RegisterTool(const FShineMCPTool& Tool);
    void UnregisterTool(const FString& ToolName);
    void Clear();

    const TArray<FShineMCPTool>& GetTools() const { return Tools; }
    const FShineMCPTool* FindTool(const FString& ToolName) const;

    /** 生成 tools/list 的返回对象：{"tools":[...]}。 */
    TSharedPtr<FJsonObject> BuildToolsListJson() const;

    /** 调用工具。返回 nullptr 表示工具不存在。 */
    FShineMCPResult CallTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, bool& bOutFound);

private:
    TArray<FShineMCPTool> Tools;
};
