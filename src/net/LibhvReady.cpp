#include "net/LibhvReady.h"

#include "core/Log.h"

#include <hlog.h>

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace shine::net {
namespace {

// ⚠️ libhv 默认 logger 惰性 + 非线程安全：首次 `hv_default_logger()` 会 `atexit(...)`，
// 多 worker 并发首次写日志会在 msvcrt `_onexit` 上死锁（见 坑与手法）。
// 修法：第一次碰 libhv 之前 `call_once` 单线程初始化，并立刻把 handler 换成 shine::log。
std::once_flag g_libhvLoggerOnce;

// ---- 重试族日志限流（2026-11-24）----
//
// 动机：ComfyUI 没启动时健康探活每 5 秒撞一次 `connect 127.0.0.1:8188 failed!`
// —— 探活是设计行为，但 libhv 把每次 connect 失败按 **error** 打进来，
// 每分钟 12 条刷屏，真错误反而淹没了。
//
// 口径：**同一条消息文本** 60s 窗口内只放行首条，被静默的条数在下一条上带出来
// （「（前 60 秒内 N 条重复已静默）」）—— 状态变化永远看得见，重复噪音有上限。
// 只对 warn/error 限流：info 多为一次性事件（初始化、TLS 握手之类），限了会丢信息。
// 在桥接层做而不是改 libhv 内部：不动 third/ 源码，且对所有重试族生效。
struct Window {
    std::chrono::steady_clock::time_point last{};
    int suppressed = 0;
    bool seen = false;
};

// 返回 -1 = 本条静默；>=0 = 放行，且此前一个窗口内静默了这么多条重复。
int RateLimitedAdmit(std::string_view msg) {
    static std::mutex mu;
    static std::unordered_map<std::string, Window> seen;
    static constexpr std::chrono::seconds kWindow{60};
    std::lock_guard<std::mutex> lock(mu);
    if (seen.size() > 256) {
        // 有界：消息变体（带不同地址/端口）多到失控时整表重来，宁可多放一条也不泄内存。
        seen.clear();
    }
    Window& w = seen[std::string(msg)];
    const auto now = std::chrono::steady_clock::now();
    if (w.seen && now - w.last < kWindow) {
        ++w.suppressed;
        return -1;
    }
    const int suppressed = w.suppressed;
    w = Window{now, 0, true};
    return suppressed;
}

// libhv 已拼好 printf 文本；映射到 spdlog（UI + 控制台），不再写 libhv*.log
extern "C" void ShineLibhvLogHandler(int loglevel, const char* buf, int len) {
    if (!buf || len <= 0) return;
    std::string_view sv{buf, static_cast<std::size_t>(len)};
    while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    if (sv.empty()) return;

    int level = 0;
    switch (loglevel) {
    case LOG_LEVEL_VERBOSE:
    case LOG_LEVEL_DEBUG:
    case LOG_LEVEL_INFO:
        // 应用侧没有 debug 档 → 归到 info，带 [libhv] 前缀便于过滤
        level = 0;
        break;
    case LOG_LEVEL_WARN:
        level = 1;
        break;
    case LOG_LEVEL_ERROR:
    case LOG_LEVEL_FATAL:
        level = 2;
        break;
    default:
        level = 0;
        break;
    }

    std::string text = fmt::format("[libhv] {}", sv);
    if (level >= 1) {
        const int suppressed = RateLimitedAdmit(sv);
        if (suppressed < 0) {
            return; // 重复噪音：静默
        }
        if (suppressed > 0) {
            text += fmt::format("（前 60 秒内 {} 条重复已静默）", suppressed);
        }
    }
    shine::log::LogText(level, text);
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
