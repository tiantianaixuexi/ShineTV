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

// 上一帧的时间步（秒）。过渡补间要它；pinned 时返回 0。
[[nodiscard]] float LastFrameDelta();

// 动画时钟当前是否被钉住。过渡模块据此决定「走补间」还是「直接落终值」。
[[nodiscard]] bool AnimationPinned();

// ---- 取证用：钉住动画时钟 ----
//
// 用途只有一个：让「像素变化的唯一变量」可以被指定。资产页与总控页有**永不静止**的
// 指示器（Progress 微光、StatusDot 呼吸、Tag busyPulse），它们靠 `Now()` 驱动，
// 于是同一状态下连拍两帧也不逐字节相同 —— 悬停探针分不清「hover 生效了」和
// 「页面正好在闪」，实测 8 个探针里 4 个是这种假信号。
//
// 钉住之后帧与帧之间唯一变的是鼠标位置，A==B 才真的说明页面静止。
//
// ⚠️ 更正一条已经不成立的断言（原来就写在这里）：
//    「本工程所有 hover 样式都是 `hit.hovered ? A : B` 的直接状态切换，没有基于
//    时间的插值」—— **接了 third/ImAnim 之后这句就不成立了**，过渡已经是真的
//    时间插值。改成新语义：
//
//    钉住时，**连续动画**（`Now()` 驱动：脉冲 / 旋转 / 呼吸）冻结在给定时刻；
//    **过渡**（`Anim.h` 的 `TransitionTo` / `TransitionColorTo`）**直接落终值**。
//
//    为什么不冻结过渡：冻在半路 ⇒ 52 张静息态截图拍到的是随机中间色，每轮 md5 都变；
//    落终值 ⇒ 截图确定，hover 探针也照样测得到（终态色 ≠ 静息态色）。
//    所以 `PinAnimation` 还会顺手清一次补间池（`iam_pool_clear`），让在飞的
//    过渡重建时直接以终值起步。
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

// box-shadow。ImGui 没有 blur API，所以按「若干层同心圆角矩形 + 高斯权重 alpha」
// 逼近；逐主题的几何与逐层 alpha 取自 design tokens（见 theme/Theme.cpp 的
// kShadowSpecs 与 Tokens.h 的 theme::ShadowTier）。
//
// ⚠️ `tier` 默认 **None**，这不是偷懒，是设计稿就这样：ui.css:175 的 `.card` 基线
// 规则只有 background / border / radius / transition，**没有 box-shadow**；投影是
// `.card.hoverable:hover`（:196）才加的，还配了 translateY(-2px)。给卡片默认加
// 投影等于凭空发明一个设计稿在静止态根本没有的效果，比不做还偏离。
// 请按 CSS 选择器逐个对：
//   ui.css:947 .modal / :998 .toast / :655 .drawer          → Overlay（常驻）
//   shell.css:101 .menu-pop / :494 .cmdk                     → Overlay（常驻）
//   views.css:1035 .fnode                                     → Card（**常驻**，不是 hover）
//   ui.css:198 .card.hoverable:hover / views.css:969 .tl-card:hover → Card（hover 才有）
//   views.css:725 .asset-card.on / ui.css:861 .snode.run     → Accent
// 完整清单见 Tokens.h 的 theme::ShadowTier 注释。
void DrawShadowed(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 fill,
                  ImU32 border, float borderWidth = 1.0f, theme::ShadowTier tier = theme::ShadowTier::None);

// 只画投影，不画本体。
// `alphaScale` 给「本体带淡入淡出」的场合（toast：设计稿 ui.css:998 的 .toast 有
// --shadow-2，而它的 0.4s 尾部淡出应当**连投影一起淡**，否则影子会在本体消失后还
// 挂一会儿）。1.0 = 不缩放。
// `scale` 给「本体本身被缩放」的场合（FlowCanvas 的节点随 view.z 缩放：投影的
// 偏移与模糊半径必须乘同一个系数，否则缩小后影子会比节点还大）。
void DrawShadow(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, theme::ShadowTier tier,
                float alphaScale = 1.0f, float scale = 1.0f);

// ---- 文本 ----
// CSS text-overflow: ellipsis 的等价：超宽时尾部出省略号。
// 返回实际绘制宽度。
float DrawTextClipped(ImDrawList* draw, ImFont* font, float fontSize, ImVec2 pos, float maxWidth,
                      ImU32 color, std::string_view text, bool wrap = false);

// 量一段文本的宽度（含截断处理后），不绘制。
[[nodiscard]] float MeasureClipped(ImFont* font, float fontSize, float maxWidth,
                                   std::string_view text);

// ⚠️ 这两个是**文字居中的唯一入口**，页面层与组件层都不该再写
//    `centerY - fontSize * 0.5f` 或 `- 6.25f` 这类算式。
//
//    语义要说准，否则很容易被「优化」掉：`ImFont::RenderText` 里
//    `const float line_height = size;` —— **ImGui 的行盒高度就等于请求字号**，
//    基线落在 `pos.y + Ascent*scale`。所以 `centerY - fontSize/2` 居中的正是
//    ImGui 的行盒，**是对的，别改**。
//
//    ⚠️ 别改成「按字体真实 Ascent/Descent 算」：这一版 ImGui 的 `Descent` 是
//    **负数**（本机实测 size=13 时 asc=11 / desc=-3，合计只有 8 而不是 14），
//    照它算会把字往下推 2.5px，方向与「字偏高」正好相反。踩过，已撤回。
//
//    残留的 0.5px 光学偏差（顶栏「运行」按钮实测：墨迹中心比按钮中心高 0.5px）
//    来源是 CJK 墨迹盒中心在基线上方 0.38em，而 Latin 的光学中心不同 —— 一个
//    公式伺候不了两种文字，0.5px 量级不值得为它动 40 处调用点。
[[nodiscard]] float CenterTextY(ImFont* font, float fontSize, float centerY);
[[nodiscard]] float CenterTextX(float minX, float maxX, float textWidth);

// `DrawTextCentered(…, Rect, …)` 需要 `kit::Rect`，而 `Rect` 声明在 Widgets.h
//（Widgets.h 依赖 Draw.h，反过来就成循环包含），所以那个重载放在 Widgets.h。

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

// ---- 命中自检（2026-09-30 加）----
//
// 用途只有一个：让「悬停探针」能区分**「坐标点空了」**和**「这个控件的 hover 链路
// 是断的」**。只看像素差的话这两种情况长得一模一样（都是「悬停前后逐字节相同」），
// 于是我一度把一个「探针没摆好状态」当成产品缺陷去改产品。
//
// 每个 `HitTestImpl` 报告 hovered 时记一次。取证在注入鼠标的那一帧先 Reset 再数：
//   count == 0  → 鼠标没落进任何热区 = **探针坐标写偏了**（工具的问题）
//   count  > 0  且像素没变 → 命中了却什么都不画 = **产品的 hover 链路断了**
// 顺带：静息帧（鼠标用 ImGui 的 -FLT_MAX 哨兵）必须 count == 0，否则哨兵失效，
// 后面所有比较都不可信 —— 这条也会被抓出来。
void NoteHoveredItem(const char* id);
[[nodiscard]] int HoveredItemCount();
void ResetHoveredItemCount();
[[nodiscard]] const char* LastHoveredItem();

} // namespace shine::kit
