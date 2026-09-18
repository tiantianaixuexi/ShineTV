#include "mcp/MCPServer.h"

#include "core/Log.h"
#include "mcp/BuiltinTools.h"
#include "mcp/McpBootstrap.h"
#include "util/Json.h"
#include "util/Time.h"

#include <fmt/format.h>
#include <yyjson.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>

namespace shine::mcp {
namespace {

std::mutex g_callLogMutex;
LastCallInfo g_lastCall;
std::atomic<std::uint64_t> g_rpcCount{0};

// MCP 协议版本（S2：主版本 + 兼容）
constexpr std::string_view kProtoPrimary = "2025-06-18";
constexpr std::string_view kProtoCompat[] = {"2025-03-26", "2024-11-05"};

[[nodiscard]] std::string JsonRpcResult(yyjson_val* id, yyjson_mut_val* result) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "jsonrpc", "2.0");
    if (id) {
        size_t len = 0;
        char* idText = yyjson_val_write(id, 0, &len);
        if (idText) {
            yyjson_doc* idDoc = yyjson_read(idText, len, 0);
            if (idDoc) {
                yyjson_mut_val* idCopy = yyjson_val_mut_copy(doc, yyjson_doc_get_root(idDoc));
                if (idCopy) yyjson_mut_obj_add_val(doc, root, "id", idCopy);
                yyjson_doc_free(idDoc);
            }
            std::free(idText);
        }
    } else {
        yyjson_mut_obj_add_null(doc, root, "id");
    }
    if (result) {
        yyjson_mut_obj_add_val(doc, root, "result", result);
    } else {
        yyjson_mut_obj_add_val(doc, root, "result", yyjson_mut_obj(doc));
    }
    size_t outLen = 0;
    char* s = yyjson_mut_val_write(root, 0, &outLen);
    std::string out = s ? std::string{s, outLen} : R"({"jsonrpc":"2.0","id":null,"result":{}})";
    if (s) std::free(s);
    yyjson_mut_doc_free(doc);
    return out;
}

[[nodiscard]] std::string JsonRpcError(yyjson_val* id, int code, std::string_view message) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "jsonrpc", "2.0");
    if (id && yyjson_is_num(id)) {
        if (yyjson_is_int(id)) {
            yyjson_mut_obj_add_int(doc, root, "id", yyjson_get_sint(id));
        } else {
            yyjson_mut_obj_add_real(doc, root, "id", yyjson_get_num(id));
        }
    } else if (id && yyjson_is_str(id)) {
        yyjson_mut_obj_add_strcpy(doc, root, "id", yyjson_get_str(id));
    } else {
        yyjson_mut_obj_add_null(doc, root, "id");
    }
    yyjson_mut_val* err = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, err, "code", code);
    yyjson_mut_obj_add_strncpy(doc, err, "message", message.data(), message.size());
    yyjson_mut_obj_add_val(doc, root, "error", err);
    size_t outLen = 0;
    char* s = yyjson_mut_val_write(root, 0, &outLen);
    std::string out = s ? std::string{s, outLen} : R"({"jsonrpc":"2.0","id":null,"error":{"code":-32000,"message":"serialize fail"}})";
    if (s) std::free(s);
    yyjson_mut_doc_free(doc);
    return out;
}

[[nodiscard]] yyjson_mut_val* ParseJsonObjectToMut(yyjson_mut_doc* doc, std::string_view text) {
    yyjson_doc* d = yyjson_read(text.data(), text.size(), 0);
    if (!d) return nullptr;
    yyjson_val* root = yyjson_doc_get_root(d);
    yyjson_mut_val* copy = root ? yyjson_val_mut_copy(doc, root) : nullptr;
    yyjson_doc_free(d);
    return copy;
}

[[nodiscard]] std::string WriteVal(yyjson_mut_val* v) {
    if (!v) return "null";
    size_t len = 0;
    char* s = yyjson_mut_val_write(v, 0, &len);
    if (!s) return "null";
    std::string out{s, len};
    std::free(s);
    return out;
}

