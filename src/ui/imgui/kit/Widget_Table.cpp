#include "ui/imgui/kit/Widget_Table.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Widget_Badge.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

#include <algorithm>
#include <string>

namespace shine::kit {

// ------------------------------------------------------------------ 21 DataTable
float TableHeaderHeight(bool compact) {
    // th padding 8px 12px（.compact 是 7px 8px）+ 11.5px 字高 + 1px 下边。
    return (compact ? 7.0f : 8.0f) * 2.0f + 14.0f + 1.0f;
}

float TableRowHeight(bool compact) {
    // td padding 9px 12px（.compact 7px 8px）+ 13px 字高（.compact 12）+ 1px 下边。
    return (compact ? 7.0f : 9.0f) * 2.0f + (compact ? 14.0f : 16.0f) + 1.0f;
}

namespace {

// 列 x 偏移表：width<=0 的列平分剩余宽度（CSS 的 table-layout: auto 近似）。
std::vector<float> TableColumnOffsets(const std::vector<TableColumn>& columns, float available) {
    std::vector<float> offsets(columns.size() + 1, 0.0f);
    int flexible = 0;
    float fixed = 0.0f;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].width > 0.0f) {
            fixed += columns[i].width;
        } else {
            ++flexible;
        }
    }
    const float each = flexible > 0 ? std::max(0.0f, available - fixed) / flexible : 0.0f;
    float x = 0.0f;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        offsets[i] = x;
        x += columns[i].width > 0.0f ? columns[i].width : each;
    }
    offsets[columns.size()] = x;
    return offsets;
}

} // namespace

