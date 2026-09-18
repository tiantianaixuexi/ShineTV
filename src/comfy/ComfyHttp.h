#pragma once
// 注：新增函数直接用 std::expected（Doc/RULES-LANG.md §13.3）；旧的 HttpResponse 系列保持不动。
#include <chrono>
#include <expected>
#include <map>
#include <string>
#include <string_view>

namespace shine::comfy {

// 初始化 libhv（**幂等 + 线程安全**）。要求在**任何 libhv 调用之前**、由**主线程**调用一次
// （目前由 `ComfySession::Init()` 调用）。
// 原因：libhv 的默认 logger 是惰性初始化且无锁，首次写日志会 `atexit(...)`
// （`third/libhv/base/hlog.c:534`）；**并发首次使用**会在 msvcrt 的 `_onexit` 上死锁
// （ComfyUI 连不上时多个 worker 同时报错正是这种情况），进而卡死进程退出。
// HTTP 侧在 `Send()` 里另有一道 `call_once` 兜底。
void EnsureLibhvReady();

struct HttpResponse {
    bool ok = false;
    int status = 0;
    std::string body;
    // 中文、**必须是 UTF-8**：Win32 A 版 API（`FormatMessageA` 等）返回的 ANSI 文本
    // 必须先过 `util/Encoding.h` 转一次，否则 UI 上会变成一串 `?`（详见该头文件注释）。
    std::string error;
};

// Synchronous HTTP via libhv. Call from worker threads only.
// 参数用 std::string_view；libhv 的 HttpRequest 要 std::string，在 .cpp 里一次性转换（Doc/RULES-LANG.md §13.5 例外 1）。
// 返回类型保持 HttpResponse（它是 §13.5 认可的 libhv 边界封装），故不加 [[nodiscard]] 之外的处理。
[[nodiscard]] HttpResponse HttpGet(std::string_view url,
                                   std::chrono::seconds timeout = std::chrono::seconds{15});
[[nodiscard]] HttpResponse HttpPostJson(std::string_view url, std::string_view jsonBody,
                                        std::chrono::seconds timeout = std::chrono::seconds{60});
[[nodiscard]] HttpResponse HttpPostEmpty(std::string_view url,
                                         std::chrono::seconds timeout = std::chrono::seconds{15});

// multipart/form-data image upload (P1.2)。文件字段名固定为 "image"（ComfyUI 约定）。
[[nodiscard]] HttpResponse HttpUploadImage(std::string_view url, std::string_view fileName,
                                           std::string_view fileBytes,
                                           const std::map<std::string, std::string>& fields,
                                           std::chrono::seconds timeout = std::chrono::seconds{120});

// —— P4.1 S5：二进制下载（图片/视频字节）——
struct HttpError {
    int status = 0;            // HTTP 状态码（网络错误时为 0）
    std::string message;       // 中文/可直接显示
};

// **只在 worker 线程调用**（见 MEMORY.md「异步任务规范」）；返回原始字节（二进制安全）
[[nodiscard]] std::expected<std::string, HttpError>
HttpDownloadBinary(std::string_view url, std::chrono::seconds timeout = std::chrono::seconds{60});

} // namespace shine::comfy
