#include "ui/imgui/kit/Draw.h"

#include "core/Log.h"
#include "im_anim.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>

namespace shine::kit {
namespace {

bool g_reduceMotion = false;
float g_now = 0.0f;
// 上一帧的时间步，pinned 时为 0。过渡补间要它（见 kit/Anim.h）。
float g_lastDelta = 0.0f;
// 取证用：钉住时钟后 TickAnimation 不再推进（见 PinAnimation）。
bool g_animationPinned = false;

// 混合两个**已打包的 ImU32**。字节序必须用 ImGui 自己的位移宏：
// 默认（未定义 IMGUI_USE_BGRA_PACKED_COLOR）下 R 在**最低**字节、alpha 在最高字节。
// 手写 24/16/8/0 会把 alpha 当成红通道 —— 实测表现为 accent 主按钮被插值成一团灰紫。
ImU32 Mix(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto channel = [a, b, t](int shift) {
        const auto ca = (a >> shift) & 0xFFu;
        const auto cb = (b >> shift) & 0xFFu;
        return static_cast<ImU32>(std::lround(ca + (cb - ca) * t));
    };
    return (channel(IM_COL32_R_SHIFT) | (channel(IM_COL32_G_SHIFT) << IM_COL32_G_SHIFT) |
            (channel(IM_COL32_B_SHIFT) << IM_COL32_B_SHIFT) |
            (channel(IM_COL32_A_SHIFT) << IM_COL32_A_SHIFT));
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
    //
    // ⚠️ UV 必须是白像素 UV。ImDrawList 的默认纹理是**字体图集**，写 (0,0)-(1,1) 等于
    // 把整张字形图集缩微采样进每个三角形 —— 画出来是一排字形条纹，而不是渐变。
    // ImGui 为此专门留了公开 API：ImGui::GetFontTexUvWhitePixel()。
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
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
        draw->PrimVtx(a, uv, ca);
        draw->PrimVtx(b, uv, ca);
        draw->PrimVtx(c, uv, cb);
        draw->PrimVtx(a, uv, ca);
        draw->PrimVtx(c, uv, cb);
        draw->PrimVtx(d, uv, cb);
    }
}

float TextWidth(ImFont* font, float size, const char* begin, const char* end) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x;
}

} // namespace

void SetReduceMotion(bool on) { g_reduceMotion = on; }
bool ReduceMotion() { return g_reduceMotion; }

void TickAnimation(float deltaSeconds) {
    // ImAnim 的每帧泵。**即使钉住时钟也要调**：否则在飞的补间不推进，
    // 解钉后会带着一大段欠账一次性跳完。
    iam_update_begin_frame();
    g_lastDelta = g_animationPinned ? 0.0f : deltaSeconds;
    // 减少动效只压缩**过渡时长**，不冻结时间轴：脉冲/进度仍要走完，
    // 否则「运行中」的呼吸感全没了，验收截图会看起来像卡住。
    if (g_animationPinned) {
        return;
    }
    g_now += deltaSeconds;
}

float LastFrameDelta() { return g_lastDelta; }

bool AnimationPinned() { return g_animationPinned; }

void PinAnimation(float seconds) {
    g_animationPinned = true;
    g_now = seconds;
    // ⚠️ 清一次补间池：钉住期间 `Anim.h` 的过渡函数会**直接返回 target**，
    //    而 ImAnim 池里那些在飞的补间还停在中间值。不清的话，解钉那一帧它们会
    //    从中间值继续走，视觉上就是「取证结束后界面突然闪一下」。
    iam_pool_clear();
}

void UnpinAnimation() { g_animationPinned = false; }

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

namespace {
// 反向矩形自检的**唯一一份**状态。三个对外函数共用它 —— 早先各写一个函数内
// static，结果 count 和 last 互不相干，Reset 也是空壳，等于没记。
struct InvertedRectState {
    int count = 0;
    char last[128] = {};
};
InvertedRectState& InvertedState() {
    static InvertedRectState state;
    return state;
}
} // namespace

