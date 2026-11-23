#include "ui/imgui/kit/Text.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Widget_Color.h"

#include <cmath>

namespace shine::kit {
namespace {

// 色的兜底只发生在这一处：style.color == 0（调用点没给）按正文色画。
// 主题色一律来自 Widget_Color 的取色入口，本文件不出现任何颜色字面量。
ImU32 ResolveColor(const TextStyle& style) { return style.color != 0 ? style.color : ColorText(); }

// 字面（常规 / 粗 / 等宽）三选一。字号 token 在 FontAt 系内部乘全局缩放档
// （kit/Fonts.cpp 的 g_fontScale），这里不再乘第二次。
ImFont* FaceOf(const TextStyle& style) {
    if (style.mono) {
        return MonoAt(style.size);
    }
    return style.bold ? FontBoldAt(style.size) : FontAt(style.size);
}

} // namespace

float AscentOf(float size) { return std::ceil(size * 0.8017f); }
float BaselineToY(float baseline, float size) { return baseline - AscentOf(size); }

ImVec2 TextSize(std::string_view text, const TextStyle& style) {
    ImFont* face = FaceOf(style);
    if (face == nullptr || text.empty()) {
        return ImVec2(0.0f, 0.0f);
    }
    return face->CalcTextSizeA(style.size, 1e9f, style.wrapWidth, text.data(),
                               text.data() + text.size());
}

void Text(ImDrawList* draw, ImVec2 pos, std::string_view text, const TextStyle& style) {
    if (text.empty() || draw == nullptr) {
        return;
    }
    draw->AddText(FaceOf(style), style.size, pos, ResolveColor(style), text.data(),
                  text.data() + text.size(), style.wrapWidth);
}

void TextAtBaseline(ImDrawList* draw, float x, float baseline, std::string_view text,
                    const TextStyle& style) {
    Text(draw, ImVec2(x, BaselineToY(baseline, style.size)), text, style);
}

// ⚠️ 以下三族是**逐位转发**，不是新实现：`FaceOf` / `ResolveColor` 决定字面与色，
//    量与画都从同一个 TextStyle 出发 —— 「画用 A 档、量宽用 B 档」这个分叉
//    结构上不再可能（这正是收编它们的目的）。改这里的任何一行都要重跑像素计数
//    判据，别拿「看起来一样」当等价。
float TextClipped(ImDrawList* draw, ImVec2 pos, float maxWidth, std::string_view text,
                  const TextStyle& style, bool wrap) {
    return DrawTextClipped(draw, FaceOf(style), style.size, pos, maxWidth, ResolveColor(style),
                           text, wrap);
}

float TextClippedWidth(float maxWidth, std::string_view text, const TextStyle& style) {
    return MeasureClipped(FaceOf(style), style.size, maxWidth, text);
}

float TextWidth(std::string_view text, const TextStyle& style) { return TextSize(text, style).x; }

} // namespace shine::kit
