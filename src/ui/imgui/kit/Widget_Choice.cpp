#include "ui/imgui/kit/Widget_Choice.h"

#include "ui/imgui/kit/Anim.h"
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
        // 项 id 栈上拼（每帧 × 每项都走这里，不许堆分配）。
        char itemIdBuf[160];
        std::string itemIdOverflow;
        const Hit hit = detail::HitTestItem(
            item, detail::FormatUniqueId(itemIdBuf, sizeof(itemIdBuf), id, "", static_cast<int>(i),
                                         &itemIdOverflow));
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
        // 补间版：非选中项 hover 的文字色硬切换走 ch3 色补间（kDurFast）。目标式与
        // 原硬切换逐字相同 —— 选中项恒 ColorText()，hover 反馈照旧没有，终值不变。
        const ImU32 fg = TransitionColorTo(
            hit.tid, 3, on ? ColorText() : (hit.hovered ? ColorText() : ColorTextSecondary()),
            kDurFast);
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
    // 补间版：hover 底 / 选中底（ch0）与描边（ch2）/文字（ch3）的硬切换全部走色补间
    // （kDurFast，含 alpha）；三条目标式与原硬切换逐字相同，settle 后逐值相同。
    const ImU32 bg = TransitionColorTo(
        hit.tid, 0,
        spec.selected ? ColorAccentDim() : (hit.hovered ? ColorFillMuted() : ColorTransparent()),
        kDurFast);
    const ImU32 border = TransitionColorTo(
        hit.tid, 2,
        spec.selected ? ColorAccentGlow() : (hit.hovered ? ColorLineStrong() : ColorLineNormal()),
        kDurFast);
    const ImU32 fg = TransitionColorTo(
        hit.tid, 3,
        spec.selected ? ColorAccent() : (hit.hovered ? ColorText() : ColorTextSecondary()),
        kDurFast);
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
        // 补间版：计数底的选中态硬切换走 ch4 色补间（kDurFast；.cnt 是子结构，
        // 组内 0/2/3 已被底/描边/文字占用）。
        DrawRoundRect(draw, countBox.min, countBox.max, countBox.height() * 0.5f,
                      TransitionColorTo(hit.tid, 4,
                                        spec.selected ? ColorAccentDim() : ColorFillMuted(),
                                        kDurFast));
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
namespace {

float TabsPadTop(const TabsSpec& spec) {
    return spec.paddingTop >= 0.0f ? spec.paddingTop : spec.paddingY;
}

float TabsPadBottom(const TabsSpec& spec) {
    return spec.paddingBottom >= 0.0f ? spec.paddingBottom : spec.paddingY;
}

} // namespace

float TabsHeight(const TabsSpec& spec) {
    if (spec.height > 0.0f) {
        return spec.height;
    }
    // border-box：页签自己的 `border-bottom` 也算在盒高里，所以要加回来。
    return TabsPadTop(spec) + spec.fontSize * spec.lineHeightRatio + TabsPadBottom(spec) +
           spec.borderBottom;
}

