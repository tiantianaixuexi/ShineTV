#pragma once
// Template log API — compile-time fmt checks, no vsnprintf.

namespace shine::log {

// level: 0=info 1=warn 2=error。text 用 std::string_view 传，避免每条日志多一次拷贝。
void LogText(int level, std::string_view text);

template <typename... Args>
void Info(fmt::format_string<Args...> fmtStr, Args&&... args) {
    LogText(0, fmt::format(fmtStr, std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(fmt::format_string<Args...> fmtStr, Args&&... args) {
    LogText(1, fmt::format(fmtStr, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(fmt::format_string<Args...> fmtStr, Args&&... args) {
    LogText(2, fmt::format(fmtStr, std::forward<Args>(args)...));
}

} // namespace shine::log
