#include "ui/imgui/pages/WorkspacePages.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace shine::pages {
namespace {

using namespace shine::kit;

constexpr float kGap = 16.0f;

float LabelWidth(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, 1e9f, 0.0f, text, text + std::strlen(text)).x;
}

} // namespace

// ================================================================ P5.4 分镜
void StoryboardPage::Draw(Rect area, ImDrawList* draw) {
    Rect right;
    Rect content = ViewHeader(area, draw, "clapper", "S012 · 转身", "分镜 · 12 / 48", &right);

    const float tagW = TagWidth("待出图", false, true);
    Tag(draw, RectAt(right.max.x - 320.0f, right.min.y, tagW, 20.0f), "待出图", theme::Tone::Idle,
        false, true);
    ButtonSpec regen;
    regen.variant = ButtonVariant::Secondary;
    const char* regenLabel = "V10 重生成";
    const float regenW = ButtonWidth(ButtonSize::Medium, 0.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, regenLabel));
    Button(draw, RectAt(right.max.x - regenW, right.min.y, regenW, 30.0f), regenLabel, regen, "sb-regen");
    ButtonSpec run;
    run.variant = ButtonVariant::Primary;
    const char* runLabel = "运行 V1-V8";
    const float runW = ButtonWidth(ButtonSize::Medium, 0.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, runLabel));
    Button(draw, RectAt(right.max.x - runW - 12.0f, right.min.y, runW, 30.0f), runLabel, run, "sb-run");

    // V1-V8 状态卡
    std::vector<StageNode> vs;
    for (int i = 1; i <= 8; ++i) {
        StageState state = StageState::Done;
        if (i == 5) {
            state = StageState::Running;
        } else if (i > 5) {
            state = StageState::Todo;
        }
        vs.push_back(StageNode{"V" + std::to_string(i), "", state});
    }
    StageFlow(draw, Rect{content.min.x, content.min.y, content.max.x, content.min.y + 30.0f}, vs);

    // .shots-wrap = minmax(0,1.2fr) / minmax(0,1fr) gap16
    const float detailTop = content.min.y + 46.0f;
    const float timelineTop = content.max.y - 118.0f;
    const float detailW = (content.width() - kGap) * 1.2f / 2.2f;
    const Rect detail{content.min.x, detailTop, content.min.x + detailW, timelineTop - kGap};
    const Rect continuity{detail.max.x + kGap, detailTop, content.max.x, timelineTop - kGap};

    Rect detailBody = Card(draw, detail, "镜头详情", "target", false, false);
    KeyValues(draw, detailBody,
              {{"动作", "转身"}, {"空间", "旧桥下"}, {"表演", "克制"}, {"机位", "中景"},
               {"光线", "夜雨"}, {"时长", "6.0s"}, {"情绪", "冷"}});
    const float ty = detailBody.min.y + 170.0f;
    const char* beat = "{\n  \"code\": \"S012\",\n  \"duration\": 6.0,\n  \"emotion\": \"cold\"\n}";
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(detailBody.min.x, ty), ColorTextMuted(), "BEAT",
                  "BEAT" + 4);
    DrawRoundRect(draw, ImVec2(detailBody.min.x, ty + 16.0f),
                  ImVec2(detailBody.max.x, ty + 104.0f), 6.0f, ColorFillMuted(), ColorLineNormal(),
                  1.0f);
    DrawTextClipped(draw, MonoAt(12.5f), 12.5f, ImVec2(detailBody.min.x + 10.0f, ty + 26.0f),
                    detailBody.width() - 20.0f, ColorTextSecondary(), beat, true);

    Rect continuityBody = Card(draw, continuity, "连续性 C1-C12", "check", false, false);
    float cy = continuityBody.min.y;
    for (int i = 0; i < 6; ++i) {
        StatusDot(draw, ImVec2(continuityBody.min.x + 4.0f, cy + 6.0f), theme::Tone::Ok, false);
        const std::string label = "C" + std::to_string(i + 1) + " 服装一致";
        draw->AddText(FontAt(12.5f), 12.5f, ImVec2(continuityBody.min.x + 16.0f, cy),
                      ColorTextSecondary(), label.data(), label.data() + label.size());
        cy += 22.0f;
    }

    // 故事板时间线：横向滚动 128px 卡
    const Rect timeline{content.min.x, timelineTop, content.max.x, content.max.y};
    DrawShadowed(draw, timeline.min, timeline.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
    float x = timeline.min.x + 10.0f;
    for (int i = 0; i < 8; ++i) {
        const Rect card{x, timeline.min.y + 10.0f, x + 96.0f, timeline.max.y - 10.0f};
        const bool on = (i == selectedShot_);
        DrawRoundRect(draw, card.min, card.max, 8.0f, ColorFillMuted(),
                      on ? ColorAccent() : ColorLineNormal(), on ? 1.5f : 1.0f);
        if (on) {
            DrawRoundRect(draw, card.min - ImVec2(2, 2), card.max + ImVec2(2, 2), 10.0f, 0,
                          ColorOf(theme::CurrentDerived().accentDim), 2.0f);
        }
        Art(draw, Rect{card.min.x + 6.0f, card.min.y + 6.0f, card.max.x - 6.0f, card.min.y + 66.0f},
            i, true);
        const std::string code = "S0" + std::to_string(10 + i);
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(card.min.x + 8.0f, card.min.y + 70.0f),
                      ColorAccent(), code.data(), code.data() + code.size());
        DrawRoundRect(draw, ImVec2(card.min.x + 6.0f, card.max.y - 10.0f),
                      ImVec2(card.min.x + 6.0f + (card.width() - 12.0f) * 0.6f, card.max.y - 6.0f),
                      2.0f, ColorAccent());
        if (Clicked(card, "sb-tl-" + std::to_string(i))) {
            selectedShot_ = i;
        }
        x += 104.0f;
    }
}

