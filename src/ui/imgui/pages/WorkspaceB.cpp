#include "ui/imgui/pages/WorkspacePages.h"

#include "core/Log.h"
#include "project/Project.h"
#include "project/ProjectIndex.h"
#include "project/ProjectTemplate.h"
#include "util/Encoding.h"
#include "util/Shell.h"
#include "util/Strings.h"

#include <objbase.h>  // 必须在 windows.h（util/Encoding.h 已经带进来）之后
#include <shlobj.h>   // SHBrowseForFolderW：向导第 2 步的「浏览…」

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::pages {
namespace {

using namespace shine::kit;

constexpr float kGap = 16.0f;

float LabelWidth(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, 1e9f, 0.0f, text, text + std::strlen(text)).x;
}

// 出图/出片的节点图。节点与连线照 webui mock（ImageFlow.jsx / VideoFlow.jsx
// 的 IMAGE_NODES / VIDEO_NODES），后续接 flow::GraphHost 的真数据时只换这里。
std::vector<FlowNode> MakeImageFlowNodes() {
    struct Def {
        int id;
        const char* icon;
        const char* title;
        const char* sub;
        FlowState state;
    };
    static const Def defs[] = {
        {1, "book", "章节", "第 3 章 雨夜", FlowState::Done},
        {2, "text", "提示词", "prompt_v3", FlowState::Done},
        {3, "masks", "角色参考", "沈砚 · 3 视图", FlowState::Done},
        {4, "image", "姿势骨架", "openpose_v2", FlowState::Done},
        {5, "wand", "KSampler", "seed 42 · 28 step", FlowState::Running},
        {6, "aperture", "VAE 解码", "latent → rgb", FlowState::Todo},
        {7, "eye", "一致性校验", "CLIP 0.918", FlowState::Todo},
        {8, "download", "落盘", "out/S012_v3.png", FlowState::Todo},
    };
    std::vector<FlowNode> nodes;
    for (const Def& def : defs) {
        nodes.push_back(FlowNode{def.id, def.icon, def.title, def.sub, def.state, 0.0f, 0.0f});
    }
    return nodes;
}

std::vector<FlowLink> MakeImageFlowLinks() {
    // 线性主干 1→2→3→4→5→6→7→8，外加一条 3→4 的参考分支
    std::vector<FlowLink> links;
    for (int i = 1; i < 8; ++i) {
        links.push_back(FlowLink{i, i + 1});
    }
    return links;
}

std::vector<FlowNode> MakeVideoFlowNodes() {
    struct Def {
        int id;
        const char* icon;
        const char* title;
        const char* sub;
        FlowState state;
    };
    static const Def defs[] = {
        {1, "film", "首帧", "S011 尾帧", FlowState::Done},
        {2, "film", "尾帧", "S012 首帧", FlowState::Done},
        {3, "wand", "H3 生成", "24fps · 112 帧", FlowState::Running},
        {4, "zap", "RIFE 补帧", "2× → 48fps", FlowState::Todo},
        {5, "encode", "编码", "h264 · 1080p", FlowState::Todo},
    };
    std::vector<FlowNode> nodes;
    for (const Def& def : defs) {
        nodes.push_back(FlowNode{def.id, def.icon, def.title, def.sub, def.state, 0.0f, 0.0f});
    }
    return nodes;
}

std::vector<FlowLink> MakeVideoFlowLinks() {
    return {FlowLink{1, 3}, FlowLink{2, 3}, FlowLink{3, 4}, FlowLink{4, 5}};
}

} // namespace

// 首次进入时建图并适应视图；之后由 FlowCanvas 维护坐标与缩放。
// fit 需要画布尺寸，而首帧 Draw 时才有 —— 所以用一个「待适配」标志，
// 在第一次拿到尺寸后 fit 一次（webui FlowCanvas.jsx:56-62 的 setTimeout(fit) 同理）。
namespace {
bool g_imageFlowNeedsFit = true;
bool g_videoFlowNeedsFit = true;
} // namespace

void ImageFlowPage::BuildGraph() {
    flowNodes_ = MakeImageFlowNodes();
    flowLinks_ = MakeImageFlowLinks();
    FlowLayoutNodes(flowNodes_, flowView_);
    flowSelected_ = 5; // 默认选中运行中的节点，对齐 webui 的初始 sel
    g_imageFlowNeedsFit = true;
}

