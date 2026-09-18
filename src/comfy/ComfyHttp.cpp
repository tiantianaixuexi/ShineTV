#include "comfy/ComfyHttp.h"

#include "core/Log.h"
#include "util/Encoding.h"

#include <HttpClient.h>
#include <HttpMessage.h>
#include <hlog.h>
#include <http_content.h>
#include <fmt/format.h>

#include <memory>
#include <mutex>
#include <utility>

// libhv HttpRequest/HttpResponse live in the global namespace.
using HvRequest = HttpRequest;
using HvResponse = HttpResponse;

namespace shine::comfy {
namespace {

// ⚠️ libhv 的默认 logger 是**惰性 + 非线程安全**的：宏 `hlog` = `hv_default_logger()`，
// 首次调用会 `logger_create()` 并 **`atexit(hv_destroy_default_logger)`**
// （`third/libhv/base/hlog.c:534-539`，没有锁保护）。
// 后果（2026-09-17 实测崩溃+卡死）：**ComfyUI 连不上时多个 worker 同时首次写 libhv 日志**
// → 并发进 msvcrt 的 `atexit`/`_onexit` → 在 `_onexit` 的临界区上**死锁**：
//   gdb 栈 `http_client_connect → hv_default_logger → msvcrt!atexit → _onexit → RtlSleepConditionVariableCS`
// → worker 永不返回 → 退出时 `async::Shutdown()` 的 join 永久卡住（关窗后进程不消失）。
// 修法：**第一次碰 libhv 之前**用 `std::call_once` 单线程初始化一次（不改 `third/`）。
std::once_flag g_libhvLoggerOnce;

HttpResponse FromResponse(const std::shared_ptr<HvResponse>& resp) {
    HttpResponse out;
    if (!resp) {
        out.error = "请求失败（无响应）";
        return out;
    }
    out.status = resp->status_code;
    out.body = resp->body;
    out.ok = resp->status_code >= 200 && resp->status_code < 300;
    if (!out.ok) {
        out.error = "HTTP " + std::to_string(resp->status_code);
    }
    return out;
}

HttpResponse Fail(std::string_view err) {
    HttpResponse out;
    out.error = std::string{err};
    return out;
}

// std::chrono::seconds -> libhv 的秒级 int 超时
constexpr int TimeoutSecs(std::chrono::seconds s) noexcept { return static_cast<int>(s.count()); }

// ⚠️ **不要用 `http_client_strerror()` 的结果直接显示**（2026-09-16 实测）：
//   它内部是 `socket_strerror()` → `FormatMessageA()`（`third/libhv/base/hsocket.c:38`），
//   返回的是**当前 ANSI 代码页**(本机 936/GBK)的字节；拼进 UTF-8 的 `std::string` 后，
//   ImGui 会把每个非法字节渲染成一个 `?`，UI 上就是「网络错误: ?????????」
//   （实测那 9 个 `?` = GBK 的「函数不正确。 」）。
//   而且它取的是 `ABS(ret)`：libhv 连接失败常常只返回 -1 → 变成毫无意义的系统消息。
// 所以这里自己给中文原因：先按 WSA 错误码给可执行提示，其余走 **W 版**系统消息（天然 UTF-8）。
[[nodiscard]] std::string NetworkErrorText(int ret) {
    const int code = ret < 0 ? -ret : ret;
    if (code >= 10000) { // WSA 错误码
        switch (code) {
        case 10061:
            return "连接被拒绝：端口没有服务在监听，ComfyUI 是否已启动？（WSA 10061）";
        case 10060:
            return "连接超时：地址不可达或被防火墙静默丢弃（WSA 10060）";
        case 10065:
            return "网络不可达：检查本机网络与 ComfyUI 地址（WSA 10065）";
        case 10013:
            return "访问该端口被拒绝：权限或防火墙拦截（WSA 10013）";
        case 11001:
            return "无法解析主机名：检查设置里的 ComfyUI 地址（WSA 11001）";
        case 10054:
            return "连接被对端重置：服务可能刚重启（WSA 10054）";
        default:
            break;
        }
        const std::string sys = util::WinErrorMessage(static_cast<unsigned long>(code));
        return sys.empty() ? fmt::format("网络错误（WSA {}）", code) : fmt::format("{}（WSA {}）", sys, code);
    }
    // libhv 没把具体错误码带出来（最常见：连接失败直接返回 -1）。
    // 文案里不用括号包整句 —— 上游 `HealthSummary()` 会再套一层「连接中（…）」，
    // 嵌套括号既难读也容易看错。
    return fmt::format("连接失败：ComfyUI 未启动或端口不通（libhv {}）", ret);
}

HttpResponse Send(HvRequest& req) {
    EnsureLibhvReady(); // 兜底：任何 libhv 调用前先单线程初始化（详见上方注释）
    auto resp = std::make_shared<HvResponse>();
    const int ret = http_client_send(&req, resp.get());
    if (ret != 0) {
        return Fail(fmt::format("网络错误: {}", NetworkErrorText(ret)));
    }
    return FromResponse(resp);
}

} // namespace

void EnsureLibhvReady() {
    std::call_once(g_libhvLoggerOnce, []() { (void)hv_default_logger(); });
}

std::expected<std::string, HttpError> HttpDownloadBinary(std::string_view url, std::chrono::seconds timeout) {
    if (url.empty()) {
        return std::unexpected(HttpError{0, "空 URL"});
    }
    const HttpResponse resp = HttpGet(url, timeout);
    if (!resp.ok) {
        if (!resp.error.empty()) {
            return std::unexpected(HttpError{resp.status, resp.error});
        }
        return std::unexpected(HttpError{resp.status, "下载失败（HTTP " + std::to_string(resp.status) + "）"});
    }
    if (resp.body.empty()) {
        return std::unexpected(HttpError{resp.status, "响应为空"});
    }
    return resp.body; // libhv 的 body 是二进制安全的 std::string
}

HttpResponse HttpGet(std::string_view url, std::chrono::seconds timeout) {
    if (url.empty()) {
        return Fail("空 URL");
    }
    HvRequest req;                              // libhv 边界：这里把视图转成 std::string
    req.method = HTTP_GET;
    req.url = std::string{url};
    req.timeout = TimeoutSecs(timeout);
    return Send(req);
}

HttpResponse HttpPostJson(std::string_view url, std::string_view jsonBody, std::chrono::seconds timeout) {
    if (url.empty()) {
        return Fail("空 URL");
    }
    HvRequest req;
    req.method = HTTP_POST;
    req.url = std::string{url};
    req.timeout = TimeoutSecs(timeout);
    req.body = std::string{jsonBody};
    req.headers["Content-Type"] = "application/json";
    return Send(req);
}

HttpResponse HttpPostEmpty(std::string_view url, std::chrono::seconds timeout) {
    return HttpPostJson(url, "{}", timeout);
}

HttpResponse HttpUploadImage(std::string_view url, std::string_view fileName, std::string_view fileBytes,
                             const std::map<std::string, std::string>& fields, std::chrono::seconds timeout) {
    if (url.empty()) {
        return Fail("空 URL");
    }
    static const char* kBoundary = "----ShineTVBoundary7d4a6e";
    std::string body;
    for (const auto& [k, v] : fields) {
        body += "--";
        body += kBoundary;
        body += "\r\nContent-Disposition: form-data; name=\"";
        body += k;
        body += "\"\r\n\r\n";
        body += v;
        body += "\r\n";
    }
    body += "--";
    body += kBoundary;
    body += "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"";
    body += fileName;
    body += "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    body += fileBytes;
    body += "\r\n--";
    body += kBoundary;
    body += "--\r\n";

    HvRequest req;
    req.method = HTTP_POST;
    req.url = std::string{url};
    req.timeout = TimeoutSecs(timeout);
    req.body = std::move(body); // body 之后不再使用，直接搬移
    req.headers["Content-Type"] = std::string("multipart/form-data; boundary=") + kBoundary;
    return Send(req);
}

} // namespace shine::comfy
