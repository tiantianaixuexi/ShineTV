#include "ui/imgui/kit/Views.h"

#include <algorithm>
#include <vector>

namespace shine::kit {
namespace {

// UI.jsx:184 的 12 组调色板（深 / 中 / 亮）。这是**设计稿的常量**，
// 不是主题色 —— 所以在 theme-ok 标记之外单列，页面不引用它们。
struct ArtPalette {
    const char* dark;
    const char* mid;
    const char* light;
};
const ArtPalette kArtPalettes[12] = {
    {"#1b3a4a", "#35d0b4", "#f2a65a"}, {"#3a2233", "#e86a8b", "#f2c94c"},
    {"#1d2b53", "#6fa8ff", "#a78bfa"}, {"#26313d", "#8fbbff", "#eaf0f7"},
    {"#41301f", "#f2a65a", "#f07178"}, {"#1f3d33", "#6bcb8a", "#e8c56a"},
    {"#2d1f3f", "#a78bfa", "#6fa8ff"}, {"#402a2a", "#f07178", "#f2a65a"},
    {"#153a3f", "#3ecfb2", "#8fbbff"}, {"#3b2f1a", "#e8c56a", "#6bcb8a"},
    {"#22293a", "#a7b3c4", "#f07178"}, {"#31203d", "#e86a8b", "#6fa8ff"},
}; // theme-ok

ImU32 Hex(std::string_view text) {
    std::uint32_t value = 0;
    for (char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<std::uint32_t>(c - 'a' + 10);
        }
    }
    return ImU32(value | 0xFF000000u);
}

// ImVec2 没有两参聚合初始化（它有构造函数），列表里必须显式包一层。
ImVec2 V(float x, float y) { return ImVec2(x, y); }

int AbsMod(int value, int modulus) {
    const int r = value % modulus;
    return r < 0 ? r + modulus : r;
}

// 24 网格里画一条二次贝塞尔山脊（三段 T 平滑）。
void Ridge(ImDrawList* draw, const std::vector<ImVec2>& points) {
    if (points.size() < 2) {
        return;
    }
    draw->PathLineTo(points.front());
    for (std::size_t i = 1; i < points.size(); ++i) {
        const ImVec2& p0 = points[i - 1];
        const ImVec2& p1 = points[i];
        const ImVec2 control(p1.x, p0.y);
        draw->PathBezierQuadraticCurveTo(control, p1, 4);
    }
}

} // namespace

// ---------------------------------------------------------------- 18. Art
void Art(ImDrawList* draw, Rect bounds, int seed, bool cover) {
    const ArtPalette& palette = kArtPalettes[AbsMod(seed, 12)];
    const ImU32 dark = Hex(palette.dark);
    const ImU32 mid = Hex(palette.mid);
    const ImU32 light = Hex(palette.light);

    // viewBox 160×100，preserveAspectRatio="xMidYMid slice" → cover：
    // 缩放系数取 max(w/160, h/100)，居中裁切。
    // ⚠️ 锚点必须按**左上角**推：内容层不做中心锚缩放，否则整幅画会左上平移
    //    w/2*(k-1) / h/2*(k-1) 只画出一角（本机在 QML 版踩过，见 P5.5）。
    const float k = cover ? std::max(bounds.width() / 160.0f, bounds.height() / 100.0f) : 1.0f;
    const float drawW = 160.0f * k;
    const float drawH = 100.0f * k;
    const float x0 = bounds.min.x + (bounds.width() - drawW) * 0.5f;
    const float y0 = bounds.min.y + (bounds.height() - drawH) * 0.5f;
    const auto X = [&](float u) { return x0 + u * k; };
    const auto Y = [&](float v) { return y0 + v * k; };

    // 竖直渐变天空：上 = dark，下 = mid 55% 透明
    DrawVGradient(draw, bounds.min, bounds.max, 0.0f, dark, WithAlpha(mid, 0.55f));

    const float sunX = 30.0f + static_cast<float>(AbsMod(seed * 37, 40));
    // 日轮 r13 + r20 光晕
    draw->AddCircleFilled(ImVec2(X(sunX), Y(34.0f)), 20.0f * k, WithAlpha(light, 0.25f), 24);
    draw->AddCircleFilled(ImVec2(X(sunX), Y(34.0f)), 13.0f * k, WithAlpha(light, 0.9f), 24);

    const int s0 = AbsMod(seed % 20, 20);
    const int s1 = AbsMod(seed % 15, 15);
    const int s2 = AbsMod(seed % 12, 12);
    const int s3 = AbsMod(seed % 18, 18);

    // 三层山形（JSX 的 T 平滑用二次贝塞尔近似）
    const std::vector<ImVec2> layer1{V(X(0), Y(78)), V(X(20.0f + s0), Y(58)), V(X(45.0f + s1), Y(74)), V(X(100), Y(70)), V(X(160), Y(76)), V(X(160), Y(100)), V(X(0), Y(100))};
    draw->PathClear();
    Ridge(draw, layer1);
    draw->PathFillConvex(WithAlpha(dark, 0.75f));

    const std::vector<ImVec2> layer2{V(X(0), Y(88)), V(X(35.0f - s3), Y(72)), V(X(70.0f + s2), Y(86)), V(X(160), Y(84)), V(X(160), Y(100)), V(X(0), Y(100))};
    draw->PathClear();
    Ridge(draw, layer2);
    draw->PathFillConvex(WithAlpha(mid, 0.3f));

    const std::vector<ImVec2> layer3{V(X(0), Y(94)), V(X(50), Y(86)), V(X(100), Y(92)), V(X(160), Y(90)), V(X(160), Y(100)), V(X(0), Y(100))};
    draw->PathClear();
    Ridge(draw, layer3);
    draw->PathFillConvex(WithAlpha(dark, 0.9f));
}