// ================================================================ P5.5 出图
// 满幅画布页（.canvas-page），不走 .vw 骨架。
void ImageFlowPage::Draw(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorFillMuted());
    // 点阵背景：radial-gradient(circle at 1px 1px, line-normal 1px, transparent 0) 0 0 / 22px 22px
    DrawDotGrid(draw, area.min, area.max, 22.0f, ColorLineNormal());

    // 浮动工具条（左上，玻璃 → --glass 实色，r10，pad 8/12）
    const float barW = 520.0f;
    const Rect bar{area.min.x + 16.0f, area.min.y + 16.0f, area.min.x + 16.0f + barW,
                   area.min.y + 56.0f};
    DrawShadowed(draw, bar.min, bar.max, 10.0f, GlassColor(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "image", ImVec2(bar.min.x + 12.0f, bar.center().y - 7.0f), 14.0f, ColorAccent());
    const char* barTitle = "出图流程 · 分镜图_v3";
    draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(bar.min.x + 32.0f, bar.center().y - 6.25f),
                  ColorText(), barTitle, barTitle + std::strlen(barTitle));
    draw->AddLine(ImVec2(bar.min.x + 196.0f, bar.min.y + 8.0f),
                  ImVec2(bar.min.x + 196.0f, bar.max.y - 8.0f), ColorLineNormal(), 1.0f);
    ButtonSpec smallPrimary;
    smallPrimary.variant = ButtonVariant::Primary;
    smallPrimary.size = ButtonSize::Small;
    const char* submit = "批量出图";
    const float submitW = ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, submit));
    Button(draw, RectAt(bar.max.x - submitW - 12.0f, bar.center().y - 12.0f, submitW, 24.0f), submit,
           smallPrimary, "if-submit");

    // 浮动面板（右 348px，内缩 16，r14）
    const float panelW = 348.0f;
    const float panelHeight = folded_ ? 48.0f : std::min(560.0f, area.height() - 32.0f);
    const Rect panel{area.max.x - panelW - 16.0f, area.min.y + 16.0f, area.max.x - 16.0f,
                     area.min.y + 16.0f + panelHeight};
    DrawShadowed(draw, panel.min, panel.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "link", ImVec2(panel.min.x + 16.0f, panel.min.y + 16.0f), 15.0f, ColorAccent());
    const float headTagW = TagWidth("运行中", false, true);
    Tag(draw, RectAt(panel.max.x - 60.0f - headTagW, panel.min.y + 13.0f, headTagW, 20.0f), "运行中",
        theme::Tone::Accent, false, true);
    if (IconButton(draw, RectAt(panel.max.x - 48.0f, panel.min.y + 12.0f, 24.0f, 24.0f), "chevdown",
                   false, false, "if-fold")) {
        folded_ = !folded_;
    }
    if (folded_) {
        return;
    }

    const std::vector<SegmentOption> tabs{
        {"0", "绑定"}, {"1", "批量出图"}, {"2", "图评审"}, {"3", "结果"}};
    const std::string value = std::to_string(panelTab_);
    const std::string_view picked =
        Segmented(draw,
                  Rect{panel.min.x + 16.0f, panel.min.y + 52.0f, panel.max.x - 16.0f, panel.min.y + 84.0f},
                  tabs, value, "if-tabs");
    if (!picked.empty()) {
        panelTab_ = std::atoi(std::string(picked).c_str());
    }

    const Rect body{panel.min.x + 16.0f, panel.min.y + 94.0f, panel.max.x - 16.0f, panel.max.y - 56.0f};
    if (panelTab_ == 0) {
        const char* from[] = {"S012 · 转身", "prompt_v3"};
        const char* to[] = {"Comfy / ksampler", "workflow.json"};
        float y = body.min.y;
        for (int i = 0; i < 2; ++i) {
            draw->AddText(FontAt(12.5f), 12.5f, ImVec2(body.min.x, y), ColorTextSecondary(), from[i],
                          from[i] + std::strlen(from[i]));
            draw->AddText(FontAt(12.5f), 12.5f, ImVec2(body.min.x + 180.0f, y), ColorTextMuted(), "→",
                          "→" + 3);
            draw->AddText(FontAt(12.5f), 12.5f, ImVec2(body.min.x + 210.0f, y), ColorText(), to[i],
                          to[i] + std::strlen(to[i]));
            y += 24.0f;
        }
        const char* warn = "缺少 KSampler.seed";
        DrawRoundRect(draw, ImVec2(body.min.x, y + 8.0f), ImVec2(body.max.x, y + 44.0f), 6.0f,
                      ColorOf(theme::CurrentDerived().dangerBg));
        draw->AddText(FontAt(12.0f), 12.0f, ImVec2(body.min.x + 10.0f, y + 18.0f),
                      ColorOf(theme::Current().statusDanger), warn, warn + std::strlen(warn));
    } else if (panelTab_ == 2) {
        Art(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 150.0f}, 6, true);
        float y = body.min.y + 162.0f;
        for (int i = 0; i < 5; ++i) {
            Checkbox(draw, Rect{body.min.x, y, body.max.x, y + 20.0f}, i < 3, "构图稳定",
                     "if-ck-" + std::to_string(i));
            y += 24.0f;
        }
    } else {
        Art(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 190.0f}, 9, true);
        KeyValues(draw, Rect{body.min.x, body.min.y + 200.0f, body.max.x, body.min.y + 300.0f},
                  {{"尺寸", "1024x576"}, {"步数", "28"}, {"种子", "1289471"}, {"耗时", "12.4s"}});
    }

    // 画布工具（左下竖排）
    const float toolX = area.min.x + 16.0f;
    float toolY = area.max.y - 16.0f - 3.0f * 32.0f;
    for (const char* icon : {"plus", "minus", "grid"}) {
        const kit::Rect tool = RectAt(toolX, toolY, 28.0f, 28.0f);
        DrawRoundRect(draw, tool.min, tool.max, 6.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
        DrawIconCentered(draw, icon, ImVec2(toolX + 14.0f, toolY + 14.0f), 14.0f, ColorTextSecondary());
        toolY += 32.0f;
    }
}

