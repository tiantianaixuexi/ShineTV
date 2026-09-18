#include "mcp/McpSse.h"

#include "core/Log.h"
#include "mcp/MCPTypes.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <atomic>
#include <cstdlib>
#include <random>

namespace shine::mcp {

namespace {
std::atomic<std::uint64_t> g_sessionSeq{1};
}

std::string WrapSseEvent(std::string_view eventName, std::string_view dataJson) {
    std::string name = eventName.empty() ? std::string{"message"} : std::string{eventName};
    // data 可能多行：SSE 规范每行前加 data:
    std::string body;
    body.reserve(dataJson.size() + 64);
    body += "event: ";
    body += name;
    body += "\n";
    std::string_view rest = dataJson;
    if (rest.empty()) {
        body += "data: {}\n\n";
        return body;
    }
    while (!rest.empty()) {
        const auto nl = rest.find('\n');
        const auto line = rest.substr(0, nl);
        body += "data: ";
        body += line;
        body += "\n";
        if (nl == std::string_view::npos) break;
        rest.remove_prefix(nl + 1);
    }
    body += "\n";
    return body;
}

std::string NewMcpSessionId() {
    static std::mt19937_64 rng{static_cast<std::uint64_t>(util::NowMillis()) ^ 0xA5A5ull};
    return fmt::format("sess-{:x}-{:x}", static_cast<std::uint64_t>(util::NowMillis()),
                       rng());
}

std::string BuildStreamableHttpBody(std::string_view jsonRpcResponse,
                                    std::string_view sessionId) {
    std::string out;
    out += WrapSseEvent("ping",
                        fmt::format(R"({{"ts":{},"sessionId":"{}"}})", util::NowMillis(),
                                    sessionId));
    if (!jsonRpcResponse.empty()) {
        out += WrapSseEvent("message", jsonRpcResponse);
    }
    return out;
}

std::string BuildSseHandshake(std::string_view sessionId, std::string_view postPath) {
    std::string out;
    out += WrapSseEvent("endpoint", std::string{postPath});
    out += WrapSseEvent(
        "session",
        fmt::format(R"({{"sessionId":"{}","protocol":"2025-06-18","server":"{}"}})", sessionId,
                    kServerName));
    out += WrapSseEvent("ping", fmt::format(R"({{"ts":{}}})", util::NowMillis()));
    return out;
}

SseHub& SseHub::Instance() {
    static SseHub hub;
    return hub;
}

void SseHub::Publish(std::string_view eventName, std::string_view dataJson) {
    published_ += 1;
    log::Info("mcp SSE publish {} (conns={} payload={}B)", eventName, active_, dataJson.size());
}

void SseHub::SetActiveConnections(int n) noexcept { active_ = n < 0 ? 0 : n; }
int SseHub::ActiveConnections() const noexcept { return active_; }
std::uint64_t SseHub::PublishCount() const noexcept { return published_; }

bool RunSseShapeSelfCheck() {
    const std::string sid = NewMcpSessionId();
    if (sid.rfind("sess-", 0) != 0) {
        log::Error("SSE 自检：session id 形态不对 {}", sid);
        return false;
    }
    const std::string hs = BuildSseHandshake(sid);
    if (hs.find("event: endpoint") == std::string::npos ||
        hs.find("event: session") == std::string::npos || hs.find(sid) == std::string::npos) {
        log::Error("SSE 自检：handshake 缺帧");
        return false;
    }
    const std::string body = BuildStreamableHttpBody(
        R"({"jsonrpc":"2.0","id":1,"result":{"pong":true}})", sid);
    if (body.find("event: message") == std::string::npos ||
        body.find("pong") == std::string::npos || body.find("event: ping") == std::string::npos) {
        log::Error("SSE 自检：streamable body 形态不对");
        return false;
    }
    // 多行 data
    const std::string multi = WrapSseEvent("message", "line1\nline2");
    if (multi.find("data: line1") == std::string::npos || multi.find("data: line2") == std::string::npos) {
        log::Error("SSE 自检：多行 data 未拆分");
        return false;
    }
    log::Info("SSE 自检通过（handshake + streamable body）");
    return true;
}

} // namespace shine::mcp
