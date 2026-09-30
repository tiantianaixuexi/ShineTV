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

} // namespace shine::kit