void VideoFlowPage::BuildGraph() {
    flowNodes_ = MakeVideoFlowNodes();
    flowLinks_ = MakeVideoFlowLinks();
    FlowLayoutNodes(flowNodes_, flowView_);
    flowSelected_ = 3;
    g_videoFlowNeedsFit = true;
}

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

    // 浮动面板占右侧 348+16，画布在它左边。画布边界已经排除了面板，
    // 所以 FlowCanvas 的 fitInset 传 0 —— 再传 380 会重复扣一次宽度，
    // 把可用宽压到接近 0，缩放退化成看不清的一团。
    const float panelW = 348.0f;
    const float canvasRight = area.max.x - panelW - 32.0f;
    const Rect canvas{area.min, ImVec2(std::max(canvasRight, area.min.x + 200.0f), area.max.y)};
    if (flowNodes_.empty()) {
        BuildGraph();
    }
    if (g_imageFlowNeedsFit) {
        FlowFit(flowNodes_, canvas, 0.0f, flowView_);
        g_imageFlowNeedsFit = false;
    }
    FlowCanvas(draw, canvas, flowNodes_, flowLinks_, flowView_, flowSelected_, 0.0f);

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

    // 与出图页同构：右侧浮动面板 348+16，画布让出这块再 fit
    const float panelW = 348.0f;
    const float canvasRight = area.max.x - panelW - 32.0f;
    const Rect canvas{area.min, ImVec2(std::max(canvasRight, area.min.x + 200.0f), area.max.y)};
    if (flowNodes_.empty()) {
        BuildGraph();
    }
    if (g_videoFlowNeedsFit) {
        FlowFit(flowNodes_, canvas, 0.0f, flowView_);
        g_videoFlowNeedsFit = false;
    }
    FlowCanvas(draw, canvas, flowNodes_, flowLinks_, flowView_, flowSelected_, 0.0f);

    const Rect bar{area.min.x + 16.0f, area.min.y + 16.0f, area.min.x + 460.0f, area.min.y + 56.0f};
    DrawShadowed(draw, bar.min, bar.max, 10.0f, GlassColor(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "film", ImVec2(bar.min.x + 12.0f, bar.center().y - 7.0f), 14.0f, ColorAccent());
    const char* barTitle = "出片流程 · H3 视频";
    draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(bar.min.x + 32.0f, bar.center().y - 6.25f),
                  ColorText(), barTitle, barTitle + std::strlen(barTitle));

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
// 数据全部来自 shine::project，本页不再有任何编造的卡片：
//   最近列表 = ProjectService::Recent()  —— 读 %APPDATA%/ShineTVStudio/projects.json
//   卡片副行 = project.json 的 premise；封面标签 / meta = project.json 的 templateId
//   打开     = ProjectService::Open()    —— 真读 project.json + 置顶索引
//   新建     = ProjectService::Create()  —— 真落骨架（ValidateSpec + Materialize）
//   移除     = ProjectIndex::Remove()    —— 只摘登记，不删项目文件
// 业务层返回空就是空态，**不补假卡**。
//
// I/O 口径：ProjectService::Recent() 每次调用都重读索引文件（Project.h 有意如此），
// 所以卡片只在本文件首次进入 / 一次写操作之后刷新，不在每帧读盘。
namespace {

// 卡片上要用到、但 RecentEntry 里没有的字段（都来自各自的 project.json）。
struct HubCard {
    project::RecentEntry entry;
    std::string premise;   // project.json.premise（读不到就留空，不拿模板名顶替）
    std::string tplLabel;  // 封面短标签：小说 / 影视 / 空白
    std::string tplName;   // meta 行用的模板全名
    std::string when;      // lastOpened 的相对时间
    bool readable = false; // project.json 是否读得到
    int artSeed = 0;       // 封面种子：由项目 id 稳定派生（不是循环下标）
};

// DrawProjectHub 是自由函数，没有实例可挂交互态，状态放函数内 static
// （与本文件 g_imageFlowNeedsFit 同一手法）。
struct HubState {
    project::ProjectService service;
    std::vector<HubCard> cards; // 排序后的全量
    std::vector<int> shown;     // 搜索过滤后的下标
    bool loaded = false;
    std::string query;
    std::string sort = "r"; // Segmented 的 value：r=最近打开 / n=名称
    bool wizard = false;
    int step = 0;
    std::string tpl = "novel";
    std::string name;
    std::string dir;
    std::string idea;
    bool openDlg = false;
    bool confirm = false;
    std::string confirmTitle;
    std::string confirmBody;
    std::string confirmOk = "确定";
    std::string pendingId;
    std::string status;
    bool statusError = false;
};

HubState& Hub() {
    static HubState state;
    return state;
}

// 封面种子跟着项目 id 走：跨帧、跨排序、跨过滤都是同一张封面。
int SeedOf(std::string_view id) {
    std::uint32_t hash = 2166136261u;
    for (const char ch : id) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619u;
    }
    return static_cast<int>(hash % 12u); // Art 只有 12 组调色板
}

// project.json 的 templateId → 封面短标签（设计稿：小说 / 影视 / 空白）。
std::string TplLabel(std::string_view templateId) {
    if (templateId == "novel") {
        return "小说";
    }
    if (templateId == "film") {
        return "影视";
    }
    if (templateId == "blank") {
        return "空白";
    }
    return {};
}

// 模板 id → 图标（向导第 1 步每行一个）。
const char* TemplateIcon(std::string_view templateId) {
    if (templateId == "novel") {
        return "book";
    }
    if (templateId == "film") {
        return "film";
    }
    return "folder";
}

