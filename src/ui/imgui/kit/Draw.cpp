#include "ui/imgui/kit/Draw.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace shine::kit {
namespace {

bool g_reduceMotion = false;
float g_now = 0.0f;

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto channel = [a, b, t](int shift) {
        const auto ca = (a >> shift) & 0xFFu;
        const auto cb = (b >> shift) & 0xFFu;
        return static_cast<ImU32>(std::lround(ca + (cb - ca) * t));
    };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

ImU32 LerpColor(ImU32 a, ImU32 b, float t) { return Mix(a, b, t); }

// 圆角矩形在高度 y 处的水平内缩（CSS border-radius 的几何）。
float RoundedInset(float rounding, float height, float y) {
    if (rounding <= 0.0f) {
        return 0.0f;
    }
    const float r = std::min(rounding, 0.5f * std::min(height, 1e9f));
    if (r <= 0.0f) {
        return 0.0f;
    }
    const float fromTop = y;
    const float fromBottom = height - y;
    const float dy = std::min(fromTop, fromBottom);
    if (dy >= r) {
        return 0.0f;
    }
    return r - std::sqrt(std::max(0.0f, r * r - (r - dy) * (r - dy)));
}

// 圆角 + 渐变：ImGui 的 AddRectFilledMultiColor **不支持圆角**，而
// 主按钮 / 进度条 / 开关的渐变填充都要求胶囊或 r-sm 圆角。
// 做法：按行切成横带，每带算一次圆角内缩，用 ImDrawList 的原始图元
// 接口直接写三角形。圆角矩形是凸的，按水平带切不会自交。
void FillRoundedBands(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 from,
                      ImU32 to, bool vertical) {
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    if (rounding <= 0.0f) {
        draw->AddRectFilledMultiColor(min, max, from, from, to, to);
        return;
    }

    const float extent = vertical ? height : width;
    const int bands = std::clamp(static_cast<int>(std::ceil(extent)), 1, 96);
    const float step = extent / static_cast<float>(bands);

    // PrimReserve 的参数顺序是 (索引数, 顶点数)；索引由 PrimVtx 自动写当前绝对下标。
    draw->PrimReserve(6 * bands, 6 * bands);
    for (int i = 0; i < bands; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(bands);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(bands);
        const ImU32 c0 = LerpColor(from, to, t0);
        const ImU32 c1 = LerpColor(from, to, t1);

        ImVec2 a;
        ImVec2 b;
        ImVec2 c;
        ImVec2 d;
        ImU32 ca;
        ImU32 cb;
        if (vertical) {
            const float y0 = min.y + t0 * height;
            const float y1 = min.y + t1 * height;
            // 上下两个角各算一次内缩，带内取较大者（保守，不会溢出圆角）
            const float inset = std::max(RoundedInset(rounding, height, y0 - min.y),
                                         RoundedInset(rounding, height, y1 - min.y));
            a = ImVec2(min.x + inset, y0);
            b = ImVec2(max.x - inset, y0);
            c = ImVec2(max.x - inset, y1);
            d = ImVec2(min.x + inset, y1);
            ca = c0;
            cb = c1;
        } else {
            const float x0 = min.x + t0 * width;
            const float x1 = min.x + t1 * width;
            const float inset = std::max(RoundedInset(rounding, width, x0 - min.x),
                                         RoundedInset(rounding, width, x1 - min.x));
            a = ImVec2(x0, min.y + inset);
            b = ImVec2(x1, min.y + inset);
            c = ImVec2(x1, max.y - inset);
            d = ImVec2(x0, max.y - inset);
            ca = c0;
            cb = c1;
        }
        draw->PrimVtx(a, ImVec2(0, 0), ca);
        draw->PrimVtx(b, ImVec2(1, 0), ca);
        draw->PrimVtx(c, ImVec2(1, 1), cb);
        draw->PrimVtx(a, ImVec2(0, 0), ca);
        draw->PrimVtx(c, ImVec2(1, 1), cb);
        draw->PrimVtx(d, ImVec2(0, 1), cb);
    }
}

float TextWidth(ImFont* font, float size, const char* begin, const char* end) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x;
}

} // namespace

void SetReduceMotion(bool on) { g_reduceMotion = on; }
bool ReduceMotion() { return g_reduceMotion; }

void TickAnimation(float deltaSeconds) {
    // 减少动效只压缩**过渡时长**，不冻结时间轴：脉冲/进度仍要走完，
    // 否则「运行中」的呼吸感全没了，验收截图会看起来像卡住。
    g_now += deltaSeconds;
}