void NoteCall(std::string_view tool, bool ok) {
    std::lock_guard lock(g_callLogMutex);
    g_lastCall.tool = std::string{tool};
    g_lastCall.ok = ok;
    g_lastCall.timeText = fmt::format("{}ms", util::NowMillis());
}

} // namespace

std::string WrapToolResultAsMcp(const CallOutcome& outcome) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "isError", !outcome.ok());
    yyjson_mut_val* content = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "content", content);
    yyjson_mut_val* item = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, item, "type", "text");
    yyjson_mut_obj_add_strncpy(doc, item, "text", outcome.text.data(), outcome.text.size());
    yyjson_mut_arr_add_val(content, item);
    const std::string out = WriteVal(root);
    yyjson_mut_doc_free(doc);
    return out;
}

LastCallInfo GetLastCallInfo() {
    std::lock_guard lock(g_callLogMutex);
    return g_lastCall;
}

std::uint64_t McpRequestCount() { return g_rpcCount.load(); }

std::string HandleJsonRpc(std::string_view body) {
    g_rpcCount.fetch_add(1, std::memory_order_relaxed);
    yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) {
        return JsonRpcError(nullptr, -32700, "非法 JSON");
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return JsonRpcError(nullptr, -32600, "JSON-RPC 请求必须是对象");
    }
    yyjson_val* id = yyjson_obj_get(root, "id");
    yyjson_val* ver = yyjson_obj_get(root, "jsonrpc");
    if (!ver || !yyjson_is_str(ver) || yyjson_get_str(ver) != std::string_view{"2.0"}) {
        std::string r = JsonRpcError(id, -32600, "jsonrpc 必须为 \"2.0\"");
        yyjson_doc_free(doc);
        return r;
    }
    yyjson_val* methodVal = yyjson_obj_get(root, "method");
    if (!methodVal || !yyjson_is_str(methodVal)) {
        std::string r = JsonRpcError(id, -32600, "缺少 method");
        yyjson_doc_free(doc);
        return r;
    }
    const std::string_view method{yyjson_get_str(methodVal)};
    yyjson_val* params = yyjson_obj_get(root, "params");
    const bool isNotification = (id == nullptr);

    auto finish = [&](std::string response) {
        yyjson_doc_free(doc);
        return response;
    };

    if (method.starts_with("notifications/")) {
        // S3：通知无 id → 空响应
        return isNotification ? finish(std::string{}) : finish(JsonRpcResult(id, nullptr));
    }

    if (method == "initialize") {
        yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* result = yyjson_mut_obj(rdoc);
        yyjson_mut_obj_add_strncpy(rdoc, result, "protocolVersion", kProtoPrimary.data(),
                                   kProtoPrimary.size());
        yyjson_mut_val* caps = yyjson_mut_obj(rdoc);
        yyjson_mut_obj_add_val(rdoc, result, "capabilities", caps);
        yyjson_mut_val* toolsCap = yyjson_mut_obj(rdoc);
        yyjson_mut_obj_add_val(rdoc, caps, "tools", toolsCap);
        yyjson_mut_obj_add_bool(rdoc, toolsCap, "listChanged", false);
        yyjson_mut_val* info = yyjson_mut_obj(rdoc);
        yyjson_mut_obj_add_val(rdoc, result, "serverInfo", info);
        yyjson_mut_obj_add_strncpy(rdoc, info, "name", kServerName.data(), kServerName.size());
        yyjson_mut_obj_add_strncpy(rdoc, info, "version", kServerVersion.data(),
                                   kServerVersion.size());
        // 兼容版本表
        yyjson_mut_val* alt = yyjson_mut_arr(rdoc);
        yyjson_mut_obj_add_val(rdoc, result, "compatibleProtocolVersions", alt);
        for (auto v : kProtoCompat) {
            yyjson_mut_arr_add_strncpy(rdoc, alt, v.data(), v.size());
        }
        std::string r = JsonRpcResult(id, result);
        yyjson_mut_doc_free(rdoc);
        return finish(r);
    }

    if (method == "ping") {
        yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* result = yyjson_mut_obj(rdoc);
        std::string r = JsonRpcResult(id, result);
        yyjson_mut_doc_free(rdoc);
        return finish(r);
    }

    if (method == "tools/list") {
        RegisterAllBuiltinTools(ToolRegistry::Instance());
        const std::string listJson = ToolRegistry::Instance().BuildToolsListJson();
        yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* result = ParseJsonObjectToMut(rdoc, listJson);
        if (!result) result = yyjson_mut_obj(rdoc);
        std::string r = JsonRpcResult(id, result);
        yyjson_mut_doc_free(rdoc);
        return finish(r);
    }

    if (method == "resources/list" || method == "prompts/list") {
        yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* result = yyjson_mut_obj(rdoc);
        const char* key = (method == "resources/list") ? "resources" : "prompts";
        yyjson_mut_val* arr = yyjson_mut_arr(rdoc);
        yyjson_mut_obj_add_val(rdoc, result, key, arr);
        std::string r = JsonRpcResult(id, result);
        yyjson_mut_doc_free(rdoc);
        return finish(r);
    }

    if (method == "tools/call") {
        RegisterAllBuiltinTools(ToolRegistry::Instance());
        std::string toolName;
        yyjson_val* args = nullptr;
        if (params && yyjson_is_obj(params)) {
            yyjson_val* n = yyjson_obj_get(params, "name");
            if (n && yyjson_is_str(n)) toolName = yyjson_get_str(n);
            args = yyjson_obj_get(params, "arguments");
        }
        if (toolName.empty()) {
            return finish(JsonRpcError(id, -32602, "tools/call 缺少 params.name"));
        }
        if (args != nullptr && !yyjson_is_obj(args)) {
            NoteCall(toolName, false);
            return finish(JsonRpcError(id, -32602, "params.arguments 必须是对象"));
        }
        const CallOutcome outcome = ToolRegistry::Instance().Call(toolName, args);
        NoteCall(toolName, outcome.ok());
        yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* result = ParseJsonObjectToMut(rdoc, WrapToolResultAsMcp(outcome));
        if (!result) result = yyjson_mut_obj(rdoc);
        std::string r = JsonRpcResult(id, result);
        yyjson_mut_doc_free(rdoc);
        return finish(r);
    }

    return finish(JsonRpcError(id, -32601, fmt::format("未知方法: {}", method)));
}