// ================================================================ P5.6 出片
void VideoFlowPage::Draw(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorFillMuted());
    DrawDotGrid(draw, area.min, area.max, 22.0f, ColorLineNormal());

    const Rect bar{area.min.x + 16.0f, area.min.y + 16.0f, area.min.x + 460.0f, area.min.y + 56.0f};
    DrawShadowed(draw, bar.min, bar.max, 10.0f, GlassColor(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "film", ImVec2(bar.min.x + 12.0f, bar.center().y - 7.0f), 14.0f, ColorAccent());
    const char* barTitle = "出片流程 · H3 视频";
    draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(bar.min.x + 32.0f, bar.center().y - 6.25f),
                  ColorText(), barTitle, barTitle + std::strlen(barTitle));

    const float panelW = 348.0f;
    const Rect panel{area.max.x - panelW - 16.0f, area.min.y + 16.0f, area.max.x - 16.0f,
                     area.min.y + 16.0f + std::min(520.0f, area.height() - 140.0f)};
    DrawShadowed(draw, panel.min, panel.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f);
    const float headTagW = TagWidth("出片中", false, true);
    Tag(draw, RectAt(panel.max.x - 16.0f - headTagW, panel.min.y + 13.0f, headTagW, 20.0f), "出片中",
        theme::Tone::Accent, false, true);

    const std::vector<SegmentOption> tabs{{"0", "首尾帧链"}, {"1", "视频任务"}, {"2", "成片"}};
    const std::string value = std::to_string(panelTab_);
    const std::string_view picked =
        Segmented(draw,
                  Rect{panel.min.x + 16.0f, panel.min.y + 48.0f, panel.max.x - 16.0f, panel.min.y + 80.0f},
                  tabs, value, "vf-tabs");
    if (!picked.empty()) {
        panelTab_ = std::atoi(std::string(picked).c_str());
    }
    const Rect body{panel.min.x + 16.0f, panel.min.y + 90.0f, panel.max.x - 16.0f, panel.max.y - 16.0f};
    if (panelTab_ == 0) {
        float y = body.min.y;
        for (int i = 0; i < 3; ++i) {
            const std::string label = "S0" + std::to_string(10 + i) + " -> S0" + std::to_string(11 + i);
            draw->AddText(FontAt(12.5f), 12.5f, ImVec2(body.min.x, y), ColorTextSecondary(),
                          label.data(), label.data() + label.size());
            const float tw = TagWidth(i == 1 ? "断链" : "已连接", false, false);
            Tag(draw, RectAt(body.max.x - tw, y - 3.0f, tw, 20.0f), i == 1 ? "断链" : "已连接",
                i == 1 ? theme::Tone::Warn : theme::Tone::Ok, false, false);
            y += 26.0f;
        }
    } else if (panelTab_ == 1) {
        float y = body.min.y;
        for (int i = 0; i < 4; ++i) {
            const std::string code = "S0" + std::to_string(10 + i);
            draw->AddText(MonoAt(12.0f), 12.0f, ImVec2(body.min.x, y), ColorTextSecondary(),
                          code.data(), code.data() + code.size());
            Progress(draw, Rect{body.min.x + 50.0f, y + 2.0f, body.max.x - 40.0f, y + 8.0f},
                     20.0f + 20.0f * static_cast<float>(i), i == 0, false);
            y += 26.0f;
        }
    } else {
        KeyValues(draw, body,
                  {{"分辨率", "1920x1080"}, {"帧率", "24fps"}, {"帧数", "112"}, {"缺失镜头", "S013"}});
    }

    // 底部胶片条：left16 / right384，6 × 118px
    const Rect strip{area.min.x + 16.0f, area.max.y - 92.0f, area.max.x - 384.0f, area.max.y - 16.0f};
    DrawShadowed(draw, strip.min, strip.max, 14.0f, GlassColor(), ColorLineNormal(), 1.0f);
    const char* stripTitle = "成片";
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(strip.min.x + 14.0f, strip.min.y + 12.0f),
                  ColorTextMuted(), stripTitle, stripTitle + std::strlen(stripTitle));
    float x = strip.min.x + 52.0f;
    for (int i = 0; i < 6; ++i) {
        const bool ready = i < 3;
        const Rect cell{x, strip.min.y + 8.0f, x + 118.0f, strip.max.y - 8.0f};
        DrawRoundRect(draw, cell.min, cell.max, 8.0f, ready ? ColorFillMuted() : 0, ColorLineNormal(),
                      1.0f);
        if (ready) {
            Art(draw, Rect{cell.min.x + 6.0f, cell.min.y + 6.0f, cell.min.x + 62.0f, cell.max.y - 6.0f},
                i, true);
            const std::string code = "S0" + std::to_string(10 + i);
            draw->AddText(MonoAt(10.5f), 10.5f, ImVec2(cell.min.x + 70.0f, cell.min.y + 12.0f),
                          ColorAccent(), code.data(), code.data() + code.size());
            const char* fps = "24fps";
            draw->AddText(FontAt(10.5f), 10.5f, ImVec2(cell.min.x + 70.0f, cell.min.y + 28.0f),
                          ColorTextMuted(), fps, fps + 5);
            StatusDot(draw, ImVec2(cell.min.x + 74.0f, cell.min.y + 44.0f), theme::Tone::Ok, false);
        } else {
            const char* pending = "待出片";
            draw->AddText(FontAt(11.5f), 11.5f, ImVec2(cell.min.x + 12.0f, cell.center().y - 5.75f),
                          ColorTextMuted(), pending, pending + std::strlen(pending));
        }
        x += 124.0f;
    }
}

