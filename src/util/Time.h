#pragma once
// shine::util —— 时间工具（统一走 std::chrono，禁止 clock()/GetTickCount 裸算）
#include <chrono>
#include <cstdint>
#include <string>

#include <fmt/format.h>

namespace shine::util {

// 墙上时间（毫秒，Unix epoch）—— 用于日志/队列条目时间戳
[[nodiscard]] inline std::int64_t NowMillis() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// 单调时间（毫秒）—— 用于耗时/超时/健康判定（不受系统改时间影响）
[[nodiscard]] inline std::int64_t MonotonicMillis() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 距某个单调时间点过了多少毫秒
[[nodiscard]] inline std::int64_t ElapsedMillis(std::int64_t sinceMonotonicMillis) noexcept {
    return MonotonicMillis() - sinceMonotonicMillis;
}

// 给日志用的"耗时"字符串（如 "43ms" / "1.21s"）
[[nodiscard]] inline std::string FormatDurationMs(std::int64_t ms) {
    if (ms < 1000) {
        return fmt::format("{}ms", ms);
    }
    return fmt::format("{:.2f}s", static_cast<double>(ms) / 1000.0);
}

} // namespace shine::util