void RegisterMcpRoutes(HttpServer& srv) {
    RegisterHealthRoute(srv);
    RegisterAllBuiltinTools(ToolRegistry::Instance());

    auto handleRpcBody = [](const Request& req, Response& out) {
        out.status = 200;
        out.contentType = "application/json; charset=utf-8";
        const std::string resp = HandleJsonRpc(req.body);
        out.body = resp.empty() ? std::string{} : resp;
        if (resp.empty()) {
            out.status = 204;
            out.body.clear();
        }
    };

    srv.Route("/mcp", HttpMethod::Post, handleRpcBody);
    srv.Route("/messages", HttpMethod::Post, handleRpcBody);
    srv.Route("/message", HttpMethod::Post, handleRpcBody);

    srv.Route(
        "/tools", HttpMethod::Get, [](const Request&, Response& out) {
            RegisterAllBuiltinTools(ToolRegistry::Instance());
            out.body = ToolRegistry::Instance().BuildToolsListJson();
        });

    srv.Route(
        "/tools/", HttpMethod::Post, [](const Request& req, Response& out) {
            // /tools/<name>
            std::string name = req.path;
            const auto pos = name.find_last_of('/');
            if (pos != std::string::npos) name = name.substr(pos + 1);
            yyjson_doc* adoc = nullptr;
            yyjson_val* args = nullptr;
            if (!req.body.empty()) {
                adoc = yyjson_read(req.body.data(), req.body.size(), 0);
                if (adoc) args = yyjson_doc_get_root(adoc);
            }
            RegisterAllBuiltinTools(ToolRegistry::Instance());
            const CallOutcome o = ToolRegistry::Instance().Call(name, args);
            NoteCall(name, o.ok());
            out.body = WrapToolResultAsMcp(o);
            out.status = 200;
            if (adoc) yyjson_doc_free(adoc);
        });

    // S4 骨架：先回 endpoint（完整长连接心跳后续加强）
    auto sse = [](const Request&, Response& out) {
        out.status = 200;
        out.contentType = "text/event-stream; charset=utf-8";
        out.body = "event: endpoint\ndata: /mcp\n\n";
    };
    srv.Route("/sse", HttpMethod::Get, sse);
    srv.Route("/mcp/sse", HttpMethod::Get, sse);
}

