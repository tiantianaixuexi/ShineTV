#include "ui/imgui/kit/View_Stage.h"

namespace shine::kit {
namespace {

// 5 态 → 界面色。StageFlow 的 pill 描边/标记与 StageList 的状态色共用它。
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
