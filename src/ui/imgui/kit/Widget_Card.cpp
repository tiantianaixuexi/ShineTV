#include "ui/imgui/kit/Widget_Card.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

#include <string>

namespace shine::kit {

// ------------------------------------------------------------------ 6 Card
Rect Card(ImDrawList* draw, Rect bounds, std::string_view title, std::string_view icon,
          bool hoverable, bool glow) {
    const float radius = 10.0f;
    // .card.hoverable:hover / .card.glow:hover（ui.css:196-204）需要真的命中测试。
    // 原实现是三目两支都取 ColorLineSubtle 的死代码：卡片标了 hoverable/glow，
    // 组件画廊也按这个标签铺演示，但 hover 时**什么都看不出来**。
    // ⚠️ ID 用标题不可靠：同页多张无标题卡片会拿到同一个 ID，hover 状态串卡。
    //   用卡片左上角的屏幕坐标做 key：同一帧内唯一，且不随标题变化。
    const Hit hit =
        (hoverable || glow)
            ? detail::HitTestItem(bounds, "card@" + std::to_string(static_cast<int>(bounds.min.x)) + "," +
                                       std::to_string(static_cast<int>(bounds.min.y)))
            : Hit{};
    // hover 上浮 2px（transform: translateY(-2px)）—— 用整体偏移近似
    const float lift = (hit.hovered && hoverable) ? -2.0f : 0.0f;
    const ImVec2 min(bounds.min.x, bounds.min.y + lift);
    const ImVec2 max(bounds.max.x, bounds.max.y + lift);

    ImU32 border = ColorLineSubtle();
    if (hit.hovered) {
        // glow 用 accent-glow 边，普通 hoverable 用 line-strong（ui.css:197/202）
        border = glow ? ColorAccentGlow() : ColorLineStrong();
    }
    // ui.css:196-204：投影与边框是**同两条规则**给的，别只接一半。
    //   .card.hoverable:hover → border-color: var(--line-strong) + box-shadow: var(--shadow-1)
    //   .card.glow:hover       → border-color: var(--accent-glow) + box-shadow: var(--shadow-accent)
    // 特异度上 glow 那条更高（0,3,0 对 0,3,0 里类名更多），一个元素同时带两个类时按 glow 走。
    DrawShadowed(draw, min, max, radius, ColorPanel(), border, 1.0f,
                 hit.hovered ? (glow ? theme::ShadowTier::Accent : theme::ShadowTier::Card)
                             : theme::ShadowTier::None);

    const bool hasHeader = !title.empty() || !icon.empty();
    float y = min.y + 16.0f;
    if (hasHeader) {
        // `.card-h`（ui.css:182-188）：display:flex + align-items:center + gap 8 +
        // padding 12px 16px + border-bottom 1px；`.card-title` 13.5px/600。
        // 头高 = 12 + 行盒(13.5 × 1.6 = 21.6) + 12 + 1（border-box，border 算在里头）
        //        = **46.6px**。原来按 43px 画，短了 3.6px。
        //
        // ⚠️ 但**文字位置 `headerY = min.y + 12` 本来就是对的**：内容盒从
        //    min.y+12 起，行盒顶就在那儿（`line_height = fontSize`），
        //    不该「修正」成按头高居中 —— 那是把一个对的式子改成另一个。
        //    真正错的是**图标**：`align-items:center` ⇒ 图标中心要与行盒中心
        //    `min.y + 12 + 10.8` 齐平 ⇒ 顶边 `min.y + 15.3`；原来画在
        //    `headerY + 1.0f`，比文字高 2.3px。同一行里图标与字错开，一眼可见。
        constexpr float kCardHeadPadY = 12.0f;
        constexpr float kCardHeadH = 46.6f;  // 12 + 21.6 + 12 + 1
        const float headerY = min.y + kCardHeadPadY;
        const float iconSize = 15.0f;
        const float iconY = headerY + (kCardHeadH - 1.0f - iconSize) * 0.5f;
        float x = min.x + 16.0f;
        if (!icon.empty()) {
            DrawIcon(draw, icon, ImVec2(x, iconY), iconSize, ColorAccent());
            x += iconSize + 8.0f;
        }
        ImFont* font = FontBoldAt(13.5f);
        DrawTextClipped(draw, font, 13.5f, ImVec2(x, headerY), max.x - min.x - 32.0f,
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
