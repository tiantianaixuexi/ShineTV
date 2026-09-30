#pragma once
// shine::kit::Widget_Status —— 状态展示系（Progress / Empty / KeyValues / Spinner / Divider）
//
// 从 kit/Widgets.cpp 拆出。这一族全是**无命中**的纯展示：进度、空态、键值表、
// 转圈、分隔线。

#include "ui/imgui/kit/Widget_Core.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::kit {

// ---- 15. Progress（UI.jsx:145）----
// 轨 h6（thin 4）r-pill；填充 grad-accent；run 时叠 100° 白 35% 微光扫过
void Progress(ImDrawList* draw, Rect bounds, float value, bool run, bool thin);
[[nodiscard]] float ProgressHeight(bool thin);

// ---- 16. Empty（UI.jsx:154）----
// 居中 gap10 pad 40/20；字形 52×52 r14 虚线边 + 24px 图标 + 4s 上下浮动；
// 标题 13/600；正文 max-w 320
void Empty(ImDrawList* draw, Rect bounds, std::string_view icon, std::string_view title,
           std::string_view body);

// ---- 17. KV（UI.jsx:165）----
// 网格 auto 1fr gap 6px 14px，12.5；键 muted，值 primary/500
void KeyValues(ImDrawList* draw, Rect bounds,
               const std::vector<std::pair<std::string, std::string>>& rows);

// ---- 18. Spinner（ui.css:457）----
// 14×14 圆环 2px 边（line-normal），顶边 accent，0.7s/圈；sm 11×11 边宽 1.5。
// 组件画廊（Gallery.jsx:96-97）与 StageFlow/Overlays 的「运行中」都用这一档。
// 画在中心点：外接圆 = bounds（直径即 size），描边向内吃 thickness。
void Spinner(ImDrawList* draw, ImVec2 center, bool small);
// 按 .spin / .spin.sm 的直径返回占位边长（布局用它预留空间）。
[[nodiscard]] float SpinnerSize(bool small);

// ---- 20. Divider（ui.css:134-138 .msep）----
// 1px line-subtle 水平发丝线，上下各留 5px（.msep 的 margin）。菜单与分区之间用。
void Divider(ImDrawList* draw, Rect bounds);

} // namespace shine::kit
