#pragma once
// shine::theme —— 主题运行时（5 套 JSON + 派生色 + ImGuiStyle 生成 + 持久化）
//
// 对应 refactor/phases.md P2.1–P2.4、P2.6。关键约定：
//   * 5 个 Themes/*.json 原地复用，格式不改（{"name":…, "colors":{点分名:"#RRGGBBAA"}}）。
//   * 31 个 ColorToken → ImGuiStyle 的映射**一次写全**（ApplyTheme 内），
//     不许用循环凑 —— 漏一项就是某个控件在某主题下颜色错，而且不报错。
//   * color-mix() 的 12 组派生色在主题加载时预混（见 Derived）。
#include "ui/imgui/theme/Tokens.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::theme {

// ---- 色值工具（0xRRGGBBAA ↔ ImVec4 / ImU32）----
[[nodiscard]] ImVec4 Rgba(std::uint32_t rgba);
[[nodiscard]] std::uint32_t PackRgba(const ImVec4& rgba);

// color-mix(in srgb, X N%, transparent) 的等价物：保留 X 的 RGB，alpha = N%。
[[nodiscard]] std::uint32_t MixAlpha(std::uint32_t rgb, float percent);

// 7 色调（ui.css:139-145 的 .tag.ok/.warn/.danger/.busy/.info/.accent/.idle）
enum class Tone { Accent = 0, Info, Ok, Warn, Danger, Busy, Idle };
inline constexpr std::size_t kToneCount = 7;

// 派生色：ImGui 没有 color-mix 助手，淡底/淡边必须在主题加载时算好。
struct Derived {
    std::array<std::uint32_t, kToneCount> tagBg;      // tone 12%
    std::array<std::uint32_t, kToneCount> tagBorder;  // tone 35%
    std::array<std::uint32_t, kToneCount> gateBg;     // tone 10%
    std::array<std::uint32_t, kToneCount> ganttCell;  // tone 12%
    std::array<std::uint32_t, kToneCount> checkRowBg; // tone 10%
    std::uint32_t stageRunBg;      // accent 14%
    std::uint32_t jumpBtnBg;       // accent 22%
    std::uint32_t inputFocusRing;  // accent 14%
    std::uint32_t btnPrimaryShadow;// accent 28%
    std::uint32_t dangerBg;        // danger 12%
    std::uint32_t accentDim;       // 每主题显式值（tokens.css 的 --accent-dim）
    std::uint32_t accentGlow;      // 每主题显式值（tokens.css 的 --accent-glow）
    std::uint32_t glass;           // 每主题显式值（--glass；毛玻璃降级用的实色）
};

// ---- 载入 / 查询 ----
[[nodiscard]] bool LoadThemesFrom(const std::filesystem::path& dir);
[[nodiscard]] const ColorToken& ThemeColorsOf(ThemeId id);
[[nodiscard]] const Derived& ThemeDerivedOf(ThemeId id);
[[nodiscard]] bool ThemesLoaded();

[[nodiscard]] ThemeId CurrentThemeId() noexcept;
void SetCurrentTheme(ThemeId id) noexcept;
[[nodiscard]] const ColorToken& Current();
[[nodiscard]] const Derived& CurrentDerived();

// 每主题字体族：只有水墨换衬线族（tokens.css:195）。
[[nodiscard]] std::string_view ThemeFontFamilyOf(ThemeId id);
[[nodiscard]] bool ThemeUsesSerif(ThemeId id);

// ---- 应用到 ImGui ----
void ApplyTheme(ThemeId id);      // 换 ImGuiStyle + 记下当前主题
void ApplyCurrentTheme();          // 当前主题重铺（P0.4 初始化 / 字体图集重建后）

// 主题无关几何（design-spec §1）：写进 ImGuiStyle 的圆角/间距/边框档。
void ApplyGeometry(ImGuiStyle& style);

// ---- 持久化（webui 没有，Qt 侧有 theme.json → 保留 Qt 的行为）----
[[nodiscard]] bool LoadPersistedTheme(const std::filesystem::path& file);
[[nodiscard]] bool PersistTheme(const std::filesystem::path& file);
[[nodiscard]] std::filesystem::path DefaultThemeFile();

// ---- 自检（SHINE_THEME_SET / SHINE_THEME_SELFTEST）----
[[nodiscard]] bool SelfTestRoundTrip(std::string* report);

} // namespace shine::theme
