#include "ui/imgui/kit/Widget_Card.h"

#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

#include <cstdio>
#include <string>

namespace shine::kit {

float CardHoverLift(bool hovered, bool hoverable) {
    return (hovered && hoverable) ? -2.0f : 0.0f;
}

// ------------------------------------------------------------------ 6 Card
Rect Card(ImDrawList* draw, Rect bounds, std::string_view title, std::string_view icon,
          bool hoverable, bool glow) {
    const float radius = 10.0f;
    // .card.hoverable:hover / .card.glow:hover（ui.css:196-204）需要真的命中测试。
    // 原实现是三目两支都取 ColorLineSubtle 的死代码：卡片标了 hoverable/glow，
    // 组件画廊也按这个标签铺演示，但 hover 时**什么都看不出来**。
    // ⚠️ ID 用标题不可靠：同页多张无标题卡片会拿到同一个 ID，hover 状态串卡。
    //   用卡片左上角的屏幕坐标做 key：同一帧内唯一，且不随标题变化。
    //   id 在栈上拼（每帧 × 每张卡都走这里，不许堆分配）。
    const Hit hit = [&] {
        if (!hoverable && !glow) {
            return Hit{};
        }
        char idBuf[48];
        const int n = std::snprintf(idBuf, sizeof(idBuf), "card@%d,%d",
                                    static_cast<int>(bounds.min.x), static_cast<int>(bounds.min.y));
        return detail::HitTestItemId(bounds, n > 0 ? idBuf : "card@");
    }();
    // hover 上浮 2px（transform: translateY(-2px)）—— 用整体偏移近似。补间版：
    // 上浮/回落都走 kDurBase 滑过去（终值与 CardHoverLift 硬切换逐像素同）。
    // ⚠️ 无命中的纯展示卡（Hit{}，tid=0）不许进补间：tid=0 会与别的卡撞车。
    const float lift = (hoverable && hit.tid)
                           ? -2.0f * TransitionFloatTo(hit.tid, 1, hit.hovered, kDurBase)
                           : CardHoverLift(hit.hovered, hoverable);
    const ImVec2 min(bounds.min.x, bounds.min.y + lift);
    const ImVec2 max(bounds.max.x, bounds.max.y + lift);

    // hover 边框色补间（glow 用 accent-glow 边，普通 hoverable 用 line-strong，ui.css:197/202）
    const ImU32 border =
        (hit.tid != 0) ? TransitionColorTo(hit.tid, 0,
                                           hit.hovered ? (glow ? ColorAccentGlow() : ColorLineStrong())
                                                       : ColorLineSubtle(),
                                           kDurBase)
                       : (hit.hovered ? (glow ? ColorAccentGlow() : ColorLineStrong())
                                      : ColorLineSubtle());
    // ui.css:196-204：投影与边框是**同两条规则**给的，别只接一半。
    //   .card.hoverable:hover → border-color: var(--line-strong) + box-shadow: var(--shadow-1)
    //   .card.glow:hover       → border-color: var(--accent-glow) + box-shadow: var(--shadow-accent)
    // 特异度上 glow 那条更高（0,3,0 对 0,3,0 里类名更多），一个元素同时带两个类时按 glow 走。
    // 投影档切换刻意保持离散（档间交叉渐变要动 DrawShadowed 的分层 alpha，见
    // refactor/animations.md「刻意不动的」）。
    DrawShadowed(draw, min, max, radius, ColorPanel(), border, 1.0f,
                 hit.hovered ? (glow ? theme::ShadowTier::Accent : theme::ShadowTier::Card)
                             : theme::ShadowTier::None);

    const bool hasHeader = !title.empty() || !icon.empty();
    float y = min.y + 16.0f;
    if (hasHeader) {
        // 图标与标题行盒共用卡头中心，头高包含底部 1px 分隔线。
        constexpr float kCardHeadH = 46.6f;  // 12 + 21.6 + 12 + 1
        const float centerY = min.y + (kCardHeadH - 1.0f) * 0.5f;
        constexpr float kTitleSize = theme::font::kBodyLarge;
        const float iconSize = 15.0f;
        float x = min.x + 16.0f;
        if (!icon.empty()) {
            DrawIcon(draw, icon, ImVec2(x, centerY - iconSize * 0.5f), iconSize, ColorAccent());
            x += iconSize + 8.0f;
        }
        ImFont* font = FontBoldAt(kTitleSize);
        const float titleY = CenterTextY(font, kTitleSize, centerY);
        DrawTextClipped(draw, font, kTitleSize, ImVec2(x, titleY), max.x - min.x - 32.0f,
                        ColorText(), title);
        // 头下边框落在头底（border-box：含那 1px）。
        const float headBottom = min.y + kCardHeadH;
        draw->AddLine(ImVec2(min.x, headBottom - 0.5f), ImVec2(max.x, headBottom - 0.5f),
                      ColorLineSubtle(), 1.0f);
        // ⚠️ 有头时正文起点原来给的是 `min.y + 44`，**比无头时的 `min.y + 16` 少算**：
        //    无头走的就是 `.card-b { padding: 16px }` 的 16，有头却没加。
        //    统一成「头底 + 16」，与无头情形同一条规则。
        y = headBottom + 16.0f;
    }
    return Rect{ImVec2(min.x + 16.0f, y), ImVec2(max.x - 16.0f, max.y - 16.0f)};
}

Rect CardHeaderRow(Rect card, std::string_view title, std::string_view icon) {
    const float iconSize = 15.0f;
    float x = card.min.x + 16.0f + (icon.empty() ? 0.0f : iconSize + 8.0f);
    ImFont* font = FontBoldAt(13.5f);
    x += font->CalcTextSizeA(13.5f, 1e9f, 0.0f, title.data(), title.data() + title.size()).x + 8.0f;
    return Rect{ImVec2(x, card.min.y + 10.0f), ImVec2(card.max.x - 16.0f, card.min.y + 32.0f)};
}

} // namespace shine::kit
