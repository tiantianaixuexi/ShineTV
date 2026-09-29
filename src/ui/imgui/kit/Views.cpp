#include "ui/imgui/kit/Views.h"

#include <algorithm>
#include <cmath>

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

ImU32 StageColor(StageState state) {
    switch (state) {
    case StageState::Running: return ColorAccent();
    case StageState::Done: return ColorOf(theme::Current().statusOk);
    case StageState::Failed: return ColorOf(theme::Current().statusDanger);
    case StageState::Skipped: return ColorTextMuted();
    case StageState::Todo: break;
    }
    return ColorTextSecondary();
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

// ---------------------------------------------------------------- 20. Steps
float StepsWidth(const std::vector<std::string>& steps) {
    ImFont* font = FontBoldAt(12.0f);
    float total = 0.0f;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        total += 20.0f + 6.0f +
                 font->CalcTextSizeA(12.0f, 1e9f, 0.0f, steps[i].data(),
                                     steps[i].data() + steps[i].size())
                         .x +
                 10.0f;
        if (i + 1 < steps.size()) {
            total += 24.0f;
        }
    }
    return total;
}

float Steps(ImDrawList* draw, Rect bounds, const std::vector<std::string>& steps, int current) {
    ImFont* font = FontBoldAt(12.0f);
    float x = bounds.min.x;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) {
            draw->AddLine(ImVec2(x, bounds.center().y), ImVec2(x + 24.0f, bounds.center().y),
                          ColorLineStrong(), 1.5f);
            x += 24.0f;
        }
        const bool on = static_cast<int>(i) == current;
        const bool done = static_cast<int>(i) < current;
        const Rect dot{x, bounds.center().y - 10.0f, x + 20.0f, bounds.center().y + 10.0f};
        if (on) {
            draw->AddCircleFilled(dot.center(), 10.0f, ColorAccent(), 20);
        } else {
            draw->AddCircle(dot.center(), 10.0f, done ? ColorAccent() : ColorLineStrong(), 20,
                            1.0f);
        }
        if (done) {
            DrawIconCentered(draw, "check", dot.center(), 10.0f, ColorAccent(), 2.2f);
        } else {
            const std::string index = std::to_string(i + 1);
            const float w = font->CalcTextSizeA(12.0f, 1e9f, 0.0f, index.data(),
                                                index.data() + index.size())
                                .x;
            draw->AddText(font, 12.0f, ImVec2(dot.center().x - 0.5f * w, dot.center().y - 6.0f),
                          on ? ColorAccentFg() : ColorTextSecondary(), index.data(),
                          index.data() + index.size());
        }
        draw->AddText(font, 12.0f, ImVec2(dot.max.x + 6.0f, bounds.center().y - 6.0f),
                      on ? ColorText() : ColorTextSecondary(), steps[i].data(),
                      steps[i].data() + steps[i].size());
        x = dot.max.x + 6.0f +
            font->CalcTextSizeA(12.0f, 1e9f, 0.0f, steps[i].data(),
                                steps[i].data() + steps[i].size())
                .x +
            10.0f;
    }
    return x - bounds.min.x;
}

// ---------------------------------------------------------------- 21. StageFlow
float StageFlowWidth(const std::vector<StageNode>& nodes) {
    ImFont* code = MonoAt(10.5f);
    float total = 0.0f;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        total += 22.0f +
                 code->CalcTextSizeA(10.5f, 1e9f, 0.0f, nodes[i].code.data(),
                                     nodes[i].code.data() + nodes[i].code.size())
                         .x +
                 (!nodes[i].label.empty()
                      ? 6.0f + FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, nodes[i].label.data(),
                                                              nodes[i].label.data() + nodes[i].label.size())
                                  .x
                      : 0.0f) +
                 22.0f;
        if (i + 1 < nodes.size()) {
            total += 18.0f;
        }
    }
    return total;
}

