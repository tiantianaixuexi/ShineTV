#pragma once
// shine::theme —— 主题数据层（P02-S1）：4 套主题 JSON + 反射序列化 + theme::Current()
//
// 判据（P02-S1）：① 每个 token 有 4 个取值（深空 / 薄暮 / 纸墨 / 极夜 四份 JSON）；
//                ② JSON 可被 C++26 反射序列化往返（ColorToken ↔ {"bg.void":"#RRGGBBAA",…}）。
// 切换 / 持久化 / 200ms 交叉淡入在 ThemeService（S3）；Token 结构与几何常量见 Token.h。
#include "widget/theme/Token.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace shine::theme {

inline constexpr std::array<ThemeId, 4> kAllThemes = {
    ThemeId::DeepSpace, ThemeId::Dusk, ThemeId::PaperInk, ThemeId::PolarNight};

// 主题文件名（= 显示名；文件在 src/widget/theme/Themes/<名字>.json）
[[nodiscard]] std::string_view ThemeFileName(ThemeId id);
[[nodiscard]] std::string_view ThemeDisplayName(ThemeId id);
[[nodiscard]] bool ThemeIdFromName(std::string_view name, ThemeId& out);

// 载入目录下的 4 套主题 JSON（幂等；false = 有主题缺失/解析失败，日志有明细）
[[nodiscard]] bool LoadThemesFrom(const std::filesystem::path& dir);

[[nodiscard]] const ColorToken& ThemeColorsOf(ThemeId id);
[[nodiscard]] ThemeId CurrentThemeId() noexcept;
void SetCurrentTheme(ThemeId id) noexcept;
[[nodiscard]] const ColorToken& Current();

// ColorToken → 22 值数组（序 = kColorTokenNames；QssBuilder 的 %N 占位用）
[[nodiscard]] std::array<std::uint32_t, 22> TokenValues(const ColorToken& c);

// 反射序列化：字段名取 kColorTokenNames 点分名，色值 #RRGGBBAA（6 位读入按不透明处理）
[[nodiscard]] std::string ColorTokenToJson(const ColorToken& c);
[[nodiscard]] bool ColorTokenFromJson(std::string_view json, ColorToken& out);

// S1 自检（SHINE_THEME_SELFTEST=1 触发）：4×22 取值齐 + 往返相等
[[nodiscard]] bool SelfTestRoundTrip(std::string* report);

// —— 自定义主题（P02-S8）：样式编辑器另存 → 出现在主题菜单 → 重启仍可选 ——
[[nodiscard]] std::size_t CustomThemeCount();
[[nodiscard]] std::string CustomThemeName(std::size_t i);
[[nodiscard]] const ColorToken& CustomThemeColors(std::size_t i);
[[nodiscard]] bool LoadCustomThemes(const std::filesystem::path& dir); // 幂等重扫
[[nodiscard]] bool SaveCustomTheme(const std::filesystem::path& dir, const std::string& name,
                                   const ColorToken& c);
[[nodiscard]] bool ActivateCustom(const std::string& name); // false = 无此自定义主题
[[nodiscard]] bool CurrentIsCustom() noexcept;
[[nodiscard]] std::string CurrentCustomName();
void RevertToBuiltin(); // 内置主题接管（预览取消 / 切回内置时调用）

} // namespace shine::theme