// ---------------------------------------------------------------- 19. ArtInk
void ArtInk(ImDrawList* draw, Rect bounds, int seed, float opacity) {
    // viewBox 800×260，preserveAspectRatio="xMidYMax slice"
    const float k = std::max(bounds.width() / 800.0f, bounds.height() / 260.0f);
    const float x0 = bounds.min.x + (bounds.width() - 800.0f * k) * 0.5f;
    const float y0 = bounds.max.y - 260.0f * k;
    const auto X = [&](float u) { return x0 + u * k; };
    const auto Y = [&](float v) { return y0 + v * k; };
    const ImU32 ink = ColorText();

    // 远两层带 stdDeviation=7 的高斯模糊：ImGui 无模糊，用 5 层递减 alpha 的
    // 同形偏移叠出软边（已知降级：只影响远山的边缘锐度）。
    const auto blurredRidge = [&](const std::vector<ImVec2>& points, float alpha) {
        for (int i = 4; i >= 0; --i) {
            const float spread = static_cast<float>(i) * 2.2f * k;
            const ImU32 shade = WithAlpha(ink, alpha * opacity / 5.0f);
            draw->PathClear();
            std::vector<ImVec2> shifted;
            shifted.reserve(points.size());
            for (const ImVec2& p : points) {
                shifted.push_back(ImVec2(p.x, p.y - spread));
            }
            Ridge(draw, shifted);
            draw->PathFillConvex(shade);
        }
    };

    blurredRidge({V(X(0), Y(208)), V(X(90), Y(120)), V(X(190), Y(176)), V(X(400), Y(158)), V(X(620), Y(178)), V(X(800), Y(150)), V(X(800), Y(260)), V(X(0), Y(260))},
                  0.08f);
    blurredRidge({V(X(0), Y(226)), V(X(130), Y(150)), V(X(250), Y(200)), V(X(520), Y(186)), V(X(800), Y(196)), V(X(800), Y(260)), V(X(0), Y(260))},
                  0.13f);

    const std::vector<ImVec2> mid{V(X(0), Y(240)), V(X(110), Y(178)), V(X(230), Y(222)), V(X(470), Y(210)), V(X(720), Y(226)), V(X(800), Y(214)), V(X(800), Y(260)), V(X(0), Y(260))};
    draw->PathClear();
    Ridge(draw, mid);
    draw->PathFillConvex(WithAlpha(ink, 0.22f * opacity));

    const std::vector<ImVec2> near{V(X(0), Y(254)), V(X(160), Y(216)), V(X(330), Y(244)), V(X(660), Y(238)), V(X(800), Y(246)), V(X(800), Y(260)), V(X(0), Y(260))};
    draw->PathClear();
    Ridge(draw, near);
    draw->PathFillConvex(WithAlpha(ink, 0.42f * opacity));

    // 飞白小舟
    const float boatX = 520.0f + static_cast<float>(AbsMod(seed, 40));
    draw->PathArcTo(ImVec2(X(boatX + 17.0f), Y(236.0f)), 17.0f * k * 0.9f, 0.0f, 3.14159265f, 12);
    draw->PathStroke(WithAlpha(ink, 0.5f * opacity), 0, 2.0f * k);
    draw->PathClear();
    draw->AddLine(ImVec2(X(boatX + 16.0f), Y(224.0f)), ImVec2(X(boatX + 19.0f), Y(215.0f)),
                  WithAlpha(ink, 0.4f * opacity), 1.4f * k);
    draw->AddLine(ImVec2(X(boatX + 19.0f), Y(215.0f)), ImVec2(X(boatX + 27.0f), Y(217.0f)),
                  WithAlpha(ink, 0.4f * opacity), 1.4f * k);

    // 26×26 印泥印章
    const float sealX = 700.0f + static_cast<float>(AbsMod(seed, 30));
    const Rect seal{V(X(sealX), Y(40.0f)), V(X(sealX) + 26.0f * k, Y(40.0f) + 26.0f * k)};
    DrawRoundRect(draw, seal.min, seal.max, 4.0f * k, WithAlpha(ColorAccent(), 0.9f * opacity));
    DrawRoundRect(draw, ImVec2(seal.min.x + 5.0f * k, seal.min.y + 5.0f * k),
                  ImVec2(seal.min.x + 21.0f * k, seal.min.y + 21.0f * k), 2.0f * k, 0,
                  WithAlpha(ColorAccentFg(), 0.9f * opacity), 1.6f * k);
    draw->AddLine(ImVec2(X(sealX) + 9.0f * k, Y(40.0f) + 13.0f * k),
                  ImVec2(X(sealX) + 17.0f * k, Y(40.0f) + 13.0f * k),
                  WithAlpha(ColorAccentFg(), 0.9f * opacity), 1.6f * k);
    draw->AddLine(ImVec2(X(sealX) + 13.0f * k, Y(40.0f) + 9.0f * k),
                  ImVec2(X(sealX) + 13.0f * k, Y(40.0f) + 17.0f * k),
                  WithAlpha(ColorAccentFg(), 0.9f * opacity), 1.6f * k);
}

} // namespace shine::kit