float StageFlow(ImDrawList* draw, Rect bounds, const std::vector<StageNode>& nodes) {
    ImFont* code = MonoAt(10.5f);
    ImFont* label = FontAt(11.5f);
    const float nodeH = 30.0f;
    float x = bounds.min.x;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const StageNode& node = nodes[i];
        const float codeW = code->CalcTextSizeA(10.5f, 1e9f, 0.0f, node.code.data(),
                                               node.code.data() + node.code.size())
                                .x;
        const float labelW =
            node.label.empty()
                ? 0.0f
                : 6.0f + label->CalcTextSizeA(11.5f, 1e9f, 0.0f, node.label.data(),
                                              node.label.data() + node.label.size())
                             .x;
        const float nodeW = 22.0f + codeW + labelW + 22.0f;
        const Rect pill{x, 0.5f * (bounds.min.y + bounds.max.y) - 0.5f * nodeH, x + nodeW,
                        0.5f * (bounds.min.y + bounds.max.y) + 0.5f * nodeH};

        const ImU32 tone = StageColor(node.state);
        const bool active = node.state == StageState::Running;
        const bool done = node.state == StageState::Done;
        if (active) {
            DrawRoundRect(draw, pill.min, pill.max, nodeH * 0.5f,
                          ColorOf(theme::CurrentDerived().stageRunBg), tone, 1.0f);
        } else if (node.state == StageState::Skipped) {
            DrawRoundRect(draw, pill.min, pill.max, nodeH * 0.5f, WithAlpha(ColorFillMuted(), 0.6f),
                          WithAlpha(ColorLineNormal(), 0.6f), 1.0f);
        } else if (done) {
            DrawRoundRect(draw, pill.min, pill.max, nodeH * 0.5f,
                          WithAlpha(ColorOf(theme::Current().statusOk), 0.14f), tone, 1.0f);
        } else {
            DrawRoundRect(draw, pill.min, pill.max, nodeH * 0.5f, ColorFillMuted(),
                          ColorLineNormal(), 1.0f);
        }

        const float cy = pill.center().y;
        if (done) {
            DrawIconCentered(draw, "check", ImVec2(pill.min.x + 11.0f, cy), 10.0f, tone, 2.2f);
        } else if (active) {
            // running 时节点左侧转圈
            draw->PathArcTo(ImVec2(pill.min.x + 11.0f, cy), 4.0f, Now() * 4.0f,
                            Now() * 4.0f + 4.4f, 8);
            draw->PathStroke(tone, 0, 1.6f);
            draw->PathClear();
        } else if (node.state == StageState::Failed) {
            DrawIconCentered(draw, "x", ImVec2(pill.min.x + 11.0f, cy), 9.0f, tone, 2.0f);
        } else if (node.state == StageState::Skipped) {
            draw->AddLine(ImVec2(pill.min.x + 7.0f, cy), ImVec2(pill.min.x + 15.0f, cy),
                          WithAlpha(tone, 0.7f), 1.4f);
        }

        draw->AddText(code, 10.5f, ImVec2(pill.min.x + 22.0f, cy - 5.25f), tone, node.code.data(),
                      node.code.data() + node.code.size());
        if (!node.label.empty()) {
            draw->AddText(label, 11.5f, ImVec2(pill.min.x + 22.0f + codeW + 6.0f, cy - 5.75f),
                          ColorTextSecondary(), node.label.data(),
                          node.label.data() + node.label.size());
        }
        x = pill.max.x;
        if (i + 1 < nodes.size()) {
            // 连接线 18×1.5：done 时 accent 填充
            const bool linked = node.state == StageState::Done;
            draw->AddLine(ImVec2(x, cy), ImVec2(x + 18.0f, cy),
                          linked ? ColorAccent() : ColorLineStrong(), 1.5f);
            x += 18.0f;
        }
    }
    return x - bounds.min.x;
}

// ---------------------------------------------------------------- 22. StageList
void StageList(ImDrawList* draw, Rect bounds, const std::vector<StageNode>& nodes,
               std::string_view artifactColumn) {
    ImFont* code = MonoAt(11.5f);
    ImFont* text = FontAt(12.5f);
    const float rowH = 34.0f;
    const float codeW = 44.0f;
    const float stateW = 88.0f;
    const float artW = artifactColumn.empty() ? 0.0f : 110.0f;
    const float labelW = bounds.width() - codeW - stateW - artW - 20.0f - 30.0f;

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const StageNode& node = nodes[i];
        const float y = bounds.min.y + static_cast<float>(i) * rowH;
        const Rect row{bounds.min.x, y, bounds.max.x, y + rowH - 4.0f};
        DrawRoundRect(draw, row.min, row.max, 6.0f, i % 2 == 0 ? ColorFillMuted() : 0);

        const ImU32 tone = StageColor(node.state);
        DrawTextClipped(draw, code, 11.5f, ImVec2(row.min.x + 10.0f, row.center().y - 5.75f),
                        codeW, tone, node.code);
        DrawTextClipped(draw, text, 12.5f, ImVec2(row.min.x + 10.0f + codeW, row.center().y - 6.25f),
                        labelW, ColorText(), node.label.empty() ? "—" : node.label);

        // 状态 Tag
        const char* label = "待执行";
        theme::Tone tone_kind = theme::Tone::Idle;
        switch (node.state) {
        case StageState::Running: label = "运行中"; tone_kind = theme::Tone::Accent; break;
        case StageState::Done: label = "完成"; tone_kind = theme::Tone::Ok; break;
        case StageState::Failed: label = "失败"; tone_kind = theme::Tone::Danger; break;
        case StageState::Skipped: label = "跳过"; tone_kind = theme::Tone::Idle; break;
        case StageState::Todo: break;
        }
        const float tagW = TagWidth(label, false, true);
        Tag(draw, RectAt(row.max.x - stateW - 10.0f - tagW, row.center().y - 10.0f, tagW, 20.0f),
            label, tone_kind, false, true);

        if (artW > 0.0f) {
            DrawTextClipped(draw, text, 11.5f, ImVec2(row.max.x - artW, row.center().y - 5.75f),
                            artW - 10.0f, ColorTextMuted(), artifactColumn);
        }
    }
}

} // namespace shine::kit
