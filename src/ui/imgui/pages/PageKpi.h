#pragma once
// shine::pages —— KPI 卡（右上角 accent 柔光 + 数值 + 单位 + 口径脚注）
//
// 为什么提成组件：总控页一处定义、五处调用（阶段进度 / LLM 调用 / 估算成本 /
// 镜头 / 阶段产物）。它原先是 WorkspaceA.cpp 匿名命名空间里的 `KpiCard`，而
// 「右上角那团柔光」是**整页 KPI 行的共同视觉**，不是某一页的私有装饰。
//
// 收录标准（与 PageCommon.h 同一把尺）：**换一页也照样成立**。目前只有总控页
// 用它，但那不是收录的理由也不是不收录的理由 —— 理由是这 5 张卡**逐像素同构**，
// 复制一份就多一处会各自漂移的数值。
// ⚠️ 走 kit/Widgets.h 这层**门面**（它转 include 其余 Widget_*.h），不要直接写
//    某个 Widget_*.h：kit 正在按组件拆，下游 include 门面才不会跟着动。
#include "ui/imgui/kit/Widgets.h"
#include "ui/imgui/theme/Theme.h"

#include <cstdint>
#include <string_view>

namespace shine::pages {

// KPI 卡高与行距（`.kpis` 的 `gap`）。**全树只存这一份**。
//
// 卡高来自 CSS 的**一条算式**（`views.css:409-456`，行高 1.6 来自 `base.css:16`
// 的全局 line-height，`.kpi` / `.k-label` / `.k-value` / `.k-foot` 都没有覆盖它）：
//
//   边框 1 + padding 14 + 标签盒(11.5×1.6 = 18.4) + 数值 margin 4
//   + 数值盒(24×1.6 = 38.4) + 脚注 margin 6 + 脚注盒(11.5×1.6 = 18.4)
//   + padding 14 + 边框 1                                            = **115.2**
//
// ⚠️ 实测（Chrome 无头 + 设计稿 dist 的真实 CSS，`getBoundingClientRect`）= **115.171875**，
//    与算式一致（差在半点字号的浮点尾数）。**早先这里写死 96** —— 少了 19.2px，
//    三段文字被整体压扁，而「卡矮了」在静息截图上只会读成「排版有点紧」，不会读成缺陷。
//
// 这两个值要同时喂给「卡片矩形」与 `WorkspaceA.cpp` 的 `gridTop`：两处各写一个字面量
// 就会在改一个的时候漏掉另一个（那一处早先写的就是 `(96.0f + 14.0f) * rows`）。
inline constexpr float kKpiCardHeight = 115.2f;
inline constexpr float kKpiRowGap = 14.0f;

// 卡片本体。
//
// ⚠️ **没有 `tone` 形参**：设计稿那团柔光是 `background: var(--accent-dim)`
//    （`views.css:427`），五张卡**同色**。早先按 `theme::Tone` 取色是没记录在案的
//    偏差 —— 4/5 张卡的颜色是错的（只有 Tone::Accent 那张碰巧对）。`PageKpi.h`
//    自己的注释当时也自相矛盾（抬头写「accent 柔光」、形参写「按色调取色」）。
void KpiCard(ImDrawList* draw, kit::Rect bounds, std::string_view label, std::string_view value,
             std::string_view unit, std::string_view footnote);

// 柔光的**实际像素色**（`0xRRGGBB`，已按 `opacity: .7` 合成到卡底色上）。
//
// 为什么要有这个出口：柔光的色是**主题相关**的（accent-dim 逐主题不同），而取证那边
// 「数某个纯色的像素」需要的是**最终落在屏上的那个值**。写死在取证里会让判据只在
// 默认主题下成立；让取证自己去做 alpha 合成则是第二份实现（两边迟早分叉）。
// 所以由页面交出它当帧用的那个色 —— 与 `ComfyRunningForBadge()` 同一类「只读出口」。
[[nodiscard]] std::uint32_t KpiGlowRgb();

} // namespace shine::pages