void NoteInvertedRect(float minX, float minY, float maxX, float maxY, const char* where) {
    InvertedRectState& state = InvertedState();
    ++state.count;
    std::snprintf(state.last, sizeof(state.last), "%s min=(%.0f,%.0f) max=(%.0f,%.0f)", where,
                  minX, minY, maxX, maxY);
    // 只报前 8 次：同一个控件每帧都会触发一次，不限量的话日志会被刷爆，
    // 真正的「唯一一处」反而被埋掉。判据用的是计数，不是条数。
    if (state.count <= 8) {
        shine::log::Error("kit: 反向矩形 —— {} 会被整块丢弃（画不出、也点不到）", state.last);
    }
}
int InvertedRectCount() { return InvertedState().count; }
void ResetInvertedRectCount() { InvertedState() = InvertedRectState{}; }
const char* LastInvertedRect() { return InvertedState().last; }

namespace {
// 命中自检的**唯一一份**状态，理由与 InvertedState 相同：共用才有意义。
struct HoverProbeState {
    int count = 0;
    char last[128] = {};
};
HoverProbeState& HoverState() {
    static HoverProbeState state;
    return state;
}
} // namespace

void NoteHoveredItem(const char* id) {
    HoverProbeState& state = HoverState();
    ++state.count;
    std::snprintf(state.last, sizeof(state.last), "%s", id != nullptr ? id : "(null)");
}
int HoveredItemCount() { return HoverState().count; }
void ResetHoveredItemCount() { HoverState() = HoverProbeState{}; }
const char* LastHoveredItem() { return HoverState().last; }

void DrawRoundRect(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 fill,
                   ImU32 border, float borderWidth, bool topHighlight) {
    if (max.x <= min.x || max.y <= min.y) {
        // 严格反向才是 bug；退化（max == min，宽高为 0 的空控件）是合法用法。
        if (max.x < min.x || max.y < min.y) {
            NoteInvertedRect(min.x, min.y, max.x, max.y, "DrawRoundRect");
        }
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
    // 白像素 UV —— 见 FillRoundedBands 里的同一条说明，这里踩过一次同样的坑。
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    draw->PrimReserve(6 * bands, 6 * bands);
    for (int i = 0; i < bands; ++i) {
        const float y0 = min.y + step * static_cast<float>(i);
        const float y1 = y0 + step;
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(bands);
        const ImU32 left = LerpColor(from, to, t);
        const ImU32 right = LerpColor(from, to, std::min(1.0f, t + 0.5f));
        const float inset = std::max(RoundedInset(rounding, height, y0 - min.y),
                                     RoundedInset(rounding, height, y1 - min.y));
        draw->PrimVtx(ImVec2(min.x + inset, y0), uv, left);
        draw->PrimVtx(ImVec2(max.x - inset, y0), uv, left);
        draw->PrimVtx(ImVec2(max.x - inset, y1), uv, right);
        draw->PrimVtx(ImVec2(min.x + inset, y0), uv, left);
        draw->PrimVtx(ImVec2(max.x - inset, y1), uv, right);
        draw->PrimVtx(ImVec2(min.x + inset, y1), uv, right);
    }
}

void DrawHGradient(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 from,
                   ImU32 to) {
    FillRoundedBands(draw, min, max, rounding, from, to, /*vertical=*/false);
}

namespace {
// 一个 CSS box-shadow 单层的近似。
//
// ⚠️ ImGui 的 draw list **没有 blur**：没有 filter、不能画高斯。所以这里用
//    「K 层同心圆角矩形」逼近，扩散范围取 CSS 的 blur 半径。
//
// ⚠️⚠️ 这几层的 alpha 只能按**增量**给，不能每层各带一份：
//    CSS 的模糊是**同一个轮廓**被高斯糊开，所以距离本体边 d 处的 alpha 是
//    单调衰减的一条曲线。如果把第 i 环画成「只覆盖 [g_i, g_{i+1}] 这一圈」并
//    各带 alpha·w_i，那各环**互不重叠**、互不累加，边缘处的可见 alpha 反而
//    只有 w_0/Σw —— 6 环时 Σw≈3.2，峰值被摊到 31%，投影等于淡了三分之二，
//    而且「看起来加了、实际几乎看不见」，是最难发现的一类偏差。
//    正确做法：第 i 环铺满 [本体边, g_i]，自带**增量** alpha·(w_i − w_{i+1})，
//    由外向内叠。任意 d 处的累计 = Σ(增量) = w_{i(d)}·alpha，边最浓、往外渐隐。
constexpr int kShadowSteps = 6;
// 权重 exp(-k·t²)，t = d / blur。k=3.0 时最外圈剩 4.9%，硬边落在 8 位色差
// 约 5/255，看不出来；再大就会让中段偏薄。
constexpr float kShadowFalloff = 3.0f;

// 改 alpha，保留 RGB。⚠️ **必须走 ImGui 自己的 float4 往返，不能手写位移**：
// ImU32 的字节序是编译期宏（IMGUI_USE_BGRA_PACKED_COLOR 决定 R 在高位还是低位），
// 手写过一次 —— 默认字节序下 R 在最低字节，于是 alpha 被写进了红通道。
// 这里用与 kit::WithAlphaSet 同一套做法，只是就地实现，免得为此把 Widgets.h
// 拉进 Draw.cpp（那是循环包含：Widgets.h 依赖 Draw.h）。
ImU32 WithShadowAlpha(ImU32 color, float alpha) {
    const ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, alpha));
}
}  // namespace

