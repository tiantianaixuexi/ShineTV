#include "gallery/GalleryLayout.h"

#include <algorithm>
#include <cmath>

namespace shine::gallery {

GridMetrics ComputeGrid(float availW, float thumbSize, float zoom, std::size_t itemCount, float gap, float labelH) {
    GridMetrics m;
    m.count = itemCount;
    m.gap = gap;
    m.labelH = labelH;
    const float z = zoom > 0.01f ? zoom : 1.f;
    m.thumbSide = std::max(16.f, thumbSize * z);
    m.cellW = m.thumbSide + gap;
    m.cellH = m.thumbSide + labelH + gap;
    const float usable = std::max(m.cellW, availW - gap);
    m.columns = std::max(1, static_cast<int>(usable / m.cellW));
    m.rows = itemCount == 0 ? 0 : static_cast<int>((itemCount + static_cast<std::size_t>(m.columns) - 1) /
                                                    static_cast<std::size_t>(m.columns));
    return m;
}

VisibleRange ComputeVisible(const GridMetrics& m, float scrollY, float viewportH, std::size_t prefetchRows,
                            ScrollDir dir) {
    VisibleRange r;
    if (m.count == 0 || m.columns <= 0 || m.cellH <= 0.f) {
        return r;
    }
    const float top = std::max(0.f, scrollY);
    const float bottom = top + std::max(0.f, viewportH);
    const int firstRow = static_cast<int>(std::floor(top / m.cellH));
    const int lastRow = static_cast<int>(std::ceil(bottom / m.cellH)); // 不含
    int preUp = static_cast<int>(prefetchRows);
    int preDown = static_cast<int>(prefetchRows);
    if (dir == ScrollDir::Down) {
        preDown += 1;
    } else if (dir == ScrollDir::Up) {
        preUp += 1;
    }
    const int rowBegin = std::max(0, firstRow - preUp);
    const int rowEnd = std::min(m.rows, lastRow + preDown);
    if (rowEnd <= rowBegin) {
        return r;
    }
    r.begin = static_cast<std::size_t>(rowBegin) * static_cast<std::size_t>(m.columns);
    r.end = std::min(m.count, static_cast<std::size_t>(rowEnd) * static_cast<std::size_t>(m.columns));
    return r;
}

std::size_t HitTest(const GridMetrics& m, float localX, float localY) {
    if (m.count == 0 || m.columns <= 0 || m.cellW <= 0.f || m.cellH <= 0.f) {
        return m.count;
    }
    if (localX < 0.f || localY < 0.f) {
        return m.count;
    }
    const int col = static_cast<int>(localX / m.cellW);
    const int row = static_cast<int>(localY / m.cellH);
    if (col < 0 || col >= m.columns || row < 0 || row >= m.rows) {
        return m.count;
    }
    const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(m.columns) +
                              static_cast<std::size_t>(col);
    return index < m.count ? index : m.count;
}

} // namespace shine::gallery
