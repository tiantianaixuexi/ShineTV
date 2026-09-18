#pragma once
// shine::util —— 字符串工具（视图优先：只读处理一律返回 string_view 或新串，不做隐式拷贝）
#include <algorithm>
#include <cctype>
#include <charconv>
#include <concepts>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace shine::util {

inline constexpr std::string_view kWhitespace = " \t\r\n\f\v";

// 去掉两端空白。返回的是**视图**：调用方要保证原串生命周期足够长
[[nodiscard]] inline std::string_view Trim(std::string_view s) noexcept {
    const auto b = s.find_first_not_of(kWhitespace);
    if (b == std::string_view::npos) {
        return {};
    }
    const auto e = s.find_last_not_of(kWhitespace);
    return s.substr(b, e - b + 1);
}

// 去掉两端指定字符（例如从 baseUrl 去掉尾部 '/'）
[[nodiscard]] inline std::string_view TrimEnd(std::string_view s, std::string_view chars) noexcept {
    const auto e = s.find_last_not_of(chars);
    return e == std::string_view::npos ? std::string_view{} : s.substr(0, e + 1);
}

[[nodiscard]] inline std::string ToLower(std::string_view s) {
    std::string out{s};
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] inline std::string ToUpper(std::string_view s) {
    std::string out{s};
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

[[nodiscard]] inline bool StartsWith(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] inline bool EndsWith(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// 大小写不敏感的后缀判断（文件名扩展名场景，内部只折叠 ASCII）
[[nodiscard]] inline bool EndsWithNoCase(std::string_view s, std::string_view suffix) noexcept {
    if (s.size() < suffix.size()) {
        return false;
    }
    const auto tail = s.substr(s.size() - suffix.size());
    return std::ranges::equal(tail, suffix, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

// 按单字符分隔符切分，返回视图（不拷贝）；连续分隔符会产生空片段
[[nodiscard]] inline std::vector<std::string_view> Split(std::string_view s, char delim) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const auto pos = s.find(delim, start);
        if (pos == std::string_view::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

// 字符串 ↔ 数值统一走 from_chars / to_chars（禁止 atoi / sprintf）
[[nodiscard]] inline std::optional<int> ToInt(std::string_view s) noexcept {
    int v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

[[nodiscard]] inline std::optional<double> ToDouble(std::string_view s) noexcept {
    double v = 0.0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

// 整型 → 字符串（to_chars，禁止 sprintf/itoa）
template <std::integral T>
[[nodiscard]] inline std::string FromInt(T v) {
    char buf[32] = {};
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, r.ptr);
}

[[nodiscard]] inline std::string FromDouble(double v) {
    std::string out(32, '\0');
    const auto r = std::to_chars(out.data(), out.data() + out.size(), v);
    out.resize(static_cast<std::size_t>(r.ptr - out.data()));
    return out;
}

} // namespace shine::util