float Now() { return g_now; }

float Pulse(float periodSeconds, float phase) {
    if (periodSeconds <= 0.0f) {
        return 0.0f;
    }
    const float t = std::fmod(g_now + phase, periodSeconds) / periodSeconds;
    return 0.5f * (0.5f * (1.0f - std::cos(t * 2.0f * 3.14159265358979f)));
}

int AutoGridCols(float available, float minColumn, float gap) {
    if (available <= 0.0f || minColumn <= 0.0f) {
        return 1;
    }
    // auto-fit：列宽不低于 minColumn 的前提下尽量多塞几列，放不下就换行。
    int best = 1;
    for (int n = 1; n <= 32; ++n) {
        const float needed = static_cast<float>(n) * minColumn + static_cast<float>(n - 1) * gap;
        if (needed <= available) {
            best = n;
        }
    }
    return best;
}

int AutoFillCols(float available, float minColumn, float gap) {
    return AutoGridCols(available, minColumn, gap);
}

void DrawRoundRect(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 fill,
                   ImU32 border, float borderWidth, bool topHighlight) {
    if (max.x <= min.x || max.y <= min.y) {
        return;
    }
    const float radius = std::max(0.0f, std::min(rounding, 0.5f * (max.x - min.x)));
    if ((fill >> 24) != 0) {
        draw->AddRectFilled(min, max, fill, radius);
    }
    if (borderWidth > 0.0f && (border >> 24) != 0) {
        draw->AddRect(min, max, border, radius, borderWidth);
    }
    if (topHighlight) {
        // box-shadow 的上沿高光近似：内侧 1px 半透明白线。已知降级（R5）。
        const float inset = std::min(1.0f, radius);
        draw->AddLine(ImVec2(min.x + inset, min.y + 0.5f), ImVec2(max.x - inset, min.y + 0.5f),
                      ImU32(0x38FFFFFFu), 1.0f);
    }
}

void DrawVGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 top,
                   ImU32 bottom) {
    FillRoundedBands(draw, min, max, rounding, top, bottom, /*vertical=*/true);
}

void DrawDiagGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 from,
                      ImU32 to) {
    // 120° 斜渐变：ImGui 原生只有四角双线性且不支持圆角。
    // 沿对角线分带，每带的左右两端取不同的插值比例来逼近斜向过渡。
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    const int bands = std::clamp(static_cast<int>(std::ceil(height)), 1, 96);
    const float step = height / static_cast<float>(bands);
    draw->PrimReserve(6 * bands, 6 * bands);
    for (int i = 0; i < bands; ++i) {
        const float y0 = min.y + step * static_cast<float>(i);
        const float y1 = y0 + step;
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(bands);
        const ImU32 left = LerpColor(from, to, t);
        const ImU32 right = LerpColor(from, to, std::min(1.0f, t + 0.5f));
        const float inset = std::max(RoundedInset(rounding, height, y0 - min.y),
                                     RoundedInset(rounding, height, y1 - min.y));
        draw->PrimVtx(ImVec2(min.x + inset, y0), ImVec2(0, 0), left);
        draw->PrimVtx(ImVec2(max.x - inset, y0), ImVec2(1, 0), left);
        draw->PrimVtx(ImVec2(max.x - inset, y1), ImVec2(1, 1), right);
        draw->PrimVtx(ImVec2(min.x + inset, y0), ImVec2(0, 0), left);
        draw->PrimVtx(ImVec2(max.x - inset, y1), ImVec2(1, 1), right);
        draw->PrimVtx(ImVec2(min.x + inset, y1), ImVec2(0, 1), right);
    }
}

void DrawHGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 from,
                   ImU32 to) {
    FillRoundedBands(draw, min, max, rounding, from, to, /*vertical=*/false);
}

void DrawShadowed(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 fill,
                  ImU32 border, float borderWidth) {
    DrawRoundRect(draw, min, max, rounding, fill, border, borderWidth, /*topHighlight=*/true);
}

