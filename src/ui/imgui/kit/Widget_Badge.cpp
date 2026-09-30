#include "ui/imgui/kit/Widget_Badge.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Widget_Color.h"

#include <algorithm>

namespace shine::kit {

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

} // namespace shine::kit
