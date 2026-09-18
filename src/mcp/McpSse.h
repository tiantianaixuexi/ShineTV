#pragma once
// P10.3：MCP Streamable HTTP / SSE 传输
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace shine::mcp {

// 把 JSON-RPC 响应包成 SSE 帧（event: message\ndata: …\n\n）
[[nodiscard]] std::string WrapSseEvent(std::string_view eventName, std::string_view dataJson);

// POST /mcp 在 Accept 含 text/event-stream 或 query stream=1 时：单响应 SSE
// 返回完整 SSE body（含 message 帧；可含 ping 前缀）；非 stream 场景由调用方走纯 JSON。
[[nodiscard]] std::string BuildStreamableHttpBody(std::string_view jsonRpcResponse,
                                                  std::string_view sessionId);

// 新会话 id（进程内自增 + 随机后缀）
[[nodiscard]] std::string NewMcpSessionId();

// GET /sse 握手帧：endpoint + session + ping
[[nodiscard]] std::string BuildSseHandshake(std::string_view sessionId,
                                            std::string_view postPath = "/mcp");

// 服务器 → 客户端事件总线（长连接 SSE 订阅）
class SseHub {
public:
    static SseHub& Instance();

    // 连接注册（返回 connection id）；writer 生命周期由 libhv 管理
    void Publish(std::string_view eventName, std::string_view dataJson);
    void SetActiveConnections(int n) noexcept;
    [[nodiscard]] int ActiveConnections() const noexcept;
    [[nodiscard]] std::uint64_t PublishCount() const noexcept;

private:
    int active_ = 0;
    std::uint64_t published_ = 0;
};

// 自检：streamable body 形态 + handshake 含 endpoint/session
[[nodiscard]] bool RunSseShapeSelfCheck();

} // namespace shine::mcp
