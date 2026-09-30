#include "ui/imgui/kit/Widget_Status.h"

#include "ui/imgui/kit/Draw.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Icon.h"
#include "ui/imgui/kit/Widget_Color.h"

#include <algorithm>

namespace shine::kit {

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
        //    侧栏只有 240px 宽时，一个 320 的框从 x=-30 铺到 290，两端都溢出面板
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

// ------------------------------------------------------------------ 20 Divider
void Divider(ImDrawList* draw, Rect bounds) {
    // .msep（ui.css:134-138）：1px line-subtle。调用方给的是**已含上下 5px
    // 外边距**的矩形，所以线画在垂直居中。
    const float y = 0.5f * (bounds.min.y + bounds.max.y);
    draw->AddLine(ImVec2(bounds.min.x, y), ImVec2(bounds.max.x, y), ColorLineSubtle(), 1.0f);
}

} // namespace shine::kit
