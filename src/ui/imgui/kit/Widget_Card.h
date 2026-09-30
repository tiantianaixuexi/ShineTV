#pragma once
// shine::kit::Widget_Card —— 容器卡（Card）
//
// 从 kit/Widgets.cpp 拆出。

#include "ui/imgui/kit/Widget_Core.h"

#include <string_view>

namespace shine::kit {

// ---- 6. Card（UI.jsx:52）----
// bg-panel + 1px line-subtle + r10；头 pad 12/16 + 下边框；体 pad 16；
// hover = line-strong + 上浮 2px；glow = accent 边 + accent 辉光
// 返回内容区（头之下、体之内）。
Rect Card(ImDrawList* draw, Rect bounds, std::string_view title, std::string_view icon,
          bool hoverable, bool glow);
// 头部右侧的自绘控件区（extra 按钮放这里）。
[[nodiscard]] Rect CardHeaderRow(Rect card, std::string_view title, std::string_view icon);

// `.card.hoverable:hover { transform: translateY(-2px) }`（ui.css:196-204）的位移量。
//
// 走 `kit::Card()` 的卡片自动就有；**手写卡片必须自己加上它** —— 这就是这个函数
// 存在的唯一理由：页面里那几处手写 `DrawShadowed` 的卡片（资产总览网格卡、
// 项目中心项目卡、分镜时间线上的 `.tl-card`）都漏了这条位移，而它们的 hover
// 只改了边框与投影。只补边框不补位移，等于「半条 hover」：鼠标上去卡片不动，
// 而设计稿里它要浮起来 2px。
//
// ⚠️ 位移只影响**绘制**，不要把它算进命中矩形：`Card()` 就是先按原 `bounds` 命中、
//    再对绘制矩形加 lift。命中区跟着位移会导致鼠标停在边缘时状态来回抖。
[[nodiscard]] float CardHoverLift(bool hovered, bool hoverable);

} // namespace shine::kit