// 民用日期 → 天序（Hinnant）。只为算「今天 / 昨天 / N 天前」的真实天数差。
long long DayNumber(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const int yoe = year - static_cast<int>(era * 400);
    const int mp = month + (month > 2 ? -3 : 9);
    const int doy = (153 * mp + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// lastOpened 的相对时间：今天 HH:mm / 昨天 HH:mm / N 天前 / 更早给日期。
// 数据来自 RecentEntry::lastOpened（口径照 Qt 版 ProjectHubView.cpp:98）。
std::string RelativeTime(std::chrono::system_clock::time_point tp) {
    const std::time_t when = std::chrono::system_clock::to_time_t(tp);
    const std::tm* lt = std::localtime(&when);
    if (lt == nullptr) {
        return {};
    }
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::tm* ln = std::localtime(&now);
    const long long days =
        (ln != nullptr ? DayNumber(ln->tm_year + 1900, ln->tm_mon + 1, ln->tm_mday) : 0) -
        DayNumber(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    char buf[32] = {};
    if (days <= 0) {
        std::snprintf(buf, sizeof buf, "今天 %02d:%02d", lt->tm_hour, lt->tm_min);
    } else if (days == 1) {
        std::snprintf(buf, sizeof buf, "昨天 %02d:%02d", lt->tm_hour, lt->tm_min);
    } else if (days < 7) {
        std::snprintf(buf, sizeof buf, "%lld 天前", days);
    } else {
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", lt->tm_year + 1900, lt->tm_mon + 1,
                      lt->tm_mday);
    }
    return buf;
}

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

// UTF-8 字符数：设计稿的「200 字」上限按字符算，不是字节。
std::size_t CharCount(std::string_view text) {
    std::size_t count = 0;
    for (const char ch : text) {
        if ((static_cast<unsigned char>(ch) & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

// 重新拉最近列表（Recent() 每次都重读索引，所以只在需要时调）。
void RefreshHub(HubState& hub) {
    hub.cards.clear();
    for (const project::RecentEntry& entry : hub.service.Recent()) {
        HubCard card;
        card.entry = entry;
        card.artSeed = SeedOf(entry.id);
        card.when = RelativeTime(entry.lastOpened);
        if (const std::expected<project::ProjectFile, project::Error> file =
                project::LoadProjectFile(entry.rootDir);
            file.has_value()) {
            card.readable = true;
            card.premise = std::string(util::Trim(file->premise));
            card.tplLabel = TplLabel(file->templateId);
            if (const project::ProjectTemplate* tpl = project::FindTemplate(file->templateId);
                tpl != nullptr) {
                card.tplName = tpl->name;
            }
        }
        hub.cards.push_back(std::move(card));
    }
    if (hub.sort == "n") { // 名称
        std::stable_sort(hub.cards.begin(), hub.cards.end(), [](const HubCard& a, const HubCard& b) {
            return a.entry.name < b.entry.name;
        });
    } else { // 最近打开
        std::stable_sort(hub.cards.begin(), hub.cards.end(), [](const HubCard& a, const HubCard& b) {
            return a.entry.lastOpened > b.entry.lastOpened;
        });
    }
    hub.loaded = true;
}

// 搜索过滤：项目名或一句话创意命中即可（webui 匹配 name + desc）。
void RefilterHub(HubState& hub) {
    const std::string key = LowerAscii(util::Trim(hub.query));
    hub.shown.clear();
    for (std::size_t i = 0; i < hub.cards.size(); ++i) {
        const HubCard& card = hub.cards[i];
        if (key.empty() || LowerAscii(card.entry.name).find(key) != std::string::npos ||
            LowerAscii(card.premise).find(key) != std::string::npos) {
            hub.shown.push_back(static_cast<int>(i));
        }
    }
}

// 打开项目：真 Open（读 project.json + 置顶索引），失败把业务层的中文 message 摆出来。
void OpenHubCard(HubState& hub, const HubCard& card) {
    const std::expected<project::ProjectRef, project::Error> ref =
        hub.service.Open(card.entry.rootDir);
    if (!ref) {
        hub.status = "打开项目失败：" + ref.error().message;
        hub.statusError = true;
        return;
    }
    hub.status = "已打开项目「" + ref->name + "」· " + util::PathToUtf8(ref->rootDir);
    hub.statusError = false;
    RefreshHub(hub);
}

// 从最近列表移除：ProjectService 没这个接口，走 ProjectIndex（只摘登记，不删文件）。
void RemoveHubCard(HubState& hub, const std::string& id) {
    project::ProjectIndex index;
    (void)index.Load(); // 坏文件也已回退空索引
    (void)index.Remove(id);
    if (const std::expected<void, project::Error> saved = index.Save(); !saved) {
        hub.status = "移除未落盘：" + saved.error().message;
        hub.statusError = true;
    } else {
        hub.status = "已从最近列表移除（项目文件未删除）";
        hub.statusError = false;
    }
    RefreshHub(hub);
}

// 主题轮转（pfoot 的 palette 按钮）。动作与 Shell::SetTheme 一致 ——
// 水墨换衬线族必须重建字体图集，否则中文缺字。
void CycleHubTheme(HubState& hub) {
    const auto it =
        std::find(theme::kAllThemes.begin(), theme::kAllThemes.end(), theme::CurrentThemeId());
    const std::size_t index =
        (it == theme::kAllThemes.end()) ? 0 : static_cast<std::size_t>(it - theme::kAllThemes.begin());
    const theme::ThemeId next = theme::kAllThemes[(index + 1) % theme::kAllThemes.size()];
    theme::ApplyTheme(next);
    if (theme::ThemeUsesSerif(next)) {
        if (!BuildFontAtlas(/*serif=*/true)) {
            shine::log::Error("serif font atlas rebuild failed — falling back to sans, 文字可能缺字");
        }
        theme::ApplyCurrentTheme();
    }
    (void)theme::PersistTheme(theme::DefaultThemeFile());
    hub.status = "主题已切到「" + std::string(theme::ThemeDisplayName(next)) + "」";
    hub.statusError = false;
}

// 向导第 2 步的「浏览…」：系统选目录（Win32 边界调用，按钮触发一次即返回）。
std::string PickFolder() {
    BROWSEINFOW info = {};
    info.hwndOwner = reinterpret_cast<HWND>(ImGui::GetMainViewport()->PlatformHandle);
    info.ulFlags = BIF_RETURNONLYFSDIRS;
    info.lpszTitle = L"选择项目位置";
    PIDLIST_ABSOLUTE picked = SHBrowseForFolderW(&info);
    if (picked == nullptr) {
        return {}; // 用户取消
    }
    wchar_t buffer[MAX_PATH] = {};
    const bool ok = SUCCEEDED(SHGetPathFromIDListW(picked, buffer));
    CoTaskMemFree(picked);
    return ok ? util::PathToUtf8(std::filesystem::path{buffer}) : std::string{};
}

// 项目将创建到的目录：<位置>\<项目名>（名字空就还没有目标）。
std::filesystem::path HubTargetDir(const HubState& hub) {
    if (util::Trim(hub.dir).empty() || util::Trim(hub.name).empty()) {
        return {};
    }
    return util::PathFromUtf8(util::Trim(hub.dir)) / util::PathFromUtf8(util::Trim(hub.name));
}

struct ModalBox {
    Rect frame;
    Rect header;
    Rect body;
    Rect footer;
};

// 弹窗外壳：scrim + 圆角面板 + 头 / 体 / 脚（ProjectHub.jsx 的 .modal）。
ModalBox DrawModal(ImDrawList* draw, Rect area, float width, float height) {
    const float top = area.height() * 0.15f;
    const Rect frame{(area.width() - width) * 0.5f, top, (area.width() + width) * 0.5f, top + height};
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorScrim());
    DrawShadowed(draw, frame.min, frame.max, 14.0f, ColorOverlay(), ColorLineNormal(), 1.0f);
    constexpr float headerH = 52.0f;
    constexpr float footerH = 60.0f;
    ModalBox box;
    box.frame = frame;
    box.header = RectAt(frame.min.x, frame.min.y, frame.width(), headerH);
    box.body = RectAt(frame.min.x, frame.min.y + headerH, frame.width(),
                      frame.height() - headerH - footerH);
    box.footer = RectAt(frame.min.x, frame.max.y - footerH, frame.width(), footerH);
    return box;
}

void DrawModalTitle(ImDrawList* draw, Rect header, const char* icon, const char* title) {
    DrawIcon(draw, icon, ImVec2(header.min.x + 18.0f, header.center().y - 7.0f), 14.0f, ColorAccent());
    draw->AddText(FontBoldAt(15.0f), 15.0f, ImVec2(header.min.x + 40.0f, header.center().y - 7.5f),
                  ColorText(), title, title + std::strlen(title));
    DrawRoundRect(draw, ImVec2(header.min.x, header.max.y - 1.0f), ImVec2(header.max.x, header.max.y),
                  0.0f, ColorLineSubtle());
}

struct ModalAction {
    std::string_view label;
    ButtonSpec spec;
    std::string_view id;
};

// 弹窗页脚：按钮右对齐（.modal-f 是「提示 + spacer + 按钮组」）。
// 返回每个按钮本帧是否被点。
std::vector<int> DrawModalFooter(ImDrawList* draw, Rect footer,
                                 const std::vector<ModalAction>& actions) {
    std::vector<int> hit(actions.size(), 0);
    float x = footer.max.x - 20.0f;
    for (std::size_t i = actions.size(); i-- > 0;) {
        const ModalAction& action = actions[i];
        const std::string label(action.label);
        const float textSize = action.spec.size == ButtonSize::Small ? 12.0f : 13.0f;
        ImFont* font = action.spec.variant == ButtonVariant::Primary ? FontBoldAt(textSize)
                                                                    : FontAt(textSize);
        const float iconW = action.spec.icon.empty()
                                ? 0.0f
                                : (action.spec.size == ButtonSize::Small ? 13.0f : 15.0f);
        const float width =
            ButtonWidth(action.spec.size, iconW, LabelWidth(font, textSize, label.c_str()));
        const float height = ButtonHeight(action.spec.size);
        const Rect bounds{x - width, footer.center().y - height * 0.5f, x,
                          footer.center().y + height * 0.5f};
        hit[i] = Button(draw, bounds, label, action.spec, action.id) ? 1 : 0;
        x -= width + 8.0f;
    }
    return hit;
}

// 卡片：整卡可点 + pfoot 的四个入口（打开 / 资源管理器 / 更多 / 主题）。
// 返回 true = 本帧通过整卡或「打开」按钮触发了打开。
bool DrawHubCard(ImDrawList* draw, HubState& hub, const HubCard& card, Rect bounds,
                 float bodyWidth) {
    // 整卡命中先登记：页脚按钮后登记，ImGui 里后命中的 item 优先，
    // 效果等价于 webui 的 e.stopPropagation()（点「移除」不会顺手打开项目）。
    const Hit hit = HitTest(bounds, "hub-card-" + card.entry.id);
    constexpr float radius = 10.0f;
    DrawShadowed(draw, bounds.min, bounds.max, radius, ColorPanel(),
                 hit.hovered ? ColorAccentGlow() : ColorLineSubtle(), 1.0f);

    // 封面满幅（views.css:82 .cover 无内缩）。这版 ImGui 没有 PushClipPath，
    // 卡片顶部的两个圆角用同色三角补掉。
    draw->PushClipRect(bounds.min, bounds.max, true);
    Art(draw, Rect{bounds.min.x, bounds.min.y, bounds.max.x, bounds.min.y + 120.0f}, card.artSeed,
        true);
    draw->PopClipRect();
    draw->PathClear();
    draw->PathLineTo(ImVec2(bounds.min.x, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.min.x + radius, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.min.x, bounds.min.y + radius));
    draw->PathFillConvex(ColorPanel());
    draw->PathClear();
    draw->PathLineTo(ImVec2(bounds.max.x, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.max.x - radius, bounds.min.y));
    draw->PathLineTo(ImVec2(bounds.max.x, bounds.min.y + radius));
    draw->PathFillConvex(ColorPanel());
    if (!card.tplLabel.empty()) {
        Tag(draw, RectAt(bounds.min.x + 10.0f, bounds.min.y + 10.0f, TagWidth(card.tplLabel, true, false),
                         TagHeight(true)),
            card.tplLabel, theme::Tone::Accent, true);
    }

    const float bodyX = bounds.min.x + 14.0f;
    float y = bounds.min.y + 120.0f + 12.0f;
    // 项目名：索引里的 name（与 project.json 同源）
    DrawTextClipped(draw, FontBoldAt(14.5f), 14.5f, ImVec2(bodyX, y), bodyWidth, ColorText(),
                    card.entry.name);
    y += 21.0f;
    // 副行：一句话创意。没有就整行留空 —— 不拿模板名之类的字段顶替
    if (!card.premise.empty()) {
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(bodyX, y), bodyWidth, ColorTextMuted(),
                        card.premise);
    }
    y += 21.0f;
    // meta：模板名 · lastOpened 相对时间
    DrawIcon(draw, "book", ImVec2(bodyX, y + 2.0f), 12.0f, ColorTextMuted());
    const std::string meta =
        card.tplName.empty() ? card.when : (card.tplName + " · " + card.when);
    DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(bodyX + 20.0f, y), bodyWidth - 20.0f,
                    ColorTextMuted(), meta);
    y += 20.0f;
    DrawRoundRect(draw, ImVec2(bodyX, y), ImVec2(bounds.max.x - 14.0f, y + 1.0f), 0.0f,
                  ColorLineSubtle());
    y += 11.0f;

    bool open = false;
    ButtonSpec openSpec;
    openSpec.variant = ButtonVariant::Primary;
    openSpec.size = ButtonSize::Small;
    const float openW = ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, "打开"));
    const Rect openRect{bodyX, y, bodyX + openW, y + ButtonHeight(ButtonSize::Small)};
    const bool openPressed =
        Button(draw, openRect, "打开", openSpec, "hub-open-" + card.entry.id);

    float fx = openRect.max.x + 6.0f;
    const float iconSize = 22.0f;
    const float iconH = ButtonHeight(ButtonSize::Small);
    const bool revealPressed =
        IconButton(draw, RectAt(fx, y, iconSize, iconH), "folder", false, false,
                   "hub-reveal-" + card.entry.id, "在资源管理器中显示");
    fx += iconSize + 6.0f;
    const bool morePressed = IconButton(draw, RectAt(fx, y, iconSize, iconH), "dots", false, false,
                                        "hub-more-" + card.entry.id, "更多");
    const bool themePressed = IconButton(
        draw, RectAt(bounds.max.x - 14.0f - iconSize, y, iconSize, iconH), "palette", false, false,
        "hub-theme-" + card.entry.id, "切换主题");

    // 整卡点击要扣掉页脚按钮：ImGui 的 IsItemClicked 只看「光标在本 item 矩形内」，
    // 页脚按钮压在卡片上，不扣掉的话点「移除」会顺手把项目也打开
    // （等价 webui 里 pfoot 上的 e.stopPropagation）。
    open = hit.clicked && !openPressed && !revealPressed && !morePressed && !themePressed;

    if (revealPressed) {
        const std::string error = util::ShellReveal(card.entry.rootDir);
        hub.status = error.empty() ? ("已在资源管理器中显示「" + card.entry.name + "」") : error;
        hub.statusError = !error.empty();
    }
    if (morePressed) {
        hub.confirm = true;
        hub.confirmTitle = "从列表移除「" + card.entry.name + "」？";
        hub.confirmBody = "仅从最近项目列表移除，不会删除项目文件；之后可通过「打开…」重新加入。";
        hub.confirmOk = "移除";
        hub.pendingId = card.entry.id;
    }
    if (themePressed) {
        CycleHubTheme(hub);
    }
    return open || openPressed;
}

// 新建项目向导：模板 / 命名 / 创意 / 确认。数据源 = AllTemplates / PreviewTree / Create。
void DrawHubWizard(HubState& hub, Rect area, ImDrawList* draw) {
    static const std::vector<std::string> stepNames{"模板", "命名", "创意", "确认"};
    const ModalBox box = DrawModal(draw, area, 600.0f, 420.0f);
    DrawModalTitle(draw, box.header, "sparkles", "新建项目");
    const float stepsW = StepsWidth(stepNames);
    Steps(draw, RectAt(box.header.max.x - 20.0f - stepsW, box.header.center().y - 10.0f, stepsW, 20.0f),
          stepNames, hub.step);

    const Rect body{box.body.min.x + 20.0f, box.body.min.y + 16.0f, box.body.max.x - 20.0f,
                    box.body.max.y - 8.0f};
    if (hub.step == 0) {
        float y = body.min.y;
        for (const project::ProjectTemplate& tpl : project::AllTemplates()) {
            const Rect row{body.min.x, y, body.max.x, y + 62.0f};
            const Hit hit = HitTest(row, "hub-tpl-" + tpl.id);
            const bool on = hub.tpl == tpl.id;
            DrawShadowed(draw, row.min, row.max, 10.0f, on ? ColorFillMuted() : ColorPanel(),
                         on ? ColorAccent() : (hit.hovered ? ColorLineStrong() : ColorLineSubtle()),
                         1.0f);
            DrawIconCentered(draw, TemplateIcon(tpl.id), ImVec2(row.min.x + 28.0f, row.center().y),
                             20.0f, on ? ColorAccent() : ColorTextMuted());
            draw->AddText(FontBoldAt(13.0f), 13.0f, ImVec2(row.min.x + 60.0f, row.min.y + 14.0f),
                          ColorText(), tpl.name.data(), tpl.name.data() + tpl.name.size());
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(row.min.x + 60.0f, row.min.y + 34.0f),
                            row.width() - 100.0f, ColorTextMuted(), tpl.description);
            if (on) {
                DrawIconCentered(draw, "check", ImVec2(row.max.x - 20.0f, row.center().y), 16.0f,
                                 ColorAccent());
            }
            if (hit.clicked) {
                hub.tpl = tpl.id;
            }
            y += 70.0f;
        }
    } else if (hub.step == 1) {
        const Rect nameField =
            Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.min.y + 48.0f}, "项目名称", {});
        Input(draw, nameField, hub.name, "例如：灯语回声", "hub-wiz-name");
        const Rect dirField = Field(
            draw, Rect{body.min.x, nameField.max.y + 18.0f, body.max.x, nameField.max.y + 66.0f},
            "位置", "项目目录将创建在该路径下");
        constexpr const char* browseLabel = "浏览…";
        const float browseW =
            ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontAt(13.0f), 13.0f, browseLabel));
        Input(draw, Rect{dirField.min.x, dirField.min.y, dirField.max.x - browseW - 8.0f,
                         dirField.max.y},
              hub.dir, "E:\\projects", "hub-wiz-dir");
        ButtonSpec browseSpec;
        browseSpec.variant = ButtonVariant::Secondary;
        browseSpec.icon = "folder";
        if (Button(draw, RectAt(dirField.max.x - browseW, dirField.min.y, browseW, 30.0f),
                   browseLabel, browseSpec, "hub-wiz-browse")) {
            const std::string picked = PickFolder();
            if (!picked.empty()) {
                hub.dir = picked;
            }
        }
        // 将创建目录：目标路径 + PreviewTree 给出的真实骨架清单
        const Rect preview{body.min.x, dirField.max.y + 16.0f, body.max.x, body.max.y};
        DrawRoundRect(draw, preview.min, preview.max, 10.0f, ColorFillMuted(), ColorLineSubtle(),
                      1.0f);
        const char* previewLabel = "将创建目录";
        draw->AddText(FontAt(11.0f), 11.0f, ImVec2(preview.min.x + 14.0f, preview.min.y + 10.0f),
                      ColorTextMuted(), previewLabel, previewLabel + std::strlen(previewLabel));
        const std::filesystem::path target = HubTargetDir(hub);
        const std::string targetText = target.empty() ? "<项目名>" : util::PathToUtf8(target);
        DrawTextClipped(draw, MonoAt(12.0f), 12.0f, ImVec2(preview.min.x + 14.0f, preview.min.y + 28.0f),
                        preview.width() - 28.0f, ColorText(), targetText);
        const std::vector<std::string> tree = project::PreviewTree(hub.tpl);
        float ty = preview.min.y + 50.0f;
        for (std::size_t i = 0; i < tree.size() && i < 6; ++i) {
            DrawTextClipped(draw, MonoAt(11.0f), 11.0f,
                            ImVec2(preview.min.x + 14.0f, ty), preview.width() - 28.0f,
                            ColorTextMuted(), tree[i]);
            ty += 15.0f;
        }
        if (tree.size() > 6) {
            const std::string more = "… 共 " + std::to_string(tree.size()) + " 项";
            draw->AddText(MonoAt(11.0f), 11.0f, ImVec2(preview.min.x + 14.0f, ty), ColorTextMuted(),
                          more.data(), more.data() + more.size());
        }
    } else if (hub.step == 2) {
        const std::string label = "一句话创意（" + std::to_string(CharCount(hub.idea)) + "/200）";
        const Rect ideaField = Field(draw, Rect{body.min.x, body.min.y, body.max.x, body.max.y},
                                     label, "将写入 project.json，供 LLM 初始化参考；留空可跳过");
        TextArea(draw,
                 Rect{ideaField.min.x, ideaField.min.y, ideaField.max.x, ideaField.min.y + 120.0f},
                 hub.idea, 5, "hub-wiz-idea");
    } else {
        const Rect card{body.min.x, body.min.y, body.max.x, body.min.y + 96.0f};
        DrawShadowed(draw, card.min, card.max, 10.0f, ColorPanel(), ColorLineSubtle(), 1.0f);
        Art(draw, Rect{card.min.x + 16.0f, card.min.y + 16.0f, card.min.x + 112.0f, card.min.y + 80.0f},
            7, false);
        const std::string title = util::Trim(hub.name).empty() ? "未命名项目"
                                                                    : std::string(util::Trim(hub.name));
        draw->AddText(FontBoldAt(17.0f), 17.0f, ImVec2(card.min.x + 128.0f, card.min.y + 16.0f),
                      ColorText(), title.data(), title.data() + title.size());
        const std::filesystem::path target = HubTargetDir(hub);
        const std::string pathText = target.empty() ? std::string{} : util::PathToUtf8(target);
        DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(card.min.x + 128.0f, card.min.y + 40.0f),
                        card.width() - 144.0f, ColorTextMuted(), pathText);
        const project::ProjectTemplate* tpl = project::FindTemplate(hub.tpl);
        const std::string tplName = tpl != nullptr ? tpl->name : std::string{};
        float tagX = card.min.x + 128.0f;
        if (!tplName.empty()) {
            const float tagW = TagWidth(tplName, true, false);
            Tag(draw, RectAt(tagX, card.min.y + 62.0f, tagW, TagHeight(true)), tplName,
                theme::Tone::Accent, true);
            tagX += tagW + 6.0f;
        }
        const char* ideaTag = util::Trim(hub.idea).empty() ? "无创意" : "含一句话创意";
        const float ideaW = TagWidth(ideaTag, true, false);
        Tag(draw, RectAt(tagX, card.min.y + 62.0f, ideaW, TagHeight(true)), ideaTag,
            theme::Tone::Idle, true);
        const char* note = "创建后将打开工作坊：总控 / 小说 / 资产 / 分镜 / 出图 / 出片 六个工作区可用。";
        DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                        ImVec2(body.min.x, card.max.y + 14.0f), body.width(), ColorTextMuted(), note);
    }

    // 页脚左侧：本次操作的实际结果（错误时用 danger 色），没有就摆事实提示
    const char* hint = hub.status.empty() ? "项目骨架只写入本机目录" : nullptr;
    if (hub.status.empty()) {
        draw->AddText(FontAt(11.0f), 11.0f, ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f),
                      ColorTextMuted(), hint, hint + std::strlen(hint));
    } else {
        DrawTextClipped(draw, FontAt(11.0f), 11.0f,
                        ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f), 300.0f,
                        hub.statusError ? ToneColor(theme::Tone::Danger) : ColorTextMuted(),
                        hub.status, true);
    }

    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec secondarySpec;
    secondarySpec.variant = ButtonVariant::Secondary;
    ButtonSpec primarySpec;
    primarySpec.variant = ButtonVariant::Primary;
    if (hub.step == 3) {
        primarySpec.icon = "sparkles";
    }
    std::vector<ModalAction> actions;
    actions.push_back({"取消", ghostSpec, "hub-wiz-cancel"});
    if (hub.step > 0) {
        actions.push_back({"上一步", secondarySpec, "hub-wiz-prev"});
    }
    actions.push_back({hub.step < 3 ? "下一步" : "创建", primarySpec,
                       hub.step < 3 ? "hub-wiz-next" : "hub-wiz-create"});
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);

    if (hit[0] != 0) {
        hub.wizard = false;
        hub.status.clear();
        hub.statusError = false;
        return;
    }
    if (hub.step > 0 && hit[1] != 0) {
        hub.step -= 1;
        hub.status.clear();
        hub.statusError = false;
        return;
    }
    if (hit.back() == 0) {
        return;
    }
    if (hub.step < 3) {
        // canNext：只有第 1 步（命名）会挡人，其余步都能过（设计稿 ProjectHub.jsx:22）
        if (hub.step == 1 && util::Trim(hub.name).empty()) {
            hub.status = "项目名称不能为空（ValidateSpec 也会拦，这里先提示一次）";
            hub.statusError = true;
            return;
        }
        if (hub.step == 2 && CharCount(hub.idea) > 200) {
            hub.status = "一句话创意超过 200 字，请删减后再继续";
            hub.statusError = true;
            return;
        }
        hub.step += 1;
        hub.status.clear();
        hub.statusError = false;
        return;
    }

    // 第 4 步：真建。ValidateSpec / Materialize 的中文错误直接摆给用户。
    project::ProjectSpec spec;
    spec.name = std::string(util::Trim(hub.name));
    spec.rootDir = HubTargetDir(hub);
    spec.templateId = hub.tpl;
    spec.premise = std::string(util::Trim(hub.idea));
    const std::expected<project::ProjectRef, project::Error> created = hub.service.Create(spec);
    if (!created) {
        hub.status = "创建失败：" + created.error().message;
        hub.statusError = true;
        return;
    }
    hub.wizard = false;
    hub.name.clear();
    hub.idea.clear();
    hub.step = 0;
    hub.status = "已创建并打开项目「" + created->name + "」· " + util::PathToUtf8(created->rootDir);
    hub.statusError = false;
    RefreshHub(hub);
}

