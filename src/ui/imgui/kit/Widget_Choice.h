#pragma once
// shine::kit::Widget_Choice —— 单选系（Segmented / Tabs / Chip）
//
// 从 kit/Widgets.cpp 拆出。三者共享同一份契约：**返回本帧被选中的 value**，
// 展开态不由组件持有（与 Views.h 的约定一致）。Segmented 与 Tabs 还共用
// SegmentOption 这一组选项结构。

#include "ui/imgui/kit/Widget_Core.h"

#include <string_view>
#include <vector>

namespace shine::kit {

// ---- 7. Segmented（UI.jsx:69）----
// 容器 pad3 gap2 fill-muted r6 1px；项 h26 pad 0 13 r4 12.5/600；
// 选中 bg-elevated + shadow + 4px accent 圆点
struct SegmentOption {
    std::string_view value;
    std::string_view label;
};
// 返回被选中的 value（点击时更新 *value）。
std::string_view Segmented(ImDrawList* draw, Rect bounds,
                           const std::vector<SegmentOption>& options, std::string_view value,
                           std::string_view id);
[[nodiscard]] float SegmentedWidth(const std::vector<SegmentOption>& options);

// ---- 7b. Chip（views.css:670-706）----
// 默认 h26 pad 0 11 r-pill 1px line-normal 12/600 secondary；hover line-strong + primary；
// 选中 accent-dim 底 + accent-glow 边 + accent 字。count 非空时右侧画 .cnt
// （10.5px / pad 0 6 / r-pill / fill-muted 底 muted 字；选中态底换成 accent 18%）。
// compact 是资产侧栏的内联覆写（h22 pad 0 8 11px，Shell.jsx:540）——
// 设计稿那处是写死的 style，不是另一个变体，所以做成参数而不是第二份规格。
struct ChipSpec {
    bool selected = false;
    bool compact = false;
    std::string_view count;  // 空 = 不画计数
};
// 返回 true = 本帧被点击。
bool Chip(ImDrawList* draw, Rect bounds, std::string_view label, const ChipSpec& spec,
          std::string_view id);
[[nodiscard]] float ChipHeight(bool compact);
// 量宽：pad×2 + 字宽 + (count ? gap + count 宽 : 0)。
[[nodiscard]] float ChipWidth(std::string_view label, const ChipSpec& spec);

// ---- 8. Tabs ----
// 设计稿里有**三条**「一横排下划线页签」规则，结构相同、尺寸不同。一份实现 + 本规格覆盖，
// 不要再在页面里手写第二份（历史上小说页就手写过一份，dock 那条又错用了 `.tabs` 的尺寸）：
//
// | 出处 | 项内边距 | 字号 | gap | 下划线 | 选中底 | hover 底 |
// |---|---|---|---|---|---|---|
// | `ui.css:245-271` `.tabs` | 8 / 12 | 13 | 0 | 左右各内缩 10 | — | — |
// | `shell.css:382-403` `.dock-tabs` | 5 / 12 / 7 | 12 | 2 | **满宽** | — | — |
// | `views.css` `.novel-modes .ntab` | 上下居中 | 12.5 | 0 | 满宽 | fill-selected | fill-muted |
//
// ⚠️ `.dock-tabs` 的下划线是按钮自己的 `border-bottom: 2px solid`（在盒内、满宽）；
//    `.tabs` 的是 `::after{left:10px;right:10px;bottom:-1px}`（内缩、出盒 1px）。
//    两者**不能**用同一组数字，`underlineInset` 就是这一处差异。
struct TabsSpec {
    float fontSize = 13.0f;        // `.tabs` 13 / `.dock-tabs` 12 / `.ntab` 12.5
    float paddingX = 12.0f;        // 左右内边距
    float paddingY = 8.0f;         // 上下内边距（`.dock-tabs` 上下不等，见下）
    float paddingTop = -1.0f;      // < 0 = 用 paddingY
    float paddingBottom = -1.0f;   // < 0 = 用 paddingY
    float gap = 0.0f;              // `.dock-tabs` 是 2
    float lineHeightRatio = 1.6f;  // base.css 的全局 line-height
    // 页签**盒高**。默认 <0 = 由 paddingTop + 行盒 + paddingBottom 推出（`.tabs` /
    // `.dock-tabs` 就是这样，它们没有声明高度）。
    // ⚠️ `.novel-modes` 是 `align-items:stretch` + `min-height:44px`，页签被**拉满**到
    //    43（44 减容器 1px 下边框），比它自己的内边距推出来高 6px —— 这条别省，
    //    省了 accent 下划线会浮在容器下边框上方 6px，而页签本身只有 37 高。
    float height = -1.0f;
    // 页签**盒内**的底部边框宽（CSS 的 `border-bottom: 2px`，box-sizing:border-box ⇒
    // 它算在盒高里，也算在 padding 之外）。`.dock-tabs` / `.ntab` 都是 2；`.tabs` 是 0。
    // ⚠️ 漏了它页签就矮 2px，而**下划线位置仍对** —— 于是症状是「条与内容之间多 2px
    //    空白」，在整页里几乎看不出来，只有探针坐标会失手。
    float borderBottom = 0.0f;
    float underlineInset = 10.0f;  // `.dock-tabs` / `.ntab` 是 0（满宽）
    // 下划线**中心**相对页签底边的偏移：
    //   `.tabs` 的 `::after{bottom:-1px;height:2px}` ⇒ 跨 [底-1, 底+1] ⇒ 中心在底边 ⇒ 0
    //   `.dock-tabs` 的 `border-bottom:2px`（盒内） ⇒ 跨 [底-2, 底] ⇒ 中心在底-1 ⇒ -1
    float underlineDy = 0.0f;
    float radius = 0.0f;           // `.ntab` 是 6（选中/hover 底要圆角）
    // `.ntab` 的圆角是 `r-sm r-sm 0 0` —— **只有上面两个角**圆，下面两个角是方的
    // （它坐在容器的下边框上）。true 时底/描边走 ImDrawFlags_RoundCornersTop。
    bool roundTopOnly = false;
    bool hoverFill = false;        // 只有 `.ntab` 的 hover 换底
    bool selectedFill = false;     // 只有 `.ntab` 的选中换底（fill-selected）
};

// 一条页签条的高度：paddingTop + 行盒 + paddingBottom(+ 盒内下边框)。
// 调用方自己算版面时用它，别把 8+13+8 这种式子再写第四遍。
[[nodiscard]] float TabsHeight(const TabsSpec& spec = {});

// 返回**本帧被点中的那个 value**；本帧没人点则返回**空**。
// ⚠️ 所以调用方一律用 `if (!picked.empty())` 判「有没有点击」，不要拿返回值当
//    「当前值」用来回写 —— 早先它返回的是入参 `value`，而调用点常写成
//    `Tabs(..., std::to_string(i), ...)`：那个临时 string 在语句结束就析构，
//    返回的 view 当场悬空，`!empty()` 读的是释放后的内存。
//    （症状不是崩溃，是**模式页签永远回落到 0**，于是 8 张模式图两两重样 ——
//     由 `identical-driven-pairs` 判据抓到，静息截图完全看不出来。）
// 同理 `tabs` 里的 `SegmentOption::value` / `label` 也必须是**寿命长过本次调用**的
// 存储（字面量或具名成员），别塞 `std::to_string(...)` 的临时量进去。
std::string_view Tabs(ImDrawList* draw, Rect bounds, const std::vector<SegmentOption>& tabs,
                      std::string_view value, std::string_view id, const TabsSpec& spec = {});

} // namespace shine::kit
