#pragma once
// shine::kit::Widget_Badge —— 徽标系（Tag / StatusDot / Kbd）
//
// 从 kit/Widgets.cpp 拆出。三个都是**无命中**的小自绘标记：
// Tag 是胶囊标签，StatusDot 是状态圆点，Kbd 是按键提示。

#include "ui/imgui/kit/Widget_Core.h"
#include "ui/imgui/theme/Theme.h"

#include <string_view>

namespace shine::kit {

// ---- 3. Tag（UI.jsx:32）----
// h20 pad 0 8 r-pill 11.5/600 1px 边；sm h17 pad 0 6 10.5；7 色调；可选 7px 圆点
void Tag(ImDrawList* draw, Rect bounds, std::string_view label, theme::Tone tone, bool small = false,
         bool dot = false, bool busyPulse = false);
[[nodiscard]] float TagWidth(std::string_view label, bool small, bool dot);
[[nodiscard]] float TagHeight(bool small);

// ---- 4. StatusDot（UI.jsx:42）----
// 7×7 圆；7 色调；run 时 1.6s 脉冲环
void StatusDot(ImDrawList* draw, ImVec2 center, theme::Tone tone, bool run);

// ---- 5. Kbd（UI.jsx:47）----
// min-w18 h18 pad 0 5 r4，1px 边 + 下边 2px，10.5/600
void Kbd(ImDrawList* draw, Rect bounds, std::string_view label);
[[nodiscard]] float KbdWidth(std::string_view label);

// ---- 5b. LogoMark（品牌标：顶栏 `.brand .mark` / 项目中心 `.hub-logo`）----
// `--grad-accent` 120° 斜渐变底 + 居中 play 图标 + `--shadow-accent`（0 4px 20px）柔光。
// 两处只有尺寸不同：顶栏 22×22 / r6 / 图标 12（shell.css:39-53），
//                     项目中心 52×52 / r-lg(14) / 图标 26（views.css:25-40）。
//
// ⚠️ 水墨主题下 tokens.css:202-207 把这两处**一起**覆盖成
//    「纯 `--accent` 实底 + `box-shadow: none`」—— 不是换个色 token 就完事，
//    而是换了一种画法（渐变→实色、投影→不画）。判断走 `theme::ThemeLogoHasShadow()`。
//
// ⚠️ 投影用投影档位表的 `Accent` 档，**不要**再用一圈 `accent-glow` 描边去近似：
//    这一处原来是两个页面各自画的描边环（±2/半径+2/线宽 2 与 ±4/半径+4/线宽 8），
//    同一个 token 被近似成两个不同形状，而且都跟 CSS 的 blur 半径无关。
//    档位表里 `Accent` 的几何就是 `--shadow-accent` 原文（0 4px 20px，逐主题核对过）。
void LogoMark(ImDrawList* draw, Rect bounds, float radius, float iconSize);

} // namespace shine::kit