// ================================================================ P4.11 项目中心
// 全屏，不套外壳。居中列 max-w 1080，底两层径向渐变 + 240px 高 ArtInk 带。
void DrawProjectHub(Rect area, ImDrawList* draw) {
    DrawVGradient(draw, area.min, area.max, 0.0f, ColorOf(theme::Current().bgVoid),
                  ColorOf(theme::CurrentDerived().accentDim));
    ArtInk(draw, Rect{area.min.x, area.max.y - 240.0f, area.max.x, area.max.y}, 3, 0.3f);

    const float columnW = std::min(1080.0f, area.width() - 64.0f);
    const Rect column{(area.width() - columnW) * 0.5f, 40.0f, (area.width() + columnW) * 0.5f,
                      area.max.y - 40.0f};

    const float float_ = ReduceMotion() ? 0.0f : (Pulse(5.0f) - 0.5f) * 8.0f;
    const Rect logo{column.min.x, column.min.y + float_, column.min.x + 52.0f,
                    column.min.y + 52.0f + float_};
    DrawRoundRect(draw, ImVec2(logo.min.x, logo.min.y - 4.0f), ImVec2(logo.max.x, logo.max.y + 4.0f),
                  18.0f, 0, ColorAccentGlow(), 8.0f);
    DrawDiagGradient(draw, logo.min, logo.max, 14.0f, ColorAccent(), ColorAccentHover());
    DrawIconCentered(draw, "play", logo.center(), 26.0f, ColorAccentFg());
    const char* brand = "ShineTV Studio";
    draw->AddText(FontBoldAt(26.0f), 26.0f, ImVec2(logo.max.x + 16.0f, column.min.y + 8.0f),
                  ColorText(), brand, brand + std::strlen(brand));
    const char* subtitle = "把小说变成画面";
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(logo.max.x + 16.0f, column.min.y + 40.0f),
                  ColorTextMuted(), subtitle, subtitle + std::strlen(subtitle));

    const float barY = column.min.y + 76.0f;
    const Rect search{column.min.x, barY, column.min.x + 280.0f, barY + 30.0f};
    DrawRoundRect(draw, search.min, search.max, 6.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "search", ImVec2(search.min.x + 10.0f, search.center().y - 7.0f), 14.0f,
             ColorTextMuted());
    const char* placeholder = "搜索项目";
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(search.min.x + 30.0f, search.center().y - 6.25f),
                  ColorTextMuted(), placeholder, placeholder + std::strlen(placeholder));
    const std::vector<SegmentOption> sortOptions{{"r", "最近打开"}, {"n", "名称"}};
    Segmented(draw, RectAt(search.max.x + 12.0f, barY, SegmentedWidth(sortOptions), 30.0f),
              sortOptions, "r", "hub-sort");

    ButtonSpec newProject;
    newProject.variant = ButtonVariant::Primary;
    newProject.icon = "plus";
    const char* newLabel = "新建项目";
    const float newW = ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, newLabel));
    Button(draw, RectAt(column.max.x - newW, barY, newW, 30.0f), newLabel, newProject, "hub-new");

    const Rect grid{column.min.x, barY + 46.0f, column.max.x, column.max.y - 40.0f};
    const int columns = AutoFillCols(grid.width(), 240.0f, 14.0f);
    const float cardW =
        (grid.width() - 14.0f * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    for (int i = 0; i < 6; ++i) {
        const int column = i % columns;
        const int row = i / columns;
        const Rect card{grid.min.x + (cardW + 14.0f) * static_cast<float>(column),
                        grid.min.y + (188.0f + 14.0f) * static_cast<float>(row), cardW, 188.0f};
        DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
        Art(draw, Rect{card.min.x + 8.0f, card.min.y + 8.0f, card.max.x - 8.0f, card.min.y + 128.0f},
            i * 3, true);
        const float tW = TagWidth("小说", false, false);
        Tag(draw, RectAt(card.min.x + 12.0f, card.min.y + 96.0f, tW, 20.0f), "小说",
            theme::Tone::Accent, false, false);
        const std::string name = "第 " + std::to_string(i + 1) + " 部作品";
        draw->AddText(FontBoldAt(14.5f), 14.5f, ImVec2(card.min.x + 12.0f, card.min.y + 134.0f),
                      ColorText(), name.data(), name.data() + name.size());
        const std::string meta = "12 章 · 48 镜头 · 3 小时前";
        draw->AddText(FontAt(11.5f), 11.5f, ImVec2(card.min.x + 12.0f, card.min.y + 156.0f),
                      ColorTextMuted(), meta.data(), meta.data() + meta.size());
    }
}

} // namespace shine::pages
