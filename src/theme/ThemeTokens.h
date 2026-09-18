#pragma once
// shine::theme —— 主题 token 表 + 用户自定义配色（R-S7）
//
// 目的：把 `ThemeColors` 的 25 个颜色从"写死的字段"变成**可遍历、可编辑、可保存**的数据，
// 这样设置窗口能自动列出所有颜色（加新 token 不用改 UI 代码），用户也能存下自己喜欢的配色。
//
// 分层：
//   * 本文件**只依赖** `theme/Theme.h`（预设与 ApplyTheme）+ `core/Settings.h`（存盘字段），
//     不碰 ImGui / 不碰 App —— UI 在 `app/dialogs/DrawSettingsWindow.cpp` 里遍历 `Tokens()`。
//   * 覆盖值存内存 + 字符串（`themeCustomColors`），格式 `name=#RRGGBBAA;name=#RRGGBBAA`
//     （**刻意不用 JSON**：少一个解析依赖、没有转义问题、肉眼可读、以后加 token 向后兼容）。
#include "theme/Theme.h"

#include <array>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace shine::theme {

struct ThemeToken {
    const char* name;                  // 与配置字符串里的键一致（"accent" …）
    const char* group;                 // 设置窗口分组折叠用（"基础" / "强调与状态" / "控件" / "标签页"）
    float (ThemeColors::*member)[4];   // 指向 25 个 token 之一（注意是 float[4] 数组，不是 float）
};

[[nodiscard]] std::span<const ThemeToken> Tokens();
[[nodiscard]] const ThemeToken* FindToken(std::string_view name) noexcept;

// —— 自定义配色（内存态）——
[[nodiscard]] const std::map<std::string, std::array<float, 4>>& Overrides();
void SetOverride(std::string_view name, const std::array<float, 4>& rgba); // 只改内存，调用方随后 ReapplyTheme()
void ClearOverride(std::string_view name);
void ClearOverrides();
[[nodiscard]] std::size_t OverrideCount() noexcept;

// 预设 + 覆盖 → 应用（**立即生效**；不改 Settings）
void ReapplyTheme();

// —— 持久化（字符串 ↔ 覆盖表）——
[[nodiscard]] std::string ExportOverrides();          // 只导出被改过的 token
void ApplyOverrides(std::string_view text, bool reapply = true); // 解析并套用；非法项忽略

// —— 颜色文本工具（UI 用）——
[[nodiscard]] std::string ToHex(const float rgba[4]);                 // "#RRGGBBAA"
[[nodiscard]] bool FromHex(std::string_view hex, float out[4]) noexcept;

} // namespace shine::theme
