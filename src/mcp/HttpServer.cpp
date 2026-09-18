#include "mcp/HttpServer.h"

#include "comfy/ComfyHttp.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "mcp/MCPServer.h"
#include "mcp/McpBootstrap.h"
#include "mcp/ToolRegistry.h"
#include "net/LibhvReady.h"
#include "util/Encoding.h"

#include <hv/HttpMessage.h>
#include <hv/HttpServer.h>
#include <hv/HttpService.h>
#include <hv/httpdef.h>
#include <fmt/format.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <windows.h>

#include "util/File.h"

namespace shine::mcp {
namespace {

[[nodiscard]] bool PathPrefixMatch(std::string_view pattern, std::string_view path) {
    // pattern 已去掉尾部 '*'；`/tools/` 匹配 `/tools` 与 `/tools/xxx`
    if (pattern.empty()) return false;
    if (path == pattern) return true;
    if (pattern.back() == '/') {
        if (path.starts_with(pattern)) return true;
        // `/tools/` 也匹配精确 `/tools`
        const std::string_view without = pattern.substr(0, pattern.size() - 1);
        return path == without;
    }
    if (path.starts_with(pattern)) {
        return path.size() == pattern.size() || path[pattern.size()] == '/';
    }
    return false;
}

void FillCors(Response& r) {
    // 本机 MCP：宽松 CORS，便于浏览器扩展调试（P7.5 可再收紧）
    (void)r;
}

[[nodiscard]] std::string Error404Json(std::string_view path) {
    return fmt::format(R"({{"error":"not_found","path":"{}"}})", path);
}

} // namespace

struct HttpServer::Impl {
    hv::HttpService service;
    std::unique_ptr<hv::HttpServer> server;
    // stream path → handler（Start 时拷贝给 libhv async 路由）
    std::vector<RouteEntry> streamRoutes;
};

const char* HttpMethodStr(HttpMethod m) noexcept {
    switch (m) {
    case HttpMethod::Get: return "GET";
    case HttpMethod::Post: return "POST";
    case HttpMethod::Put: return "PUT";
    case HttpMethod::Delete: return "DELETE";
    case HttpMethod::Patch: return "PATCH";
    case HttpMethod::Options: return "OPTIONS";
    case HttpMethod::Any: return "ANY";
    }
    return "ANY";
}

HttpServer& HttpServer::Instance() {
    static HttpServer inst;
    return inst;
}

void HttpServer::SetUiDispatcher(UiDispatcher post) {
    std::lock_guard lock(routesMutex_);
    uiDispatcher_ = std::move(post);
}

void HttpServer::Route(std::string path, HttpMethod method, Handler handler) {
    if (path.empty() || path[0] != '/') {
        log::Warn("mcp HttpServer::Route：路径必须以 / 开头，忽略 {}", path);
        return;
    }
    if (!handler) {
        log::Warn("mcp HttpServer::Route：{} 缺少 handler", path);
        return;
    }
    RouteEntry e;
    e.path = std::move(path);
    e.method = method;
    e.handler = std::move(handler);
    if (e.path == "*" || e.path == "/*") {
        e.prefix = true;
        e.path = "/";
    } else if (e.path.back() == '*' || e.path.back() == '/') {
        e.prefix = true;
        if (e.path.back() == '*') e.path.pop_back();
        if (e.path.empty() || e.path.back() != '/') {
            if (!e.path.empty() && e.path.back() != '/') e.path.push_back('/');
        }
    }

    std::lock_guard lock(routesMutex_);
    for (auto& r : routes_) {
        if (r.path == e.path && r.method == e.method) {
            r.handler = std::move(e.handler);
            r.prefix = e.prefix;
            r.stream = false;
            r.streamHandler = nullptr;
            return;
        }
    }
    routes_.push_back(std::move(e));
}

void HttpServer::RouteStream(std::string path, HttpMethod method, StreamHandler handler) {
    if (path.empty() || path[0] != '/' || !handler) {
        log::Warn("mcp RouteStream 参数无效");
        return;
    }
    RouteEntry e;
    e.path = std::move(path);
    e.method = method;
    e.streamHandler = std::move(handler);
    e.stream = true;
    std::lock_guard lock(routesMutex_);
    for (auto& r : routes_) {
        if (r.path == e.path && r.method == e.method) {
            r.streamHandler = std::move(e.streamHandler);
            r.stream = true;
            r.handler = nullptr;
            return;
        }
    }
    routes_.push_back(std::move(e));
}

void HttpServer::ClearRoutes() {
    std::lock_guard lock(routesMutex_);
    routes_.clear();
}

const Handler* HttpServer::FindHandler(std::string_view path, std::string_view method) const {
    const Handler* prefixHit = nullptr;
    std::size_t prefixLen = 0;
    for (const auto& r : routes_) {
        if (r.method != HttpMethod::Any && method != HttpMethodStr(r.method)) continue;
        if (!r.prefix) {
            if (r.path == path) return &r.handler;
            continue;
        }
        if (PathPrefixMatch(r.path, path)) {
            if (r.path.size() >= prefixLen) {
                prefixLen = r.path.size();
                prefixHit = &r.handler;
            }
        }
    }
    return prefixHit;
}

Response HttpServer::HandleOnCurrentThread(Request req) {
    Response resp;
    FillCors(resp);

    // CORS 预检：未注册 OPTIONS 时统一 204
    if (req.method == "OPTIONS") {
        const Handler* h = FindHandler(req.path, "OPTIONS");
        if (!h) {
            resp.status = 204;
            resp.contentType.clear();
            resp.body.clear();
            return resp;
        }
    }

    const Handler* handler = nullptr;
    {
        std::lock_guard lock(routesMutex_);
        handler = FindHandler(req.path, req.method);
    }
    if (!handler) {
        resp.status = 404;
        resp.body = Error404Json(req.path);
        return resp;
    }

    Handler fn = *handler; // 拷贝，避免 routes 重入
    UiDispatcher dispatch;
    {
        std::lock_guard lock(routesMutex_);
        dispatch = uiDispatcher_;
    }

    if (!dispatch) {
        fn(req, resp);
        return resp;
    }

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool timedOut = false;
    Request reqCopy = std::move(req);
    Response respLocal;

    dispatch([&] {
        try {
            fn(reqCopy, respLocal);
        } catch (const std::exception& ex) {
            log::Error("mcp Http handler 异常：{}", ex.what());
            respLocal.status = 500;
            respLocal.body = fmt::format(R"({{"error":"internal","message":"{}"}})", ex.what());
        } catch (...) {
            respLocal.status = 500;
            respLocal.body = R"({"error":"internal","message":"unknown"})";
        }
        {
            std::lock_guard lock(m);
            done = true;
        }
        cv.notify_one();
    });

    {
        std::unique_lock lock(m);
        if (!cv.wait_for(lock, std::chrono::seconds{5}, [&] { return done; })) {
            timedOut = true;
        }
    }
    if (timedOut) {
        resp.status = 504;
        resp.body = R"({"error":"ui_timeout","message":"UI 线程未在 5s 内处理请求"})";
        return resp;
    }
    return respLocal;
}

std::expected<void, std::string> HttpServer::Start(std::string_view listenAddr, int port) {
    if (running_.load()) {
        return std::unexpected(std::string{"HTTP 服务已在运行"});
    }
    const std::string host{listenAddr.empty() ? "127.0.0.1" : listenAddr};
    if (port < 0 || port > 65535) {
        return std::unexpected(fmt::format("端口非法：{}", port));
    }
    if (host.find('/') != std::string::npos || host.find('\\') != std::string::npos) {
        return std::unexpected(fmt::format("监听地址非法：{}", host));
    }

    if (!impl_) impl_ = std::make_unique<Impl>();

    // 每次 Start 重建 service/server，避免 stop 后状态残留
    impl_->service = hv::HttpService{};
    impl_->streamRoutes.clear();
    {
        std::lock_guard lock(routesMutex_);
        for (const auto& r : routes_) {
            if (r.stream && r.streamHandler) impl_->streamRoutes.push_back(r);
        }
    }

    impl_->service.processor = [this](HttpRequest* req, HttpResponse* resp) -> int {
        const std::string path = req->path.empty() ? "/" : std::string{req->path};
        const std::string method = http_method_str(req->method);
        // 流式路由交给 router 的 async_handler（HttpResponseWriter）
        for (const auto& sr : impl_->streamRoutes) {
            const bool methodOk = sr.method == HttpMethod::Any || method == HttpMethodStr(sr.method);
            const bool pathOk = sr.prefix ? PathPrefixMatch(sr.path, path) : (sr.path == path);
            if (methodOk && pathOk) {
                return HTTP_STATUS_NEXT; // 落到 AddRoute 的 async handler
            }
        }
        requestCount_.fetch_add(1, std::memory_order_relaxed);
        Request r;
        r.method = method;
        r.path = path;
        // query：FullPath 去掉 path 前缀
        const std::string full = req->FullPath();
        if (full.size() > r.path.size()) {
            const auto pos = full.find('?');
            if (pos != std::string::npos) r.query = full.substr(pos + 1);
        }
        r.body = req->body;
        for (const auto& [k, v] : req->headers) {
            r.headers.emplace(k, v);
        }
        r.remote = std::string{req->client_addr.ip};

        Response out = HandleOnCurrentThread(std::move(r));
        resp->status_code = static_cast<http_status>(out.status);
        if (!out.contentType.empty()) {
            resp->content_type = APPLICATION_JSON;
            resp->headers["Content-Type"] = out.contentType;
        }
        resp->body = out.body;
        return out.status;
    };

    // 注册 stream 路由（async_handler 在 libhv 线程池）
    for (const auto& sr : impl_->streamRoutes) {
        StreamHandler sh = sr.streamHandler;
        const std::string spath = sr.path;
        const std::string smethod = HttpMethodStr(sr.method);
        auto makeHvHandler = [sh](const HttpRequestPtr& req, const HttpResponseWriterPtr& writer) {
            if (!writer) return;
            Request r;
            r.method = http_method_str(req->method);
            r.path = req->path.empty() ? "/" : std::string{req->path};
            const std::string full = req->FullPath();
            if (full.size() > r.path.size()) {
                const auto pos = full.find('?');
                if (pos != std::string::npos) r.query = full.substr(pos + 1);
            }
            r.body = req->body;
            for (const auto& [k, v] : req->headers) r.headers.emplace(k, v);
            writer->Begin();
            writer->WriteStatus(HTTP_STATUS_OK);
            writer->WriteHeader("Cache-Control", "no-cache");
            writer->WriteHeader("Connection", "keep-alive");
            writer->EndHeaders("Content-Type", "text/event-stream; charset=utf-8");
            auto write = [writer](std::string_view chunk) {
                if (chunk.empty()) return;
                writer->write(std::string{chunk});
            };
            auto close = [writer]() { writer->End(); };
            try {
                sh(r, write, close);
            } catch (const std::exception& ex) {
                log::Error("mcp stream handler 异常：{}", ex.what());
                close();
            }
        };
        impl_->service.AddRoute(
            spath.c_str(), HTTP_GET, http_async_handler(makeHvHandler));
        if (smethod == "POST" || sr.method == HttpMethod::Any) {
            impl_->service.AddRoute(spath.c_str(), HTTP_POST, http_async_handler(makeHvHandler));
        }
    }

    impl_->server = std::make_unique<hv::HttpServer>(&impl_->service);
    impl_->server->setHost(host.c_str());
    impl_->server->setPort(port);
    impl_->server->setThreadNum(2);

    const std::string ipPort = fmt::format("{}:{}", host, port);
    const int rc = impl_->server->start(ipPort.c_str());
    if (rc != 0) {
        impl_->server.reset();
        return std::unexpected(
            fmt::format("监听 {} 失败（rc={}）{}；请检查端口占用或地址是否合法", ipPort, rc,
                        port > 0 ? fmt::format("，端口 {}", port) : std::string{}));
    }

    listenAddr_ = host;
    port_.store(impl_->server->port > 0 ? impl_->server->port : port);
    running_.store(true);
    log::Info("mcp HTTP 已监听 {}:{}（toolCount={}）", host, port_.load(),
              ToolRegistry::Instance().ToolCount());
    return {};
}

void HttpServer::Stop() {
    if (!running_.exchange(false)) {
        if (impl_ && impl_->server) impl_->server.reset();
        return;
    }
    if (impl_ && impl_->server) {
        impl_->server->stop();
        impl_->server.reset();
    }
    log::Info("mcp HTTP 已停止");
}

std::string HttpServer::BaseUrl() const {
    return fmt::format("http://{}:{}", listenAddr_.empty() ? "127.0.0.1" : listenAddr_, port_.load());
}

std::string HttpServer::ListenAddr() const { return listenAddr_; }

Response HttpServer::DispatchForTest(const Request& req) {
    requestCount_.fetch_add(1, std::memory_order_relaxed);
    return HandleOnCurrentThread(req);
}

void InstallDefaultUiDispatcher(HttpServer& srv) {
    srv.SetUiDispatcher([](std::function<void()> fn) {
        if (!fn) return;
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        // 调用方线程 wait_for 保活本帧栈上的 m/cv/done；超时后不再访问（见下方 wait 分支）
        async::PostToUi([fn = std::move(fn), &m, &cv, &done]() mutable {
            try {
                fn();
            } catch (...) {
                log::Error("mcp UI dispatcher 任务异常");
            }
            {
                std::lock_guard lock(m);
                done = true;
            }
            cv.notify_one();
        });
        std::unique_lock lock(m);
        if (!cv.wait_for(lock, std::chrono::seconds{5}, [&] { return done; })) {
            log::Warn("mcp UI dispatcher 等待超时（DrainUiQueue 可能未运行）");
        }
    });
}

void RegisterHealthRoute(HttpServer& srv) {
    HttpServer* s = &srv;
    srv.Route(
        "/health", HttpMethod::Get,
        [s](const Request&, Response& out) {
            auto& reg = ToolRegistry::Instance();
            out.status = 200;
            out.body = fmt::format(
                R"({{"server":"{}","version":"{}","port":{},"toolCount":{}}})", kServerName,
                kServerVersion, s->Port(), reg.ToolCount());
        });
}

std::expected<void, std::string> StartHttpFromSettings() {
    auto& s = Settings();
    if (!s.mcpEnabled) {
        log::Info("mcp HTTP 未启用（Settings.mcpEnabled=false）");
        return {};
    }
    auto& srv = HttpServer::Instance();
    InstallDefaultUiDispatcher(srv);
    // P7.3：协议路由 + 内置工具（含 /health /mcp /tools …）
    RegisterMcpRoutes(srv);
    return srv.Start(s.mcpListenAddr, s.mcpPort);
}

void StopHttpFromSettings() { HttpServer::Instance().Stop(); }

bool RunHttpServerSelfCheck() {
    bool pass = true;
    std::string notes;
    auto fail = [&](std::string_view why) {
        log::Error("mcp Http 自检 FAIL：{}", why);
        notes += fmt::format("FAIL {}\n", why);
        pass = false;
    };
    auto ok = [&](std::string_view what) { notes += fmt::format("PASS {}\n", what); };

    // 1) 精确路由 + 自定义 dispatcher（同步内联，不依赖 UI 循环）
    {
        HttpServer srv;
        srv.SetUiDispatcher([](std::function<void()> fn) { fn(); });
        bool dispatched = false;
        srv.Route(
            "/ping", HttpMethod::Get,
            [&](const Request& req, Response& out) {
                dispatched = true;
                out.body = fmt::format(R"({{"pong":true,"path":"{}"}})", req.path);
            });
        const auto r1 = srv.DispatchForTest(Request{.method = "GET", .path = "/ping"});
        if (r1.status != 200 || r1.body.find("pong") == std::string::npos)
            fail("精确路由 /ping");
        else ok("精确路由 /ping");
        if (!dispatched) fail("dispatcher 未被调用");
        else ok("dispatcher 被调用");

        const auto r2 = srv.DispatchForTest(Request{.method = "GET", .path = "/nope"});
        if (r2.status != 404 || r2.body.find("not_found") == std::string::npos) fail("404 JSON");
        else ok("404 JSON");

        const auto r3 = srv.DispatchForTest(Request{.method = "OPTIONS", .path = "/ping"});
        if (r3.status != 204) fail("OPTIONS 应 204");
        else ok("OPTIONS → 204");
    }

    // 2) 前缀路由 /tools/
    {
        HttpServer srv;
        srv.SetUiDispatcher(nullptr); // IO 同步
        srv.Route(
            "/tools/", HttpMethod::Post,
            [](const Request& req, Response& out) {
                out.body = fmt::format(R"({{"tool":"{}"}})", req.path);
            });
        const auto r = srv.DispatchForTest(Request{.method = "POST", .path = "/tools/comfy_ping"});
        if (r.status != 200 || r.body.find("comfy_ping") == std::string::npos) fail("前缀 /tools/");
        else ok("前缀 /tools/ → /tools/comfy_ping");
        const auto rBad = srv.DispatchForTest(Request{.method = "GET", .path = "/tools/comfy_ping"});
        if (rBad.status != 404) fail("前缀方法不匹配应 404");
        else ok("前缀方法不匹配 → 404");
    }

    // 3) Start port=0 + /health + 端口占用 + 非法端口
    {
        net::EnsureLibhvReady();
        HttpServer srv;
        srv.SetUiDispatcher([](std::function<void()> fn) { fn(); });
        RegisterHealthRoute(srv);

        const auto st = srv.Start("127.0.0.1", 0);
        if (!st) {
            fail(fmt::format("Start(127.0.0.1,0)：{}", st.error()));
        } else if (srv.Port() <= 0) {
            fail("Start 后 Port 无效");
        } else {
            ok(fmt::format("Start 监听 127.0.0.1:{}", srv.Port()));
            const auto url = fmt::format("http://127.0.0.1:{}/health", srv.Port());
            const auto resp = comfy::HttpGet(url, std::chrono::seconds{5});
            if (!resp.ok || resp.body.find("ShineTVStudio") == std::string::npos) {
                fail(fmt::format("GET {} → ok={} status={} body={}", url, resp.ok, resp.status,
                                 resp.body.substr(0, 80)));
            } else {
                ok(fmt::format("GET /health → {}", resp.body.substr(0, 120)));
            }
            const auto miss = comfy::HttpGet(
                fmt::format("http://127.0.0.1:{}/missing", srv.Port()), std::chrono::seconds{5});
            if (miss.status != 404) fail(fmt::format("GET /missing status={}", miss.status));
            else ok("GET /missing → 404");

            // 端口占用：第二个 server 用同一端口
            HttpServer srv2;
            const auto busy = srv2.Start("127.0.0.1", srv.Port());
            if (busy) {
                fail("端口占用时 Start 不应成功");
                srv2.Stop();
            } else if (busy.error().empty()) {
                fail("端口占用错误信息为空");
            } else {
                ok(fmt::format("端口占用失败且有原因：{}", busy.error().substr(0, 80)));
            }

            // 非法端口
            HttpServer srv3;
            const auto bad = srv3.Start("127.0.0.1", 70000);
            if (bad) fail("非法端口 70000 不应 Start 成功");
            else ok("非法端口 70000 被拒绝");
            const auto bad2 = srv3.Start("127.0.0.1", -1);
            if (bad2) fail("非法端口 -1 不应 Start 成功");
            else ok("非法端口 -1 被拒绝");

            srv.Stop();
            if (srv.Running()) fail("Stop 后仍 Running");
            else ok("Stop 后 Running=false");
        }
    }

    notes += pass ? "OVERALL PASS\n" : "OVERALL FAIL\n";
    log::Info("mcp Http 自检 {}", pass ? "PASS" : "FAIL");
    // GUI 子系统无 stdout：写报告文件便于验收取证
    if (const char* out = std::getenv("SHINE_MCP_HTTP_CHECK_OUT"); out && *out) {
        const std::filesystem::path p{out};
        (void)util::WriteFileBytes(p, notes);
    } else {
        wchar_t tmp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmp);
        const std::filesystem::path p = std::filesystem::path{tmp} / L"shine_mcp_http_check.txt";
        (void)util::WriteFileBytes(p, notes);
        log::Info("mcp Http 自检报告：{}", util::PathToUtf8(p));
    }
    return pass;
}

} // namespace shine::mcp
