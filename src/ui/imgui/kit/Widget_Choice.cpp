#include "ui/imgui/kit/Widget_Choice.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Core.h"

namespace shine::kit {

// ------------------------------------------------------------------ 7 Segmented
float SegmentedWidth(const std::vector<SegmentOption>& options) {
    ImFont* font = FontBoldAt(12.5f);
    float total = 3.0f * 2.0f; // 容器 padding
    for (std::size_t i = 0; i < options.size(); ++i) {
        total += font->CalcTextSizeA(12.5f, 1e9f, 0.0f, options[i].label.data(),
                                     options[i].label.data() + options[i].label.size()).x +
                 26.0f;
        if (i + 1 < options.size()) {
            total += 2.0f; // gap
        }
    }
    return total;
}

std::string_view Segmented(ImDrawList* draw, Rect bounds,
                           const std::vector<SegmentOption>& options, std::string_view value,
                           std::string_view id) {
    DrawRoundRect(draw, bounds.min, bounds.max, 6.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
    ImFont* font = FontBoldAt(12.5f);
    const float itemHeight = bounds.height() - 6.0f;
    float x = bounds.min.x + 3.0f;
    std::string_view picked = value;
    for (std::size_t i = 0; i < options.size(); ++i) {
        const SegmentOption& option = options[i];
        const float text =
            font->CalcTextSizeA(12.5f, 1e9f, 0.0f, option.label.data(),
                                option.label.data() + option.label.size())
                .x;
        const Rect item = RectAt(x, bounds.min.y + 3.0f, text + 26.0f, itemHeight);
        const bool on = option.value == value;
        const Hit hit = detail::HitTestItem(item, detail::UniqueId(id, static_cast<int>(i)));
        if (hit.clicked) {
            picked = option.value;
        }
        if (on) {
            // 选中 = bg-elevated + shadow + 4px accent 圆点
            DrawRoundRect(draw, item.min, item.max, 4.0f, ColorElevated(), 0, 0.0f,
                          /*topHighlight=*/true);
        }
        // ⚠️ 非选中项的 hover **只改文字色**，不换底 —— 这是设计稿 ui.css 的原话：
        //      .seg > button      { color: text-muted }   // 无 background
        //      .seg > button:hover{ color: text-primary } // 也没有 background
        //      .seg > button.on   { background: bg-elevated; color: text-primary }
        //    早先这里给 hover 加了一层 fill-hover 底，是设计稿里**没有**的效果。
        //    副作用更要紧：选中项的 hover 因此毫无变化（color 本来就是 text-primary），
        //    悬停探针判 `broken` 才发现「按设计稿，选中项本来就该没有 hover 反馈」——
        //    于是那条探针的目标得换成**非选中**项，否则它测的是一个设计稿不承诺的东西。
        const ImU32 fg = on ? ColorText() : (hit.hovered ? ColorText() : ColorTextSecondary());
        const float textX = item.center().x - 0.5f * text - (on ? 5.0f : 0.0f);
        draw->AddText(font, 12.5f, ImVec2(textX, item.center().y - 12.5f * 0.5f), fg,
                      option.label.data(), option.label.data() + option.label.size());
        // .seg > button.on::after（ui.css:233-242）：4px accent 圆点在**文字右侧**，
        // margin-left 6px、vertical-align 2px。画在文字上方会直接压住字（原先的错法）。
        if (on) {
            draw->AddCircleFilled(ImVec2(textX + text + 8.0f, item.center().y + 2.0f), 2.0f,
                                 ColorAccent(), 10);
        }
        x = item.max.x + 2.0f;
    }
    return picked;
}

// ------------------------------------------------------------------ 7b Chip
// views.css:670-706。设计稿那处 chip 高度是**内联覆写**（Shell.jsx:540 的
// style={{height:22, padding:'0 8px', fontSize:11}}），不是另一个变体 —— 所以走 compact
// 参数，规格只有一份。计数底色在选中态要换成 accent 18%（.chip.on .cnt）。
namespace {

// .chip .cnt 的量宽：pad 0 6 + 10.5px 字宽。设计稿没给 min-width，纯按字宽。
float ChipCountWidth(std::string_view count) {
    ImFont* font = FontBoldAt(10.5f);  // .cnt 继承 .chip 的 600 字重
    return 6.0f * 2.0f +
           font->CalcTextSizeA(10.5f, 1e9f, 0.0f, count.data(), count.data() + count.size()).x;
}

} // namespace

float ChipHeight(bool compact) { return compact ? 22.0f : 26.0f; }

float ChipWidth(std::string_view label, const ChipSpec& spec) {
    const float fontSize = spec.compact ? 11.0f : 12.0f;
    const float pad = spec.compact ? 8.0f : 11.0f;
    ImFont* font = FontBoldAt(fontSize);
    float total =
        pad * 2.0f + font->CalcTextSizeA(fontSize, 1e9f, 0.0f, label.data(), label.data() + label.size()).x;
    if (!spec.count.empty()) {
        total += 6.0f + ChipCountWidth(spec.count);  // gap 6（.chip 的 gap）
    }
    return total;
}

bool Chip(ImDrawList* draw, Rect bounds, std::string_view label, const ChipSpec& spec,
          std::string_view id) {
    const Hit hit = HitTest(bounds, id);
    const float fontSize = spec.compact ? 11.0f : 12.0f;
    const float pad = spec.compact ? 8.0f : 11.0f;
    const float radius = bounds.height() * 0.5f;  // r-pill

    // hover 只提边与字色（views.css:685-688），底色不动 —— 选中态的底是 accent-dim，
    // hover 若也换底会和选中态糊在一起。
    const ImU32 bg = spec.selected ? ColorAccentDim()
                                  : (hit.hovered ? ColorFillMuted() : ColorTransparent());
    const ImU32 border =
        spec.selected ? ColorAccentGlow() : (hit.hovered ? ColorLineStrong() : ColorLineNormal());
    const ImU32 fg = spec.selected ? ColorAccent() : (hit.hovered ? ColorText() : ColorTextSecondary());
    DrawRoundRect(draw, bounds.min, bounds.max, radius, bg, border, 1.0f);

    ImFont* font = FontBoldAt(fontSize);
    const float text =
        font->CalcTextSizeA(fontSize, 1e9f, 0.0f, label.data(), label.data() + label.size()).x;
    float x = bounds.min.x + pad;
    const float cy = 0.5f * (bounds.min.y + bounds.max.y);
    // ⚠️ 原来直接画在 `cy`（容器垂直中心，**一个字都没减**）⇒ ImGui 行盒整个
    //    掉到中心线以下 6.0px（12px 字）/ 5.5px（11px 字）。这是全树最明显的一处
    //    「字不在中间」，而 Chip 是资产侧栏筛选器在用的 kit 小组件。
    //    `ImFont::RenderText` 里 `line_height = size`，所以行盒中心 = pos.y + 字号/2。
    draw->AddText(font, fontSize, ImVec2(x, CenterTextY(font, fontSize, cy)), fg, label.data(),
                  label.data() + label.size());
    x += text;

    if (!spec.count.empty()) {
        x += 6.0f;
        const Rect countBox{x, cy - 8.5f, x + ChipCountWidth(spec.count), cy + 8.5f};
        // 选中态计数底是 accent 18%（.chip.on .cnt），未选中是 fill-muted。
        DrawRoundRect(draw, countBox.min, countBox.max, countBox.height() * 0.5f,
                      spec.selected ? ColorAccentDim() : ColorFillMuted());
        // .cnt 只覆盖 font-size，font-weight 600 从 .chip 继承下来，所以用粗体。
        ImFont* cfont = FontBoldAt(10.5f);
        // 原来画在 `countBox.center().y` ⇒ 行盒比中心低 5.25px（10.5 / 2）。
        draw->AddText(cfont, 10.5f,
                      ImVec2(countBox.min.x + 6.0f, CenterTextY(cfont, 10.5f, countBox.center().y)),
                      fg, spec.count.data(), spec.count.data() + spec.count.size());
    }
    return hit.clicked;
}

// ------------------------------------------------------------------ 8 Tabs
std::string_view Tabs(ImDrawList* draw, Rect bounds, const std::vector<SegmentOption>& tabs,
                      std::string_view value, std::string_view id) {
    ImFont* font = FontBoldAt(13.0f);
    float x = bounds.min.x;
    std::string_view picked = value;
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        const SegmentOption& tab = tabs[i];
        const float text =
            font->CalcTextSizeA(13.0f, 1e9f, 0.0f, tab.label.data(),
                                tab.label.data() + tab.label.size())
                .x;
        const Rect item = RectAt(x, bounds.min.y, text + 24.0f, bounds.height());
        const Hit hit = detail::HitTestItem(item, detail::UniqueId(id, static_cast<int>(i)));
        if (hit.clicked) {
            picked = tab.value;
        }
        const bool on = tab.value == value;
        const ImU32 fg = on ? ColorAccent() : (hit.hovered ? ColorText() : ColorTextSecondary());
        // 原来写死 `item.min.y + 8.0f` —— 那个 8 只在页签高 29（`8 + 13 + 8`）时
        // 才对；实际调用方给的是 32（外壳底栏页签条），偏上 1.5px。改按页签中心算。
        draw->AddText(font, 13.0f, ImVec2(item.min.x + 12.0f, CenterTextY(font, 13.0f, item.center().y)),
                      fg, tab.label.data(), tab.label.data() + tab.label.size());
        if (on) {
            // 2px 下划线，左右各内缩 10px
            draw->AddLine(ImVec2(item.min.x + 10.0f, item.max.y - 1.0f),
                          ImVec2(item.max.x - 10.0f, item.max.y - 1.0f), ColorAccent(), 2.0f);
        }
        x = item.max.x;
    }
    return picked;
}

} // namespace shine::kit
