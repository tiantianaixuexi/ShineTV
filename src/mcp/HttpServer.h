#pragma once
// shine::mcp::HttpServer —— libhv HTTP Server 封装（P7.2，MCP 传输层骨架）
//
// 线程模型：
//  - libhv IO 线程收包；**业务 handler 默认投递到 UI 线程**（图/工程等状态只允许 UI 访问）；
//  - IO 线程等待 handler 完成后再写响应（MCP 低 QPS 可接受）；
//  - 未设置 dispatcher 时 handler 在 IO 线程同步执行（仅测试/无 UI 依赖时允许）。
//  - Route 的 path 见实现：精确匹配；以 `/` 或 `*` 结尾则为前缀（`/tools/`）。
#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace shine::mcp {

enum class HttpMethod { Get, Post, Put, Delete, Patch, Options, Any };

[[nodiscard]] const char* HttpMethodStr(HttpMethod m) noexcept;

struct Request {
    std::string method; // "GET"
    std::string path;   // "/health"（不含 query）
    std::string query;  // "a=1&b=2"（不含 ?）
    std::map<std::string, std::string> headers;
    std::string body;
    std::string remote;
};

struct Response {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

using Handler = std::function<void(const Request&, Response&)>;
// post(fn) 必须在**当前调用线程阻塞直到 fn 在目标线程跑完**（或超时后放弃）
using UiDispatcher = std::function<void(std::function<void()>)>;

class HttpServer {
public:
    HttpServer() = default;

    // 进程级 MCP HTTP 服务（App 启停这个）；自检可用局部实例
    [[nodiscard]] static HttpServer& Instance();

    void SetUiDispatcher(UiDispatcher post);

    // path 以 '/' 或 '*' 结尾 → 前缀匹配（如 `/tools/`、`/tools/*`）；否则精确匹配
    // 同 path+method 后注册覆盖先注册
    void Route(std::string path, HttpMethod method, Handler handler);
    void ClearRoutes();

    // port=0 表示由系统分配；失败返回中文原因（程序不崩）
    [[nodiscard]] std::expected<void, std::string> Start(std::string_view listenAddr, int port);
    void Stop();
    [[nodiscard]] bool Running() const noexcept { return running_.load(); }
    [[nodiscard]] int Port() const noexcept { return port_.load(); }
    [[nodiscard]] std::string BaseUrl() const;
    [[nodiscard]] std::string ListenAddr() const;
    [[nodiscard]] std::uint64_t RequestCount() const noexcept { return requestCount_.load(); }

    // 测试/诊断：不经网络直接走路由分发
    [[nodiscard]] Response DispatchForTest(const Request& req);

private:
    [[nodiscard]] const Handler* FindHandler(std::string_view path, std::string_view method) const;
    Response HandleOnCurrentThread(Request req);

    struct RouteEntry {
        std::string path;
        HttpMethod method = HttpMethod::Any;
        Handler handler;
        bool prefix = false;
    };

    mutable std::mutex routesMutex_;
    std::vector<RouteEntry> routes_;
    UiDispatcher uiDispatcher_;
    std::atomic<bool> running_{false};
    std::atomic<int> port_{0};
    std::string listenAddr_{"127.0.0.1"};
    std::atomic<std::uint64_t> requestCount_{0};

    struct Impl; // hv::HttpServer + HttpService
    std::unique_ptr<Impl> impl_;
};

// —— 与 Settings / 启动接线（App 使用）——
// 默认 dispatcher：async::PostToUi + 条件变量等待（UI 帧 DrainUiQueue）
void InstallDefaultUiDispatcher(HttpServer& srv);
// 注册 GET /health → {"server","version","port","toolCount"}
void RegisterHealthRoute(HttpServer& srv);
// 按 Settings 启动（mcpEnabled=false 则什么都不做）
[[nodiscard]] std::expected<void, std::string> StartHttpFromSettings();
void StopHttpFromSettings();

// P7.2 自检（端口 0 / 404 / OPTIONS / 端口占用）；可用 SHINE_MCP_HTTP_CHECK=1
[[nodiscard]] bool RunHttpServerSelfCheck();

} // namespace shine::mcp