float DataTable(ImDrawList* draw, Rect bounds, const std::vector<TableColumn>& columns,
                const std::vector<TableRow>& rows, TableSort& sort, bool compact,
                std::string_view id) {
    if (columns.empty()) {
        return 0.0f;
    }
    const float padX = compact ? 8.0f : 12.0f;
    const float headH = TableHeaderHeight(compact);
    const float rowH = TableRowHeight(compact);
    const float fontSize = compact ? 12.0f : 13.0f;
    const std::vector<float> offsets = TableColumnOffsets(columns, bounds.width());

    ImFont* headFont = FontBoldAt(11.5f);
    // th 底色 bg-panel + position:sticky（z-index:2）—— 先铺满表头高度，
    // 滚动时下面 td 从它底下过去。
    draw->AddRectFilled(bounds.min, ImVec2(bounds.max.x, bounds.min.y + headH), ColorPanel());
    float y = bounds.min.y;
    for (std::size_t c = 0; c < columns.size(); ++c) {
        const TableColumn& column = columns[c];
        const float x0 = bounds.min.x + offsets[c] + padX;
        const float cellWidth = offsets[c + 1] - offsets[c] - padX * 2.0f;
        if (cellWidth <= 0.0f) {
            continue;
        }
        // th 可点排序：点同一列在升/降之间翻，点新列切成降序（设计稿是单向的 ↓，
        // Overview.jsx:96 只在选中时追加 ' ↓'，所以这里用三角方向表达即可）。
        // ⚠️ 先处理点击再算 sorted：反过来的话本帧的三角/配色会滞后一帧，
        // 视觉上就是「点下去没反应，下一帧才亮」。
        if (column.sortable) {
            const Rect hitRect = RectAt(bounds.min.x + offsets[c], y, offsets[c + 1] - offsets[c],
                                        headH);
            if (detail::HitTestItem(hitRect, detail::UniqueId(id, static_cast<int>(c))).clicked) {
                if (sort.column == static_cast<int>(c)) {
                    sort.ascending = !sort.ascending;
                } else {
                    sort.column = static_cast<int>(c);
                    sort.ascending = false;
                }
            }
        }
        const bool sorted = sort.column == static_cast<int>(c);
        // 排序中的表头转 accent（设计稿没有这条，但排序列不给任何视觉反馈的话
        // 点完看不出排到哪去了 —— 用的是 .tabs > button.on 的既有语义，不新增颜色档）。
        const ImU32 fg = sorted ? ColorAccent() : ColorTextMuted();
        // 原来写死 `y + (compact ? 7.0f : 8.0f)` —— 那个值是 `.table th` 的
        // **padding**，被当成了文字偏移。表头高 31（compact 29）时偏上 1.75px。
        // 下面的排序小三角取 `textY + 6.0f`，会跟着一起下移，与文字的相对关系不变。
        const float textY = CenterTextY(headFont, 11.5f, y + headH * 0.5f);
        // 只量一次（MeasureClipped 不绘制）。之前这里用 DrawTextClipped 拿宽度，
        // 标题就被画了两遍 —— 第二遍还带三角占位，裁剪宽度和第一遍不一致。
        const float textWidth = MeasureClipped(headFont, 11.5f, cellWidth, column.title);
        const float textX = column.centered
                                ? bounds.min.x + offsets[c] +
                                      (offsets[c + 1] - offsets[c] - textWidth) * 0.5f
                                : x0;
        DrawTextClipped(draw, headFont, 11.5f, ImVec2(textX, textY),
                        column.centered ? textWidth : cellWidth, fg, column.title);
        if (sorted) {
            // 小三角而不是 '↓' 字符：U+2193 不在字体图集里（见 Widget_Table.h 注释）。
            const float tx = std::min(textX + textWidth + 4.0f, bounds.max.x - padX - 8.0f);
            const float cy = textY + 6.0f;
            const float dir = sort.ascending ? -1.0f : 1.0f;
            ImVec2 tri[3] = {ImVec2(tx + 3.0f, cy + 2.0f * dir), ImVec2(tx + 7.0f, cy + 2.0f * dir),
                             ImVec2(tx + 5.0f, cy - 2.0f * dir)};
            // PathFillConvex 只吃已经 PathLineTo 进去的点（现版 ImGui 的签名是
            // PathFillConvex(ImU32)，不是 ImGui 老版本的 (点数组, 个数, 色)）。
            draw->PathLineTo(tri[0]);
            draw->PathLineTo(tri[1]);
            draw->PathLineTo(tri[2]);
            draw->PathFillConvex(ColorAccent());
        }
    }
    // th 下边 line-normal
    draw->AddLine(ImVec2(bounds.min.x, y + headH - 0.5f), ImVec2(bounds.max.x, y + headH - 0.5f),
                  ColorLineNormal(), 1.0f);
    y += headH;

    for (std::size_t r = 0; r < rows.size(); ++r) {
        const TableRow& row = rows[r];
        const Rect rowRect = RectAt(bounds.min.x, y, bounds.width(), rowH);
        const Hit hit = detail::HitTestItem(rowRect,
                                           detail::UniqueId(std::string(id) + "#r", static_cast<int>(r)));
        if (row.selected) {
            // tr.sel（ui.css:753-759）：fill-selected 底 + inset 2px 0 0 accent 左侧条。
            draw->AddRectFilled(rowRect.min, rowRect.max, ColorFillSelected());
            draw->AddRectFilled(ImVec2(rowRect.min.x, rowRect.min.y),
                                ImVec2(rowRect.min.x + 2.0f, rowRect.max.y), ColorAccent());
        } else if (hit.hovered) {
            draw->AddRectFilled(rowRect.min, rowRect.max, ColorFillHover());
        }
        for (std::size_t c = 0; c < columns.size() && c < row.cells.size(); ++c) {
            const TableColumn& column = columns[c];
            const float x0 = rowRect.min.x + offsets[c] + padX;
            const float cellWidth = offsets[c + 1] - offsets[c] - padX * 2.0f;
            if (cellWidth <= 0.0f) {
                continue;
            }
            const std::string& cell = row.cells[c];
            // `tag` 列：画小号 Tag 而不是文字。Tag 自带量宽与色相，按单元格
            // 左内边距摆、垂直**居中在行中心**（原来页面层写 `y + 6.0f`，
            // 行高 29 时偏上 0.5px，且那两列的色相要调用方自己传）。
            if (column.tag) {
                const theme::Tone tone =
                    static_cast<std::size_t>(c) < row.tones.size() ? row.tones[c] : theme::Tone::Idle;
                Tag(draw, RectAt(x0, rowRect.center().y - TagHeight(true) * 0.5f,
                                 std::min(TagWidth(cell, true, false), cellWidth), TagHeight(true)),
                    cell, tone, /*small=*/true);
                continue;
            }
            // `.table .num`（ui.css:760-764）：等宽 11.5 muted。
            // 选中行的 td 转 text-primary（ui.css:757），但 .num 保持 muted ——
            // 数字列的低对比是刻意的，等宽小字转正色会和主文本抢层级。
            // `mono` 是同一字形但保留 secondary（规则码那一列）。
            ImFont* font = (column.numeric || column.mono) ? MonoAt(11.5f) : FontAt(fontSize);
            const float size = (column.numeric || column.mono) ? 11.5f : fontSize;
            ImU32 fg = column.numeric  ? ColorTextMuted()
                       : column.mono  ? (row.selected ? ColorText() : ColorAccent())
                                       : (row.selected ? ColorText() : ColorTextSecondary());
            // 原来写死 `y + (compact ? 7.0f : 9.0f)` —— 同样是 `.table td` 的
            // **padding** 而不是文字偏移，行高 35（compact 29）时按实际字号
            // 13 / 12 / 11.5 分别偏上 2.0 / 1.5 / 2.75px。
            // ⚠️ 必须排在 font/size 之后：数字列字号是 11.5，用外层 fontSize
            //    居中会把等宽小字再推低 0.25px。
            const float textY = CenterTextY(font, size, rowRect.center().y);
            if (column.centered) {
                const float w = font->CalcTextSizeA(size, 1e9f, 0.0f, cell.data(),
                                                    cell.data() + cell.size())
                                    .x;
                draw->AddText(font, size,
                              ImVec2(rowRect.min.x + offsets[c] +
                                         (offsets[c + 1] - offsets[c] - w) * 0.5f,
                                     textY),
                              fg, cell.data(), cell.data() + cell.size());
            } else {
                DrawTextClipped(draw, font, size, ImVec2(x0, textY), cellWidth, fg, cell);
            }
        }
        // td 下边 line-subtle
        draw->AddLine(ImVec2(rowRect.min.x, rowRect.max.y - 0.5f),
                      ImVec2(rowRect.max.x, rowRect.max.y - 0.5f), ColorLineSubtle(), 1.0f);
        y += rowH;
    }
    return y - bounds.min.y;
}

} // namespace shine::kit
