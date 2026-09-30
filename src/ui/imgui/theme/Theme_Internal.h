#pragma once
// shine::theme 内部共享契约 —— **不是**公共 API。
//
// 拆分背景：Theme.cpp 原来一个文件塞了十个职责族（全局状态 / 派生色预混 /
// 十六进制编解码 / 色值数学 / token 字段反射 / 主题身份 / 访问器 / 阴影几何 /
// 应用到 ImGuiStyle / 持久化 + 自检），750 行、跨 400 行以上连续互不相干的
// 逻辑。按职责拆成 Theme_Color / Theme_Derived / Theme_Parse 三个文件后，它们
// 之间唯一需要共享的就是本文件里的这几样。
//
// 公共契约仍然是 Theme.h —— 其它层只该 include 那个。这个头只在 theme/ 内部用。

#include "ui/imgui/theme/Theme.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace shine::theme::detail {

// 一套主题的完整记录：主色 + 加载期预混的派生色 + 是否成功载入。
struct ThemeRecord {
    ColorToken colors;
    Derived derived;
    bool loaded = false;
};

inline constexpr std::size_t IndexOf(ThemeId id) { return static_cast<std::size_t>(id); }

// 5 套主题的全局表。**定义只有一份**，在 Theme.cpp；这里只声明。
// 之前它们在 Theme.cpp 的匿名 namespace 里，拆文件后匿名 namespace 做不到
// 跨 TU 共享，所以提到具名的 detail 下并用 extern 声明。
extern std::array<ThemeRecord, 5> g_themes;
extern ThemeId g_current;
extern bool g_loaded;

// ---- 十六进制色值编解码（实现见 Theme_Color.cpp）----
// "#RRGGBB" / "#RRGGBBAA" → 0xRRGGBBAA（6 位按不透明处理，与 Qt 侧契约一致）
bool ParseHexColor(std::string_view text, std::uint32_t& out);
// 点分名 → ColorToken 字段。31 项逐个列出，与 kColorTokenNames 同序。
bool AssignByName(std::string_view name, std::uint32_t value, ColorToken& out);
// 0xRRGGBBAA → "#RRGGBBAA"（自检与持久化用）
std::string ToHex(std::uint32_t rgba);

// ---- 派生色预混（实现见 Theme_Derived.cpp）----
// 逐主题算一遍 kGlassTriples / kGradEnds 寻址的那批派生色。
void ComputeDerived(ThemeRecord& record, ThemeId id);

// ---- 单个主题 JSON 解析（实现见 Theme_Parse.cpp）----
bool ParseThemeFile(const std::filesystem::path& path, ThemeId id);

} // namespace shine::theme::detail