bool RunMcpProtocolSelfCheck() {
    bool pass = true;
    std::string notes;
    auto fail = [&](std::string_view why) {
        log::Error("mcp 协议自检 FAIL：{}", why);
        notes += fmt::format("FAIL {}\n", why);
        pass = false;
    };
    auto ok = [&](std::string_view what) { notes += fmt::format("PASS {}\n", what); };

    RegisterAllBuiltinTools(ToolRegistry::Instance());

    // initialize
    {
        const std::string resp = HandleJsonRpc(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})");
        if (resp.find("\"jsonrpc\"") == std::string::npos ||
            resp.find(kServerName) == std::string::npos) {
            fail("initialize 响应");
        } else {
            ok("initialize");
        }
    }
    // ping
    {
        const std::string resp = HandleJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
        if (resp.find("\"result\"") == std::string::npos) fail("ping");
        else ok("ping");
    }
    // tools/list
    {
        const std::string resp = HandleJsonRpc(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
        if (resp.find("tools") == std::string::npos) fail("tools/list");
        else ok("tools/list");
    }
    // tools/call demo
    {
        const std::string resp = HandleJsonRpc(
            R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"mcp_ping","arguments":{}}})");
        if (resp.find("isError") == std::string::npos || resp.find("content") == std::string::npos)
            fail("tools/call 形态");
        else if (resp.find("pong") == std::string::npos) fail("tools/call pong");
        else ok("tools/call mcp_ping");
    }
    // unknown method
    {
        const std::string resp = HandleJsonRpc(R"({"jsonrpc":"2.0","id":5,"method":"no/such"})");
        if (resp.find("-32601") == std::string::npos) fail("未知方法错误码");
        else ok("未知方法 → -32601");
    }
    // bad jsonrpc version
    {
        const std::string resp = HandleJsonRpc(R"({"jsonrpc":"1.0","id":6,"method":"ping"})");
        if (resp.find("-32600") == std::string::npos) fail("非法版本错误码");
        else ok("非法 jsonrpc → -32600");
    }
    // notification → empty
    {
        const std::string resp =
            HandleJsonRpc(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        if (!resp.empty()) fail("通知应空响应");
        else ok("notifications → 空响应");
    }
    // resources/list empty
    {
        const std::string resp = HandleJsonRpc(R"({"jsonrpc":"2.0","id":7,"method":"resources/list"})");
        if (resp.find("resources") == std::string::npos) fail("resources/list");
        else ok("resources/list 空数组");
    }

    notes += pass ? "OVERALL PASS\n" : "OVERALL FAIL\n";
    log::Info("mcp 协议自检 {}", pass ? "PASS" : "FAIL");
    if (const char* outp = std::getenv("SHINE_MCP_PROTO_CHECK_OUT"); outp && *outp) {
        // 简单写文件：复用报告路径
        FILE* f = std::fopen(outp, "wb");
        if (f) {
            std::fwrite(notes.data(), 1, notes.size(), f);
            std::fclose(f);
        }
    }
    return pass;
}

} // namespace shine::mcp
