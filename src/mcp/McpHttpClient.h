#pragma once
// P10：极简 MCP HTTP 客户端（自建 Agent / 自检用）
// 约定：POST {base}/mcp，body 为 JSON-RPC；Accept 可选 text/event-stream
#include <chrono>
#include <expected>
#include <string>
#include <string_view>

namespace shine::mcp {

struct McpHttpError {
    int status = 0;
    std::string message;
};

// base 形如 http://127.0.0.1:8931（自动去尾斜杠）；rpcBody 为 JSON-RPC 文本
// stream=true 时 Accept: text/event-stream，返回完整 SSE body
[[nodiscard]] std::expected<std::string, McpHttpError>
McpHttpRpc(std::string_view baseUrl, std::string_view rpcBody, bool stream = false,
           std::chrono::seconds timeout = std::chrono::seconds{15});

// 便捷：initialize / tools/list / tools/call
[[nodiscard]] std::expected<std::string, McpHttpError>
McpHttpInitialize(std::string_view baseUrl, std::chrono::seconds timeout = std::chrono::seconds{10});
[[nodiscard]] std::expected<std::string, McpHttpError>
McpHttpToolsList(std::string_view baseUrl, std::chrono::seconds timeout = std::chrono::seconds{10});
[[nodiscard]] std::expected<std::string, McpHttpError>
McpHttpToolsCall(std::string_view baseUrl, std::string_view toolName, std::string_view argsJson,
                 std::chrono::seconds timeout = std::chrono::seconds{30});

// 离线/本机自检：起临时 HTTP 服务 → streamable POST + tools/list → 停服
[[nodiscard]] bool RunMcpHttpClientSelfCheck();

} // namespace shine::mcp
