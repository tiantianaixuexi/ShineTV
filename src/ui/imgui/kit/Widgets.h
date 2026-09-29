#pragma once
// shine::kit::Widgets —— 设计稿 20 个组件的 ImGui 实现（P3.2 / P3.3）
//
// 权威是 webui/src/components/UI.jsx（文件头自己写着「对应 Qt 端 kit::widgets
// 语义」）+ webui/src/styles/ui.css 的尺寸。**不是**把 Qt 的 32 个 widget 逐个翻译。
//
// 全部尺寸来自 design-spec.md §5，逐条标了 CSS 出处。这里只写契约，实现见 .cpp。
//
// 统一约定：
//   * 所有控件吃 **绝对矩形**（design-spec 的布局是 CSS 绝对定位式），
//     不走 ImGui 的线性布局 —— 否则外壳的自绘栅格无法控制。
//   * 命中测试统一用 InvisibleButton 占位（保持 ImGui 的 ID 栈与焦点语义）。
//   * 颜色一律从 theme 取，**不许**在调用点写 ImVec4 字面量（check-layers 规则 3）。
#pragma once

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/theme/Theme.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace shine::kit {

struct Rect {
    ImVec2 min;
    ImVec2 max;
    // 显式两套构造：ImVec2 不是聚合类型，四个裸 float 的字面量要能直接初始化。
    Rect() = default;
    Rect(ImVec2 a, ImVec2 b) : min(a), max(b) {}
    Rect(float x0, float y0, float x1, float y1) : min(x0, y0), max(x1, y1) {}
    [[nodiscard]] float width() const { return max.x - min.x; }
    [[nodiscard]] float height() const { return max.y - min.y; }
    [[nodiscard]] ImVec2 center() const { return ImVec2(0.5f * (min.x + max.x), 0.5f * (min.y + max.y)); }
    [[nodiscard]] bool contains(ImVec2 p) const { return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y; }
};

[[nodiscard]] inline Rect RectAt(float x, float y, float w, float h) {
    return Rect{ImVec2(x, y), ImVec2(x + w, y + h)};
}

// ---- 命中测试 ----
// 所有自绘控件都靠它在绝对位置占一个不可见 item，ImGui 才能维护 ID 栈与焦点。
struct Hit {
    bool hovered = false;
    bool held = false;
    bool clicked = false;
    bool doubleClicked = false;
};
[[nodiscard]] Hit HitTest(Rect bounds, std::string_view id);
// 外壳/页面层最常用的一条：本帧是否点了这块矩形。
[[nodiscard]] bool Clicked(Rect bounds, std::string_view id);
[[nodiscard]] bool Hovered(Rect bounds, std::string_view id);

// 主题相关的常用取色（控件内部用，页面不要直接拼 ImU32）。
[[nodiscard]] ImU32 ColorOf(std::uint32_t rgba);
[[nodiscard]] ImU32 ColorText();
[[nodiscard]] ImU32 ColorTextSecondary();
[[nodiscard]] ImU32 ColorTextMuted();
[[nodiscard]] ImU32 ColorTextInverse();
[[nodiscard]] ImU32 ColorSurface();
[[nodiscard]] ImU32 ColorPanel();
[[nodiscard]] ImU32 ColorElevated();
[[nodiscard]] ImU32 ColorOverlay();
[[nodiscard]] ImU32 ColorVoid();
[[nodiscard]] ImU32 ColorFillHover();
[[nodiscard]] ImU32 ColorFillSelected();
[[nodiscard]] ImU32 ColorFillMuted();
[[nodiscard]] ImU32 ColorLineSubtle();
[[nodiscard]] ImU32 ColorLineNormal();
[[nodiscard]] ImU32 ColorLineStrong();
[[nodiscard]] ImU32 ColorAccent();
[[nodiscard]] ImU32 ColorAccentHover();
[[nodiscard]] ImU32 ColorAccentFg();
[[nodiscard]] ImU32 ColorAccentDim();
[[nodiscard]] ImU32 ColorAccentGlow();
[[nodiscard]] ImU32 ColorFocusRing();
[[nodiscard]] ImU32 ColorScrim();

// 半透明**调制**：结果 alpha = 原 alpha × alpha。传 1.0 保持原样。
// 这是给「已经在派生色里带了 alpha」的场景用的（tagBg 12% / gateBg 10% / accentGlow 30%），
// 参数只用来做动画调制（脉冲、hover 渐变），不要用它把 12% 的淡底拉成 100% 实心。
[[nodiscard]] ImU32 WithAlpha(ImU32 color, float alpha);

// 半透明**覆盖**：直接把 alpha 设成给定值。用于实色补透明度（阴影、scrim、叠层）。
[[nodiscard]] ImU32 WithAlphaSet(ImU32 color, float alpha);
[[nodiscard]] ImU32 LerpColorTo(ImU32 from, ImU32 to, float t);

// ---- 1. Button（UI.jsx:7）----
// md h30 / pad 0 14 / r6 / 13px / 600；sm h24 pad 0 10 12px；lg h36 pad 0 20 14px r10
// 图标 15（sm 13）；:active scale(.97)；disabled opacity .45
enum class ButtonVariant { Primary, Secondary, Ghost, Danger };
enum class ButtonSize { Small, Medium, Large };

struct ButtonSpec {
    ButtonVariant variant = ButtonVariant::Ghost;
    ButtonSize size = ButtonSize::Medium;
    std::string_view icon;      // 空 = 无图标
    bool loading = false;       // 转圈占位 13×13
    bool disabled = false;
};

[[nodiscard]] float ButtonHeight(ButtonSize size);
[[nodiscard]] float ButtonWidth(ButtonSize size, float iconWidth, float textWidth);
[[nodiscard]] float ButtonPadding(ButtonSize size);
// 返回 true = 本帧被点击（disabled / loading 时恒 false）。
bool Button(ImDrawList* draw, Rect bounds, std::string_view label, const ButtonSpec& spec,
            std::string_view id);

// ---- 2. IconBtn（UI.jsx:17）----
// 28×28 r6（sm 22×22），图标 16（sm 13）；hover fill-hover；active fill-selected+accent
bool IconButton(ImDrawList* draw, Rect bounds, std::string_view icon, bool active, bool disabled,
                std::string_view id, std::string_view tip = {});

// ---- 3. Tag（UI.jsx:32）----
// h20 pad 0 8 r-pill 11.5/600 1px 边；sm h17 pad 0 6 10.5；7 色调；可选 7px 圆点
void Tag(ImDrawList* draw, Rect bounds, std::string_view label, theme::Tone tone, bool small = false,
         bool dot = false, bool busyPulse = false);
[[nodiscard]] float TagWidth(std::string_view label, bool small, bool dot);
[[nodiscard]] float TagHeight(bool small);

// ---- 4. StatusDot（UI.jsx:42）----
// 7×7 圆；7 色调；run 时 1.6s 脉冲环
void StatusDot(ImDrawList* draw, ImVec2 center, theme::Tone tone, bool run);

// ---- 5. Kbd（UI.jsx:47）----
// min-w18 h18 pad 0 5 r4，1px 边 + 下边 2px，10.5/600
void Kbd(ImDrawList* draw, Rect bounds, std::string_view label);
[[nodiscard]] float KbdWidth(std::string_view label);

// ---- 6. Card（UI.jsx:52）----
// bg-panel + 1px line-subtle + r10；头 pad 12/16 + 下边框；体 pad 16；
// hover = line-strong + 上浮 2px；glow = accent 边 + accent 辉光
// 返回内容区（头之下、体之内）。
Rect Card(ImDrawList* draw, Rect bounds, std::string_view title, std::string_view icon,
          bool hoverable, bool glow);
// 头部右侧的自绘控件区（extra 按钮放这里）。
[[nodiscard]] Rect CardHeaderRow(Rect card, std::string_view title, std::string_view icon);

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

// ---- 8. Tabs（UI.jsx:86）----
// 项 pad 8/12 13/600；选中 accent + 2px 下划线（左右各内缩 10px）
std::string_view Tabs(ImDrawList* draw, Rect bounds, const std::vector<SegmentOption>& tabs,
                      std::string_view value, std::string_view id);

// ---- 9. Field（UI.jsx:103）----
// 竖排 gap6；标签 12px/600 secondary；help 12px muted。返回控件该放的位置。
Rect Field(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view help);

// ---- 10-12. Input / TextArea / Select ----
// Input h30 pad 0 10 r6 fill-muted 1px line-normal；
// focus = line-focus 边 + 0 0 0 3px accent-dim 外环
bool Input(ImDrawList* draw, Rect bounds, std::string& value, std::string_view placeholder,
           std::string_view id);
bool TextArea(ImDrawList* draw, Rect bounds, std::string& value, int lines, std::string_view id);
bool Select(ImDrawList* draw, Rect bounds, const std::vector<std::string>& options,
            int& index, std::string_view id);

// ---- 13. Switch（UI.jsx:131）----
// 34×19 r-pill；旋钮 13×13 位移 15px
bool Switch(ImDrawList* draw, Rect bounds, bool on, std::string_view id);

// ---- 14. Checkbox（UI.jsx:133）----
// 盒 15×15 r4 1.5px 边 + 内联对勾（accent-fg）
bool Checkbox(ImDrawList* draw, Rect bounds, bool on, std::string_view label, std::string_view id);

// ---- 15. Progress（UI.jsx:145）----
// 轨 h6（thin 4）r-pill；填充 grad-accent；run 时叠 100° 白 35% 微光扫过
void Progress(ImDrawList* draw, Rect bounds, float value, bool run, bool thin);
[[nodiscard]] float ProgressHeight(bool thin);

// ---- 16. Empty（UI.jsx:154）----
// 居中 gap10 pad 40/20；字形 52×52 r14 虚线边 + 24px 图标 + 4s 上下浮动；
// 标题 13/600；正文 max-w 320
void Empty(ImDrawList* draw, Rect bounds, std::string_view icon, std::string_view title,
           std::string_view body);

// ---- 17. KV（UI.jsx:165）----
// 网格 auto 1fr gap 6px 14px，12.5；键 muted，值 primary/500
void KeyValues(ImDrawList* draw, Rect bounds,
               const std::vector<std::pair<std::string, std::string>>& rows);

} // namespace shine::kit
