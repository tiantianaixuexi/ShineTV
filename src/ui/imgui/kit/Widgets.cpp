#include "ui/imgui/kit/Widgets.h"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace shine::kit {
namespace {

// 命中测试：在绝对位置放一个不可见 item，让 ImGui 维护 ID 栈与焦点。
// 返回 hovered / active / clicked / doubleClicked。
Hit HitTestImpl(Rect bounds, std::string_view id) {
    Hit hit;
    // 命中测试同样吃「反向矩形」这一套：InvisibleButton 的尺寸取自
    // width()/height()，反向时是负数，ItemAdd 出来的区域是反的 —— 不崩、不报，
    // 但这个控件**点不中**。所以和 DrawRoundRect 共用同一个计数。
    if (bounds.max.x < bounds.min.x || bounds.max.y < bounds.min.y) {
        NoteInvertedRect(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y, "HitTest");
    }
    ImGui::SetCursorScreenPos(bounds.min);
    const std::string idStr(id);
    // 同一帧内重叠热区自检：ImGui 先注册者独占，第二个 InvisibleButton
    // 永远 clicked=false，而它上面的控件外观画得好好的。调用点在 kit 组件
    // 之外补第二次命中是最常见的触发方式。
    // ⚠️ 传 idStr.c_str() 而不是 id.data()：string_view 的 data() **不保证**
    //    以 '\0' 结尾，而 NoteDuplicateHit 内部按 C 字符串写进定长缓冲，
    //    拿 string_view 的裸指针会越界读。
    NoteDuplicateHit(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y, idStr.c_str());
    ImGui::InvisibleButton(idStr.c_str(), ImVec2(bounds.width(), bounds.height()));
    hit.hovered = ImGui::IsItemHovered();
    hit.held = ImGui::IsItemActive();
    hit.clicked = ImGui::IsItemClicked();
    hit.doubleClicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    // 命中自检：取证要能分清「鼠标没落进热区」与「命中了却什么都不画」。
    // 记在 InvisibleButton **之后**、返回之前 —— 早先返回前漏了这一句，
    // 记的是上一帧的 hovered，探针的判据就成了延迟一帧的假信号。
    if (hit.hovered) {
        NoteHoveredItem(idStr.c_str());
    }
    return hit;
}
std::string Unique(std::string_view id, int index) {
    return std::string(id) + "##" + std::to_string(index);
}

void DrawSpinner(ImDrawList* draw, ImVec2 center, float radius, ImU32 color) {
    const float angle = Now() * 5.2f; // .7s 一圈（UI.jsx 的 .spin .7s linear infinite）
    const int segments = 10;
    for (int i = 0; i < segments; ++i) {
        const float a0 = angle + static_cast<float>(i) * 2.0f * 3.14159265f / 12.0f;
        const ImU32 shade = WithAlpha(color, 0.15f + 0.85f * static_cast<float>(i) / segments);
        draw->PathArcTo(center, radius, a0, a0 + 0.45f, 4);
        draw->PathStroke(shade, 0, std::max(1.0f, radius * 0.28f));
        draw->PathClear();
    }
}

float FontSizeOf(ButtonSize size) {
    switch (size) {
    case ButtonSize::Small: return 12.0f;
    case ButtonSize::Large: return 14.0f;
    case ButtonSize::Medium: break;
    }
    return 13.0f;
}

} // namespace

// 对外的命中测试（外壳/页面层用）。必须在匿名命名空间之外，
// 内部版叫 HitTest，名字不能重。
Hit HitTest(Rect bounds, std::string_view id) { return HitTestImpl(bounds, id); }
bool Clicked(Rect bounds, std::string_view id) { return HitTestImpl(bounds, id).clicked; }
bool Hovered(Rect bounds, std::string_view id) { return HitTestImpl(bounds, id).hovered; }

// ------------------------------------------------------------------ 取色
ImU32 ColorOf(std::uint32_t rgba) { return theme::ToImU32((rgba)); }
ImU32 ColorText() { return ColorOf(theme::Current().textPrimary); }
ImU32 ColorTextSecondary() { return ColorOf(theme::Current().textSecondary); }
ImU32 ColorTextMuted() { return ColorOf(theme::Current().textMuted); }
ImU32 ColorTextInverse() { return ColorOf(theme::Current().textInverse); }
ImU32 ColorSurface() { return ColorOf(theme::Current().bgSurface); }
ImU32 ColorPanel() { return ColorOf(theme::Current().bgPanel); }
ImU32 ColorElevated() { return ColorOf(theme::Current().bgElevated); }
ImU32 ColorOverlay() { return ColorOf(theme::Current().bgOverlay); }
ImU32 ColorVoid() { return ColorOf(theme::Current().bgVoid); }
ImU32 ColorFillHover() { return ColorOf(theme::Current().fillHover); }
ImU32 ColorFillSelected() { return ColorOf(theme::Current().fillSelected); }
ImU32 ColorFillMuted() { return ColorOf(theme::Current().fillMuted); }
ImU32 ColorLineSubtle() { return ColorOf(theme::Current().lineSubtle); }
ImU32 ColorLineNormal() { return ColorOf(theme::Current().lineNormal); }
ImU32 ColorLineStrong() { return ColorOf(theme::Current().lineStrong); }
ImU32 ColorAccent() { return ColorOf(theme::Current().accentPrimary); }
ImU32 ColorAccentHover() { return ColorOf(theme::Current().accentPrimaryHover); }
ImU32 ColorAccentFg() { return ColorOf(theme::Current().accentPrimaryFg); }
ImU32 ColorAccentDim() { return ColorOf(theme::CurrentDerived().accentDim); }
ImU32 ColorAccentGlow() { return ColorOf(theme::CurrentDerived().accentGlow); }
ImU32 ColorFocusRing() { return ColorOf(theme::Current().lineFocus); }
ImU32 ColorScrim() { return ColorOf(theme::Current().shadowScrim); }

// 「不画」而不是「画黑色」：返回全 0，让 draw call 的 alpha 通道自己把它关掉。
//
// ⚠️ 这里**故意**不用 `IM_COL32(0,0,0,0)`：那正是 tools/check-colors.ps1 要拦的
//    硬编码颜色字面量，写在这里会让「唯一合规的出口」自己成为违规样本。
//    ImU32 的字节序宏（IMGUI_USE_BGRA_PACKED_COLOR 决定 R 在高位还是低位）对全 0
//    无所谓 —— 两种字节序下 0 都是 0，所以写字面量是安全的。
constexpr ImU32 kTransparent = 0u;
ImU32 ColorTransparent() { return kTransparent; }