float DrawTextClipped(ImDrawList* draw, ImFont* font, float fontSize, ImVec2 pos, float maxWidth,
                      ImU32 color, std::string_view text, bool wrap) {
    if (font == nullptr || text.empty() || maxWidth <= 0.0f) {
        return 0.0f;
    }
    if (!wrap) {
        const float width = MeasureClipped(font, fontSize, maxWidth, text);
        // 截断时画截断后的内容：逐字退到能容下「…」。
        std::size_t end = text.size();
        if (TextWidth(font, fontSize, text.data(), text.data() + text.size()) > maxWidth) {
            const char* ellipsis = "…";
            const float ellipsisWidth = TextWidth(font, fontSize, ellipsis, ellipsis + 3);
            const float budget = maxWidth - ellipsisWidth;
            while (end > 0) {
                if (TextWidth(font, fontSize, text.data(), text.data() + end) <= budget) {
                    break;
                }
                --end;
                while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
                    --end;
                }
            }
            draw->AddText(font, fontSize, pos, color, text.data(), text.data() + end);
            draw->AddText(font, fontSize, ImVec2(pos.x + budget, pos.y), color, ellipsis,
                          ellipsis + 3);
            return maxWidth;
        }
        draw->AddText(font, fontSize, pos, color, text.data(), text.data() + text.size());
        (void)width;
        return TextWidth(font, fontSize, text.data(), text.data() + text.size());
    }

    // 手工折行：ImGui 的 AddText 不自动换行。行高按 tokens.css 的 1.6。
    const float lineHeight = fontSize * 1.6f;
    float y = pos.y;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = start;
        while (end < text.size()) {
            std::size_t next = end + 1;
            while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) {
                ++next;
            }
            if (TextWidth(font, fontSize, text.data() + start, text.data() + next) > maxWidth &&
                end > start) {
                break;
            }
            end = next;
        }
        draw->AddText(font, fontSize, ImVec2(pos.x, y), color, text.data() + start,
                      text.data() + end);
        y += lineHeight;
        start = end;
    }
    return y - pos.y;
}

float MeasureClipped(ImFont* font, float fontSize, float maxWidth, std::string_view text) {
    if (font == nullptr || text.empty()) {
        return 0.0f;
    }
    const float full = TextWidth(font, fontSize, text.data(), text.data() + text.size());
    if (full <= maxWidth) {
        return full;
    }
    const char* ellipsis = "…";
    const float ellipsisWidth = TextWidth(font, fontSize, ellipsis, ellipsis + 3);
    const float budget = maxWidth - ellipsisWidth;
    if (budget <= 0.0f) {
        return ellipsisWidth;
    }
    std::size_t end = text.size();
    while (end > 0) {
        if (TextWidth(font, fontSize, text.data(), text.data() + end) <= budget) {
            return TextWidth(font, fontSize, text.data(), text.data() + end) + ellipsisWidth;
        }
        --end;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end;
        }
    }
    return ellipsisWidth;
}

void DrawDotGrid(ImDrawList* draw, ImVec2 min, ImVec2 max, float cell, ImU32 dot) {
    if (cell <= 1.0f) {
        return;
    }
    for (float y = min.y + 1.0f; y < max.y; y += cell) {
        for (float x = min.x + 1.0f; x < max.x; x += cell) {
            draw->AddCircleFilled(ImVec2(x, y), 1.0f, dot, 1);
        }
    }
}

ImU32 GlassColor() { return theme::PackRgba(theme::Rgba(theme::CurrentDerived().glass)); }

ImU32 ToneColor(theme::Tone tone) {
    const auto& c = theme::Current();
    switch (tone) {
    case theme::Tone::Accent: return theme::PackRgba(theme::Rgba(c.accentPrimary));
    case theme::Tone::Info: return theme::PackRgba(theme::Rgba(c.accentInfo));
    case theme::Tone::Ok: return theme::PackRgba(theme::Rgba(c.statusOk));
    case theme::Tone::Warn: return theme::PackRgba(theme::Rgba(c.statusWarn));
    case theme::Tone::Danger: return theme::PackRgba(theme::Rgba(c.statusDanger));
    case theme::Tone::Busy: return theme::PackRgba(theme::Rgba(c.statusBusy));
    case theme::Tone::Idle: return theme::PackRgba(theme::Rgba(c.statusIdle));
    }
    return theme::PackRgba(theme::Rgba(c.statusIdle));
}

ImU32 ToneBackground(theme::Tone tone) {
    return theme::PackRgba(
        theme::Rgba(theme::CurrentDerived().tagBg[static_cast<std::size_t>(tone)]));
}

ImU32 ToneBorder(theme::Tone tone) {
    return theme::PackRgba(
        theme::Rgba(theme::CurrentDerived().tagBorder[static_cast<std::size_t>(tone)]));
}

} // namespace shine::kit
