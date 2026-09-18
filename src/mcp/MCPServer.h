#pragma once
// shine::mcp —— MCP JSON-RPC 协议层（P7.3）
#include "mcp/HttpServer.h"
#include "mcp/ToolRegistry.h"

#include <string>
#include <string_view>

namespace shine::mcp {

// 在 HttpServer 上注册 MCP 路由：
//   GET  /health
//   POST /mcp  (别名 /messages、/message)
//   GET  /sse   (别名 /mcp/sse) —— P10.3 长连接 SSE：endpoint/session/ping
//   POST /mcp   —— JSON-RPC；Accept: text/event-stream 时返回 streamable SSE body
//   GET  /tools
//   POST /tools/<name>
void RegisterMcpRoutes(HttpServer& srv);

// 处理一条 JSON-RPC 请求文本，返回响应 JSON；通知（无 id）返回空串。
[[nodiscard]] std::string HandleJsonRpc(std::string_view body);

// tools/call 的 MCP 内容形态封装
[[nodiscard]] std::string WrapToolResultAsMcp(const CallOutcome& outcome);

// 最近一次 tools/call 诊断（设置页显示）
struct LastCallInfo {
    std::string tool;
    std::string timeText;
    bool ok = false;
};
[[nodiscard]] LastCallInfo GetLastCallInfo();
[[nodiscard]] std::uint64_t McpRequestCount();

// P7.3 自检：initialize / tools/list / tools/call / 错误码
[[nodiscard]] bool RunMcpProtocolSelfCheck();

// P10.1：stdio MCP Server（stdin→JSON-RPC→stdout）。仅 CLI：`--mcp-stdio`
// 日志走 stderr/文件，不写 stdout。挂库：SHINE_NOVEL_DB 或 Settings.mcpNovelDbPath。
int RunStdioServerMain();

} // namespace shine::mcp