// ImU32 的字节序是编译期宏（IMGUI_USE_BGRA_PACKED_COLOR 决定 R 在高位还是低位）。
// 这两个函数一律走 ImGui 自己的 float4 往返，**不手写位移** ——
// 手写过一次：默认字节序下 R 在最低字节，于是 alpha 被写进了红通道，
// 所有半透明叠层（tag 底 12%、gate 底 10%、KPI 光斑）其实全是 100% 不透明。
// ⚠️ 语义是**相乘调制**，不是「设为该 alpha」。
// 设成 1.0 就等于把颜色原本的 alpha 抹成全不透明，而预混派生色（tagBg 12%、
// gateBg 10%、ganttCell 12%）恰恰靠 alpha 表达"淡底"这个 CSS color-mix 语义
// （ui.css:139-144 `color-mix(in srgb, var(--ok) 12%, transparent)`）。
// 曾经按「设为」写，于是 Tag 传 busyPulse 的 1.0 → 12% 底变成 100% 实心，
// 绿字压在绿底上完全读不出来（assets 详情页的「已确认」）。
ImU32 WithAlpha(ImU32 color, float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

// 需要**覆盖** alpha（而不是调制）时用这个，名字自带"覆盖"语义。
// 只在明确知道目标 alpha 时用：阴影/scrim 叠层、以及给无 alpha 的实色补 alpha。
ImU32 WithAlphaSet(ImU32 color, float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w = std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

ImU32 LerpColorTo(ImU32 from, ImU32 to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    const ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                                a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
}

// ------------------------------------------------------------------ 1 Button
float ButtonHeight(ButtonSize size) {
    switch (size) {
    case ButtonSize::Small: return 24.0f;
    case ButtonSize::Large: return 36.0f;
    case ButtonSize::Medium: break;
    }
    return 30.0f;
}

float ButtonPadding(ButtonSize size) {
    switch (size) {
    case ButtonSize::Small: return 10.0f;
    case ButtonSize::Large: return 20.0f;
    case ButtonSize::Medium: break;
    }
    return 14.0f;
}

float ButtonWidth(ButtonSize size, float iconWidth, float textWidth) {
    const float padding = ButtonPadding(size);
    const float gap = (iconWidth > 0.0f && textWidth > 0.0f) ? 6.0f : 0.0f;
    return padding * 2.0f + iconWidth + gap + textWidth;
}

bool Button(ImDrawList* draw, Rect bounds, std::string_view label, const ButtonSpec& spec,
            std::string_view id) {
    const Hit hit = HitTestImpl(bounds, id);
    const bool enabled = !spec.disabled && !spec.loading;

    // :active scale(.97) —— 按下时整体缩到 97%，绕自身中心。
    Rect body = bounds;
    if (hit.held && enabled) {
        const ImVec2 c = bounds.center();
        body.min = ImVec2(c.x - bounds.width() * 0.485f, c.y - bounds.height() * 0.485f);
        body.max = ImVec2(c.x + bounds.width() * 0.485f, c.y + bounds.height() * 0.485f);
    }

    const float rounding = spec.size == ButtonSize::Large ? 10.0f : 6.0f;
    const ImU32 accent = ColorAccent();
    const ImU32 accentHover = ColorAccentHover();

    ImU32 fill = 0;
    ImU32 border = 0;
    ImU32 text = ColorTextSecondary();
    switch (spec.variant) {
    case ButtonVariant::Primary:
        // .btn-primary（ui.css:51-59）：常态是**纯 accent 底** + inset 顶高光，
        // accent-h 渐变和 --shadow-accent 辉光都只在 :hover。早先常态就画
        // accent→accent-h 斜渐变 + 常驻 28% 外光环，比设计稿重一档。
        if (hit.hovered && enabled) {
            DrawRoundRect(draw, body.min, body.max, rounding, accentHover, 0, 0.0f,
                          /*topHighlight=*/true);
            // hover 才有的 accent 28% 外扩辉光
            const Rect halo{body.min - ImVec2(0, 2), body.max + ImVec2(0, 2)};
            DrawRoundRect(draw, halo.min, halo.max, rounding + 2.0f, 0,
                          ColorOf(theme::CurrentDerived().btnPrimaryShadow), 2.0f);
        } else {
            DrawRoundRect(draw, body.min, body.max, rounding, accent, 0, 0.0f,
                          /*topHighlight=*/true);
        }
        text = ColorAccentFg();
        break;
    case ButtonVariant::Secondary:
        // .btn-secondary（ui.css:60-68）：常态 bg-elevated + line-normal 边；
        // hover 换 fill-hover 底并把边提到 line-strong。
        if (hit.hovered && enabled) {
            fill = ColorFillHover();
            border = ColorLineStrong();
        } else {
            fill = ColorElevated();
            border = ColorLineNormal();
        }
        text = ColorText();
        break;
    case ButtonVariant::Danger:
        // .btn-danger（ui.css:77-85）：常态透明底 + line-normal 边，红只用在文字；
        // hover 才升到 14% 红底 + 42% 红边。
        if (hit.hovered && enabled) {
            fill = WithAlpha(ColorOf(theme::Current().statusDanger), 0.14f);
            border = WithAlpha(ColorOf(theme::Current().statusDanger), 0.42f);
        } else {
            fill = 0;
            border = ColorLineNormal();
        }
        text = ColorOf(theme::Current().statusDanger);
        break;
    case ButtonVariant::Ghost:
        fill = hit.hovered && enabled ? ColorFillHover() : 0;
        text = hit.hovered && enabled ? ColorText() : ColorTextSecondary();
        break;
    }
    if (spec.variant != ButtonVariant::Primary) {
        DrawRoundRect(draw, body.min, body.max, rounding, fill, border,
                      border == 0 ? 0.0f : 1.0f);
    }

    if (!enabled) {
        // disabled opacity .45：整块压暗
        DrawRoundRect(draw, body.min, body.max, rounding, WithAlpha(ColorVoid(), 0.42f));
    }

    const float fontSize = FontSizeOf(spec.size);
    ImFont* font = FontBoldAt(fontSize);
    const float iconSize = spec.size == ButtonSize::Small ? 13.0f : 15.0f;
    const float iconWidth = (spec.loading || !spec.icon.empty()) ? iconSize : 0.0f;
    const float textWidth =
        label.empty()
            ? 0.0f
            : font->CalcTextSizeA(fontSize, 1e9f, 0.0f, label.data(), label.data() + label.size()).x;
    const float gap = (iconWidth > 0.0f && textWidth > 0.0f) ? 6.0f : 0.0f;
    float x = body.min.x + (body.width() - (iconWidth + gap + textWidth)) * 0.5f;
    const float y = body.min.y + (body.height() - iconSize) * 0.5f;
    ImU32 fg = enabled ? text : WithAlpha(text, 0.45f);

    if (spec.loading) {
        DrawSpinner(draw, ImVec2(x + iconSize * 0.5f, y + iconSize * 0.5f), iconSize * 0.5f, fg);
    } else if (!spec.icon.empty()) {
        DrawIcon(draw, spec.icon, ImVec2(x, y), iconSize, fg);
    }
    if (textWidth > 0.0f) {
        // ⚠️ 这里**保持代数恒等**：`body.min.y + (body.height() - fontSize) * 0.5f`
        //    与 `CenterTextY(font, fontSize, body.center().y)` 完全等价（都是
        //    `中心 - 字号/2`），换成函数只是为了让全树只有一处算式。
        //    ⚠️ 早先这里写过一版注释说「旧算式假设 Ascent+Descent==fontSize、逐字号各偏各的」
        //    —— **那句是错的**，两条式子数值上一样，注释在骗人，已撤回。
        //    真正让字「看着不在中间」的是写死偏移（检查器段头的 `min.y + 5.0f`
        //    配 24px 框 = 偏上 6.2px），量级差两个数量级，不是 0.7px 那种。
        const float textY = CenterTextY(font, fontSize, body.center().y);
        draw->AddText(font, fontSize, ImVec2(x + iconWidth + gap, textY), fg, label.data(),
                      label.data() + label.size());
    }
    return hit.clicked && enabled;
}

// ------------------------------------------------------------------ 2 IconBtn
bool IconButton(ImDrawList* draw, Rect bounds, std::string_view icon, bool active, bool disabled,
                std::string_view id, std::string_view tip) {
    const Hit hit = HitTestImpl(bounds, id);
    const bool small = bounds.width() <= 24.0f;
    const float iconSize = small ? 13.0f : 16.0f;
    ImU32 fill = 0;
    ImU32 text = ColorTextMuted();
    if (active) {
        fill = ColorFillSelected();
        text = ColorAccent();
    } else if (hit.hovered && !disabled) {
        fill = ColorFillHover();
        text = ColorAccent();
    }
    DrawRoundRect(draw, bounds.min, bounds.max, 6.0f, fill);
    if (disabled) {
        text = WithAlpha(text, 0.45f);
    }
    DrawIconCentered(draw, icon, bounds.center(), iconSize, text);
    // data-tip（ui.css:472-507）的浮层由 Tooltip 自己画：ImGui::SetTooltip 走的是
    // ImGui 自己的 tooltip 窗口样式（圆角/底色/字号/延迟全都不是设计稿那一套），
    // 且它拿不到锚点矩形 —— 设计稿要求贴在控件右侧 10px、垂直居中。
    // ⚠️ 必须留在 hover 分支里：Tooltip 自己是**无条件**画的（它只管延迟计时，
    // 判「该不该显示」是调用方的责任）。放到分支外 = 工具栏上每个带 tip 的图标
    // 都会同时糊出一块 tooltip。
    if (!tip.empty() && hit.hovered && !disabled) {
        Tooltip(bounds, tip, /*below=*/false);
    }
    return hit.clicked && !disabled;
}

// ------------------------------------------------------------------ 3 Tag
float TagHeight(bool small) { return small ? 17.0f : 20.0f; }

float TagWidth(std::string_view label, bool small, bool dot) {
    const float fontSize = small ? 10.5f : 11.5f;
    ImFont* font = FontBoldAt(fontSize);
    const float text =
        font->CalcTextSizeA(fontSize, 1e9f, 0.0f, label.data(), label.data() + label.size()).x;
    return (dot ? 7.0f + 5.0f : 0.0f) + text + (small ? 12.0f : 16.0f);
}

void Tag(ImDrawList* draw, Rect bounds, std::string_view label, theme::Tone tone, bool small,
         bool dot, bool busyPulse) {
    const bool idle = tone == theme::Tone::Idle;
    const ImU32 fg = idle ? ColorTextMuted() : ToneColor(tone);
    const ImU32 bg = idle ? ColorFillMuted() : ToneBackground(tone);
    const ImU32 border = idle ? ColorLineNormal() : ToneBorder(tone);
    const float radius = bounds.height() * 0.5f;

    float alphaScale = 1.0f;
    if (busyPulse) {
        alphaScale = 0.72f + 0.28f * Pulse(1.6f);
    }
    DrawRoundRect(draw, bounds.min, bounds.max, radius, WithAlpha(bg, alphaScale), border, 1.0f);

    const float fontSize = small ? 10.5f : 11.5f;
    ImFont* font = FontBoldAt(fontSize);
    const float text =
        font->CalcTextSizeA(fontSize, 1e9f, 0.0f, label.data(), label.data() + label.size()).x;
    const float dotSize = dot ? 7.0f : 0.0f;
    const float gap = dot > 0.0f ? 5.0f : 0.0f;
    float x = bounds.min.x + (bounds.width() - (dotSize + gap + text)) * 0.5f;
    const float cy = 0.5f * (bounds.min.y + bounds.max.y);

    if (dot > 0.0f) {
        if (busyPulse) {
            const float halo = Pulse(1.6f);
            draw->AddCircleFilled(ImVec2(x + dotSize * 0.5f, cy), dotSize * 0.5f + halo * 2.4f,
                                 WithAlpha(fg, 0.28f * (1.0f - halo)), 12);
        }
        draw->AddCircleFilled(ImVec2(x + dotSize * 0.5f, cy), dotSize * 0.5f,
                             WithAlpha(fg, alphaScale), 12);
        x += dotSize + gap;
    }
    const float textY = cy - fontSize * 0.5f;
    draw->AddText(font, fontSize, ImVec2(x, textY), WithAlpha(fg, alphaScale), label.data(),
                  label.data() + label.size());
}

// ------------------------------------------------------------------ 4 StatusDot
void StatusDot(ImDrawList* draw, ImVec2 center, theme::Tone tone, bool run) {
    const ImU32 color = ToneColor(tone);
    if (run) {
        // .dot.run：1.6s 脉冲环（pulse-dot）
        const float t = Pulse(1.6f);
        draw->AddCircleFilled(center, 3.5f + 4.0f * t, WithAlpha(color, 0.30f * (1.0f - t)), 16);
    }
    draw->AddCircleFilled(center, 3.5f, color, 12);
}

// ------------------------------------------------------------------ 5 Kbd
float KbdWidth(std::string_view label) {
    ImFont* font = FontBoldAt(10.5f);
    return std::max(
        18.0f,
        font->CalcTextSizeA(10.5f, 1e9f, 0.0f, label.data(), label.data() + label.size()).x + 10.0f);
}

void Kbd(ImDrawList* draw, Rect bounds, std::string_view label) {
    // 1px 边 + **下边 2px**（kbd 的立体感来自这条 2px 底边）
    DrawRoundRect(draw, bounds.min, bounds.max, 4.0f, ColorOf(theme::Current().bgElevated),
                  ColorLineNormal(), 1.0f);
    draw->AddLine(ImVec2(bounds.min.x + 4.0f, bounds.max.y - 1.0f),
                  ImVec2(bounds.max.x - 4.0f, bounds.max.y - 1.0f), ColorLineNormal(), 2.0f);
    ImFont* font = FontBoldAt(10.5f);
    const float text =
        font->CalcTextSizeA(10.5f, 1e9f, 0.0f, label.data(), label.data() + label.size()).x;
    draw->AddText(font, 10.5f,
                  ImVec2(0.5f * (bounds.min.x + bounds.max.x) - 0.5f * text,
                         0.5f * (bounds.min.y + bounds.max.y) - 5.25f),
                  ColorTextSecondary(), label.data(), label.data() + label.size());
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
    const Hit hit =
        (hoverable || glow)
            ? HitTestImpl(bounds, "card@" + std::to_string(static_cast<int>(bounds.min.x)) + "," +
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
        const Hit hit = HitTestImpl(item, Unique(id, static_cast<int>(i)));
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
        const Hit hit = HitTestImpl(item, Unique(id, static_cast<int>(i)));
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

// ------------------------------------------------------------------ 9 Field
Rect Field(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view help) {
    float y = bounds.min.y;
    if (!label.empty()) {
        ImFont* font = FontBoldAt(12.0f);
        draw->AddText(font, 12.0f, ImVec2(bounds.min.x, y), ColorTextSecondary(), label.data(),
                      label.data() + label.size());
        y += 18.0f;
    }
    if (!help.empty()) {
        y += 6.0f;
        ImFont* font = FontAt(12.0f);
        draw->AddText(font, 12.0f, ImVec2(bounds.min.x, y), ColorTextMuted(), help.data(),
                      help.data() + help.size());
        y += 18.0f;
    }
    return Rect{ImVec2(bounds.min.x, y), bounds.max};
}

// ------------------------------------------------------------------ 10-12 Input
bool Input(ImDrawList* draw, Rect bounds, std::string& value, std::string_view placeholder,
           std::string_view id) {
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::PushID(std::string(id).c_str());
    ImGui::SetNextItemWidth(bounds.width());
    // ⚠️ 必须 PushFont：ImGui 的 InputText 用 io.FontDefault，不推的话输入框里的
    // 文字会用默认字体（不是我们的 13px 雅黑），中文与行高都会偏。
    ImGui::PushFont(FontAt(13.0f));
    // h30 = 13px 字高 + 上下各 7.5 padding + 上下各 1px 边框
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ColorFillMuted());
    ImGui::PushStyleColor(ImGuiCol_Border, ColorLineNormal());
    ImGui::PushStyleColor(ImGuiCol_Text, ColorText());
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ColorAccentDim());
    const std::string hint(placeholder);
    const bool changed = ImGui::InputTextWithHint("##value", hint.c_str(), &value,
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
    const bool focused = ImGui::IsItemFocused();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::PopID();

    // 焦点环 = CSS 的 0 0 0 3px accent-dim 外环
    if (focused) {
        DrawRoundRect(draw, bounds.min - ImVec2(2.0f, 2.0f), bounds.max + ImVec2(2.0f, 2.0f), 8.0f,
                      0, ColorOf(theme::CurrentDerived().inputFocusRing), 3.0f);
    } else if (hovered) {
        DrawRoundRect(draw, bounds.min, bounds.max, 6.0f, 0, ColorLineStrong(), 1.0f);
    }
    return changed;
}

bool TextArea(ImDrawList* draw, Rect bounds, std::string& value, int lines, std::string_view id) {
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::PushID(std::string(id).c_str());
    ImGui::PushFont(FontAt(12.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ColorFillMuted());
    ImGui::PushStyleColor(ImGuiCol_Border, ColorLineNormal());
    ImGui::PushStyleColor(ImGuiCol_Text, ColorText());
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ColorAccentDim());
    // 行高 1.6：ImGui 的 multiline 用 FontSize + FramePadding*2，12.5*1.6 ≈ 20
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 6.0f));
    const bool changed =
        ImGui::InputTextMultiline("##value", &value,
                                  ImVec2(bounds.width(), 16.0f + lines * 20.0f),
                                  ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    ImGui::PopID();
    (void)draw;
    return changed;
}

bool Select(ImDrawList* draw, Rect bounds, const std::vector<std::string>& options, int& index,
            std::string_view id) {
    if (options.empty()) {
        return false;
    }
    index = std::clamp(index, 0, static_cast<int>(options.size()) - 1);
    bool changed = false;
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::SetNextItemWidth(bounds.width());
    ImGui::PushFont(FontAt(13.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.5f));
    if (ImGui::BeginCombo(std::string(id).c_str(),
                          options[static_cast<std::size_t>(index)].c_str())) {
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            const bool selected = (i == index);
            if (ImGui::Selectable(options[static_cast<std::size_t>(i)].c_str(), selected)) {
                index = i;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
    (void)draw;
    return changed;
}

// ------------------------------------------------------------------ 13 Switch
bool Switch(ImDrawList* draw, Rect bounds, bool on, std::string_view id) {
    const Hit hit = HitTestImpl(bounds, id);
    const float height = bounds.height();
    const float radius = height * 0.5f;
    DrawRoundRect(draw, bounds.min, bounds.max, radius,
                  on ? ColorAccent() : (hit.hovered ? ColorFillHover() : ColorFillMuted()),
                  on ? ColorAccent() : ColorLineNormal(), 1.0f);
    const float knob = height - 6.0f;
    const float travel = bounds.width() - knob - 6.0f;
    const float t = on ? 1.0f : 0.0f;
    const float cx = bounds.min.x + 3.0f + travel * t;
    DrawRoundRect(draw, ImVec2(cx, bounds.min.y + 3.0f), ImVec2(cx + knob, bounds.max.y - 3.0f),
                  knob * 0.5f, on ? ColorAccentFg() : ColorTextMuted(), 0, 0.0f);
    return hit.clicked;
}

// ------------------------------------------------------------------ 14 Checkbox
bool Checkbox(ImDrawList* draw, Rect bounds, bool on, std::string_view label,
              std::string_view id) {
    const Rect box = RectAt(bounds.min.x, 0.5f * (bounds.min.y + bounds.max.y) - 7.5f, 15.0f, 15.0f);
    const Hit hit = HitTestImpl(bounds, id);
    if (on) {
        DrawRoundRect(draw, box.min, box.max, 4.0f, ColorAccent());
        DrawIcon(draw, "check", ImVec2(box.min.x + 2.6f, box.min.y + 2.6f), 10.0f, ColorAccentFg(),
                 3.4f);
    } else {
        DrawRoundRect(draw, box.min, box.max, 4.0f, 0, ColorLineStrong(), 1.5f);
    }
    if (!label.empty()) {
        ImFont* font = FontAt(12.5f);
        draw->AddText(font, 12.5f, ImVec2(box.max.x + 8.0f, box.min.y + 1.0f),
                      hit.hovered ? ColorText() : ColorTextSecondary(), label.data(),
                      label.data() + label.size());
    }
    return hit.clicked;
}

// ------------------------------------------------------------------ 15 Progress
float ProgressHeight(bool thin) { return thin ? 4.0f : 6.0f; }

void Progress(ImDrawList* draw, Rect bounds, float value, bool run, bool thin) {
    const float height = thin ? ProgressHeight(true) : ProgressHeight(false);
    const Rect track{bounds.min, ImVec2(bounds.max.x, bounds.min.y + height)};
    DrawRoundRect(draw, track.min, track.max, height * 0.5f, ColorFillMuted());
    const float filled = std::clamp(value, 0.0f, 100.0f) * 0.01f;
    if (filled <= 0.0f) {
        return;
    }
    const Rect fill{track.min, ImVec2(track.min.x + track.width() * filled, track.max.y)};
    // .prog > i 的 `background: var(--grad-accent)`（ui.css:441）：grad-accent 是
    // accent → **info**（tokens.css:70，每套主题都不同），不是 accent → accent-h。
    // 画成同族渐变时整条进度都是绿的，丢掉设计稿那一眼可辨的冷暖过渡。
    DrawHGradient(draw, fill.min, fill.max, height * 0.5f, ColorAccent(),
                  ColorOf(theme::Current().accentInfo));
    if (run) {
        // run：叠 100° 白 35% 微光扫过
        const float t = Pulse(1.6f);
        const float band = track.width() * 0.18f;
        const float x = track.min.x - band + (track.width() + band) * t;
        const float left = std::max(x, track.min.x);
        const float right = std::min(x + band, track.max.x);
        if (right > left) {
            DrawRoundRect(draw, ImVec2(left, fill.min.y), ImVec2(right, fill.max.y),
                          height * 0.5f, ImU32(0x59FFFFFFu));
        }
    }
}

// ------------------------------------------------------------------ 16 Empty
void Empty(ImDrawList* draw, Rect bounds, std::string_view icon, std::string_view title,
           std::string_view body) {
    // 4s 上下浮动（±5px）
    const float float_ = ReduceMotion() ? 0.0f : (Pulse(4.0f) - 0.5f) * 10.0f;
    const ImVec2 glyphSize(52.0f, 52.0f);
    const ImVec2 glyphMin(bounds.center().x - 0.5f * glyphSize.x,
                          bounds.min.y + 40.0f + float_);
    const Rect glyph{glyphMin, glyphMin + glyphSize};

    // 52×52 r14 **虚线** 边框
    const float radius = 14.0f;
    const ImU32 dash = ColorLineNormal();
    for (int i = 0; i < 4; ++i) {
        const ImVec2 a = glyph.min;
        const ImVec2 b = glyph.max;
        ImVec2 p0;
        ImVec2 p1;
        switch (i) {
        case 0: p0 = ImVec2(a.x + radius, a.y); p1 = ImVec2(b.x - radius, a.y); break;
        case 1: p0 = ImVec2(b.x, a.y + radius); p1 = ImVec2(b.x, b.y - radius); break;
        case 2: p0 = ImVec2(b.x - radius, b.y); p1 = ImVec2(a.x + radius, b.y); break;
        default: p0 = ImVec2(a.x, b.y - radius); p1 = ImVec2(a.x, a.y + radius); break;
        }
        draw->AddLine(p0, p1, dash, 1.0f);
    }
    for (int i = 0; i < 4; ++i) {
        const ImVec2 corner = (i == 0)   ? glyph.min
                              : (i == 1) ? ImVec2(glyph.max.x, glyph.min.y)
                              : (i == 2) ? glyph.max
                                         : ImVec2(glyph.min.x, glyph.max.y);
        const float a0 = (i == 0)   ? 0.0f
                         : (i == 1) ? 1.5707963f
                         : (i == 2) ? 3.14159265f
                                    : 4.71238898f;
        draw->PathArcTo(corner, radius, a0, a0 + 1.5707963f, 6);
        draw->PathStroke(dash, 0, 1.0f);
        draw->PathClear();
    }
    DrawIconCentered(draw, icon, glyph.center(), 24.0f, ColorTextMuted());

    float y = glyph.max.y + 10.0f;
    if (!title.empty()) {
        ImFont* font = FontBoldAt(13.0f);
        const float w =
            font->CalcTextSizeA(13.0f, 1e9f, 0.0f, title.data(), title.data() + title.size()).x;
        DrawTextClipped(draw, font, 13.0f, ImVec2(bounds.center().x - 0.5f * w, y), bounds.width(),
                        ColorText(), title);
        y += 19.0f;
    }
    if (!body.empty()) {
        ImFont* font = FontAt(12.5f);
        // ⚠️ 正文框必须夹在 bounds 里。早先这里写死 320px 宽、按 bounds 中心对齐，
        // 侧栏只有 240px 宽时，一个 320 的框从 x=-30 铺到 290，两端都溢出面板
        //    （实测正文横穿导航栏、还被面板右缘切掉半截）。
        const float bodyW = std::max(40.0f, std::min(320.0f, bounds.width() - 16.0f));
        DrawTextClipped(draw, font, 12.5f, ImVec2(bounds.center().x - 0.5f * bodyW, y), bodyW,
                        ColorTextMuted(), body, /*wrap=*/true);
    }
}

// ------------------------------------------------------------------ 17 KV
void KeyValues(ImDrawList* draw, Rect bounds,
               const std::vector<std::pair<std::string, std::string>>& rows) {
    // 网格 auto 1fr，gap 6px 14px，字号 12.5
    ImFont* font = FontAt(12.5f);
    ImFont* bold = FontBoldAt(12.5f);
    const float keyWidth = [&] {
        float w = 0.0f;
        for (const auto& [k, v] : rows) {
            (void)v;
            w = std::max(w, font->CalcTextSizeA(12.5f, 1e9f, 0.0f, k.data(), k.data() + k.size()).x);
        }
        return std::min(w, bounds.width() * 0.45f);
    }();
    const float rowHeight = 20.0f;
    float y = bounds.min.y;
    for (const auto& [key, value] : rows) {
        // ⚠️ 原来 key / value 都直接画在 `y`（游标），而 rowHeight 是 20 ——
        //    行盒中心在 `y + 10`，字却从 `y` 起步 ⇒ **偏上 3.75px**（12.5px 字）。
        //    这是检查器「属性」段、报告卡等一堆键值表共用的路径，偏一次全偏。
        //    按行盒高 = 字号，居中即 `y + (rowHeight - 12.5) / 2`。
        const float ty = CenterTextY(font, 12.5f, y + rowHeight * 0.5f);
        DrawTextClipped(draw, font, 12.5f, ImVec2(bounds.min.x, ty), keyWidth, ColorTextMuted(),
                        key);
        DrawTextClipped(draw, bold, 12.5f, ImVec2(bounds.min.x + keyWidth + 14.0f, ty),
                        bounds.width() - keyWidth - 14.0f, ColorText(), value);
        y += rowHeight;
    }
}

// ------------------------------------------------------------------ 18 Spinner
// .spin（ui.css:457-470）：14×14 圆环，2px line-normal 边，**顶边** accent，
// 0.7s 匀速一圈。画法是「底环 + 顶弧」：底环用 line-normal 描一圈，顶弧用
// accent 覆盖 1/4 圈 —— 与 CSS 的 border / border-top-color 同构。
// CSS 的 border 是向内长的，所以这里按外径画：半径 = size/2 - thickness/2。
float SpinnerSize(bool small) { return small ? 11.0f : 14.0f; }

void Spinner(ImDrawList* draw, ImVec2 center, bool small) {
    const float size = SpinnerSize(small);
    const float thickness = small ? 1.5f : 2.0f;
    const float radius = size * 0.5f - thickness * 0.5f;
    const int segments = small ? 20 : 24;
    // 0.7s 一圈：CSS `spin .7s linear infinite`，线性所以直接用 Now() 不用缓动。
    const float angle = Now() * (2.0f * 3.14159265f) / 0.7f;
    draw->AddCircle(center, radius, ColorLineNormal(), segments, thickness);
    // 顶弧：从 12 点方向顺时针扫 90°，用 accent。
    const float start = angle - 1.5707963f;
    draw->PathArcTo(center, radius, start, start + 1.5707963f, segments / 4);
    draw->PathStroke(ColorAccent(), 0, thickness);
    draw->PathClear();
}

// ------------------------------------------------------------------ 19 Tooltip
// [data-tip]::after（ui.css:476-507）。要点：
//   * 底 bg-overlay（不是 panel）+ 1px line-normal + shadow-1；
//   * 11.5px / 500 / pad 4px 9px / r6 / nowrap；
//   * 位置：默认 left: calc(100% + 10px) 垂直居中；.tip-b 变体是 top: calc(100% + 8px) 水平居中；
//   * 有 --dur-2（200ms）的**出现延迟**，不是立刻显示；
//   * pointer-events: none —— 只画不命中，所以这里不调 HitTest。
// ⚠️ 画在 GetForegroundDrawList() 而不是传入的 draw list：外壳是一帧一整块自绘，
// 画在普通 draw list 上会被后面画的控件盖住（tooltip 就成了「时有时无」）。
// 落不到前景时（没进 ImGui 帧）静默跳过，绝不崩。
void Tooltip(Rect anchor, std::string_view text, bool below) {
    if (text.empty()) {
        return;
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (draw == nullptr) {
        return;
    }
    // 出现延迟：CSS transition-delay: var(--dur-2) = 200ms。
    //
    // 本函数是**无条件绘制**的：判「该不该显示」是调用方的责任（IconButton 只在
    // hover 时调）。所以延迟计时必须自己处理「中途不再被调用」的情况：
    // 鼠标移开 → 停止调用 → 计时器停在半路 → 再次悬停**同一个**按钮时会接着
    // 旧值秒出，粘手。用帧号断档（lastFrame + 1 != now）来复位，
    // 顺带覆盖「从 A 划到 B」这种换锚点的情况。
    static float shownFor = 0.0f;
    static int lastFrame = -2;
    const ImGuiIO& io = ImGui::GetIO();
    const int now = ImGui::GetFrameCount();
    if (lastFrame + 1 != now) {
        shownFor = 0.0f; // 中间至少断了一帧 = 悬停已经结束
    }
    lastFrame = now;
    const float delay = ReduceMotion() ? 0.0f : 0.2f;
    if (io.DeltaTime > 0.0f) {
        shownFor = std::min(shownFor + io.DeltaTime, delay + 1.0f);
    }
    if (shownFor < delay) {
        return;
    }
    // 出现动画：opacity 0→1 + scale .94→1，都是 --dur-1（120ms）。
    const float t = ReduceMotion() ? 1.0f : std::clamp((shownFor - delay) / 0.12f, 0.0f, 1.0f);
    const ImU32 bg = WithAlphaSet(ColorOverlay(), t);
    const ImU32 border = WithAlpha(ColorLineNormal(), t);
    const ImU32 fg = WithAlpha(ColorText(), t);

    ImFont* font = FontAt(11.5f);
    const float textWidth =
        font->CalcTextSizeA(11.5f, 1e9f, 0.0f, text.data(), text.data() + text.size()).x;
    const float padX = 9.0f;
    const float padY = 4.0f;
    const float width = textWidth + padX * 2.0f;
    const float height = 11.5f + padY * 2.0f + 2.0f; // +2 = 上下各 1px 边

    ImVec2 min;
    if (below) {
        min = ImVec2(anchor.center().x - width * 0.5f, anchor.max.y + 8.0f);
    } else {
        min = ImVec2(anchor.max.x + 10.0f, anchor.center().y - height * 0.5f);
    }
    const ImVec2 max(min.x + width, min.y + height);
    // scale(.94)：从中心缩，所以先把 min/max 往中心收 6%。
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float s = 0.94f + 0.06f * t;
    const ImVec2 smin(c.x - width * 0.5f * s, c.y - height * 0.5f * s);
    const ImVec2 smax(c.x + width * 0.5f * s, c.y + height * 0.5f * s);
    // `[data-tip]::after`（ui.css:476-494）：参数与这里逐项吻合（11.5px 字、
    // padding 4/9、--bg-overlay、--line-normal、scale .94→1、z-index 90），
    // 其中 :494 是 `box-shadow: var(--shadow-1)` 且**不在 :hover 里** ——
    // 提示气泡一出现就该有投影，不是 hover 才有的。这是全仓唯一一处
    // 「静止态就该有投影、此前却落回默认 None」的浮层。
    DrawShadowed(draw, smin, smax, 6.0f, bg, border, 1.0f, theme::ShadowTier::Card);
    draw->AddText(font, 11.5f, ImVec2(smin.x + padX, smin.y + padY + 1.0f), fg, text.data(),
                  text.data() + text.size());
}

// ------------------------------------------------------------------ 20 Divider
void Divider(ImDrawList* draw, Rect bounds) {
    // .msep（ui.css:134-138）：1px line-subtle。调用方给的是**已含上下 5px
    // 外边距**的矩形，所以线画在垂直居中。
    const float y = 0.5f * (bounds.min.y + bounds.max.y);
    draw->AddLine(ImVec2(bounds.min.x, y), ImVec2(bounds.max.x, y), ColorLineSubtle(), 1.0f);
}

// ------------------------------------------------------------------ 20b ListRow
float ListRowHeight(const ListRowSpec& spec, float paddingY) {
    // 下限取文字与图标里更高的那个：行**只装一行**时，文字高度 + 上下内边距
    // 就够了，但图标比文字高（16px 图标 vs 10.5px 次要文字）时行会被压扁。
    const float content = std::max({spec.titleSize, spec.trailingSize,
                                    spec.icon.empty() ? 0.0f : spec.iconSize});
    return content + paddingY * 2.0f;
}

Hit ListRow(ImDrawList* draw, Rect bounds, const ListRowSpec& spec) {
    // 命中**一次**取齐：hovered 与 clicked 来自同一个 HitTestImpl。
    // 页面层曾写成 `Hovered(idA)` 叠 `Clicked(idB)`，ImGui 只让先注册的那个
    // 拿到 HoveredId，于是第二个永远 clicked=false —— 那一行点不开，
    // 而编译、日志、截图全绿。
    Hit hit;
    if (!spec.id.empty() && !spec.suppressed) {
        hit = HitTestImpl(bounds, spec.id);
    }
    // 底色：选中优先于 hover（`.mi.on` / `tr.sel` 的既有语义）。
    // 圆角 6 —— 页面层 5 处自己写的时候有 4 处用 6、1 处用 8。
    if (spec.selected) {
        DrawRoundRect(draw, bounds.min, bounds.max, 6.0f, ColorFillSelected());
    } else if (hit.hovered) {
        DrawRoundRect(draw, bounds.min, bounds.max, 6.0f, ColorFillHover());
    }

    const float centerY = bounds.center().y;
    float x = bounds.min.x + spec.paddingX;
    // chevron 先量宽度：标题可用宽要减去它，否则长标题会压在 chevron 底下
    // （页面层几处是「标题写死 220/300 宽」，容器一变窄就压字）。
    constexpr float kChevronW = 10.0f;
    const bool hasChevron = !spec.chevron.empty() && spec.chevron != "none";
    if (hasChevron) {
        DrawIcon(draw, spec.chevron, ImVec2(bounds.max.x - spec.paddingX - kChevronW,
                                            centerY - kChevronW * 0.5f),
                 kChevronW, spec.disabled ? WithAlpha(ColorTextMuted(), 0.45f) : ColorTextMuted());
    }

    const ImU32 iconColor =
        spec.disabled ? WithAlpha(ColorTextMuted(), 0.45f)
                      : (spec.iconColor != 0 ? spec.iconColor : ColorTextMuted());
    if (!spec.icon.empty()) {
        DrawIcon(draw, spec.icon, ImVec2(x, centerY - spec.iconSize * 0.5f), spec.iconSize, iconColor);
        x += spec.iconSize + spec.iconGap;
    }

    // 标题可用宽 = 到 chevron（或右内边距）为止，再减去右侧次要文字的实际宽度。
    float titleRight = bounds.max.x - spec.paddingX;
    if (hasChevron) {
        titleRight -= kChevronW + 6.0f;
    }
    ImFont* titleFont = spec.titleMono ? MonoAt(spec.titleSize) : FontAt(spec.titleSize);
    if (!spec.trailing.empty()) {
        ImFont* trailingFont =
            spec.trailingMono ? MonoAt(spec.trailingSize) : FontAt(spec.trailingSize);
        const float trailingW = MeasureClipped(trailingFont, spec.trailingSize, 1e9f, spec.trailing);
        // 右侧次要文字也从 chevron 往回退，两者互不重叠。
        const float trailingX =
            std::max(x, (hasChevron ? bounds.max.x - spec.paddingX - kChevronW - 6.0f
                                    : bounds.max.x - spec.paddingX) - trailingW);
        // ⚠️ 同样走 CenterTextY。页面层这 5 处里有 3 处写死 `y + 5.0f` / `y + 6.0f`，
        // 行高 24~30 时偏上 1.25~2.5px。
        DrawTextClipped(draw, trailingFont, spec.trailingSize,
                        ImVec2(trailingX, CenterTextY(trailingFont, spec.trailingSize, centerY)),
                        trailingW, spec.disabled ? WithAlpha(ColorTextMuted(), 0.45f) : ColorTextMuted(),
                        spec.trailing);
        titleRight = std::min(titleRight, trailingX - 8.0f);
    }
    if (!spec.title.empty()) {
        const ImU32 titleColor =
            spec.disabled ? WithAlpha(ColorTextSecondary(), 0.45f)
                          : (spec.titleColor != 0 ? spec.titleColor : ColorTextSecondary());
        DrawTextClipped(draw, titleFont, spec.titleSize,
                        ImVec2(x, CenterTextY(titleFont, spec.titleSize, centerY)),
                        std::max(0.0f, titleRight - x), titleColor, spec.title);
    }
    return hit;
}

// ------------------------------------------------------------------ 20c ListCard
float ListCardHeight(const ListCardSpec& spec, float paddingY) {
    const float textH = spec.description.empty()
                            ? spec.titleSize
                            : (spec.titleSize + spec.textGap + spec.descSize);
    return std::max(textH, spec.iconSize) + paddingY * 2.0f;
}

Hit ListCard(ImDrawList* draw, Rect bounds, const ListCardSpec& spec,
             const std::function<void(ImDrawList*, Rect)>& footer) {
    Hit hit;
    if (spec.hoverable && !spec.id.empty()) {
        hit = HitTestImpl(bounds, spec.id);
    }
    // .card.hoverable（ProjectHub.jsx:39/226）：hover 走 **shadow-1**（投影档切换），
    // 不是填色底。选中态是 accent 描边（ProjectHub.jsx:40 的 accent-dim 环）——
    // 刻意**不**折成投影档，两者视觉形态不同。
    DrawShadowed(draw, bounds.min, bounds.max, 10.0f,
                 spec.selected ? ColorFillMuted() : ColorPanel(),
                 spec.selected ? ColorAccent() : (hit.hovered ? ColorLineStrong() : ColorLineSubtle()),
                 1.0f, hit.hovered ? theme::ShadowTier::Card : theme::ShadowTier::None);

    if (!spec.icon.empty()) {
        DrawIconCentered(draw, spec.icon,
                         ImVec2(bounds.min.x + spec.paddingX + spec.iconSize * 0.5f, bounds.center().y),
                         spec.iconSize, spec.selected ? ColorAccent() : ColorTextMuted());
    }
    // 文字左界 = paddingX + 图标占位（有图标时 paddingX + iconSize + 8）+ 额外缩进。
    // ⚠️ 不要写成 `paddingX * 2`：那是把「内边距」当成了「图标占位宽」，
    //   无图标的卡会凭空多缩进一整份 paddingX。
    const float textX = bounds.min.x + spec.paddingX + spec.textInset +
                        (spec.icon.empty() ? 0.0f : (spec.iconSize + 8.0f));
    // 右侧要给 footer 留位（打开列表的「打开」按钮），所以文字右界取整个右内边距 ——
    // footer 自己在**整卡命中之后**画，两者在水平上不重叠。
    const float textRight = bounds.max.x - spec.paddingX;

    if (!spec.description.empty()) {
        // 两行的**块**按卡中心对齐：块高 = title + gap + desc，块顶 = 中心 − 块高/2。
        // 页面层两处双行卡原来各写各的 `row.min.y + 12/14` 与 `+ 34`，
        // 块中心偏上 0.875px 与 1.875px（打开列表那处还把标题顶到 −12.5）。
        const float blockH = spec.titleSize + spec.textGap + spec.descSize;
        const float blockTop = bounds.center().y - blockH * 0.5f;
        ImFont* titleFont = FontBoldAt(spec.titleSize);
        DrawTextClipped(draw, titleFont, spec.titleSize,
                        ImVec2(textX, CenterTextY(titleFont, spec.titleSize, blockTop + spec.titleSize * 0.5f)),
                        std::max(0.0f, textRight - textX), ColorText(), spec.title);
        ImFont* descFont = FontAt(spec.descSize);
        const float descCenter = blockTop + spec.titleSize + spec.textGap + spec.descSize * 0.5f;
        DrawTextClipped(draw, descFont, spec.descSize,
                        ImVec2(textX, CenterTextY(descFont, spec.descSize, descCenter)),
                        std::max(0.0f, textRight - textX), ColorTextMuted(), spec.description);
    } else if (!spec.title.empty()) {
        ImFont* titleFont = FontBoldAt(spec.titleSize);
        DrawTextClipped(draw, titleFont, spec.titleSize,
                        ImVec2(textX, CenterTextY(titleFont, spec.titleSize, bounds.center().y)),
                        std::max(0.0f, textRight - textX), ColorText(), spec.title);
    }

    if (spec.selected) {
        // 选中勾在卡的右缘，与 footer 互斥：footer 存在时把勾让出去。
        if (!footer) {
            DrawIconCentered(draw, "check",
                             ImVec2(bounds.max.x - spec.paddingX - 8.0f, bounds.center().y), 16.0f,
                             ColorAccent());
        }
    }
    // ⚠️ footer 必须在**整卡命中之后**画：ImGui 同窗口内先注册者独占 HoveredId
    // （imgui.cpp:5161），反过来按钮永远 clicked=false，而整卡照样 clicked=true ——
    // 症状是「点『打开』不是打开，而是直接打开这张卡」。顺序反了整轮静默。
    if (footer) {
        footer(draw, RectAt(textX, bounds.min.y, std::max(0.0f, textRight - textX), bounds.height()));
    }
    return hit;
}

// ------------------------------------------------------------------ 21 DataTable
float TableHeaderHeight(bool compact) {
    // th padding 8px 12px（.compact 是 7px 8px）+ 11.5px 字高 + 1px 下边。
    return (compact ? 7.0f : 8.0f) * 2.0f + 14.0f + 1.0f;
}

float TableRowHeight(bool compact) {
    // td padding 9px 12px（.compact 7px 8px）+ 13px 字高（.compact 12）+ 1px 下边。
    return (compact ? 7.0f : 9.0f) * 2.0f + (compact ? 14.0f : 16.0f) + 1.0f;
}

namespace {

// 列 x 偏移表：width<=0 的列平分剩余宽度（CSS 的 table-layout: auto 近似）。
std::vector<float> TableColumnOffsets(const std::vector<TableColumn>& columns, float available) {
    std::vector<float> offsets(columns.size() + 1, 0.0f);
    int flexible = 0;
    float fixed = 0.0f;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].width > 0.0f) {
            fixed += columns[i].width;
        } else {
            ++flexible;
        }
    }
    const float each = flexible > 0 ? std::max(0.0f, available - fixed) / flexible : 0.0f;
    float x = 0.0f;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        offsets[i] = x;
        x += columns[i].width > 0.0f ? columns[i].width : each;
    }
    offsets[columns.size()] = x;
    return offsets;
}

} // namespace

float DataTable(ImDrawList* draw, Rect bounds, const std::vector<TableColumn>& columns,
                const std::vector<TableRow>& rows, TableSort& sort, bool compact,
                std::string_view id) {
    if (columns.empty()) {
        return 0.0f;
    }
    const float padX = compact ? 8.0f : 12.0f;
    const float headH = TableHeaderHeight(compact);
    const float rowH = TableRowHeight(compact);
    const float fontSize = compact ? 12.0f : 13.0f;
    const std::vector<float> offsets = TableColumnOffsets(columns, bounds.width());

    ImFont* headFont = FontBoldAt(11.5f);
    // th 底色 bg-panel + position:sticky（z-index:2）—— 先铺满表头高度，
    // 滚动时下面 td 从它底下过去。
    draw->AddRectFilled(bounds.min, ImVec2(bounds.max.x, bounds.min.y + headH), ColorPanel());
    float y = bounds.min.y;
    for (std::size_t c = 0; c < columns.size(); ++c) {
        const TableColumn& column = columns[c];
        const float x0 = bounds.min.x + offsets[c] + padX;
        const float cellWidth = offsets[c + 1] - offsets[c] - padX * 2.0f;
        if (cellWidth <= 0.0f) {
            continue;
        }
        // th 可点排序：点同一列在升/降之间翻，点新列切成降序（设计稿是单向的 ↓，
        // Overview.jsx:96 只在选中时追加 ' ↓'，所以这里用三角方向表达即可）。
        // ⚠️ 先处理点击再算 sorted：反过来的话本帧的三角/配色会滞后一帧，
        // 视觉上就是「点下去没反应，下一帧才亮」。
        if (column.sortable) {
            const Rect hitRect = RectAt(bounds.min.x + offsets[c], y, offsets[c + 1] - offsets[c],
                                        headH);
            if (HitTestImpl(hitRect, Unique(id, static_cast<int>(c))).clicked) {
                if (sort.column == static_cast<int>(c)) {
                    sort.ascending = !sort.ascending;
                } else {
                    sort.column = static_cast<int>(c);
                    sort.ascending = false;
                }
            }
        }
        const bool sorted = sort.column == static_cast<int>(c);
        // 排序中的表头转 accent（设计稿没有这条，但排序列不给任何视觉反馈的话
        // 点完看不出排到哪去了 —— 用的是 .tabs > button.on 的既有语义，不新增颜色档）。
        const ImU32 fg = sorted ? ColorAccent() : ColorTextMuted();
        // 原来写死 `y + (compact ? 7.0f : 8.0f)` —— 那个值是 `.table th` 的
        // **padding**，被当成了文字偏移。表头高 31（compact 29）时偏上 1.75px。
        // 下面的排序小三角取 `textY + 6.0f`，会跟着一起下移，与文字的相对关系不变。
        const float textY = CenterTextY(headFont, 11.5f, y + headH * 0.5f);
        // 只量一次（MeasureClipped 不绘制）。之前这里用 DrawTextClipped 拿宽度，
        // 标题就被画了两遍 —— 第二遍还带三角占位，裁剪宽度和第一遍不一致。
        const float textWidth = MeasureClipped(headFont, 11.5f, cellWidth, column.title);
        const float textX = column.centered
                                ? bounds.min.x + offsets[c] +
                                      (offsets[c + 1] - offsets[c] - textWidth) * 0.5f
                                : x0;
        DrawTextClipped(draw, headFont, 11.5f, ImVec2(textX, textY),
                        column.centered ? textWidth : cellWidth, fg, column.title);
        if (sorted) {
            // 小三角而不是 '↓' 字符：U+2193 不在字体图集里（见 Widgets.h 注释）。
            const float tx = std::min(textX + textWidth + 4.0f, bounds.max.x - padX - 8.0f);
            const float cy = textY + 6.0f;
            const float dir = sort.ascending ? -1.0f : 1.0f;
            ImVec2 tri[3] = {ImVec2(tx + 3.0f, cy + 2.0f * dir), ImVec2(tx + 7.0f, cy + 2.0f * dir),
                             ImVec2(tx + 5.0f, cy - 2.0f * dir)};
            // PathFillConvex 只吃已经 PathLineTo 进去的点（现版 ImGui 的签名是
            // PathFillConvex(ImU32)，不是 ImGui 老版本的 (点数组, 个数, 色)）。
            draw->PathLineTo(tri[0]);
            draw->PathLineTo(tri[1]);
            draw->PathLineTo(tri[2]);
            draw->PathFillConvex(ColorAccent());
        }
    }
    // th 下边 line-normal
    draw->AddLine(ImVec2(bounds.min.x, y + headH - 0.5f), ImVec2(bounds.max.x, y + headH - 0.5f),
                  ColorLineNormal(), 1.0f);
    y += headH;

    for (std::size_t r = 0; r < rows.size(); ++r) {
        const TableRow& row = rows[r];
        const Rect rowRect = RectAt(bounds.min.x, y, bounds.width(), rowH);
        const Hit hit = HitTestImpl(rowRect, Unique(std::string(id) + "#r", static_cast<int>(r)));
        if (row.selected) {
            // tr.sel（ui.css:753-759）：fill-selected 底 + inset 2px 0 0 accent 左侧条。
            draw->AddRectFilled(rowRect.min, rowRect.max, ColorFillSelected());
            draw->AddRectFilled(ImVec2(rowRect.min.x, rowRect.min.y),
                                ImVec2(rowRect.min.x + 2.0f, rowRect.max.y), ColorAccent());
        } else if (hit.hovered) {
            draw->AddRectFilled(rowRect.min, rowRect.max, ColorFillHover());
        }
        for (std::size_t c = 0; c < columns.size() && c < row.cells.size(); ++c) {
            const TableColumn& column = columns[c];
            const float x0 = rowRect.min.x + offsets[c] + padX;
            const float cellWidth = offsets[c + 1] - offsets[c] - padX * 2.0f;
            if (cellWidth <= 0.0f) {
                continue;
            }
            const std::string& cell = row.cells[c];
            // `tag` 列：画小号 Tag 而不是文字。Tag 自带量宽与色相，按单元格
            // 左内边距摆、垂直**居中在行中心**（原来页面层写 `y + 6.0f`，
            // 行高 29 时偏上 0.5px，且那两列的色相要调用方自己传）。
            if (column.tag) {
                const theme::Tone tone =
                    static_cast<std::size_t>(c) < row.tones.size() ? row.tones[c] : theme::Tone::Idle;
                Tag(draw, RectAt(x0, rowRect.center().y - TagHeight(true) * 0.5f,
                                 std::min(TagWidth(cell, true, false), cellWidth), TagHeight(true)),
                    cell, tone, /*small=*/true);
                continue;
            }
            // `.table .num`（ui.css:760-764）：等宽 11.5 muted。
            // 选中行的 td 转 text-primary（ui.css:757），但 .num 保持 muted ——
            // 数字列的低对比是刻意的，等宽小字转正色会和主文本抢层级。
            // `mono` 是同一字形但保留 secondary（规则码那一列）。
            ImFont* font = (column.numeric || column.mono) ? MonoAt(11.5f) : FontAt(fontSize);
            const float size = (column.numeric || column.mono) ? 11.5f : fontSize;
            ImU32 fg = column.numeric  ? ColorTextMuted()
                       : column.mono  ? (row.selected ? ColorText() : ColorAccent())
                                       : (row.selected ? ColorText() : ColorTextSecondary());
            // 原来写死 `y + (compact ? 7.0f : 9.0f)` —— 同样是 `.table td` 的
            // **padding** 而不是文字偏移，行高 35（compact 29）时按实际字号
            // 13 / 12 / 11.5 分别偏上 2.0 / 1.5 / 2.75px。
            // ⚠️ 必须排在 font/size 之后：数字列字号是 11.5，用外层 fontSize
            //    居中会把等宽小字再推低 0.25px。
            const float textY = CenterTextY(font, size, rowRect.center().y);
            if (column.centered) {
                const float w = font->CalcTextSizeA(size, 1e9f, 0.0f, cell.data(),
                                                    cell.data() + cell.size())
                                    .x;
                draw->AddText(font, size,
                              ImVec2(rowRect.min.x + offsets[c] +
                                         (offsets[c + 1] - offsets[c] - w) * 0.5f,
                                     textY),
                              fg, cell.data(), cell.data() + cell.size());
            } else {
                DrawTextClipped(draw, font, size, ImVec2(x0, textY), cellWidth, fg, cell);
            }
        }
        // td 下边 line-subtle
        draw->AddLine(ImVec2(rowRect.min.x, rowRect.max.y - 0.5f),
                      ImVec2(rowRect.max.x, rowRect.max.y - 0.5f), ColorLineSubtle(), 1.0f);
        y += rowH;
    }
    return y - bounds.min.y;
}

// ------------------------------------------------------------------ 22 Tree
float TreeNodeHeight() {
    // .tree .node（ui.css:775-786）：height 28。
    return 28.0f;
}

namespace {

// 先序遍历的扁平计数 + 画一层的递归。visibleIndex 用来把「可见序号」和
// 选中项对齐：折叠的子树不占序号（与 Shell.jsx 里 open[ch] 决定渲染一致）。
float TreeWalk(ImDrawList* draw, Rect bounds, std::vector<TreeNode>& nodes, int& selectedIndex,
               int& counter, int depth, std::string_view id) {
    const float rowH = TreeNodeHeight();
    float y = bounds.min.y;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        TreeNode& node = nodes[i];
        const int index = counter++;
        const Rect row = RectAt(bounds.min.x + 20.0f * static_cast<float>(depth), y,
                                bounds.width() - 20.0f * static_cast<float>(depth), rowH);
        const bool on = selectedIndex == index;
        const Hit hit =
            HitTestImpl(row, Unique(std::string(id) + "#" + std::to_string(depth), static_cast<int>(i)));

        // 折叠箭头单独占一个 hit item（.tw，14×14）：它**套在行矩形里面**，
        // 所以点它会连带把整行的 hit 也触发 → 折叠的同时还会改选中项。
        // 设计稿里两者是同一个 onClick（Shell.jsx:486），但 ImGui 的 item 栈
        // 会让两个都算「点到」，所以这里显式让位：点箭头只翻折叠。
        bool twistClicked = false;
        if (node.hasChildren) {
            const Rect twist = RectAt(row.min.x + 8.0f, row.center().y - 7.0f, 14.0f, 14.0f);
            twistClicked = HitTestImpl(twist, Unique(std::string(id) + "#t" + std::to_string(depth),
                                                    static_cast<int>(i)))
                               .clicked;
            if (twistClicked) {
                node.expanded = !node.expanded;
            }
        }
        if (hit.clicked && !twistClicked) {
            selectedIndex = index;
        }

        if (on) {
            draw->AddRectFilled(row.min, row.max, ColorFillSelected());
            draw->AddRectFilled(ImVec2(row.min.x, row.min.y), ImVec2(row.min.x + 2.0f, row.max.y),
                                ColorAccent());
        } else if (hit.hovered) {
            draw->AddRectFilled(row.min, row.max, ColorFillHover());
        }
        const ImU32 fg = on ? ColorText() : (hit.hovered ? ColorText() : ColorTextSecondary());
        float x = row.min.x + 8.0f;
        if (node.hasChildren) {
            // .tw（ui.css:796-806）：14×14 muted，收起朝右，展开旋转 90° 朝下。
            // 不复用 DrawIcon("chevron")：图标是按路径描边的，没有旋转入口，
            // 只能自己画两条线段 —— 两态各给一组端点，避免在运行时做三角函数。
            const float cx = x + 7.0f;
            const float cy = row.center().y;
            const float r = 3.5f;
            const ImVec2 apex = node.expanded ? ImVec2(cx - r, cy) : ImVec2(cx, cy);
            const ImVec2 a0 = node.expanded ? ImVec2(cx, cy - r) : ImVec2(cx - r, cy - r);
            const ImVec2 a1 = node.expanded ? ImVec2(cx, cy + r) : ImVec2(cx - r, cy + r);
            draw->AddLine(a0, apex, ColorTextMuted(), 1.5f);
            draw->AddLine(a1, apex, ColorTextMuted(), 1.5f);
            x += 14.0f + 6.0f; // .tw 宽 14 + .node 的 gap 6
        }
        if (!node.icon.empty()) {
            // Shell.jsx:488 里的节点头图标是 12px（比 .tw 小）。
            DrawIcon(draw, node.icon, ImVec2(x, row.center().y - 6.0f), 12.0f, ColorTextMuted());
            x += 12.0f + 6.0f;
        }
        const float trailingWidth =
            node.trailing.empty()
                ? 0.0f
                : FontAt(12.0f)->CalcTextSizeA(12.0f, 1e9f, 0.0f, node.trailing.data(),
                                               node.trailing.data() + node.trailing.size())
                          .x;
        ImFont* font = FontAt(12.5f);
        // 原来写死 `row.center().y - 7.5f` —— 12.5px 字该减 6.25，偏上 1.25px。
        DrawTextClipped(draw, font, 12.5f, ImVec2(x, CenterTextY(font, 12.5f, row.center().y)),
                        row.max.x - x - 8.0f - trailingWidth, fg, node.label);
        if (!node.trailing.empty()) {
            // 右侧计数用 .tiny dim（12px muted）。
            // 原来写死 `row.center().y - 7.0f` —— 12px 字该减 6.0，偏上 1.0px。
            ImFont* tfont = FontAt(12.0f);
            DrawTextClipped(draw, tfont, 12.0f,
                            ImVec2(row.max.x - 8.0f - trailingWidth,
                                   CenterTextY(tfont, 12.0f, row.center().y)),
                            trailingWidth, ColorTextMuted(), node.trailing);
        }
        y += rowH;

        if (node.hasChildren && node.expanded) {
            // .kids（ui.css:807-811）：margin-left 14 + 1px line-subtle 竖线 + padding-left 6。
            // 我们把行整体右移了 20（14+6），竖线画在子层最左。
            const float lineX = row.min.x + 14.0f;
            const float childTop = y;
            y = TreeWalk(draw, Rect{ImVec2(bounds.min.x, y), ImVec2(bounds.max.x, bounds.max.y)},
                         node.children, selectedIndex, counter, depth + 1, id);
            draw->AddLine(ImVec2(lineX, childTop), ImVec2(lineX, y), ColorLineSubtle(), 1.0f);
        }
    }
    return y;
}

} // namespace

