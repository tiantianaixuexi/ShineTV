#include "ui/imgui/pages/PageKpi.h"

namespace shine::pages {

using namespace shine::kit;

void KpiCard(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view value,
             std::string_view unit, std::string_view footnote, theme::Tone tone) {
    DrawShadowed(draw, bounds.min, bounds.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
    // 右上角 90px accent 柔光。设计稿是 filter:blur(2px) 的模糊圆，ImGui draw list
    // 没有模糊可用 —— 用同心圆叠出径向衰减。
    //
    // 关键是**每层 alpha 相同**而不是每层颜色相同：圆环面积等分，中心累计到峰值、
    // 最外圈只有峰值的 1/layers，边界那一跳就只有 1/layers，肉眼看不出硬边。
    // 均匀叠加 4 层实心圆会读成「贴了张色斑」而不是「有光」。
    const float blobR = 45.0f; // 90px 直径
    const ImVec2 blob(bounds.max.x - 26.0f, bounds.min.y + 26.0f);
    const ImU32 toneColor = ToneColor(tone);
    constexpr int kLayers = 14;
    constexpr float kPeak = 0.16f;
    for (int i = 0; i < kLayers; ++i) {
        const float r = blobR * static_cast<float>(i + 1) / static_cast<float>(kLayers);
        draw->AddCircleFilled(blob, r, WithAlpha(toneColor, kPeak / kLayers), 24);
    }
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(bounds.min.x + 16.0f, bounds.min.y + 14.0f),
                  ColorTextSecondary(), label.data(), label.data() + label.size());
    ImFont* valueFont = FontBoldAt(24.0f);
    draw->AddText(valueFont, 24.0f, ImVec2(bounds.min.x + 16.0f, bounds.min.y + 30.0f), ColorText(),
                  value.data(), value.data() + value.size());
    const float valueW =
        valueFont->CalcTextSizeA(24.0f, 1e9f, 0.0f, value.data(), value.data() + value.size()).x;
    if (!unit.empty()) {
        draw->AddText(FontAt(12.5f), 12.5f,
                      ImVec2(bounds.min.x + 16.0f + valueW + 4.0f, bounds.min.y + 48.0f),
                      ColorTextMuted(), unit.data(), unit.data() + unit.size());
    }
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(bounds.min.x + 16.0f, bounds.max.y - 24.0f),
                  ColorTextMuted(), footnote.data(), footnote.data() + footnote.size());
}

} // namespace shine::pages
