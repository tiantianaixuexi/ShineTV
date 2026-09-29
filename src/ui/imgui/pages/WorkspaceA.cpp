#include "ui/imgui/pages/WorkspacePages.h"

#include "pipeline/Ledger.h"
#include "pipeline/StageMachine.h"
#include "pipeline/StopPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace shine::pages {
namespace {

using namespace shine::kit;

constexpr float kGap = 16.0f;

// KPI 卡：右上角 90px accent 圆模糊 2px 溢出；数值 24px/800 等宽数字
void KpiCard(ImDrawList* draw, Rect bounds, std::string_view label, std::string_view value,
             std::string_view unit, std::string_view footnote, int tone) {
    DrawShadowed(draw, bounds.min, bounds.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
    // 右上角 90px accent 柔光。设计稿是 filter:blur(2px) 的模糊圆，ImGui draw list
    // 没有模糊可用 —— 用同心圆叠出径向衰减。
    //
    // 关键是**每层 alpha 相同**而不是每层颜色相同：圆环面积等分，中心累计到峰值、
    // 最外圈只有峰值的 1/layers，边界那一跳就只有 1/layers，肉眼看不出硬边。
    // 均匀叠加 4 层实心圆会读成「贴了张色斑」而不是「有光」。
    const float blobR = 45.0f; // 90px 直径
    const ImVec2 blob(bounds.max.x - 26.0f, bounds.min.y + 26.0f);
    const ImU32 toneColor = ToneColor(static_cast<theme::Tone>(tone));
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

float LabelWidth(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, 1e9f, 0.0f, text, text + std::strlen(text)).x;
}

} // namespace

Rect ViewHeader(Rect area, ImDrawList* draw, const char* icon, const char* title,
                const char* subtitle, Rect* rightOut) {
    DrawIcon(draw, icon, ImVec2(area.min.x, area.min.y), 20.0f, ColorAccent());
    draw->AddText(FontBoldAt(18.0f), 18.0f, ImVec2(area.min.x + 28.0f, area.min.y - 2.0f),
                  ColorText(), title, title + std::strlen(title));
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(area.min.x + 28.0f, area.min.y + 22.0f),
                  ColorTextMuted(), subtitle, subtitle + std::strlen(subtitle));
    if (rightOut != nullptr) {
        *rightOut = Rect{area.max.x - 320.0f, area.min.y, area.max.x, area.min.y + 30.0f};
    }
    return Rect{area.min.x, area.min.y + 48.0f, area.max.x, area.max.y};
}