// 「打开…」对话框：列最近项目，点了就 Open。
void DrawHubOpenDialog(HubState& hub, Rect area, ImDrawList* draw) {
    const ModalBox box = DrawModal(draw, area, 520.0f, 440.0f);
    DrawModalTitle(draw, box.header, "folder", "打开项目");
    // 右上角标出列表的真实来源（索引文件所在目录）
    const std::string indexDir = util::PathToUtf8(project::DefaultIndexFile().parent_path());
    const float indexW = LabelWidth(FontAt(11.5f), 11.5f, indexDir.c_str());
    DrawTextClipped(draw, FontAt(11.5f), 11.5f,
                    ImVec2(box.header.max.x - 20.0f - indexW, box.header.center().y - 5.75f),
                    indexW + 1.0f, ColorTextMuted(), indexDir);

    if (hub.cards.empty()) {
        Empty(draw, Rect{box.body.min.x, box.body.min.y, box.body.max.x, box.body.max.y}, "folder",
              "还没有可打开的项目", "先新建一个项目，或把已有项目目录登记进来");
    } else {
        float y = box.body.min.y + 6.0f;
        for (const HubCard& card : hub.cards) {
            const Rect row{box.body.min.x + 12.0f, y, box.body.max.x - 12.0f, y + 62.0f};
            const Hit hit = HitTest(row, "hub-open-" + card.entry.id);
            DrawShadowed(draw, row.min, row.max, 10.0f, ColorPanel(),
                         hit.hovered ? ColorLineStrong() : ColorLineSubtle(), 1.0f);
            Art(draw, Rect{row.min.x + 10.0f, row.min.y + 10.0f, row.min.x + 74.0f, row.min.y + 52.0f},
                card.artSeed, false);
            const float nameX = row.min.x + 86.0f;
            float nameW = LabelWidth(FontBoldAt(13.0f), 13.0f, card.entry.name.c_str());
            draw->AddText(FontBoldAt(13.0f), 13.0f, ImVec2(nameX, row.min.y + 12.0f), ColorText(),
                          card.entry.name.data(), card.entry.name.data() + card.entry.name.size());
            if (!card.tplLabel.empty()) {
                const theme::Tone tone = card.tplLabel == "小说"  ? theme::Tone::Accent
                                          : card.tplLabel == "影视" ? theme::Tone::Info
                                                                    : theme::Tone::Idle;
                const float tagW = TagWidth(card.tplLabel, true, false);
                Tag(draw, RectAt(nameX + nameW + 8.0f, row.min.y + 11.0f, tagW, TagHeight(true)),
                    card.tplLabel, tone, true);
            }
            const std::string meta = card.tplName.empty() ? ("最近 " + card.when)
                                                          : (card.tplName + " · " + card.when);
            DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(nameX, row.min.y + 34.0f),
                            row.width() - 160.0f, ColorTextMuted(), meta);
            ButtonSpec openSpec;
            openSpec.variant = ButtonVariant::Primary;
            openSpec.size = ButtonSize::Small;
            const float openW =
                ButtonWidth(ButtonSize::Small, 0.0f, LabelWidth(FontBoldAt(12.0f), 12.0f, "打开"));
            const Rect openRect{row.max.x - 14.0f - openW, row.min.y + 19.0f, row.max.x - 14.0f,
                                row.min.y + 19.0f + ButtonHeight(ButtonSize::Small)};
            if (Button(draw, openRect, "打开", openSpec, "hub-open-btn-" + card.entry.id) ||
                hit.clicked) {
                // 先按值取出来：OpenHubCard 会刷新列表，hub.cards 随即重建
                const HubCard target = card;
                hub.openDlg = false;
                OpenHubCard(hub, target);
                return;
            }
            y += 70.0f;
        }
    }

    const char* hint = "单击卡片直接打开";
    draw->AddText(FontAt(11.0f), 11.0f, ImVec2(box.footer.min.x + 20.0f, box.footer.center().y - 5.5f),
                  ColorTextMuted(), hint, hint + std::strlen(hint));
    ButtonSpec ghostSpec;
    ghostSpec.variant = ButtonVariant::Ghost;
    ButtonSpec folderSpec;
    folderSpec.variant = ButtonVariant::Ghost;
    folderSpec.icon = "folder";
    folderSpec.disabled = hub.cards.empty();
    const std::vector<ModalAction> actions{
        {"打开所在文件夹", folderSpec, "hub-open-reveal"},
        {"关闭", ghostSpec, "hub-open-close"}};
    const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);
    if (hit[0] != 0) {
        const std::string error = util::ShellReveal(hub.cards.front().entry.rootDir);
        hub.status = error.empty() ? "已在资源管理器中显示项目目录" : error;
        hub.statusError = !error.empty();
    }
    if (hit[1] != 0) {
        hub.openDlg = false;
    }
}

} // namespace

