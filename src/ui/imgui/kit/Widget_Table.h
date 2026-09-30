#pragma once
// shine::kit::Widget_Table —— 数据表（DataTable）
//
// 从 kit/Widgets.cpp 拆出。

#include "ui/imgui/kit/Widget_Core.h"
#include "ui/imgui/theme/Theme.h"

#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

// ---- 21. DataTable（ui.css:721-768 .table）----
// th：11.5/600 muted + letter-spacing .03em + pad 8/12 + 下边 line-normal + 底 bg-panel；
// td：pad 9/12 + 下边 line-subtle + secondary；行 hover = fill-hover；
// 选中 = fill-selected + **左侧 2px accent 内阴影** + 字转 primary；
// .num = 等宽 11.5 muted；.center = 居中；.compact = pad 7/8 + 12px（views.css:286）。
struct TableColumn {
    std::string_view title;
    float width = 0.0f;   // <= 0 = 按剩余宽度均分
    bool numeric = false; // .table .num：等宽 11.5 muted
    bool centered = false;// .table.center
    bool sortable = false; // th 可点排序（Overview.jsx:96 用「↓」后缀）
    // 等宽但**不**降为 muted（`.table .num` 那一档）。规则码（C1…C12）这类
    // 「可点的标识」要 accent 色，等宽只是字形选择，不是语义降级。
    bool mono = false;
    // 该列的单元格画成**小号 Tag**（状态 / 级别 / 结论）。Tag 自带量宽与色，
    // 按单元格左内边距摆、垂直居中在行中心 —— 调用点不用再排一次 Y。
    bool tag = false;
};
struct TableRow {
    std::vector<std::string> cells;
    bool selected = false;
    // `tag` 列用这一行给色调。与 cells **下标一一对应**（不需要的列留 Idle）。
    std::vector<theme::Tone> tones;
};
struct TableSort {
    int column = -1;      // -1 = 未排序
    bool ascending = true;
};
// 排序指示按设计稿在标题右侧留 12px（' ↓' 的宽度量级），画一个小三角而不是字符：
// 图集里没有 ↓（U+2193 在 GetGlyphRangesChineseSimplifiedCommon 之外，见 Fonts.cpp:272），
// 用字符会渲染成豆腐块 —— 与 Fonts.h 里「不能有豆腐块」是同一条硬判据。
// 返回整表高度（表头 + 所有行），调用方据此决定要不要滚。
float DataTable(ImDrawList* draw, Rect bounds, const std::vector<TableColumn>& columns,
                const std::vector<TableRow>& rows, TableSort& sort, bool compact,
                std::string_view id);
[[nodiscard]] float TableHeaderHeight(bool compact);
[[nodiscard]] float TableRowHeight(bool compact);

} // namespace shine::kit