// ================================================================ P5.1 总控
// 纯数据页：先做它验证数据通路（pipeline::{StageMachine, Budget, StopPolicy, Ledger}）。
// 阶段表直接来自 pipeline::AllStages()，不是前端硬编码的假数据。
void DrawOverview(Rect area, ImDrawList* draw) {
    Rect right;
    Rect content = ViewHeader(area, draw, "gauge", "全流程总控台", "pipeline · T1-T17", &right);

    // 头右侧：运行下一阶段(secondary) + 一键全流程(primary)
    const char* nextLabel = "运行下一阶段";
    float bw = ButtonWidth(ButtonSize::Medium, 0.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, nextLabel));
    ButtonSpec secondary;
    secondary.variant = ButtonVariant::Secondary;
    Button(draw, RectAt(right.max.x - bw - 120.0f, right.min.y, bw, 30.0f), nextLabel, secondary,
           "ov-next");
    ButtonSpec primary;
    primary.variant = ButtonVariant::Primary;
    primary.icon = "play";
    const char* allLabel = "一键全流程";
    bw = ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, allLabel));
    Button(draw, RectAt(right.max.x - bw, right.min.y, bw, 30.0f), allLabel, primary, "ov-all");

    // 1) 运行状态卡 + StageFlow
    const std::vector<pipeline::StageDefinition>& stages = pipeline::AllStages();
    const Rect flowCard{content.min.x, content.min.y, content.max.x, content.min.y + 122.0f};
    Rect flowBody = Card(draw, flowCard, "运行状态", "zap", false, false);
    const char* idle = "就绪 · 未开始";
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(flowBody.min.x, flowBody.min.y), ColorTextSecondary(),
                  idle, idle + std::strlen(idle));
    const char* pct = "0%";
    draw->AddText(FontBoldAt(18.0f), 18.0f, ImVec2(flowBody.max.x - 40.0f, flowBody.min.y - 2.0f),
                  ColorText(), pct, pct + 2);
    Progress(draw, Rect{flowBody.min.x, flowBody.min.y + 22.0f, flowBody.max.x, flowBody.min.y + 28.0f},
             0.0f, false, false);

    std::vector<StageNode> nodes;
    nodes.reserve(stages.size());
    for (const auto& stage : stages) {
        nodes.push_back(StageNode{stage.code, stage.name, StageState::Todo});
    }
    if (!nodes.empty()) {
        StageFlow(draw,
                  Rect{flowBody.min.x, flowBody.max.y - 34.0f, flowBody.min.x + 1400.0f, flowBody.max.y},
                  nodes);
    }

    // 2) KPI 行：repeat(auto-fit, minmax(210px,1fr)) gap 14
    const float kpiTop = flowCard.max.y + kGap;
    const int kpiCols = AutoGridCols(content.width(), 210.0f, 14.0f);
    const float kpiW =
        (content.width() - 14.0f * static_cast<float>(kpiCols - 1)) / static_cast<float>(kpiCols);
    struct Kpi {
        const char* label;
        const char* value;
        const char* unit;
        const char* footnote;
        int tone;
    };
    const Kpi kpis[] = {
        {"总预算", "12480", "元", "本轮已用 3120", 0},  {"阶段进度", "3/17", "", "T1-T2 已完成", 1},
        {"生成图", "128", "张", "较上轮 +12", 2},      {"评审未过", "7", "项", "阈值 75 分", 3},
        {"平均分", "82.4", "", "较上轮 -1.8", 4},
    };
    for (std::size_t i = 0; i < std::size(kpis); ++i) {
        const int column = static_cast<int>(i) % kpiCols;
        const int row = static_cast<int>(i) / kpiCols;
        KpiCard(draw,
                RectAt(content.min.x + (kpiW + 14.0f) * static_cast<float>(column),
                       kpiTop + (96.0f + 14.0f) * static_cast<float>(row), kpiW, 96.0f),
                kpis[i].label, kpis[i].value, kpis[i].unit, kpis[i].footnote, kpis[i].tone);
    }

    // 3) .grid-3-1（1fr / 300px gap16）
    // ⚠️ gridTop 必须按 KPI 的**实际行数**推，不能当只有一行：auto-fit 排 4 列时
    // 5 个 KPI 会换到第二行，按一行算会让下面的甘特卡盖住第二行卡片（文字直接压在一起）。
    const int kpiRows =
        (static_cast<int>(std::size(kpis)) + kpiCols - 1) / std::max(1, kpiCols);
    const float gridTop = kpiTop + (96.0f + 14.0f) * static_cast<float>(kpiRows) + kGap;
    const float rightW = 300.0f;
    const Rect left{content.min.x, gridTop, content.max.x - rightW - kGap, content.max.y};
    const Rect rightCol{left.max.x + kGap, gridTop, content.max.x, content.max.y};

    const Rect gantt{left.min.x, left.min.y, left.max.x, left.min.y + 190.0f};
    Rect ganttBody = Card(draw, gantt, "阶段甘特", "clock", false, false);
    const float rowLabelW = 88.0f;
    const int columns = 8;
    const float cellW = std::max(64.0f, (ganttBody.width() - rowLabelW) / static_cast<float>(columns));
    for (int c = 0; c < columns; ++c) {
        const std::string label = "W" + std::to_string(c + 1);
        draw->AddText(FontBoldAt(10.5f), 10.5f,
                      ImVec2(ganttBody.min.x + rowLabelW + cellW * c + 8.0f, ganttBody.min.y),
                      ColorTextMuted(), label.data(), label.data() + label.size());
    }
    float gy = ganttBody.min.y + 20.0f;
    for (std::size_t i = 0; i < stages.size() && i < 8; ++i) {
        draw->AddText(MonoAt(10.5f), 10.5f, ImVec2(ganttBody.min.x, gy + 5.0f), ColorTextSecondary(),
                      stages[i].code.data(), stages[i].code.data() + stages[i].code.size());
        const int start = static_cast<int>((i * 3) % columns);
        for (int c = 0; c < columns; ++c) {
            const bool inSpan = (c >= start && c < start + 2);
            DrawRoundRect(draw, ImVec2(ganttBody.min.x + rowLabelW + cellW * c + 4.0f, gy),
                          ImVec2(ganttBody.min.x + rowLabelW + cellW * (c + 1) - 4.0f, gy + 22.0f),
                          4.0f,
                          inSpan ? ColorOf(theme::CurrentDerived().ganttCell[1]) : ColorFillMuted());
        }
        gy += 22.0f;
    }

    const Rect ledger{left.min.x, gantt.max.y + kGap, left.max.x, left.max.y};
    Rect ledgerBody = Card(draw, ledger, "产物账本", "list", false, false);
    float lx = ledgerBody.min.x;
    for (const char* h : {"阶段", "产物", "哈希", "降级"}) {
        draw->AddText(FontBoldAt(10.5f), 10.5f, ImVec2(lx, ledgerBody.min.y), ColorTextMuted(), h,
                      h + std::strlen(h));
        lx += 120.0f;
    }
    float ly = ledgerBody.min.y + 18.0f;
    for (std::size_t i = 0; i < 3 && i < stages.size(); ++i) {
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(ledgerBody.min.x, ly), ColorTextSecondary(),
                      stages[i].code.data(), stages[i].code.data() + stages[i].code.size());
        DrawTextClipped(draw, FontAt(12.5f), 12.5f, ImVec2(ledgerBody.min.x + 120.0f, ly), 110.0f,
                        ColorText(), stages[i].chain);
        const std::string hash = stages[i].code + "-a91f2c";
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(ledgerBody.min.x + 240.0f, ly), ColorTextMuted(),
                      hash.data(), hash.data() + hash.size());
        ly += 22.0f;
    }

    const Rect stop{rightCol.min.x, rightCol.min.y, rightCol.max.x, rightCol.min.y + 260.0f};
    Rect stopBody = Card(draw, stop, "停止条件 S1-S12", "alert", false, false);
    float sy = stopBody.min.y;
    for (int i = 0; i < 8; ++i) {
        const std::string label = "S" + std::to_string(i + 1) + " 预算上限";
        StatusDot(draw, ImVec2(stopBody.min.x + 4.0f, sy + 6.0f), theme::Tone::Ok, false);
        draw->AddText(FontAt(12.5f), 12.5f, ImVec2(stopBody.min.x + 16.0f, sy), ColorTextSecondary(),
                      label.data(), label.data() + label.size());
        sy += 20.0f;
    }
    const Rect info{rightCol.min.x, stop.max.y + kGap, rightCol.max.x, rightCol.max.y};
    Rect infoBody = Card(draw, info, "运行信息", "info", false, false);
    KeyValues(draw, infoBody, {{"模式", "半自动"}, {"根目录", "projects/demo"}, {"当前阶段", "T3"}});
}