float Tree(ImDrawList* draw, Rect bounds, std::vector<TreeNode>& nodes, int& selectedIndex,
           std::string_view id) {
    if (nodes.empty()) {
        return 0.0f;
    }
    int counter = 0;
    return TreeWalk(draw, bounds, nodes, selectedIndex, counter, 0, id) - bounds.min.y;
}

// ------------------------------------------------------------------ 23 Menu
namespace {

float MenuRowHeightOf(MenuRowKind kind) {
    switch (kind) {
    case MenuRowKind::Separator: return 11.0f; // 1px 线 + margin 5/5
    case MenuRowKind::Label: return 19.0f;     // padding 6/3 + 10.5px 字
    case MenuRowKind::Item: break;
    }
    // .mi：padding 7/10 + 12.5px 字 + 1px 边。
    return 14.0f + 15.0f + 1.0f;
}

} // namespace

float MenuWidth(const std::vector<MenuRow>& rows) {
    // min-width: 180（shell.css:105）；内容更宽时按最宽项撑开。
    ImFont* font = FontAt(12.5f);
    float widest = 0.0f;
    for (const MenuRow& row : rows) {
        if (row.kind != MenuRowKind::Item) {
            continue;
        }
        float w = font->CalcTextSizeA(12.5f, 1e9f, 0.0f, row.label.data(),
                                      row.label.data() + row.label.size())
                      .x;
        if (!row.icon.empty()) {
            w += 15.0f + 9.0f; // .mi 的 gap 9
        }
        widest = std::max(widest, w + 20.0f + 10.0f); // pad 7/10 两边 + 面板 pad5
    }
    return std::max(180.0f, widest);
}

