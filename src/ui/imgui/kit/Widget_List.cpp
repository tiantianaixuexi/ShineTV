#include "ui/imgui/kit/Widget_List.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

#include <algorithm>

namespace shine::kit {

// ------------------------------------------------------------------ 20b ListRow
float ListRowHeight(const ListRowSpec& spec, float paddingY) {
    // 下限取文字与图标里更高的那个：行**只装一行**时，文字高度 + 上下内边距
    // 就够了，但图标比文字高（16px 图标 vs 10.5px 次要文字）时行会被压扁。
    const float content = std::max({spec.titleSize, spec.trailingSize,
                                    spec.icon.empty() ? 0.0f : spec.iconSize});
    return content + paddingY * 2.0f;
}

Hit ListRow(ImDrawList* draw, Rect bounds, const ListRowSpec& spec) {
    // 命中**一次**取齐：hovered 与 clicked 来自同一个 detail::HitTestItem。
    // 页面层曾写成 `Hovered(idA)` 叠 `Clicked(idB)`，ImGui 只让先注册的那个
    // 拿到 HoveredId，于是第二个永远 clicked=false —— 那一行点不开，
    // 而编译、日志、截图全绿。
    Hit hit;
    if (!spec.id.empty() && !spec.suppressed) {
        hit = detail::HitTestItem(bounds, spec.id);
    }
    // 底色：选中优先于 hover（`.mi.on` / `tr.sel` 的既有语义）。
    // 圆角 6 —— 页面层 5 处自己写的时候有 4 处用 6、1 处用 8。
    if (spec.selected) {
        DrawRoundRect(draw, bounds.min, bounds.max, 6.0f, ColorFillSelected());
    } else if (hit.hovered) {
        DrawRoundRect(draw, bounds.min, bounds.max, 6.0f, ColorFillHover());
    }

    const float centerY = bounds.center().y;
    float x = bounds.min.x + spec.paddingX;
    // chevron 先量宽度：标题可用宽要减去它，否则长标题会压在 chevron 底下
    // （页面层几处是「标题写死 220/300 宽」，容器一变窄就压字）。
    constexpr float kChevronW = 10.0f;
    const bool hasChevron = !spec.chevron.empty() && spec.chevron != "none";
    if (hasChevron) {
        DrawIcon(draw, spec.chevron, ImVec2(bounds.max.x - spec.paddingX - kChevronW,
                                            centerY - kChevronW * 0.5f),
                 kChevronW, spec.disabled ? WithAlpha(ColorTextMuted(), 0.45f) : ColorTextMuted());
    }

    const ImU32 iconColor =
        spec.disabled ? WithAlpha(ColorTextMuted(), 0.45f)
                      : (spec.iconColor != 0 ? spec.iconColor : ColorTextMuted());
    if (!spec.icon.empty()) {
        DrawIcon(draw, spec.icon, ImVec2(x, centerY - spec.iconSize * 0.5f), spec.iconSize, iconColor);
        x += spec.iconSize + spec.iconGap;
    }

    // 标题可用宽 = 到 chevron（或右内边距）为止，再减去右侧次要文字的实际宽度。
    float titleRight = bounds.max.x - spec.paddingX;
    if (hasChevron) {
        titleRight -= kChevronW + 6.0f;
    }
    ImFont* titleFont = spec.titleMono ? MonoAt(spec.titleSize) : FontAt(spec.titleSize);
    if (!spec.trailing.empty()) {
        ImFont* trailingFont =
            spec.trailingMono ? MonoAt(spec.trailingSize) : FontAt(spec.trailingSize);
        const float trailingW = MeasureClipped(trailingFont, spec.trailingSize, 1e9f, spec.trailing);
        // 右侧次要文字也从 chevron 往回退，两者互不重叠。
        const float trailingX =
            std::max(x, (hasChevron ? bounds.max.x - spec.paddingX - kChevronW - 6.0f
                                    : bounds.max.x - spec.paddingX) - trailingW);
        // ⚠️ 同样走 CenterTextY。页面层这 5 处里有 3 处写死 `y + 5.0f` / `y + 6.0f`，
        // 行高 24~30 时偏上 1.25~2.5px。
        DrawTextClipped(draw, trailingFont, spec.trailingSize,
                        ImVec2(trailingX, CenterTextY(trailingFont, spec.trailingSize, centerY)),
                        trailingW, spec.disabled ? WithAlpha(ColorTextMuted(), 0.45f) : ColorTextMuted(),
                        spec.trailing);
        titleRight = std::min(titleRight, trailingX - 8.0f);
    }
    if (!spec.title.empty()) {
        const ImU32 titleColor =
            spec.disabled ? WithAlpha(ColorTextSecondary(), 0.45f)
                          : (spec.titleColor != 0 ? spec.titleColor : ColorTextSecondary());
        DrawTextClipped(draw, titleFont, spec.titleSize,
                        ImVec2(x, CenterTextY(titleFont, spec.titleSize, centerY)),
                        std::max(0.0f, titleRight - x), titleColor, spec.title);
    }
    return hit;
}

// ------------------------------------------------------------------ 20c ListCard
float ListCardHeight(const ListCardSpec& spec, float paddingY) {
    const float textH = spec.description.empty()
                            ? spec.titleSize
                            : (spec.titleSize + spec.textGap + spec.descSize);
    return std::max(textH, spec.iconSize) + paddingY * 2.0f;
}

Hit ListCard(ImDrawList* draw, Rect bounds, const ListCardSpec& spec,
             const std::function<void(ImDrawList*, Rect)>& footer) {
    Hit hit;
    if (spec.hoverable && !spec.id.empty()) {
        hit = detail::HitTestItem(bounds, spec.id);
    }
    // .card.hoverable（ProjectHub.jsx:39/226）：hover 走 **shadow-1**（投影档切换），
    // 不是填色底。选中态是 accent 描边（ProjectHub.jsx:40 的 accent-dim 环）——
    // 刻意**不**折成投影档，两者视觉形态不同。
    DrawShadowed(draw, bounds.min, bounds.max, 10.0f,
                 spec.selected ? ColorFillMuted() : ColorPanel(),
                 spec.selected ? ColorAccent() : (hit.hovered ? ColorLineStrong() : ColorLineSubtle()),
                 1.0f, hit.hovered ? theme::ShadowTier::Card : theme::ShadowTier::None);

    if (!spec.icon.empty()) {
        DrawIconCentered(draw, spec.icon,
                         ImVec2(bounds.min.x + spec.paddingX + spec.iconSize * 0.5f, bounds.center().y),
                         spec.iconSize, spec.selected ? ColorAccent() : ColorTextMuted());
    }
    // 文字左界 = paddingX + 图标占位（有图标时 paddingX + iconSize + 8）+ 额外缩进。
    // ⚠️ 不要写成 `paddingX * 2`：那是把「内边距」当成了「图标占位宽」，
    //   无图标的卡会凭空多缩进一整份 paddingX。
    const float textX = bounds.min.x + spec.paddingX + spec.textInset +
                        (spec.icon.empty() ? 0.0f : (spec.iconSize + 8.0f));
    // 右侧要给 footer 留位（打开列表的「打开」按钮），所以文字右界取整个右内边距 ——
    // footer 自己在**整卡命中之后**画，两者在水平上不重叠。
    const float textRight = bounds.max.x - spec.paddingX;

    if (!spec.description.empty()) {
        // 两行的**块**按卡中心对齐：块高 = title + gap + desc，块顶 = 中心 − 块高/2。
        // 页面层两处双行卡原来各写各的 `row.min.y + 12/14` 与 `+ 34`，
        // 块中心偏上 0.875px 与 1.875px（打开列表那处还把标题顶到 −12.5）。
        const float blockH = spec.titleSize + spec.textGap + spec.descSize;
        const float blockTop = bounds.center().y - blockH * 0.5f;
        ImFont* titleFont = FontBoldAt(spec.titleSize);
        DrawTextClipped(draw, titleFont, spec.titleSize,
                        ImVec2(textX, CenterTextY(titleFont, spec.titleSize, blockTop + spec.titleSize * 0.5f)),
                        std::max(0.0f, textRight - textX), ColorText(), spec.title);
        ImFont* descFont = FontAt(spec.descSize);
        const float descCenter = blockTop + spec.titleSize + spec.textGap + spec.descSize * 0.5f;
        DrawTextClipped(draw, descFont, spec.descSize,
                        ImVec2(textX, CenterTextY(descFont, spec.descSize, descCenter)),
                        std::max(0.0f, textRight - textX), ColorTextMuted(), spec.description);
    } else if (!spec.title.empty()) {
        ImFont* titleFont = FontBoldAt(spec.titleSize);
        DrawTextClipped(draw, titleFont, spec.titleSize,
                        ImVec2(textX, CenterTextY(titleFont, spec.titleSize, bounds.center().y)),
                        std::max(0.0f, textRight - textX), ColorText(), spec.title);
    }

    if (spec.selected) {
        // 选中勾在卡的右缘，与 footer 互斥：footer 存在时把勾让出去。
        if (!footer) {
            DrawIconCentered(draw, "check",
                             ImVec2(bounds.max.x - spec.paddingX - 8.0f, bounds.center().y), 16.0f,
                             ColorAccent());
        }
    }
    // ⚠️ footer 必须在**整卡命中之后**画：ImGui 同窗口内先注册者独占 HoveredId
    // （imgui.cpp:5161），反过来按钮永远 clicked=false，而整卡照样 clicked=true ——
    // 症状是「点『打开』不是打开，而是直接打开这张卡」。顺序反了整轮静默。
    if (footer) {
        footer(draw, RectAt(textX, bounds.min.y, std::max(0.0f, textRight - textX), bounds.height()));
    }
    return hit;
}

} // namespace shine::kit
