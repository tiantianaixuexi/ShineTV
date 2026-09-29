#pragma once
// shine::kit —— 绘制基建（P3.1）与动效开关（P2.7）
//
// 组件套件里所有控件都只依赖这几个原语；页面里不该再出现裸 AddRect。
// 这里刻意不用 ImGui 的 window/child 主题化外观：外壳是自绘的，
// ImGui 窗口只做输入命中与滚动容器。
#include "ui/imgui/theme/Theme.h"

#include <imgui.h>

#include <string>
#include <string_view>

namespace shine::kit {

// ---- 动效（P2.7）----
// 设计稿的 --dur-fast/base/slow 是 120/200/320ms。开启「减少动效」后
// 全部过渡时长压到 1ms（对齐 tokens.css 的 prefers-reduced-motion 分支）。
void SetReduceMotion(bool on);
[[nodiscard]] bool ReduceMotion();

// 单调递增的秒表（每帧推进）。减少动效时仍推进，只是时长被压缩。
void TickAnimation(float deltaSeconds);
[[nodiscard]] float Now();

// ---- 取证用：钉住动画时钟 ----
//
// 用途只有一个：让「像素变化的唯一变量」可以被指定。资产页与总控页有**永不静止**的
// 指示器（Progress 微光、StatusDot 呼吸、Tag busyPulse），它们靠 `Now()` 驱动，
// 于是同一状态下连拍两帧也不逐字节相同 —— 悬停探针分不清「hover 生效了」和
// 「页面正好在闪」，实测 8 个探针里 4 个是这种假信号。
//
// 钉住之后帧与帧之间唯一变的是鼠标位置，A==B 才真的说明页面静止。
// 这不会掩盖 hover 的过渡动画：本工程所有 hover 样式都是 `hit.hovered ? A : B`
// 的**直接状态切换**，没有基于时间的插值（这是钉时钟能用的前提，改样式时别破坏）。
void PinAnimation(float seconds);
void UnpinAnimation();

// 三角波 0..1，用于脉冲/呼吸。
[[nodiscard]] float Pulse(float periodSeconds, float phase = 0.0f);

// ---- 响应式网格（design-spec §8）----
// CSS 的 repeat(auto-fit, minmax(min_col, 1fr)) 在 ImGui 里**不能继承**：
// 必须在 C++ 里按可用宽度算列数。gap 是列间距。
[[nodiscard]] int AutoGridCols(float available, float minColumn, float gap = 14.0f);
// auto-fill 版本：列数相同但保留空轨道（.proj-grid 用）。
[[nodiscard]] int AutoFillCols(float available, float minColumn, float gap = 14.0f);

// ---- 形状 ----
// 圆角矩形。borderWidth > 0 才描边；topHighlight 在上边缘内侧画一条
// 1px 亮线，**近似** CSS 的 box-shadow 上沿高光（已知降级，见 R5）。
void DrawRoundRect(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 fill,
                   ImU32 border = 0, float borderWidth = 0.0f, bool topHighlight = false);

// 竖直渐变。ImGui 的 AddRectFilledMultiColor 是四角双线性，竖直渐变靠
// TL=TR=上色、BL=BR=下色 逼近。
void DrawVGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 top,
                   ImU32 bottom);

// 120° 斜渐变（设计稿的 grad-accent）。用四角的中间色逼近。
void DrawDiagGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 from,
                      ImU32 to);

// 水平渐变胶囊（项目胶囊里的 8×8 渐变点、KPI 卡、开关轨道）。
void DrawHGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 from,
                   ImU32 to);

// box-shadow 的近似：只有「上边框高光 + 一圈 scrim 描边」，
// 不做真正的模糊。验收按「有没有浮起来」判，不按阴影像素判（design-spec §2.2）。
void DrawShadowed(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 fill,
                  ImU32 border, float borderWidth = 1.0f);

// ---- 文本 ----
// CSS text-overflow: ellipsis 的等价：超宽时尾部出省略号。
// 返回实际绘制宽度。
float DrawTextClipped(ImDrawList* draw, ImFont* font, float fontSize, ImVec2 pos, float maxWidth,
                      ImU32 color, std::string_view text, bool wrap = false);

// 量一段文本的宽度（含截断处理后），不绘制。
[[nodiscard]] float MeasureClipped(ImFont* font, float fontSize, float maxWidth,
                                   std::string_view text);

// ---- 杂项 ----
// 点阵背景：radial-gradient(circle at 1px 1px, line-normal 1px, transparent 0) 0 0 / 22px 22px
void DrawDotGrid(ImDrawList* draw, ImVec2 min, ImVec2 max, float cell, ImU32 dot);

// 毛玻璃降级：ImGui 直绘没有背景合成，用 --glass 实色（已知降级，见 R4）。
[[nodiscard]] ImU32 GlassColor();
// 预混 12 组派生色里当前主题的取值。
[[nodiscard]] ImU32 ToneColor(theme::Tone tone);
[[nodiscard]] ImU32 ToneBackground(theme::Tone tone);
[[nodiscard]] ImU32 ToneBorder(theme::Tone tone);

// ---- 反向矩形自检（2026-09-30 加）----
//
// `kit::Rect` 的四参构造是 (minX, minY, maxX, maxY)，而人写出来十有八九是
// (x, y, w, h)。传错**不报编译错**，只是 max < min，于是
// `DrawRoundRect` 与 `HitTestImpl` 的 `if (max <= min) return;` 把整个控件丢掉：
// 不画、不可点，界面上留下一片空白，日志与 manifest 全绿。
//
// `tools/find-rect-wh-misuse.ps1` 靠「第 3 参像尺寸、第 4 参是裸字面量」的启发式找，
// 必然漏。这里改成**运行时兜底**：每个绘制 / 命中入口都报一次，取证跑完 52 张
// 覆盖全部 7 个工作区后若计数 > 0 就判 FAIL —— 漏网的实例会自己举手。
//
// 只记**严格反向**（max < min）。退化（max == min，如宽高为 0 的空 tag）是合法用法。
void NoteInvertedRect(float minX, float minY, float maxX, float maxY, const char* where);
[[nodiscard]] int InvertedRectCount();
void ResetInvertedRectCount();
// 最近一次反动的坐标，manifest 里原样打出来 —— 坐标通常就足以定位到是哪个面板。
[[nodiscard]] const char* LastInvertedRect();

} // namespace shine::kit
