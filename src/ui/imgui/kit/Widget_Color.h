#pragma once
// shine::kit::Widget_Color —— 主题取色与颜色运算
//
// 从 kit/Widgets.cpp 拆出。这一族**不含任何控件**，只是「颜色从哪来」的唯一出口：
// 控件内部用，页面不要直接拼 ImU32 / ImVec4 字面量（tools\check-colors.ps1 门禁）。

#include <imgui.h>

#include <cstdint>

namespace shine::kit {

// 主题相关的常用取色（控件内部用，页面不要直接拼 ImU32）。
[[nodiscard]] ImU32 ColorOf(std::uint32_t rgba);
[[nodiscard]] ImU32 ColorText();
[[nodiscard]] ImU32 ColorTextSecondary();
[[nodiscard]] ImU32 ColorTextMuted();
[[nodiscard]] ImU32 ColorTextInverse();
[[nodiscard]] ImU32 ColorSurface();
[[nodiscard]] ImU32 ColorPanel();
[[nodiscard]] ImU32 ColorElevated();
[[nodiscard]] ImU32 ColorOverlay();
[[nodiscard]] ImU32 ColorVoid();
[[nodiscard]] ImU32 ColorFillHover();
[[nodiscard]] ImU32 ColorFillSelected();
[[nodiscard]] ImU32 ColorFillMuted();
[[nodiscard]] ImU32 ColorLineSubtle();
[[nodiscard]] ImU32 ColorLineNormal();
[[nodiscard]] ImU32 ColorLineStrong();
[[nodiscard]] ImU32 ColorAccent();
[[nodiscard]] ImU32 ColorAccentHover();
[[nodiscard]] ImU32 ColorAccentFg();
[[nodiscard]] ImU32 ColorAccentDim();
[[nodiscard]] ImU32 ColorAccentGlow();
[[nodiscard]] ImU32 ColorFocusRing();
[[nodiscard]] ImU32 ColorScrim();

// 全透明。**不是主题色，是「不画」** —— 之所以要有这个出口：`IM_COL32(0,0,0,0)`
// 是源码里最常见的硬编码颜色字面量（透明滚动条底、hover 时「不画底」），而
// `tools/check-colors.ps1` 按规则禁止硬编码颜色。没有这个函数，唯一的合规写法
// 就是给每处加 `// theme-ok` 豁免 —— 豁免一旦多了就等于没有门禁。
[[nodiscard]] ImU32 ColorTransparent();

// 半透明**调制**：结果 alpha = 原 alpha × alpha。传 1.0 保持原样。
// 这是给「已经在派生色里带了 alpha」的场景用的（tagBg 12% / gateBg 10% / accentGlow 30%），
// 参数只用来做动画调制（脉冲、hover 渐变），不要用它把 12% 的淡底拉成 100% 实心。
[[nodiscard]] ImU32 WithAlpha(ImU32 color, float alpha);

// 半透明**覆盖**：直接把 alpha 设成给定值。用于实色补透明度（阴影、scrim、叠层）。
[[nodiscard]] ImU32 WithAlphaSet(ImU32 color, float alpha);
[[nodiscard]] ImU32 LerpColorTo(ImU32 from, ImU32 to, float t);

} // namespace shine::kit