void DrawShadow(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, theme::ShadowTier tier,
                float alphaScale, float scale) {
    if (draw == nullptr || tier == theme::ShadowTier::None || tier >= theme::ShadowTier::kCount) {
        return;
    }
    if (alphaScale <= 0.0f || scale <= 0.0f) {
        return;
    }
    if (max.x <= min.x || max.y <= min.y) {
        return;
    }
    const theme::ShadowSpec& spec = theme::CurrentShadowSpec(tier);
    if (spec.count <= 0) {
        return;
    }
    // ⚠️ 颜色 token 在这里**只提供色相（RGB）**，alpha 一律取 ShadowLayer 里的
    //    绝对值（逐字来自 tokens.css）。两个原因：
    //      1. 主题 JSON 的 `shadow.1` 存的就是 CSS **第二层**的 alpha（深空
    //         0x4D ≈ 0.30 对应 CSS 的 0.3），再乘一遍就只剩 45%；
    //      2. CSS 的 --shadow-1 是**两层**、两层 alpha 不同（深空是 0.35 与
    //         0.30），而 JSON 只有一个值，接触阴影那层根本无处可取。
    //    所以逐层绝对 alpha 记在 Theme.cpp 的 kShadowSpecs 里。
    //
    //    RGBA→ImU32 是 theme 层的事（theme::ToImU32；kit::ColorOf 只是转发），
    //    这里不引 kit::ColorOf —— Widgets.h 依赖 Draw.h，引了就是循环包含。
    const ImU32 base = theme::ToImU32(theme::CurrentShadowToken(tier));
    const float radius = std::max(0.0f, std::min(rounding, 0.5f * (max.x - min.x)));

    // ⚠️⚠️ `AddRectFilled` 的签名是 `(p_min, p_max, col, rounding)` —— **颜色在圆角前面**。
    //    写反了不报编译错，也不崩：col 位收到一个半径（14），于是颜色变成
    //    0x0000000E（全透明黑），圆角变成 0xFF000000 被截成巨大值。半径为 0 的那次
    //    更彻底 —— col 正好是 0，被 `if (col == 0) return;` 整块丢掉。
    //    症状是「函数明明调用了、顶点也涨了（同一帧别的图元），但屏幕上什么都没有」。
    //    这类错误编译期完全沉默，**只能靠读渲染结果发现**，所以这里写死注释。
    //
    // 逆序：CSS 里先写的层画在上面，所以**先画大模糊那层**（环境阴影在底，
    // 接触阴影压在上面），两层的叠加才和浏览器一致。
    for (int li = spec.count - 1; li >= 0; --li) {
        const theme::ShadowLayer& layer = spec.layers[li];
        const float alpha = layer.alpha * alphaScale;
        if (alpha <= 0.0f) {
            continue;
        }
        const ImVec2 off(layer.dx * scale, layer.dy * scale);
        const float reach = layer.blur * scale;
        if (reach <= 0.5f) {
            // 无模糊（0 或 CSS 的 0.5px 舍入）：直接一块实心。
            draw->AddRectFilled(min + off, max + off, WithShadowAlpha(base, alpha), radius);
            continue;
        }
        // 由外向内叠：第 i 环铺满 [本体边, g_i]，只带增量 alpha·(w_i − w_{i+1})。
        // 最外环先画（范围最大），最内环最后画（压在本体边上）。
        for (int i = kShadowSteps - 1; i >= 0; --i) {
            const float t = static_cast<float>(i + 1) / static_cast<float>(kShadowSteps);
            const float grow = t * reach;
            const float wHere = std::exp(-kShadowFalloff * t * t);
            const float wNext =
                std::exp(-kShadowFalloff * (t + 1.0f / kShadowSteps) * (t + 1.0f / kShadowSteps));
            const float stepAlpha = alpha * std::max(0.0f, wHere - wNext);
            if (stepAlpha <= 0.0f) {
                continue;
            }
            draw->AddRectFilled(min + off - ImVec2(grow, grow), max + off + ImVec2(grow, grow),
                                WithShadowAlpha(base, stepAlpha), radius + grow);
        }
    }
}

