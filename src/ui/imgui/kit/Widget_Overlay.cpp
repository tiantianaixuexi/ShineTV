#include "ui/imgui/kit/Widget_Overlay.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

#include <algorithm>

namespace shine::kit {

// ------------------------------------------------------------------ 19 Tooltip
// [data-tip]::after（ui.css:476-507）。要点：
//   * 底 bg-overlay（不是 panel）+ 1px line-normal + shadow-1；
//   * 11.5px / 500 / pad 4px 9px / r6 / nowrap；
//   * 位置：默认 left: calc(100% + 10px) 垂直居中；.tip-b 变体是 top: calc(100% + 8px) 水平居中；
//   * 有 --dur-2（200ms）的**出现延迟**，不是立刻显示；
//   * pointer-events: none —— 只画不命中，所以这里不调 HitTest。
// ⚠️ 画在 GetForegroundDrawList() 而不是传入的 draw list：外壳是一帧一整块自绘，
// 画在普通 draw list 上会被后面画的控件盖住（tooltip 就成了「时有时无」）。
// 落不到前景时（没进 ImGui 帧）静默跳过，绝不崩。
void Tooltip(Rect anchor, std::string_view text, bool below) {
    if (text.empty()) {
        return;
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (draw == nullptr) {
        return;
    }
    // 出现延迟：CSS transition-delay: var(--dur-2) = 200ms。
    //
    // 本函数是**无条件绘制**的：判「该不该显示」是调用方的责任（IconButton 只在
    // hover 时调）。所以延迟计时必须自己处理「中途不再被调用」的情况：
    // 鼠标移开 → 停止调用 → 计时器停在半路 → 再次悬停**同一个**按钮时会接着
    // 旧值秒出，粘手。用帧号断档（lastFrame + 1 != now）来复位，
    // 顺带覆盖「从 A 划到 B」这种换锚点的情况。
    static float shownFor = 0.0f;
    static int lastFrame = -2;
    const ImGuiIO& io = ImGui::GetIO();
    const int now = ImGui::GetFrameCount();
    if (lastFrame + 1 != now) {
        shownFor = 0.0f; // 中间至少断了一帧 = 悬停已经结束
    }
    lastFrame = now;
    const float delay = ReduceMotion() ? 0.0f : 0.2f;
    if (io.DeltaTime > 0.0f) {
        shownFor = std::min(shownFor + io.DeltaTime, delay + 1.0f);
    }
    if (shownFor < delay) {
        return;
    }
    // 出现动画：opacity 0→1 + scale .94→1，都是 --dur-1（120ms）。
    const float t = ReduceMotion() ? 1.0f : std::clamp((shownFor - delay) / 0.12f, 0.0f, 1.0f);
    const ImU32 bg = WithAlphaSet(ColorOverlay(), t);
    const ImU32 border = WithAlpha(ColorLineNormal(), t);
    const ImU32 fg = WithAlpha(ColorText(), t);

    ImFont* font = FontAt(11.5f);
    const float textWidth =
        font->CalcTextSizeA(11.5f, 1e9f, 0.0f, text.data(), text.data() + text.size()).x;
    const float padX = 9.0f;
    const float padY = 4.0f;
    const float width = textWidth + padX * 2.0f;
    const float height = 11.5f + padY * 2.0f + 2.0f; // +2 = 上下各 1px 边

    ImVec2 min;
    if (below) {
        min = ImVec2(anchor.center().x - width * 0.5f, anchor.max.y + 8.0f);
    } else {
        min = ImVec2(anchor.max.x + 10.0f, anchor.center().y - height * 0.5f);
    }
    const ImVec2 max(min.x + width, min.y + height);
    // scale(.94)：从中心缩，所以先把 min/max 往中心收 6%。
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float s = 0.94f + 0.06f * t;
    const ImVec2 smin(c.x - width * 0.5f * s, c.y - height * 0.5f * s);
    const ImVec2 smax(c.x + width * 0.5f * s, c.y + height * 0.5f * s);
    // `[data-tip]::after`（ui.css:476-494）：参数与这里逐项吻合（11.5px 字、
    // padding 4/9、--bg-overlay、--line-normal、scale .94→1、z-index 90），
    // 其中 :494 是 `box-shadow: var(--shadow-1)` 且**不在 :hover 里** ——
    // 提示气泡一出现就该有投影，不是 hover 才有的。这是全仓唯一一处
    // 「静止态就该有投影、此前却落回默认 None」的浮层。
    DrawShadowed(draw, smin, smax, 6.0f, bg, border, 1.0f, theme::ShadowTier::Card);
    draw->AddText(font, 11.5f, ImVec2(smin.x + padX, smin.y + padY + 1.0f), fg, text.data(),
                  text.data() + text.size());
}

// ------------------------------------------------------------------ 23 Menu
namespace {

float MenuRowHeightOf(MenuRowKind kind) {
    switch (kind) {
    case MenuRowKind::Separator: return 11.0f; // 1px 线 + margin 5/5
    case MenuRowKind::Label: return 19.0f;     // padding 6/3 + 10.5px 字
    case MenuRowKind::Item: break;
    }
    // .mi：padding 7/10 + 12.5px 字 + 1px 边。
    return 14.0f + 15.0f + 1.0f;
}

} // namespace

float MenuWidth(const std::vector<MenuRow>& rows) {
    // min-width: 180（shell.css:105）；内容更宽时按最宽项撑开。
    ImFont* font = FontAt(12.5f);
    float widest = 0.0f;
    for (const MenuRow& row : rows) {
        if (row.kind != MenuRowKind::Item) {
            continue;
        }
        float w = font->CalcTextSizeA(12.5f, 1e9f, 0.0f, row.label.data(),
                                      row.label.data() + row.label.size())
                      .x;
        if (!row.icon.empty()) {
            w += 15.0f + 9.0f; // .mi 的 gap 9
        }
        widest = std::max(widest, w + 20.0f + 10.0f); // pad 7/10 两边 + 面板 pad5
    }
    return std::max(180.0f, widest);
}

float MenuHeight(const std::vector<MenuRow>& rows) {
    float h = 5.0f * 2.0f; // .menu-pop padding 5
    for (const MenuRow& row : rows) {
        h += MenuRowHeightOf(row.kind);
    }
    return h;
}

int Menu(ImDrawList* draw, Rect anchor, const std::vector<MenuRow>& rows, std::string_view id) {
    if (rows.empty()) {
        return -1;
    }
    const float width = MenuWidth(rows);
    // top: calc(100% + 6px); right: 0（shell.css:103-104）—— 面板右边缘对齐锚点右边缘。
    const Rect panel = RectAt(anchor.max.x - width, anchor.max.y + 6.0f, width, MenuHeight(rows));
    DrawShadowed(draw, panel.min, panel.max, 10.0f, ColorOverlay(), ColorLineNormal(), 1.0f, theme::ShadowTier::Overlay);

    int picked = -1;
    float y = panel.min.y + 5.0f;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const MenuRow& row = rows[i];
        const float h = MenuRowHeightOf(row.kind);
        switch (row.kind) {
        case MenuRowKind::Separator:
            // .msep：height 1 + margin 5/6（上下各 5px 已在高度里）。
            draw->AddLine(ImVec2(panel.min.x + 6.0f, y + 5.0f),
                          ImVec2(panel.max.x - 6.0f, y + 5.0f), ColorLineSubtle(), 1.0f);
            y += h;
            continue;
        case MenuRowKind::Label: {
            // .mlabel：padding 6px 10px 3px，10.5/700 muted + 字距 .06em。
            ImFont* font = FontBoldAt(10.5f);
            draw->AddText(font, 10.5f, ImVec2(panel.min.x + 10.0f, y + 6.0f), ColorTextMuted(),
                          row.label.data(), row.label.data() + row.label.size());
            y += h;
            continue;
        }
        case MenuRowKind::Item:
            break;
        }
        // .mi 的实际可点区域是面板去掉 padding 5 后的整宽。
        const Rect item = RectAt(panel.min.x + 5.0f, y, width - 10.0f, h);
        const Hit hit = detail::HitTestItem(item, detail::UniqueId(id, static_cast<int>(i)));
        if (hit.hovered && !row.disabled) {
            // .mi.on：accent 字 + accent-dim 底；普通 hover：fill-hover + primary。
            if (row.selected) {
                draw->AddRectFilled(item.min, item.max, ColorAccentDim());
            } else {
                draw->AddRectFilled(item.min, item.max, ColorFillHover());
            }
        }
        if (hit.clicked && !row.disabled) {
            picked = static_cast<int>(i);
        }
        const ImU32 fg = row.selected ? ColorAccent()
                                      : (hit.hovered && !row.disabled ? ColorText()
                                                                      : ColorTextSecondary());
        ImFont* font = FontAt(12.5f);
        float x = item.min.x + 10.0f;
        // `.menu-pop .mi`（shell.css:114-120）：display:flex + **align-items:center** +
        // gap 9 + padding 7px 10px + 12.5px。行盒中心 = 条目中心 ⇒ 文字按条目中心
        // 居中。原来写死 `y + 7.0f`（当成了 padding-top），而 `h` 是
        // `MenuRowHeightOf(Item)` = 14+15+1 = 30 ⇒ **偏上 1.75px**。
        // 图标 15px 同样按 align-items 居中，和文字共用同一个中心。
        const float centerY = item.center().y;
        const float ty = CenterTextY(font, 12.5f, centerY);
        if (!row.icon.empty()) {
            DrawIcon(draw, row.icon, ImVec2(x, centerY - 7.5f), 15.0f,
                     row.disabled ? WithAlpha(fg, 0.45f) : fg);
            x += 15.0f + 9.0f; // .mi gap 9
        }
        const ImU32 textColor = row.disabled ? WithAlpha(fg, 0.45f) : fg;
        DrawTextClipped(draw, font, 12.5f, ImVec2(x, ty), item.max.x - x - 10.0f, textColor,
                        row.label);
        y += h;
    }
    return picked;
}

} // namespace shine::kit
