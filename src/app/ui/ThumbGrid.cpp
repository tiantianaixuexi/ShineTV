#include "app/ui/ThumbGrid.h"

#include "app/ui/Widgets.h"
#include "theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace shine::app::ui {
namespace {

[[nodiscard]] ImU32 ThemeCol(const float* c, float alphaMul = 1.f) {
    return ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], c[3] * alphaMul));
}

struct Geom {
    int columns = 1;
    int rows = 0;
    float cellW = 0.f;
    float cellH = 0.f;
    float labelArea = 0.f;
};

[[nodiscard]] Geom ComputeGeom(float availW, const ThumbGridStyle& style, std::size_t count) {
    Geom g;
    const float thumb = std::max(24.f, style.thumbSide);
    const float gap = std::max(0.f, style.gap);
    const float pad = std::max(0.f, style.pad);
    g.labelArea = std::max(style.labelLineH, style.labelLineH * static_cast<float>(std::max(1, style.labelLines))) + 4.f;
    g.cellW = thumb + pad * 2.f + gap;
    g.cellH = thumb + pad * 2.f + g.labelArea + gap;
    // 列数必须整格塞进 availW（含首格左侧 gap 的处理：从 0 起算 cellW）
    const float usable = std::max(g.cellW, availW);
    g.columns = std::max(1, static_cast<int>(std::floor(usable / g.cellW)));
    // 若 floor 后仍溢出（浮点误差），回退一列
    while (g.columns > 1 && static_cast<float>(g.columns) * g.cellW > availW + 0.5f) {
        --g.columns;
    }
    g.rows = count == 0 ? 0
                        : static_cast<int>((count + static_cast<std::size_t>(g.columns) - 1) /
                                           static_cast<std::size_t>(g.columns));
    return g;
}

[[nodiscard]] ImVec2 FitSize(std::uint32_t w, std::uint32_t h, ImVec2 box) {
    if (w == 0 || h == 0 || box.x <= 0.f || box.y <= 0.f) {
        return box;
    }
    const float s = std::min(box.x / static_cast<float>(w), box.y / static_cast<float>(h));
    return ImVec2(std::max(1.f, static_cast<float>(w) * s), std::max(1.f, static_cast<float>(h) * s));
}

} // namespace

std::string Ellipsize(std::string_view text, float maxWidth) {
    if (text.empty() || maxWidth <= 0.f) {
        return {};
    }
    const ImVec2 full = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    if (full.x <= maxWidth) {
        return std::string{text};
    }
    const std::string ell = "…";
    const float ellW = ImGui::CalcTextSize(ell.c_str()).x;
    if (maxWidth <= ellW) {
        return {};
    }
    // 扩到最长「前缀 + …」仍 ≤ maxWidth
    std::string result;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t next = pos + 1;
        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0u) == 0x80u) {
            ++next;
        }
        std::string cand = std::string{text.substr(0, next)} + ell;
        if (ImGui::CalcTextSize(cand.c_str()).x <= maxWidth) {
            result = std::move(cand);
            pos = next;
        } else {
            break;
        }
    }
    return result;
}

void DrawCheckerboard(ImVec2 min, ImVec2 max, float cell) {
    if (max.x <= min.x || max.y <= min.y) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const theme::ThemeColors& t = theme::Current();
    dl->AddRectFilled(min, max, ThemeCol(t.childBg));
    const float c = std::max(4.f, cell);
    const int nx = static_cast<int>(std::ceil((max.x - min.x) / c));
    const int ny = static_cast<int>(std::ceil((max.y - min.y) / c));
    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            if (((x + y) & 1) == 0) {
                continue;
            }
            const float x0 = min.x + static_cast<float>(x) * c;
            const float y0 = min.y + static_cast<float>(y) * c;
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(std::min(max.x, x0 + c), std::min(max.y, y0 + c)),
                              ThemeCol(t.frameBg));
        }
    }
}

ImVec2 FitInBox(std::uint32_t imageW, std::uint32_t imageH, ImVec2 box) { return FitSize(imageW, imageH, box); }

