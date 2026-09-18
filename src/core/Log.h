#pragma once
#include <fmt/format.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace shine::log {

void Init();
void Shutdown();

// fmt 风格（{} / {:.2f}），不再走 vsnprintf。
template <typename... Args>
void Info(fmt::format_string<Args...> fmtStr, Args&&... args);

template <typename... Args>
void Warn(fmt::format_string<Args...> fmtStr, Args&&... args);

template <typename... Args>
void Error(fmt::format_string<Args...> fmtStr, Args&&... args);

struct Line {
    int level = 0; // 0 info, 1 warn, 2 error
    std::string text;
};

// 线程安全快照，供 ListClipper 使用。
// 注：这些函数内部要加锁，std::mutex 加锁理论上可能抛 std::system_error，故不加 noexcept。
[[nodiscard]] std::vector<Line> LinesSnapshot();
[[nodiscard]] std::size_t LineCount();
[[nodiscard]] std::uint64_t Version(); // 内容变更时递增，UI 可缓存快照
void Clear();

} // namespace shine::log

#include "core/Log.inl"