// 全屏，不套外壳。居中列 max-w 1080，底两层径向渐变 + 240px 高 ArtInk 带。
void DrawProjectHub(Rect area, ImDrawList* draw) {
    HubState& hub = Hub();
    if (!hub.loaded) {
        if (util::Trim(hub.dir).empty()) {
            // 位置默认给当前工作目录：ValidateSpec 要求父目录真实存在，
            // 设计稿里写死的 E:\projects 在别的机器上不一定有。
            std::error_code ec;
            hub.dir = util::PathToUtf8(std::filesystem::current_path(ec));
            if (ec) {
                hub.dir = ".";
            }
        }
        RefreshHub(hub);
    }

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

    // ---- 工具条：搜索 / 排序 / 打开… / 新建项目 ----
    const float barY = column.min.y + 76.0f;
    const Rect search{column.min.x, barY, column.min.x + 280.0f, barY + 30.0f};
    // 设计稿的搜索框是「图标 + 输入」合一（.search）。kit::Input 的左内边距只有
    // 10px、塞不下 14px 图标，所以输入框右移 24px，再在没 hover/没聚焦时把它
    // 自己的左边框擦掉 —— 两个控件合起来仍然是一个 280px 的圆角搜索框。
    const Rect searchInput{search.min.x + 24.0f, search.min.y, search.max.x, search.max.y};
    Input(draw, searchInput, hub.query, "搜索项目…", "hub-search");
    if (!ImGui::IsItemFocused() && !ImGui::IsItemHovered()) {
        draw->AddRectFilled(searchInput.min, ImVec2(searchInput.min.x + 1.0f, searchInput.max.y),
                            ColorFillMuted());
    }
    DrawIcon(draw, "search", ImVec2(search.min.x + 8.0f, search.center().y - 7.0f), 14.0f,
             ColorTextMuted());

    const std::vector<SegmentOption> sortOptions{{"r", "最近打开"}, {"n", "名称"}};
    const std::string_view picked = Segmented(
        draw, RectAt(search.max.x + 10.0f, barY, SegmentedWidth(sortOptions), 30.0f), sortOptions,
        hub.sort, "hub-sort");
    if (picked != hub.sort) {
        hub.sort = std::string(picked);
        RefreshHub(hub); // 排序换了要真重排（RefreshHub 按 hub.sort 排）
    }

    ButtonSpec openProjectSpec;
    openProjectSpec.variant = ButtonVariant::Secondary;
    openProjectSpec.icon = "folder";
    const char* openLabel = "打开…";
    const float openW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontAt(13.0f), 13.0f, openLabel));
    const bool openClicked =
        Button(draw, RectAt(column.max.x - openW, barY, openW, 30.0f), openLabel, openProjectSpec,
               "hub-open");
    ButtonSpec newProject;
    newProject.variant = ButtonVariant::Primary;
    newProject.icon = "plus";
    const char* newLabel = "新建项目";
    const float newW =
        ButtonWidth(ButtonSize::Medium, 15.0f, LabelWidth(FontBoldAt(13.0f), 13.0f, newLabel));
    const bool newClicked =
        Button(draw, RectAt(column.max.x - openW - 10.0f - newW, barY, newW, 30.0f), newLabel,
               newProject, "hub-new");
    if (openClicked) {
        hub.openDlg = true;
    }
    if (newClicked) {
        hub.wizard = true;
        hub.step = 0;
        hub.status.clear();
        hub.statusError = false;
    }
    // 搜索过滤放在工具条之后：本帧输入框里刚敲进来的字立刻生效
    RefilterHub(hub);

    // ---- 计数行：搜索结果条数 + 上一次操作的结果 ----
    const Rect count{column.min.x, barY + 30.0f + 22.0f, column.max.x, barY + 30.0f + 22.0f + 16.0f};
    const std::string countText = "最近项目 · 共 " + std::to_string(hub.shown.size()) + " 个项目";
    draw->AddText(FontBoldAt(12.0f), 12.0f, ImVec2(count.min.x, count.min.y + 1.0f),
                  ColorTextMuted(), countText.data(), countText.data() + countText.size());
    if (!hub.status.empty()) {
        const float maxW = column.width() * 0.5f;
        const float statusW = std::min(LabelWidth(FontAt(12.0f), 12.0f, hub.status.c_str()), maxW);
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(count.max.x - statusW, count.min.y + 1.0f),
                        statusW, hub.statusError ? ToneColor(theme::Tone::Danger) : ColorTextMuted(),
                        hub.status);
    }

    // ---- 卡片栅格 / 空态 ----
    const Rect grid{column.min.x, count.max.y + 10.0f, column.max.x, column.max.y - 8.0f};
    if (hub.shown.empty()) {
        const Rect emptyCard{grid.min.x, grid.min.y, grid.max.x, grid.min.y + 260.0f};
        DrawShadowed(draw, emptyCard.min, emptyCard.max, 10.0f, ColorPanel(), ColorLineSubtle(),
                     1.0f);
        const bool filtering = !util::Trim(hub.query).empty();
        const char* emptyTitle = filtering ? "没有匹配的项目" : "还没有项目";
        const char* emptyBody =
            filtering ? "换个关键词试试，或新建一个项目开始你的第一部作品"
                      : "点「新建项目」开始你的第一部作品";
        Empty(draw, Rect{emptyCard.min.x, emptyCard.min.y + 60.0f, emptyCard.max.x,
                         emptyCard.max.y - 60.0f},
              "folder", emptyTitle, emptyBody);
    } else {
        constexpr float cardH = 250.0f; // 120 封面 + pbody(12+19+21+21+20+11+24+13)
        const int columns = std::max(1, AutoFillCols(grid.width(), 240.0f, 14.0f));
        const float cardW = (grid.width() - 14.0f * static_cast<float>(columns - 1)) /
                            static_cast<float>(columns);
        // 打开会重排列表（lastOpened 变了），所以只记下标、循环外再执行 ——
        // 循环里刷新会让 hub.cards 重建，后面几张卡读到的是错位的条目。
        int openIndex = -1;
        for (std::size_t i = 0; i < hub.shown.size(); ++i) {
            const HubCard& card = hub.cards[static_cast<std::size_t>(hub.shown[i])];
            const int column = static_cast<int>(i) % columns;
            const int row = static_cast<int>(i) / columns;
            const Rect bounds{grid.min.x + (cardW + 14.0f) * static_cast<float>(column),
                              grid.min.y + (cardH + 14.0f) * static_cast<float>(row), cardW, cardH};
            if (DrawHubCard(draw, hub, card, bounds, cardW - 28.0f)) {
                openIndex = static_cast<int>(i);
            }
        }
        if (openIndex >= 0) {
            OpenHubCard(hub, hub.cards[static_cast<std::size_t>(
                                 hub.shown[static_cast<std::size_t>(openIndex)])]);
        }
    }

    // ---- 弹窗：确认 > 向导 > 打开项目 ----
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (hub.confirm) {
            hub.confirm = false;
        } else if (hub.wizard) {
            hub.wizard = false;
        } else if (hub.openDlg) {
            hub.openDlg = false;
        }
    }
    if (hub.confirm) {
        const ModalBox box = DrawModal(draw, area, 460.0f, 210.0f);
        DrawModalTitle(draw, box.header, "info", hub.confirmTitle.c_str());
        DrawTextClipped(draw, FontAt(12.5f), 12.5f,
                        ImVec2(box.body.min.x + 18.0f, box.body.min.y + 18.0f),
                        box.body.width() - 36.0f, ColorTextSecondary(), hub.confirmBody, true);
        ButtonSpec ghostSpec;
        ghostSpec.variant = ButtonVariant::Ghost;
        ButtonSpec dangerSpec;
        dangerSpec.variant = ButtonVariant::Danger;
        const std::vector<ModalAction> actions{{"取消", ghostSpec, "hub-confirm-cancel"},
                                               {hub.confirmOk, dangerSpec, "hub-confirm-ok"}};
        const std::vector<int> hit = DrawModalFooter(draw, box.footer, actions);
        if (hit[0] != 0) {
            hub.confirm = false;
        }
        if (hit[1] != 0) {
            RemoveHubCard(hub, hub.pendingId);
            hub.confirm = false;
        }
    }
    if (hub.wizard) {
        DrawHubWizard(hub, area, draw);
    }
    if (hub.openDlg) {
        DrawHubOpenDialog(hub, area, draw);
    }
}

} // namespace shine::pages
