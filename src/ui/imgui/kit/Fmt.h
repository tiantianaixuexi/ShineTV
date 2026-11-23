#pragma once
// shine::kit —— 字符串格式化 / 浮点转字符串的**唯一出口**
//
// 两条硬规矩（2026-10-01 定）：
//   · 字符串格式化一律走 fmt（third/fmt，FMT_HEADER_ONLY，shine_core 已 PUBLIC）。
//     不要再手写 `+ 拼接`：改一句文案要动三处引号，还把内存分配散在表达式中间。
//   · 浮点 → 字符串一律走 zmij（third/zmij，fmt 作者的最短表示库）。
//     **不要用 std::to_string(double)** —— 它固定六位小数，设计稿的「3.5s」
//     会被画成「3.500000s」；也不要在页面层写 snprintf("%.1f")。
//
// 两个出口的分工：
//   * Fmt(...)          —— 拼文案（format-string 编译期检查，漏参是编译错）。
//   * Shortest / Fixed  —— 浮点转字符串。界面上要与设计稿口径一致（如时长
//                          「3.5s」一位小数）用 Fixed(v, 1)；诊断 / 日志用 Shortest。
//
// 实现说明：zmij 的最短与 write_fixed 落在 third/zmij/zmij.cc 的显式实例化上
// （CMakeLists 里 shine_core 已编入），头文件里只有声明。

#include <fmt/format.h>
#include <zmij.h>

#include <string>
#include <utility>

namespace shine::kit {

// fmt::format 的薄封装。全树统一从这走，日后换格式化后端只动这一处。
template <typename... Args>
[[nodiscard]] std::string Fmt(fmt::format_string<Args...> f, Args&&... args) {
    return fmt::format(f, std::forward<Args>(args)...);
}

// 最短正确舍入表示（zmij write）：Shortest(1.5) = "1.5"，Shortest(6.0) = "6"。
// ⚠️ 公开入口是 write(out, n, value) 三参版；两参模板住在 zmij::detail 里，
//    由 zmij.cc 显式实例化，不对外。
[[nodiscard]] inline std::string Shortest(double value) {
    char buf[zmij::double_buffer_size];
    return std::string(buf, zmij::write(buf, sizeof(buf), value));
}
[[nodiscard]] inline std::string Shortest(float value) {
    char buf[zmij::float_buffer_size];
    return std::string(buf, zmij::write(buf, sizeof(buf), value));
}

// 定点小数（zmij write_fixed）：Fixed(3.5, 1) = "3.5"，Fixed(6, 1) = "6.0"。
// 与设计稿口径对齐（mock.js 的 dur 全是一位小数）时**必须**用这条，不要用 Shortest。
[[nodiscard]] inline std::string Fixed(double value, int precision) {
    char buf[zmij::buffer_sizes<double>::fixed];
    return std::string(buf, zmij::write_fixed(buf, sizeof(buf), value, precision));
}

} // namespace shine::kit
