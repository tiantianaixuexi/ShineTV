#pragma once
// 小说模块 MCP 注册（P10）：外部 AI / Claude Desktop 可 list/call
// 调度原则：工具全集只在 MCP 注册表；Agent 运行时只拿 tools_json 白名单交集
#include <string>
#include <string_view>

#include "db/sqlite/SqliteDb.h"
#include "mcp/ToolRegistry.h"

namespace shine::novelcore {

// 注册 novel_* 工具到 MCP（幂等，同名覆盖）
void RegisterMcpTools(mcp::ToolRegistry& reg);

// 测试注入；nullptr = NovelDb::Instance()（需已打开工程）
void SetMcpDbOverride(db::sqlite::Database* db) noexcept;

// 写工具总开关（默认关）；SHINE_MCP_ALLOW_WRITE=1 也可打开
void SetMcpAllowWrite(bool allow) noexcept;
[[nodiscard]] bool McpWriteAllowed() noexcept;

// 自检专用：**无条件**拒绝写工具（优先级高于上面三个来源），测完必须复位。
// 存在理由：McpWriteAllowed() 是 `g_mcpAllowWrite || Settings().mcpAllowWrite || EnvWriteOn()`，
// 自检只 SetMcpAllowWrite(false) 关不掉后两个 —— 用户一旦在设置里勾了「允许 MCP 写工具」
// 或设了 SHINE_MCP_ALLOW_WRITE=1，自检的「写工具应默认拒绝」断言就会假失败。
void SetMcpWriteForceDeny(bool deny) noexcept;

// Agent 白名单 ∩ MCP 注册表 → JSON
// {"agent_id":"…","allowed":[…],"mcp_tools":[…],"missing":[…]}
[[nodiscard]] std::string ResolveAgentMcpToolsJson(std::string_view agentId,
                                                   const mcp::ToolRegistry& reg,
                                                   std::string_view toolsJson);

// 离线自检（本地注册表 + 内存库）
[[nodiscard]] bool RunNovelMcpSelfCheck();

} // namespace shine::novelcore