float MenuHeight(const std::vector<MenuRow>& rows) {
    float h = 5.0f * 2.0f; // .menu-pop padding 5
    for (const MenuRow& row : rows) {
        h += MenuRowHeightOf(row.kind);
    }
    return h;
}

int Menu(ImDrawList* draw, Rect anchor, const std::vector<MenuRow>& rows, std::string_view id) {
    if (rows.empty()) {
        return -1;
    }
    const float width = MenuWidth(rows);
    // top: calc(100% + 6px); right: 0（shell.css:103-104）—— 面板右边缘对齐锚点右边缘。
    const Rect panel = RectAt(anchor.max.x - width, anchor.max.y + 6.0f, width, MenuHeight(rows));
    DrawShadowed(draw, panel.min, panel.max, 10.0f, ColorOverlay(), ColorLineNormal(), 1.0f, theme::ShadowTier::Overlay);

    int picked = -1;
    float y = panel.min.y + 5.0f;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const MenuRow& row = rows[i];
        const float h = MenuRowHeightOf(row.kind);
        switch (row.kind) {
        case MenuRowKind::Separator:
            // .msep：height 1 + margin 5/6（上下各 5px 已在高度里）。
            draw->AddLine(ImVec2(panel.min.x + 6.0f, y + 5.0f),
                          ImVec2(panel.max.x - 6.0f, y + 5.0f), ColorLineSubtle(), 1.0f);
            y += h;
            continue;
        case MenuRowKind::Label: {
            // .mlabel：padding 6px 10px 3px，10.5/700 muted + 字距 .06em。
            ImFont* font = FontBoldAt(10.5f);
            draw->AddText(font, 10.5f, ImVec2(panel.min.x + 10.0f, y + 6.0f), ColorTextMuted(),
                          row.label.data(), row.label.data() + row.label.size());
            y += h;
            continue;
        }
        case MenuRowKind::Item:
            break;
        }
        // .mi 的实际可点区域是面板去掉 padding 5 后的整宽。
        const Rect item = RectAt(panel.min.x + 5.0f, y, width - 10.0f, h);
        const Hit hit = HitTestImpl(item, Unique(id, static_cast<int>(i)));
        if (hit.hovered && !row.disabled) {
            // .mi.on：accent 字 + accent-dim 底；普通 hover：fill-hover + primary。
            if (row.selected) {
                draw->AddRectFilled(item.min, item.max, ColorAccentDim());
            } else {
                draw->AddRectFilled(item.min, item.max, ColorFillHover());
            }
        }
        if (hit.clicked && !row.disabled) {
            picked = static_cast<int>(i);
        }
        const ImU32 fg = row.selected ? ColorAccent()
                                      : (hit.hovered && !row.disabled ? ColorText()
                                                                      : ColorTextSecondary());
        ImFont* font = FontAt(12.5f);
        float x = item.min.x + 10.0f;
        // `.menu-pop .mi`（shell.css:114-120）：display:flex + **align-items:center** +
        // gap 9 + padding 7px 10px + 12.5px。行盒中心 = 条目中心 ⇒ 文字按条目中心
        // 居中。原来写死 `y + 7.0f`（当成了 padding-top），而 `h` 是
        // `MenuRowHeightOf(Item)` = 14+15+1 = 30 ⇒ **偏上 1.75px**。
        // 图标 15px 同样按 align-items 居中，和文字共用同一个中心。
        const float centerY = item.center().y;
        const float ty = CenterTextY(font, 12.5f, centerY);
        if (!row.icon.empty()) {
            DrawIcon(draw, row.icon, ImVec2(x, centerY - 7.5f), 15.0f,
                     row.disabled ? WithAlpha(fg, 0.45f) : fg);
            x += 15.0f + 9.0f; // .mi gap 9
        }
        const ImU32 textColor = row.disabled ? WithAlpha(fg, 0.45f) : fg;
        DrawTextClipped(draw, font, 12.5f, ImVec2(x, ty), item.max.x - x - 10.0f, textColor,
                        row.label);
        y += h;
    }
    return picked;
}

} // namespace shine::kit