// ================================================================ P5.2 小说
void NovelPage::Draw(Rect area, ImDrawList* draw) {
    const float inspectorW = 280.0f;
    const Rect center{area.min.x, area.min.y, area.max.x - inspectorW, area.max.y};
    const Rect inspector{center.max.x, area.min.y, area.max.x, area.max.y};

    // 8 个模式标签（min-h 44，横向滚动；选中 = accent + 2px accent 下边框）
    const char* modes[] = {"章节", "设定", "初始化", "流水线", "评审", "模型", "状态", "自动"};
    float tx = area.min.x;
    const float tabY = area.min.y;
    ImFont* tabFont = FontBoldAt(12.5f);
    for (int i = 0; i < 8; ++i) {
        const float w = LabelWidth(tabFont, 12.5f, modes[i]) + 32.0f;
        const Rect tab{tx, tabY, tx + w, tabY + 44.0f};
        if (i == mode_) {
            DrawRoundRect(draw, tab.min, tab.max, 6.0f, ColorFillSelected());
            draw->AddLine(ImVec2(tab.min.x, tab.max.y - 1.0f), ImVec2(tab.max.x, tab.max.y - 1.0f),
                          ColorAccent(), 2.0f);
        }
        draw->AddText(tabFont, 12.5f, ImVec2(tab.min.x + 16.0f, tab.min.y + 14.0f),
                      i == mode_ ? ColorAccent() : ColorTextSecondary(), modes[i],
                      modes[i] + std::strlen(modes[i]));
        if (Clicked(tab, "novel-mode-" + std::to_string(i))) {
            mode_ = i;
        }
        tx += w + 4.0f;
    }

    const Rect body{center.min.x, tabY + 52.0f, center.max.x, center.max.y};
    if (mode_ == 0) {
        const char* title = "第 3 章 · 雨夜";
        draw->AddText(FontBoldAt(20.0f), 20.0f, ImVec2(body.min.x, body.min.y), ColorText(), title,
                      title + std::strlen(title));
        // .chap-summary：fill-muted + 3px accent 左边框 + pad 10/14
        const char* summary = "摘要：沈砚在旧桥下发现一枚刻着名字的铜钱，线索指向十年前的失踪案。";
        const Rect sum{body.min.x, body.min.y + 30.0f, body.min.x + 720.0f, body.min.y + 78.0f};
        DrawRoundRect(draw, sum.min, sum.max, 0.0f, ColorFillMuted());
        DrawRoundRect(draw, ImVec2(sum.min.x, sum.min.y), ImVec2(sum.min.x + 3.0f, sum.max.y), 1.5f,
                      ColorAccent());
        DrawTextClipped(draw, FontAt(12.5f), 12.5f, ImVec2(sum.min.x + 14.0f, sum.min.y + 10.0f),
                        sum.width() - 28.0f, ColorTextSecondary(), summary, true);
        // .draft：14px / 行高 1.9 / max-w 720
        const char* draft =
            "雨下了三天。\n\n沈砚蹲在桥墩下，把那枚铜钱在掌心翻了个面。刻痕很浅，像是被人反复摩过——"
            "「沈砚」两个字，如今成了另一个人名。\n\n他决定不问。";
        DrawTextClipped(draw, FontAt(14.0f), 14.0f, ImVec2(body.min.x, body.min.y + 96.0f), 720.0f,
                        ColorText(), draft, true);
    } else if (mode_ == 3) {
        std::vector<StageNode> nodes;
        for (const auto& stage : pipeline::AllStages()) {
            nodes.push_back(StageNode{stage.code, stage.name, StageState::Todo});
        }
        if (nodes.size() > 2) {
            nodes[0].state = StageState::Done;
            nodes[1].state = StageState::Done;
            nodes[2].state = StageState::Running;
        }
        StageFlow(draw, Rect{body.min.x, body.min.y, body.min.x + 1400.0f, body.min.y + 30.0f}, nodes);
        StageList(draw, Rect{body.min.x, body.min.y + 48.0f, body.max.x, body.min.y + 700.0f}, nodes,
                  "artifacts/");
    } else {
        Empty(draw, body, "sparkles", "尚未生成", "在「流水线」页运行 T1-T17 后，这里会显示结果。");
    }

    DrawRoundRect(draw, inspector.min, inspector.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(inspector.min.x + 0.5f, inspector.min.y),
                  ImVec2(inspector.min.x + 0.5f, inspector.max.y), ColorLineSubtle(), 1.0f);
    KeyValues(draw,
              Rect{inspector.min.x + 16.0f, tabY + 60.0f, inspector.max.x - 16.0f, tabY + 160.0f},
              {{"章节", "第 3 章"}, {"字数", "2180"}, {"状态", "草稿"}, {"更新", "刚刚"}});
    Art(draw, Rect{inspector.min.x + 16.0f, tabY + 180.0f, inspector.max.x - 16.0f, tabY + 290.0f},
        4, true);
}

