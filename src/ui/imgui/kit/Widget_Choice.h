#pragma once
// shine::kit::Widget_Choice —— 单选系（Segmented / Tabs / Chip）
//
// 从 kit/Widgets.cpp 拆出。三者共享同一份契约：**返回本帧被选中的 value**，
// 展开态不由组件持有（与 Views.h 的约定一致）。Segmented 与 Tabs 还共用
// SegmentOption 这一组选项结构。

#include "ui/imgui/kit/Widget_Core.h"

#include <string_view>
#include <vector>

namespace shine::kit {

// ---- 7. Segmented（UI.jsx:69）----
// 容器 pad3 gap2 fill-muted r6 1px；项 h26 pad 0 13 r4 12.5/600；
// 选中 bg-elevated + shadow + 4px accent 圆点
struct SegmentOption {
    std::string_view value;
    std::string_view label;
};
// 返回被选中的 value（点击时更新 *value）。
std::string_view Segmented(ImDrawList* draw, Rect bounds,
                           const std::vector<SegmentOption>& options, std::string_view value,
                           std::string_view id);
[[nodiscard]] float SegmentedWidth(const std::vector<SegmentOption>& options);

// ---- 7b. Chip（views.css:670-706）----
// 默认 h26 pad 0 11 r-pill 1px line-normal 12/600 secondary；hover line-strong + primary；
// 选中 accent-dim 底 + accent-glow 边 + accent 字。count 非空时右侧画 .cnt
// （10.5px / pad 0 6 / r-pill / fill-muted 底 muted 字；选中态底换成 accent 18%）。
// compact 是资产侧栏的内联覆写（h22 pad 0 8 11px，Shell.jsx:540）——
// 设计稿那处是写死的 style，不是另一个变体，所以做成参数而不是第二份规格。
struct ChipSpec {
    bool selected = false;
    bool compact = false;
    std::string_view count;  // 空 = 不画计数
};
// 返回 true = 本帧被点击。
bool Chip(ImDrawList* draw, Rect bounds, std::string_view label, const ChipSpec& spec,
          std::string_view id);
[[nodiscard]] float ChipHeight(bool compact);
// 量宽：pad×2 + 字宽 + (count ? gap + count 宽 : 0)。
[[nodiscard]] float ChipWidth(std::string_view label, const ChipSpec& spec);

// ---- 8. Tabs（UI.jsx:86）----
// 项 pad 8/12 13/600；选中 accent + 2px 下划线（左右各内缩 10px）
std::string_view Tabs(ImDrawList* draw, Rect bounds, const std::vector<SegmentOption>& tabs,
                      std::string_view value, std::string_view id);

} // namespace shine::kit
