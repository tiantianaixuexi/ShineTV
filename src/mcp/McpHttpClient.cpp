#include "mcp/McpHttpClient.h"

#include "core/Log.h"
#include "mcp/HttpServer.h"
#include "mcp/MCPServer.h"
#include "mcp/McpBootstrap.h"
#include "mcp/McpSse.h"
#include "net/HttpClient.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <chrono>

namespace shine::mcp {
namespace {

[[nodiscard]] std::string JoinBase(std::string_view base) {
    std::string b{base};
    while (!b.empty() && (b.back() == '/' || b.back() == '\\')) b.pop_back();
    return b;
}

} // namespace

std::expected<std::string, McpHttpError>
McpHttpRpc(std::string_view baseUrl, std::string_view rpcBody, bool stream,
           std::chrono::seconds timeout) {
    const std::string url = JoinBase(baseUrl) + "/mcp";
    net::Request req;
    req.method = "POST";
    req.url = url;
    req.body = std::string{rpcBody};
    req.timeout = timeout;
    req.headers.emplace("Content-Type", "application/json");
    if (stream) {
        req.headers.emplace("Accept", "text/event-stream");
    }
    req.headers.emplace("Mcp-Session-Id", NewMcpSessionId());
    const auto resp = net::Send(req);
    if (!resp.ok && resp.status == 0) {
        return std::unexpected(McpHttpError{.status = 0, .message = resp.error});
    }
    if (resp.status >= 400) {
        return std::unexpected(
            McpHttpError{.status = resp.status, .message = resp.error.empty()
                                                              ? fmt::format("HTTP {}", resp.status)
                                                              : resp.error});
    }
    return resp.body;
}

std::expected<std::string, McpHttpError> McpHttpInitialize(std::string_view baseUrl,
                                                           std::chrono::seconds timeout) {
    return McpHttpRpc(baseUrl,
                      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})",
                      false, timeout);
}

std::expected<std::string, McpHttpError> McpHttpToolsList(std::string_view baseUrl,
                                                          std::chrono::seconds timeout) {
    return McpHttpRpc(baseUrl, R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})", false, timeout);
}

std::expected<std::string, McpHttpError>
McpHttpToolsCall(std::string_view baseUrl, std::string_view toolName, std::string_view argsJson,
                 std::chrono::seconds timeout) {
    const std::string body = fmt::format(
        R"({{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{{"name":"{}","arguments":{}}}}})",
        toolName, argsJson.empty() ? std::string_view{"{}"} : argsJson);
    return McpHttpRpc(baseUrl, body, false, timeout);
}

bool RunMcpHttpClientSelfCheck() {
    std::string report;
    auto okLine = [&](std::string_view s) { report += std::string{s} + "\n"; };

    if (!RunSseShapeSelfCheck()) {
        okLine("sse_shape:fail");
        report += "OVERALL FAIL\n";
        if (const char* p = std::getenv("SHINE_MCP_HTTP_OUT"); p && *p) {
            FILE* f = std::fopen(p, "wb");
            if (f) {
                std::fwrite(report.data(), 1, report.size(), f);
                std::fclose(f);
            }
        }
        return false;
    }
    okLine("sse_shape:ok");

    RegisterAllModules(ToolRegistry::Instance());
    auto& srv = HttpServer::Instance();
    srv.Stop();
    srv.ClearRoutes();
    RegisterMcpRoutes(srv);
    if (auto r = srv.Start("127.0.0.1", 0); !r) {
        log::Error("McpHttpClient 自检：Start 失败 {}", r.error());
        okLine(fmt::format("start:fail {}", r.error()));
        okLine("OVERALL FAIL");
        if (const char* p = std::getenv("SHINE_MCP_HTTP_OUT"); p && *p) {
            FILE* f = std::fopen(p, "wb");
            if (f) {
                std::fwrite(report.data(), 1, report.size(), f);
                std::fclose(f);
            }
        }
        return false;
    }
    const std::string base = srv.BaseUrl();
    log::Info("McpHttpClient 自检：临时监听 {}", base);
    okLine(fmt::format("listen:{}", base));

    bool pass = true;
    auto fail = [&](std::string_view why) {
        log::Error("McpHttpClient 自检 FAIL：{}", why);
        okLine(fmt::format("FAIL {}", why));
        pass = false;
    };

    if (auto init = McpHttpInitialize(base); !init) {
        fail(fmt::format("initialize {}", init.error().message));
    } else if (init->find("jsonrpc") == std::string::npos) {
        fail("initialize 响应形态");
    } else {
        okLine("initialize:ok");
    }

    if (auto list = McpHttpToolsList(base); !list) {
        fail(fmt::format("tools/list {}", list.error().message));
    } else if (list->find("tools") == std::string::npos) {
        fail("tools/list 无 tools");
    } else {
        okLine("tools_list:ok");
    }

    if (auto sse = McpHttpRpc(base, R"({"jsonrpc":"2.0","id":9,"method":"ping"})", true); !sse) {
        fail(fmt::format("streamable ping {}", sse.error().message));
    } else if (sse->find("event:") == std::string::npos && sse->find("pong") == std::string::npos &&
               sse->find("result") == std::string::npos) {
        fail("streamable 既非 SSE 也非 JSON-RPC");
    } else {
        okLine(fmt::format("streamable:ok has_sse_event={}",
                           sse->find("event:") != std::string::npos ? "1" : "0"));
    }

    srv.Stop();
    okLine(pass ? "OVERALL PASS" : "OVERALL FAIL");
    log::Info("McpHttpClient 自检 {}", pass ? "PASS" : "FAIL");
    if (const char* p = std::getenv("SHINE_MCP_HTTP_OUT"); p && *p) {
        FILE* f = std::fopen(p, "wb");
        if (f) {
            std::fwrite(report.data(), 1, report.size(), f);
            std::fclose(f);
        }
    }
    return pass;
}

} // namespace shine::mcp
