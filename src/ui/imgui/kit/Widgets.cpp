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
    ImGui::SetCursorScreenPos(bounds.min);
    ImGui::InvisibleButton(std::string(id).c_str(), ImVec2(bounds.width(), bounds.height()));
    hit.hovered = ImGui::IsItemHovered();
    hit.held = ImGui::IsItemActive();
    hit.clicked = ImGui::IsItemClicked();
    hit.doubleClicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
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
        if (hit.hovered && enabled) {
            DrawDiagGradient(draw, body.min, body.max, rounding, accentHover, accent);
        } else {
            DrawDiagGradient(draw, body.min, body.max, rounding, accent, accentHover);
        }
        text = ColorAccentFg();
        break;
    case ButtonVariant::Secondary:
        fill = hit.hovered && enabled ? ColorFillHover() : ColorFillMuted();
        border = ColorLineNormal();
        text = ColorText();
        break;
    case ButtonVariant::Danger:
        fill = hit.hovered && enabled ? WithAlpha(ColorOf(theme::Current().statusDanger), 0.22f)
                                       : WithAlpha(ColorOf(theme::Current().statusDanger), 0.14f);
        border = WithAlpha(ColorOf(theme::Current().statusDanger), 0.42f);
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
    } else {
        // 主按钮的 btnPrimaryShadow：accent 28% 的一圈外扩光
        const Rect halo{body.min - ImVec2(0, 2), body.max + ImVec2(0, 2)};
        DrawRoundRect(draw, halo.min, halo.max, rounding + 2.0f, 0,
                      ColorOf(theme::CurrentDerived().btnPrimaryShadow), 2.0f);
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
        const float textY = body.min.y + (body.height() - fontSize) * 0.5f;
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
    if (!tip.empty() && hit.hovered) {
        ImGui::SetTooltip("%.*s", static_cast<int>(tip.size()), tip.data());
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
    const ImU32 border = glow ? ColorAccent() : (hoverable ? ColorLineSubtle() : ColorLineSubtle());
    DrawShadowed(draw, bounds.min, bounds.max, radius, ColorPanel(), border, 1.0f);

    const bool hasHeader = !title.empty() || !icon.empty();
    float y = bounds.min.y + 16.0f;
    if (hasHeader) {
        // 头：padding 12/16
        const float headerY = bounds.min.y + 12.0f;
        const float iconSize = 15.0f;
        float x = bounds.min.x + 16.0f;
        if (!icon.empty()) {
            DrawIcon(draw, icon, ImVec2(x, headerY + 1.0f), iconSize, ColorAccent());
            x += iconSize + 8.0f;
        }
        ImFont* font = FontBoldAt(13.5f);
        DrawTextClipped(draw, font, 13.5f, ImVec2(x, headerY), bounds.width() - 32.0f,
                        ColorText(), title);
        y = bounds.min.y + 12.0f + 20.0f;
        // 头下边框
        draw->AddLine(ImVec2(bounds.min.x, y + 11.0f), ImVec2(bounds.max.x, y + 11.0f),
                      ColorLineSubtle(), 1.0f);
        y += 12.0f;
    }
    return Rect{ImVec2(bounds.min.x + 16.0f, y), ImVec2(bounds.max.x - 16.0f, bounds.max.y - 16.0f)};
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
            draw->AddCircleFilled(ImVec2(item.center().x, item.min.y + 3.5f), 2.0f, ColorAccent(),
                                 10);
        } else if (hit.hovered) {
            DrawRoundRect(draw, item.min, item.max, 4.0f, ColorFillHover());
        }
        const ImU32 fg = on ? ColorText() : (hit.hovered ? ColorText() : ColorTextSecondary());
        draw->AddText(font, 12.5f,
                      ImVec2(item.center().x - 0.5f * text,
                             item.center().y - 12.5f * 0.5f),
                      fg, option.label.data(), option.label.data() + option.label.size());
        x = item.max.x + 2.0f;
    }
    return picked;
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
        draw->AddText(font, 13.0f, ImVec2(item.min.x + 12.0f, item.min.y + 8.0f), fg, tab.label.data(),
                      tab.label.data() + tab.label.size());
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
    DrawHGradient(draw, fill.min, fill.max, height * 0.5f, ColorAccent(), ColorAccentHover());
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
        draw->AddText(font, 13.0f, ImVec2(bounds.center().x - 0.5f * w, y), ColorText(), title.data(),
                      title.data() + title.size());
        y += 19.0f;
    }
    if (!body.empty()) {
        ImFont* font = FontAt(12.5f);
        DrawTextClipped(draw, font, 12.5f, ImVec2(bounds.center().x - 160.0f, y), 320.0f,
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
        DrawTextClipped(draw, font, 12.5f, ImVec2(bounds.min.x, y), keyWidth, ColorTextMuted(), key);
        DrawTextClipped(draw, bold, 12.5f, ImVec2(bounds.min.x + keyWidth + 14.0f, y),
                        bounds.width() - keyWidth - 14.0f, ColorText(), value);
        y += rowHeight;
    }
}

} // namespace shine::kit