void DrawShadowed(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, ImU32 fill,
                  ImU32 border, float borderWidth, theme::ShadowTier tier) {
    // 投影必须**先于**本体画，否则会被本体盖掉（本体是实心的）。
    DrawShadow(draw, min, max, rounding, tier);
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

// 撤回过一版「按 Ascent+Descent 算」的写法，**那版是错的**，留个记录：
//
//   ImGui 的 `ImFont::RenderText` 里 `const float line_height = size;` —— 行盒高度
//   **就等于请求字号**，基线落在 `pos.y + Ascent*scale`（`imgui_draw.cpp:4858`）。
//   所以 `pos.y = centerY - fontSize/2` 居中的正是 ImGui 的行盒，**是 ImGui 的原生
//   约定、也是对的**。按 `Ascent + Descent` 算反而错：这一版 ImGui 的 `Descent` 是
//   **负数**（本机实测 size=13 时 asc=11 / desc=-3，合计只有 8），用它会把字往下
//   推 2.5px —— 方向正好和「字偏高」相反。
//
// 那 0.5px 的真实来源（顶栏「运行」按钮实测：墨迹中心 23.0 / 按钮中心 23.5）：
//   CJK 的墨迹盒约跨基线 −0.88em ~ +0.12em，中心在基线上方 0.38em。代入
//   `基线 = centerY - 6.5 + 11`，墨迹中心 = `centerY - 0.44` —— 与实测吻合。
//   也就是说**「居中」这件事本身在 ImGui 里是近似的**（Latin 与 CJK 的光学中心
//   不同，一个公式同时伺候不了），0.5px 量级不值得为它改 40 处。真正要找的是
//   「明显不在中间」的那类，量级差两个数量级。
float CenterTextY(ImFont* font, float fontSize, float centerY) {
    (void)font;
    return centerY - fontSize * 0.5f;
}

float CenterTextX(float minX, float maxX, float textWidth) {
    return minX + (maxX - minX - textWidth) * 0.5f;
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

ImU32 GlassColor() { return theme::ToImU32((theme::CurrentDerived().glass)); }

ImU32 ToneColor(theme::Tone tone) {
    const auto& c = theme::Current();
    switch (tone) {
    case theme::Tone::Accent: return theme::ToImU32((c.accentPrimary));
    case theme::Tone::Info: return theme::ToImU32((c.accentInfo));
    case theme::Tone::Ok: return theme::ToImU32((c.statusOk));
    case theme::Tone::Warn: return theme::ToImU32((c.statusWarn));
    case theme::Tone::Danger: return theme::ToImU32((c.statusDanger));
    case theme::Tone::Busy: return theme::ToImU32((c.statusBusy));
    case theme::Tone::Idle: return theme::ToImU32((c.statusIdle));
    }
    return theme::ToImU32((c.statusIdle));
}

ImU32 ToneBackground(theme::Tone tone) {
    return theme::ToImU32(theme::CurrentDerived().tagBg[static_cast<std::size_t>(tone)]);
}

ImU32 ToneBorder(theme::Tone tone) {
    return theme::ToImU32(theme::CurrentDerived().tagBorder[static_cast<std::size_t>(tone)]);
}

} // namespace shine::kit