// ================================================================ P5.3 资产
// 第一处真正用 src/gpu 的页面：缩略图走 gpu::Textures() + gpu::TextureCache()，
// ImGui 侧只差最后一步 ImGui::Image(srv)。
void AssetsPage::Draw(Rect area, ImDrawList* draw) {
    const std::vector<SegmentOption> options{{"d", "详情"}, {"o", "总览"}};
    const std::string_view picked =
        Segmented(draw, RectAt(area.min.x, area.min.y, SegmentedWidth(options), 32.0f), options,
                  overview_ ? "o" : "d", "assets-seg");
    overview_ = (picked == "o");

    const Rect body{area.min.x, area.min.y + 44.0f, area.max.x, area.max.y};
    if (overview_) {
        const int columns = AutoGridCols(body.width(), 210.0f, 14.0f);
        const float cardW =
            (body.width() - 14.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        for (int i = 0; i < 12; ++i) {
            const int column = i % columns;
            const int row = i / columns;
            const Rect card{body.min.x + (cardW + 14.0f) * static_cast<float>(column),
                            body.min.y + (192.0f + 14.0f) * static_cast<float>(row), cardW, 192.0f};
            const bool on = (i == selected_);
            DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(),
                         on ? ColorAccent() : ColorLineSubtle(), 1.0f);
            Art(draw, Rect{card.min.x + 8.0f, card.min.y + 8.0f, card.max.x - 8.0f, card.min.y + 158.0f},
                i, true);
            const std::string name = "资产 " + std::to_string(i + 1);
            draw->AddText(FontBoldAt(13.0f), 13.0f, ImVec2(card.min.x + 12.0f, card.min.y + 164.0f),
                          ColorText(), name.data(), name.data() + name.size());
            if (Clicked(card, "asset-card-" + std::to_string(i))) {
                selected_ = i;
            }
        }
        return;
    }

    // 详情：三个 .vsec 块，发丝分隔线，无卡片外框
    float y = body.min.y;
    DrawIcon(draw, "masks", ImVec2(body.min.x, y), 16.0f, ColorAccent());
    const char* entityName = "沈砚";
    draw->AddText(FontBoldAt(15.0f), 15.0f, ImVec2(body.min.x + 24.0f, y - 1.0f), ColorText(),
                  entityName, entityName + std::strlen(entityName));
    const float tagW = TagWidth("已确认", false, true);
    Tag(draw, RectAt(body.min.x + 76.0f, y - 1.0f, tagW, 20.0f), "已确认", theme::Tone::Ok, false, true);
    y += 28.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    Art(draw, Rect{body.min.x, y, body.min.x + 220.0f, y + 138.0f}, 1, true);
    KeyValues(draw, Rect{body.min.x + 240.0f, y, body.min.x + 560.0f, y + 120.0f},
              {{"类别", "角色"}, {"别名", "老沈"}, {"出处", "第 1 章"}, {"降级策略", "保留上一版"}});
    y += 152.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    // 一致性对比：.compare 16:10 max-w 640，2px accent 滑块 + 22px 旋钮
    const float compareW = std::min(640.0f, body.width() * 0.6f);
    const Rect compare{body.min.x, y, body.min.x + compareW, y + compareW * 10.0f / 16.0f};
    Art(draw, compare, 1, true);
    Art(draw, compare, 8, false);
    const float split = compare.min.x + compare.width() * 0.5f;
    draw->AddLine(ImVec2(split, compare.min.y), ImVec2(split, compare.max.y), ColorAccent(), 2.0f);
    draw->AddCircleFilled(ImVec2(split, compare.center().y), 11.0f, ColorAccent(), 20);
    DrawIconCentered(draw, "compare", ImVec2(split, compare.center().y), 14.0f, ColorAccentFg());
    KeyValues(draw, Rect{body.min.x + compareW + 24.0f, y, body.max.x, y + 120.0f},
              {{"基线", "v2"}, {"当前", "v3"}, {"差异", "0.2418"}, {"说明", "构图偏移"}});
    y += compare.height() + 18.0f;
    draw->AddLine(ImVec2(body.min.x, y), ImVec2(body.max.x, y), ColorLineSubtle(), 1.0f);
    y += 14.0f;

    // 关联时间线：92px 高轴（2px 线 + 6 个章节刻度）+ 3 个可点 pin
    const Rect axis{body.min.x, y + 46.0f, body.max.x, y + 50.0f};
    draw->AddLine(ImVec2(axis.min.x, axis.center().y), ImVec2(axis.max.x, axis.center().y),
                  ColorLineStrong(), 2.0f);
    for (int i = 0; i < 6; ++i) {
        const float x = axis.min.x + axis.width() * static_cast<float>(i) / 5.0f;
        draw->AddLine(ImVec2(x, axis.min.y - 4.0f), ImVec2(x, axis.max.y + 4.0f), ColorLineNormal(),
                      1.0f);
    }
    for (int i = 0; i < 3; ++i) {
        const float x = axis.min.x + axis.width() * (0.2f + 0.3f * static_cast<float>(i));
        if (i == 1) {
            // .tl .ev.hot .pin 的 `0 0 10px var(--accent-glow)`（views.css:819）：
            // 用**accent-glow 自带的 alpha**（各主题 20%~30%）当外扩光晕，不再另乘系数。
            draw->AddCircleFilled(ImVec2(x, axis.center().y), 9.0f, ColorAccentGlow(), 16);
        }
        draw->AddCircleFilled(ImVec2(x, axis.center().y), 5.0f, ColorAccent(), 14);
        draw->AddCircle(ImVec2(x, axis.center().y), 6.0f, ColorVoid(), 14, 3.0f);
        const std::string label = "第 " + std::to_string(i + 2) + " 章";
        draw->AddText(FontAt(11.5f), 11.5f, ImVec2(x - 20.0f, axis.max.y + 10.0f), ColorTextMuted(),
                      label.data(), label.data() + label.size());
    }
    y += 70.0f;

    // 绑定镜头 chip 云 + 4 张 52×36 参考图
    float cx = body.min.x;
    for (int i = 0; i < 5; ++i) {
        const std::string label = "S0" + std::to_string(10 + i);
        const float w =
            LabelWidth(FontBoldAt(12.0f), 12.0f, label.c_str()) + 26.0f;
        const kit::Rect pill = RectAt(cx, y, w, 26.0f);
        DrawRoundRect(draw, pill.min, pill.max, 13.0f,
                      ColorOf(theme::CurrentDerived().accentDim));
        draw->AddText(FontBoldAt(12.0f), 12.0f, ImVec2(cx + 13.0f, y + 6.0f), ColorAccent(),
                      label.data(), label.data() + label.size());
        cx += w + 6.0f;
    }
    for (int i = 0; i < 4; ++i) {
        Art(draw, RectAt(cx + 58.0f * static_cast<float>(i), y, 52.0f, 36.0f), i + 2, true);
    }
}

} // namespace shine::pages
