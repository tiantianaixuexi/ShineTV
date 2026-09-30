#include "ui/imgui/kit/Widget_Button.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"
#include "ui/imgui/kit/Widget_Overlay.h"

#include <algorithm>

namespace shine::kit {
namespace {

// Button loading 占位转圈。**只服务 Button**，故留在本文件私有，
// 不进 Widget_Core 的 detail（放进公共 detail 就成了没人用的第二份 spinner）。
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
    const Hit hit = detail::HitTestItem(bounds, id);
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
    const Hit hit = detail::HitTestItem(bounds, id);
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

} // namespace shine::kit
