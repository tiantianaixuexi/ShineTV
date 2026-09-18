#include "net/LibhvReady.h"

#include "core/Log.h"

#include <hlog.h>

#include <mutex>
#include <string_view>

namespace shine::net {
namespace {

// ⚠️ libhv 默认 logger 惰性 + 非线程安全：首次 `hv_default_logger()` 会 `atexit(...)`，
// 多 worker 并发首次写日志会在 msvcrt `_onexit` 上死锁（见 坑与手法）。
// 修法：第一次碰 libhv 之前 `call_once` 单线程初始化，并立刻把 handler 换成 shine::log。
std::once_flag g_libhvLoggerOnce;

// libhv 已拼好 printf 文本；映射到 spdlog（UI + 控制台），不再写 libhv*.log
extern "C" void ShineLibhvLogHandler(int loglevel, const char* buf, int len) {
    if (!buf || len <= 0) return;
    std::string_view sv{buf, static_cast<std::size_t>(len)};
    while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    if (sv.empty()) return;

    switch (loglevel) {
    case LOG_LEVEL_VERBOSE:
    case LOG_LEVEL_DEBUG:
        // 应用侧没有 debug 档 → 归到 info，带 [libhv] 前缀便于过滤
        shine::log::LogText(0, fmt::format("[libhv] {}", sv));
        break;
    case LOG_LEVEL_INFO:
        shine::log::LogText(0, fmt::format("[libhv] {}", sv));
        break;
    case LOG_LEVEL_WARN:
        shine::log::LogText(1, fmt::format("[libhv] {}", sv));
        break;
    case LOG_LEVEL_ERROR:
    case LOG_LEVEL_FATAL:
        shine::log::LogText(2, fmt::format("[libhv] {}", sv));
        break;
    default:
        shine::log::LogText(0, fmt::format("[libhv] {}", sv));
        break;
    }
}

} // namespace

void EnsureLibhvReady() {
    std::call_once(g_libhvLoggerOnce, [] {
        // 先创建默认 logger（仍会 atexit 注册 destroy），再立刻改 handler，避免首条日志落到文件
        logger_t* lg = hv_default_logger();
        if (!lg) return;
        logger_set_handler(lg, ShineLibhvLogHandler);
        logger_enable_color(lg, 0);
        // 消息体由 libhv 已格式化；我们再包 [libhv] 前缀，格式只保留消息 + 源位置后缀
        logger_set_format(lg, "%s");
        logger_set_level(lg, LOG_LEVEL_INFO);
    });
}

} // namespace shine::net
