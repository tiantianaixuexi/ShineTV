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

} // namespace shine::kit
