#pragma once
// shine::gallery::GalleryLayout —— 缩略图网格几何（G-S6 S2 / G-S9 S1 虚拟化）
//
// 纯计算、无 ImGui：GalleryView 只消费这里的结果。
#include <cstddef>

namespace shine::gallery {

struct GridMetrics {
    float thumbSide = 256.f; // 单元格内缩略图边长（已含 zoom）
    float cellW = 0.f;       // 含 gap 的列宽
    float cellH = 0.f;       // 含标签与 gap 的行高
    float gap = 8.f;
    float labelH = 34.f; // 文件名区
    float pad = 4.f;     // 单元格内边距（悬停描边用）
    int columns = 1;
    int rows = 0;
    std::size_t count = 0;
};

struct VisibleRange {
    std::size_t begin = 0; // 含
    std::size_t end = 0;   // 不含
    [[nodiscard]] bool empty() const noexcept { return begin >= end; }
    [[nodiscard]] std::size_t size() const noexcept { return end > begin ? end - begin : 0; }
};

// 滚动方向：向下多预取下方一行，向上多预取上方一行（G-S9 S1）
enum class ScrollDir : int { None = 0, Up, Down };

[[nodiscard]] GridMetrics ComputeGrid(float availW, float thumbSize, float zoom, std::size_t itemCount,
                                      float gap = 8.f, float labelH = 34.f);

// 按 scrollY + viewportH 算可见行区间（下标范围，非行号）。
// prefetchRows：基础上下各预取行数；dir==Down/Up 时在对应方向再 +1 行。
// prefetchRows=0 且 dir=None → 仅当前可见（不含预取）。
[[nodiscard]] VisibleRange ComputeVisible(const GridMetrics& m, float scrollY, float viewportH,
                                          std::size_t prefetchRows = 1, ScrollDir dir = ScrollDir::None);

// localX/Y 相对网格内容原点（第一格左上角）；命中失败返回 `m.count`（哨兵）
[[nodiscard]] std::size_t HitTest(const GridMetrics& m, float localX, float localY);

} // namespace shine::gallery