std::string_view Tabs(ImDrawList* draw, Rect bounds, const std::vector<SegmentOption>& tabs,
                      std::string_view value, std::string_view id, const TabsSpec& spec) {
    ImFont* font = FontBoldAt(spec.fontSize);
    // ⚠️ bounds 只用来定**起点和行高**；每个页签的宽由 `字宽 + 2×paddingX` 决定。
    //    早先写的是 `text + 24.0f`（= 2×12）—— 与 paddingX 分开两份，改一处必漏另一处。
    //    行高也一样：早先直接用调用方给的 bounds.height()（32），而 CSS 的页签高是
    //    `paddingTop + 行盒 + paddingBottom`（`.tabs` 是 8+20.8+8 = 36.8）——
    //    给 32 就等于把上下内边距压掉，文字看着偏上。
    const float itemH = TabsHeight(spec);
    const float y = bounds.min.y;
    float x = bounds.min.x;
    // 空 = 本帧没人点（见头文件的契约）。
    std::string_view picked;
    // 下划线滑动（补间版）的帧稳定键：指示条属于**整组**，不能挂在随选中项变化的
    // id 上 —— 换键等于新补间直接落 target，成了跳变而不是滑动。取第 0 项的
    // hit.tid（每帧同一对 (id, channel)；组内 4/5 通道空闲）。tabs 为空时循环
    // 不执行，这两个值用不到。
    ImGuiID underlineTid = 0;
    bool underlineSlid = false;
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        const SegmentOption& tab = tabs[i];
        const float text =
            font->CalcTextSizeA(spec.fontSize, 1e9f, 0.0f, tab.label.data(),
                                tab.label.data() + tab.label.size())
                .x;
        const Rect item = RectAt(x, y, text + 2.0f * spec.paddingX, itemH);
        // 项 id 栈上拼（每帧 × 每项都走这里，不许堆分配）。
        char itemIdBuf[160];
        std::string itemIdOverflow;
        const Hit hit = detail::HitTestItem(
            item, detail::FormatUniqueId(itemIdBuf, sizeof(itemIdBuf), id, "", static_cast<int>(i),
                                         &itemIdOverflow));
        if (hit.clicked) {
            picked = tab.value;
        }
        const bool on = tab.value == value;
        if (i == 0) {
            underlineTid = hit.tid;
        }
        // 选中底 / hover 底都只在 spec 要求时画。三条 CSS 规则里只有 `.ntab` 有这两个底；
        // `.tabs` / `.dock-tabs` 的选中只换字色 + 下划线。
        const auto fillTab = [&](ImU32 color) {
            if (spec.roundTopOnly) {
                // `.ntab` 坐在容器的 1px 下边框上 ⇒ 下面两个角必须是方的。
                draw->AddRectFilled(item.min, item.max, color, spec.radius,
                                    ImDrawFlags_RoundCornersTop);
            } else {
                DrawRoundRect(draw, item.min, item.max, spec.radius, color);
            }
        };
        // 补间版（.ntab）：选中底 fill-selected / hover 底 fill-muted 的硬切换合成一条
        // ch0 色补间（kDurFast，含 alpha），目标式与原 if/else-if 逐值相同；settle 在
        // 「无底」一侧目标是全透明 ⇒ 不进 fillTab，与原样「什么都不画」逐像素相同。
        // 两条 CSS 规则都没有这两个底的（.tabs / .dock-tabs）保持原样：一个补间都不建。
        if (spec.selectedFill || spec.hoverFill) {
            const ImU32 tabFillNow = TransitionColorTo(
                hit.tid, 0,
                (on && spec.selectedFill)
                    ? ColorFillSelected()
                    : ((hit.hovered && spec.hoverFill) ? ColorFillMuted() : ImU32(0)),
                kDurFast);
            if (tabFillNow != 0) {
                fillTab(tabFillNow);
            }
        }
        // 补间版：非选中页签 hover 的文字色硬切换走 ch3 色补间（kDurFast）。选中页签
        // 恒 accent（没有 hover 反馈），目标式与原硬切换逐字相同。
        const ImU32 fg = TransitionColorTo(
            hit.tid, 3, on ? ColorAccent() : (hit.hovered ? ColorText() : ColorTextSecondary()),
            kDurFast);
        draw->AddText(font, spec.fontSize,
                      ImVec2(item.min.x + spec.paddingX,
                             CenterTextY(font, spec.fontSize, item.center().y)),
                      fg, tab.label.data(), tab.label.data() + tab.label.size());
        if (on) {
            const float uy = item.max.y + spec.underlineDy;
            // 补间版：下划线/指示条的 x/宽用 ch4/ch5 float 补间滑动（kDurBase，
            // TransitionFloatTo 自带 ease-out）。settle 后 x0/x1 恰为原算式本值，
            // 与硬切换逐值相同。⚠️ 只有**第一条**选中项参与滑动：value 撞上多项
            //    （病态输入，原样会各画一条）时其余仍画各自的静态几何，settle 后
            //    与原样逐值相同。
            float ux0 = item.min.x + spec.underlineInset;
            float ux1 = item.max.x - spec.underlineInset;
            if (underlineTid != 0 && !underlineSlid) {
                ux0 = TransitionFloatTo(underlineTid, 4, ux0, kDurBase);
                ux1 = TransitionFloatTo(underlineTid, 5, ux1, kDurBase);
                underlineSlid = true;
            }
            draw->AddLine(ImVec2(ux0, uy), ImVec2(ux1, uy), ColorAccent(), 2.0f);
        }
        x = item.max.x + spec.gap;
    }
    return picked;
}

} // namespace shine::kit