ThumbGridOutcome DrawThumbGrid(std::string_view strId, std::span<const ThumbGridItem> items, ThumbGridStyle& style,
                               float height) {
    ThumbGridOutcome out;
    const theme::ThemeColors& theme = theme::Current();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float h = height >= 0.f ? height : avail.y;
    const float viewH = std::max(48.f, h);
    const float availW = std::max(1.f, avail.x);

    // Ctrl+滚轮缩放：光标须落在本网格区域；clamp 到 [min, max]；先改 style 再算布局
    if (style.enableCtrlWheelZoom && ImGui::GetIO().KeyCtrl) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 regionMin = ImGui::GetCursorScreenPos();
            const ImVec2 regionMax(regionMin.x + avail.x, regionMin.y + viewH);
            if (mouse.x >= regionMin.x && mouse.x <= regionMax.x && mouse.y >= regionMin.y &&
                mouse.y <= regionMax.y) {
                const float minS = std::max(24.f, style.thumbSideMin);
                const float maxS = std::max(minS, style.thumbSideMax);
                const float step = style.zoomStep > 1.01f ? style.zoomStep : 1.12f;
                const float factor = wheel > 0.f ? step : (1.f / step);
                const float next = std::clamp(style.thumbSide * factor, minS, maxS);
                if (std::abs(next - style.thumbSide) >= 0.5f) {
                    style.thumbSide = next;
                    out.zoomChanged = true;
                }
                // 吃掉滚轮，避免同时滚动列表
                ImGui::SetScrollY(ImGui::GetScrollY()); // no-op keep; wheel already consumed by io
            }
        }
    }
    out.thumbSide = style.thumbSide;

    const Geom geom = ComputeGeom(availW, style, items.size());
    out.columns = geom.columns;
    out.rows = geom.rows;
    out.contentHeight = static_cast<float>(geom.rows) * geom.cellH + style.gap;

    const std::string childId = "##thumb_grid_" + std::string{strId};
    // 不要 HorizontalScrollbar：列数已按宽度收敛
    if (!ImGui::BeginChild(childId.c_str(), ImVec2(0, viewH), ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
        ImGui::EndChild();
        return out;
    }

    // Ctrl 缩放时不滚内容：清掉本帧 wheel 对 child 的影响（在 BeginChild 后设 scroll）
    if (out.zoomChanged) {
        // 保持大致视野：按比例缩放 scrollY
        const float oldCellH = geom.cellH; // 已是新尺寸；比例缩放意义有限，简单 clamp
        ImGui::SetScrollY(std::clamp(ImGui::GetScrollY(), 0.f, std::max(0.f, out.contentHeight - viewH)));
        (void)oldCellH;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(availW, out.contentHeight));

    const float scrollY = ImGui::GetScrollY();
    out.scrollY = scrollY;
    out.viewH = viewH;
    // 可见行 + 上下各 1 行预取（绘制用；请求加载仍由调用方决定）
    const int firstRow = geom.cellH > 0.f ? std::max(0, static_cast<int>(std::floor(scrollY / geom.cellH)) - 1) : 0;
    const int lastRow =
        geom.cellH > 0.f ? std::min(geom.rows, static_cast<int>(std::ceil((scrollY + viewH) / geom.cellH)) + 1) : 0;
    out.visibleBegin = static_cast<std::size_t>(firstRow) * static_cast<std::size_t>(std::max(1, geom.columns));
    out.visibleEnd = std::min(items.size(), static_cast<std::size_t>(std::max(0, lastRow)) *
                                                static_cast<std::size_t>(std::max(1, geom.columns)));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float thumbSide = std::max(24.f, style.thumbSide);
    const float pad = std::max(0.f, style.pad);

    // 绘制区裁剪：网格可视范围，防止任何内容穿出 child
    const ImVec2 viewMin = ImGui::GetWindowPos();
    const ImVec2 viewMax(viewMin.x + ImGui::GetWindowSize().x, viewMin.y + ImGui::GetWindowSize().y);

    const std::size_t begin = items.empty() ? 0 : std::min(out.visibleBegin, items.size());
    const std::size_t end = items.empty() ? 0 : std::min(out.visibleEnd, items.size());

    for (std::size_t i = begin; i < end; ++i) {
        const ThumbGridItem& item = items[i];
        const int col = geom.columns > 0 ? static_cast<int>(i % static_cast<std::size_t>(geom.columns)) : 0;
        const int row = geom.columns > 0 ? static_cast<int>(i / static_cast<std::size_t>(geom.columns)) : 0;
        const float cellX = origin.x + static_cast<float>(col) * geom.cellW;
        const float cellY = origin.y + static_cast<float>(row) * geom.cellH;
        const ImVec2 cellMin(cellX, cellY);
        const ImVec2 cellMax(cellX + geom.cellW - style.gap, cellY + geom.cellH - style.gap);
        if (cellMax.x < viewMin.x || cellMin.x > viewMax.x || cellMax.y < viewMin.y || cellMin.y > viewMax.y) {
            continue;
        }

        ImGui::SetCursorScreenPos(cellMin);
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 cellSize(std::max(1.f, cellMax.x - cellMin.x), std::max(1.f, cellMax.y - cellMin.y));
        ImGui::InvisibleButton("##cell", cellSize);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            out.clickedId = item.id;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            out.rightClickedId = item.id;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            out.doubleClickedId = item.id;
        }
        // G-S11：拖拽源（payload = UTF-8 路径）
        if (!item.dragPayload.empty() && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("SHINE_IMAGE_PATH", item.dragPayload.c_str(), item.dragPayload.size() + 1);
            ImGui::TextUnformatted(item.label.empty() ? item.dragPayload.c_str() : item.label.c_str());
            ImGui::EndDragDropSource();
        }

        // 整格裁剪：边框/图/字都不许穿出
        dl->PushClipRect(cellMin, cellMax, true);

        const ImU32 border =
            item.selected ? ThemeCol(theme.accent) : (hovered ? ThemeCol(theme.accentAlt) : ThemeCol(theme.border));
        const float borderT = item.selected ? style.borderSelected : (hovered ? style.borderHover : 1.f);
        dl->AddRect(cellMin, cellMax, border, style.round, 0, borderT);

        const ImVec2 thumbMin(cellMin.x + pad, cellMin.y + pad);
        const ImVec2 thumbMax(cellMax.x - pad, cellMin.y + pad + thumbSide);
        const ImVec2 thumbBox(std::max(1.f, thumbMax.x - thumbMin.x), std::max(1.f, thumbMax.y - thumbMin.y));

        // 图区再裁一层：绝不压到标签
        dl->PushClipRect(thumbMin, thumbMax, true);
        DrawCheckerboard(thumbMin, thumbMax);
        if (item.texture != 0 && !item.failed) {
            const ImVec2 fit = FitSize(item.imageW, item.imageH, thumbBox);
            const ImVec2 imgMin(thumbMin.x + (thumbBox.x - fit.x) * 0.5f, thumbMin.y + (thumbBox.y - fit.y) * 0.5f);
            dl->AddImage(ImTextureRef(item.texture), imgMin, ImVec2(imgMin.x + fit.x, imgMin.y + fit.y));
        } else if (item.failed) {
            const char* bang = "!";
            const ImVec2 ts = ImGui::CalcTextSize(bang);
            dl->AddText(ImVec2(thumbMin.x + (thumbBox.x - ts.x) * 0.5f, thumbMin.y + (thumbBox.y - ts.y) * 0.5f),
                        ThemeCol(theme.danger), bang);
        } else {
            const char* ph = item.placeholder ? "…" : "·";
            const ImVec2 ts = ImGui::CalcTextSize(ph);
            dl->AddText(ImVec2(thumbMin.x + (thumbBox.x - ts.x) * 0.5f, thumbMin.y + (thumbBox.y - ts.y) * 0.5f),
                        ThemeCol(theme.textDim, 0.7f), ph);
        }
        dl->PopClipRect();

        // 标签区：独立裁剪 + 省略号
        const ImVec2 labelMin(cellMin.x + pad, thumbMax.y + 2.f);
        const ImVec2 labelMax(cellMax.x - pad, cellMax.y - 1.f);
        const float labelW = std::max(8.f, labelMax.x - labelMin.x);
        dl->PushClipRect(ImVec2(labelMin.x, labelMin.y), ImVec2(labelMax.x, labelMax.y), true);
        if (!item.label.empty()) {
            const std::string line = Ellipsize(item.label, labelW);
            dl->AddText(labelMin, ThemeCol(theme.text), line.c_str());
        }
        if (!item.sublabel.empty() && style.labelLines >= 2) {
            const ImVec2 subPos(labelMin.x, labelMin.y + style.labelLineH);
            if (subPos.y < labelMax.y) {
                const std::string sub = Ellipsize(item.sublabel, labelW);
                dl->AddText(subPos, ThemeCol(theme.textDim), sub.c_str());
            }
        }
        dl->PopClipRect();
        dl->PopClipRect(); // cell

        if (hovered && !item.tooltip.empty()) {
            TooltipWrapped(item.tooltip, 28.f);
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    return out;
}

} // namespace shine::app::ui
