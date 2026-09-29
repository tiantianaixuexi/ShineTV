#pragma once
// shine::theme —— 主题数据层（P02-S1）：5 套主题 JSON + 反射序列化 + theme::Current()
//
// 判据（P02-S1）：① 每个 token 有 5 个取值（深空 / 薄暮 / 纸墨 / 水墨 / 极夜 五份 JSON）；
//                ② JSON 可被 C++26 反射序列化往返（ColorToken ↔ {"bg.void":"#RRGGBBAA",…}）。
// 切换 / 持久化 / 200ms 交叉淡入在 ThemeService（S3）；Token 结构与几何常量见 Token.h。
#include "ui/kit/theme/Token.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace shine::theme {

// 内置主题全集。**下标 == ThemeId 枚举值**（Theme.cpp 的 g_themes / ThemeService 的
// QSS 缓存都按这个下标寻址），新增主题必须同时在这两处扩表，否则切换到末位主题会越界。
inline constexpr std::array<ThemeId, 5> kAllThemes = {
    ThemeId::DeepSpace, ThemeId::Dusk, ThemeId::PaperInk, ThemeId::InkWash, ThemeId::PolarNight};

// 主题文件名（= 显示名；文件在 src/ui/kit/theme/Themes/<名字>.json）
[[nodiscard]] std::string_view ThemeFileName(ThemeId id);
[[nodiscard]] std::string_view ThemeDisplayName(ThemeId id);
[[nodiscard]] bool ThemeIdFromName(std::string_view name, ThemeId& out);

// 载入目录下的 5 套主题 JSON（幂等；false = 有主题缺失/解析失败，日志有明细）
[[nodiscard]] bool LoadThemesFrom(const std::filesystem::path& dir);

[[nodiscard]] const ColorToken& ThemeColorsOf(ThemeId id);
[[nodiscard]] ThemeId CurrentThemeId() noexcept;
void SetCurrentTheme(ThemeId id) noexcept;
[[nodiscard]] const ColorToken& Current();

// —— 每主题字体族（webui tokens.css 的 --font-ui）——
// ⚠️ 为什么字体**不进 ColorToken**：ColorToken 的字段顺序就是 QSS 的 %N 占位符顺序，
// 也是 ColorTokenToJson/FromJson 的键序（两者必须逐位一致，插入即错位）。
// 字体是**字符串**不是色值，硬塞进去会让「每个 token 都被 QSS 消费」的自检判据
// 失真，也会让 5 套主题 JSON 的 31 个色值契约多一个非颜色字段。
// 故另立一张按下标寻址的小表，与 kAllThemes 同序；自定义主题没有字体覆盖，
// 回落 :root 的 --font-ui。序列化契约与 %N 顺序因此零改动。
[[nodiscard]] std::string_view ThemeFontFamilyOf(ThemeId id);
[[nodiscard]] std::string_view CurrentFontFamily();

// ColorToken → kColorTokenCount 值数组（序 = kColorTokenNames；QssBuilder 的 %N 占位用）
[[nodiscard]] std::array<std::uint32_t, kColorTokenCount> TokenValues(const ColorToken& c);

// 反射序列化：字段名取 kColorTokenNames 点分名，色值 #RRGGBBAA（6 位读入按不透明处理）
[[nodiscard]] std::string ColorTokenToJson(const ColorToken& c);
[[nodiscard]] bool ColorTokenFromJson(std::string_view json, ColorToken& out);
// 宽容读（自定义主题迁移用）：只覆盖 JSON 里给到的键，缺键保留 base 传入的内置值。
// 返回命中的键数（== kColorTokenCount 才算完整主题）。
[[nodiscard]] std::size_t ColorTokenFromJsonPartial(std::string_view json, ColorToken& out);

// S1 自检（SHINE_THEME_SELFTEST=1 触发）：5×kColorTokenCount 取值齐 + 往返相等
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
