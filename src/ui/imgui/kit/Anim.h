// 过渡动画：CSS `transition` 的 ImGui 侧实现。
//
// 设计稿的 43 处 `transition` 拆开看只有三类值：
//   · 颜色（56 个槽位，占绝大多数）
//   · transform（18 处，主要是 translate / rotate）
//   · width（仅 1 处）
// 所以**不需要**通用动画引擎，只需要「一个状态 → 0..1 的进度」加上按进度插值。
//
// 引擎选 third/ImAnim（`shine_imanim` 目标）：它的 `iam_tween_float(id, channel,
// target, dur, ease, policy, dt)` 直接返回当前动画值，**不依赖任何 ImGui item** ——
// 而本工程的控件全是自绘的（只往 ImDrawList 塞 draw call，从不调 ImGui::Button），
// 这正好是它能直接用的原因。选一个基于 item 生命周期的引擎会完全用不上。
//
// ## 取证时的语义（这一条是整个模块最容易做错的地方）
//
// `kit::PinAnimation()` 会把动画时钟钉住，让 12 个悬停探针的两帧「无鼠标」完全一致
// （否则 Progress 微光 / StatusDot 呼吸会让 A≠B，探针分不清「hover 生效」和
// 「页面正好在闪」）。但**时钟钉住不等于过渡要冻在半路**：
//
//   · 过渡**冻在半路** ⇒ 52 张静息态截图拍到的是随机的中间色，md5 每轮都变；
//   · 过渡**直接落终值** ⇒ 截图确定，hover 探针也照样能测（终态色 ≠ 静息态色）。
//
// 所以本模块在 pinned 期间**直接返回 target**，不参与补间。连续动画（脉冲、旋转）
// 走的是另一条路（`Draw.h` 的 `Now()`），那才是真正需要被冻住的。
#pragma once

#include <cstdint>

#include "imgui.h"

namespace shine::kit {

// CSS 的三档时长（tokens.css:24-26 的 --dur-1/2/3）。单位是**秒**——ImAnim 要秒。
inline constexpr float kDurFast = 0.120f;
inline constexpr float kDurBase = 0.200f;
inline constexpr float kDurSlow = 0.320f;

// 状态 → 0..1 的过渡进度。`target` 是这一帧该到的状态（true=1 / false=0）。
//
// `id` 与 `channel` 共同定位一条补间。**同一个控件每帧必须传同一对 id**：
// 传错 id 不会报错，只会变成两条各自为政的补间在互相拉扯（值永远到不了 1）。
// 建议 id 用组件自己的 hit id（`ChromeHit` 那套），channel 区分同一控件的多个通道
// （比如 0=悬停，1=按下）。
//
// pinned 期间直接返回 target，理由见文件头。
[[nodiscard]] float TransitionTo(ImGuiID id, ImGuiID channel, bool target, float durSeconds);

// 颜色的同类封装。返回本帧该用的 ImU32。
//
// ⚠️ 颜色补间的插值空间：这里用 `iam_col_srgb`（直接在 sRGB 分量上线性插），
//    与浏览器默认行为一致。`--ease` / `--ease-out` 都作用在这个插值上。
[[nodiscard]] ImU32 TransitionColorTo(ImGuiID id, ImGuiID channel, ImU32 target,
                                     float durSeconds);

// 补间池的预分配。启动时调一次，避免运行中第一次悬停才扩容。
void ReserveTweenPool(std::size_t floats, std::size_t vec2, std::size_t vec4, std::size_t ints,
             std::size_t colors);

}  // namespace shine::kit
