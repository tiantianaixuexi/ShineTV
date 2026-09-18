#pragma once
// OpenAI 专用 HTTP（libhv）：带 Authorization；支持同步 JSON POST 与 SSE 增量读。
// **仅 worker 线程**；UI 禁止调用（RULES-AI §11.6）。
#include <chrono>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::openai {

struct HttpTransportResult {
    int status = 0;      // HTTP 状态；0 = 传输失败
    std::string body;    // 同步响应体；SSE 模式下为空
    std::string error;   // 中文传输错误（空 = 到达应用层）
};

// SSE 分块：on_chunk 返回 false 表示调用方要求中断
using SseChunkFn = std::function<bool(std::string_view)>;

// POST JSON，Authorization: Bearer <key>。key 空则返回 no_key 错误（不发请求）。
[[nodiscard]] HttpTransportResult PostJson(std::string_view url, std::string_view bearer,
                                           std::string_view jsonBody, std::chrono::seconds timeout);

// 自定义头 POST（Anthropic 需要 x-api-key + anthropic-version，不是 Bearer）
[[nodiscard]] HttpTransportResult
PostJsonHeaders(std::string_view url, const std::vector<std::pair<std::string, std::string>>& headers,
                std::string_view jsonBody, std::chrono::seconds timeout);

// POST JSON + Accept: text/event-stream；on_chunk 收到原始字节流（可能跨事件边界）。
[[nodiscard]] HttpTransportResult PostSse(std::string_view url, std::string_view bearer,
                                          std::string_view jsonBody, std::chrono::seconds timeout,
                                          const SseChunkFn& on_chunk);

} // namespace shine::openai
