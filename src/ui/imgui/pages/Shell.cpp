#include "ui/imgui/pages/Shell.h"

#include "comfy/ComfySession.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "pipeline/StageMachine.h"
#include "ui/imgui/host/AppEnvironment.h"
#include "ui/imgui/kit/Anim.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Overlays.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/Gallery.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Shell.h"

#include <yyjson.h>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace shine::pages {
namespace {

using namespace shine::kit;

// design-spec §4 的全部固定档（100% 缩放下）
constexpr float kTopBarHeight = 46.0f;
constexpr float kRailWidth = 56.0f;
constexpr float kSidePanelWidth = 240.0f;
constexpr float kInspectorWidth = 280.0f;
constexpr float kDockHeight = 190.0f;
constexpr float kStatusBarHeight = 26.0f;
constexpr float kCrumbHeight = 34.0f;

// 命令面板的组标题高与行高（内容总高靠这两个累加，滚动跟随选中行也靠它）。
constexpr float kPaletteGroupH = 22.0f;
constexpr float kPaletteRowH = 30.0f;

// T 链的阶段数 —— 状态栏那个「T{n}/N」的分母。
//
// ⚠️ 早先是硬编码字面量 `"/17"`（Shell.cpp:1288）。**不能**改成 AllStages().size()：
//    那是 **28**（T1–T17 + V0–V11），而状态栏报的是**文本链**的进度，分母得是
//    chain == "text" 的条数 —— 与总控页 `OverviewState().chain` 同一口径（那边
//    同样只取 text 链 17 个）。写死 17 则是「哪天业务层加了 T18 就悄悄对不上」。
int TextStageCount() {
    static const int count = [] {
        int n = 0;
        for (const pipeline::StageDefinition& def : pipeline::AllStages()) {
            if (def.chain == "text") {
                ++n;
            }
        }
        return n;
    }();
    return count;
}

// layout.dat 的 magic 与 Qt 侧一致（P1.5 的契约）
constexpr char kLayoutMagic[16] = {'s', 'h', 'i', 'n', 'e', 't', 'v', '-', 'l', 'a', 'y', 'o',
                                   'u', 't', '-', '1'};

struct RailEntry {
    const char* icon;
    const char* label;
    int workspace;
};

std::filesystem::path LayoutFile() {
    if (const std::filesystem::path dir = shine::app::EnvironmentPath(L"APPDATA");
        !dir.empty()) {
        return dir / L"ShineTVStudio" / L"layout.dat";
    }
    return {};
}

} // namespace

const char* WorkspaceIcon(int workspace) {
    switch (workspace) {
    case 0: return "gauge";
    case 1: return "book";
    case 2: return "masks";
    case 3: return "clapper";
    case 4: return "image";
    case 5: return "film";
    default: break;
    }
    return "grid";
}

const char* WorkspaceLabel(int workspace) {
    switch (workspace) {
    case 0: return "总控";
    case 1: return "小说";
    case 2: return "资产";
    case 3: return "分镜";
    case 4: return "出图";
    case 5: return "出片";
    default: break;
    }
    return "组件画廊";
}

const char* WorkspaceSubtitle(int workspace) {
    switch (workspace) {
    case 0: return "全流程总控台";
    case 1: return "小说生产工作区";
    case 2: return "视觉资产工作区";
    case 3: return "分镜工作区";
    case 4: return "出图工作区";
    case 5: return "出片工作区";
    default: break;
    }
    return "组件画廊";
}

const char* WorkspaceTarget(int workspace) {
    switch (workspace) {
    case 0: return "pipeline";
    case 1: return "novel";
    case 2: return "assets";
    case 3: return "storyboard";
    case 4: return "imageflow";
    case 5: return "videoflow";
    default: break;
    }
    return "gallery";
}

// ---------------------------------------------------------------- P4.2 顶栏
void Shell::DrawTopBar(Rect area, ImDrawList* draw) {
    // 玻璃降级：用 --glass 实色（R4 已知降级）
    DrawRoundRect(draw, area.min, area.max, 0.0f, GlassColor());
    draw->AddLine(ImVec2(area.min.x, area.max.y - 0.5f), ImVec2(area.max.x, area.max.y - 0.5f),
                  ColorLineSubtle(), 1.0f);

    float x = area.min.x + 16.0f;
    // 品牌标 22×22 / r6 / grad-accent + shadow-accent + 内嵌 12px play
    const Rect brand{area.min.x + 16.0f, 0.5f * (area.min.y + area.max.y) - 11.0f,
                     area.min.x + 38.0f, 0.5f * (area.min.y + area.max.y) + 11.0f};
    const Rect halo = Rect(brand.min.x, brand.min.y - 2.0f, brand.max.x, brand.max.y + 2.0f);
    DrawRoundRect(draw, halo.min, halo.max, 8.0f, 0, ColorAccentGlow(), 2.0f);
    DrawDiagGradient(draw, brand.min, brand.max, 6.0f, ColorAccent(), ColorAccentHover());
    DrawIconCentered(draw, "play", brand.center(), 12.0f, ColorAccentFg());

    ImFont* brandFont = FontBoldAt(13.5f);
    const std::string_view prefix = "ShineTV ";
    const std::string_view suffix = "Studio";
    x = brand.max.x + 10.0f;
    draw->AddText(brandFont, 13.5f, ImVec2(x, 0.5f * (area.min.y + area.max.y) - 6.75f),
                  ColorText(), prefix.data(), prefix.data() + prefix.size());
    x += brandFont->CalcTextSizeA(13.5f, 1e9f, 0.0f, prefix.data(), prefix.data() + prefix.size())
             .x;
    // 「Studio」是渐变字：分两段画，中间色过渡
    const float suffixW =
        brandFont->CalcTextSizeA(13.5f, 1e9f, 0.0f, suffix.data(), suffix.data() + suffix.size()).x;
    draw->AddText(brandFont, 13.5f, ImVec2(x, 0.5f * (area.min.y + area.max.y) - 6.75f),
                  ColorAccent(), suffix.data(), suffix.data() + suffix.size());
    x += suffixW + 20.0f;

    // 品牌标整块可点：同样回项目中心（webui Shell.jsx:26 的 onHub）。
    {
        const Rect hit{area.min.x + 16.0f, area.min.y, x - 20.0f, area.max.y};
        if (ChromeHit(hit, "tb-brand").clicked) {
            ToggleProjectHub();
        }
    }

    // 项目胶囊 h28 r6：8×8 渐变点 + 名字（max-w 140）+ 10px 下箭头
    const std::string& project =
        layout_.projectName.empty() ? std::string("未打开项目") : layout_.projectName;
    ImFont* capFont = FontAt(13.0f);
    const float capTextW = std::min(
        140.0f, capFont->CalcTextSizeA(13.0f, 1e9f, 0.0f, project.data(), project.data() + project.size()).x);
    const Rect capsule{x, 0.5f * (area.min.y + area.max.y) - 14.0f, x + 12.0f + 8.0f + capTextW + 10.0f + 10.0f + 8.0f,
                      0.5f * (area.min.y + area.max.y) + 14.0f};
    DrawRoundRect(draw, capsule.min, capsule.max, 6.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
    // .proj-chip .pdot 的 `0 0 6px var(--accent-glow)`（shell.css:78）：光晕用
    // accent-glow 自身的 alpha，不再额外乘 0.6（乘完比设计稿更淡）。
    DrawRoundRect(draw, ImVec2(capsule.min.x + 10.0f, capsule.center().y - 4.0f),
                  ImVec2(capsule.min.x + 18.0f, capsule.center().y + 4.0f), 4.0f,
                  ColorAccentGlow());
    DrawHGradient(draw, ImVec2(capsule.min.x + 10.0f, capsule.center().y - 4.0f),
                  ImVec2(capsule.min.x + 18.0f, capsule.center().y + 4.0f), 4.0f, ColorAccent(),
                  ColorAccentHover());
    DrawTextClipped(draw, capFont, 13.0f,
                    ImVec2(capsule.min.x + 24.0f, capsule.center().y - 6.5f), capTextW, ColorText(),
                    project);
    DrawIcon(draw, "chevdown", ImVec2(capsule.max.x - 18.0f, capsule.center().y - 5.0f), 10.0f,
             ColorTextMuted());
    // 项目胶囊可点：回项目中心（webui Shell.jsx:31 的 onHub）。早先只是画了个带箭头的死胶囊。
    if (ChromeHit(capsule, "tb-projchip").clicked) {
        ToggleProjectHub();
    }
    x = capsule.max.x + 16.0f;

    // 搜索触发器 260×28 胶囊 + 右侧 Kbd "Ctrl K"
    const Rect search{x, 0.5f * (area.min.y + area.max.y) - 14.0f, x + 260.0f,
                      0.5f * (area.min.y + area.max.y) + 14.0f};
    DrawRoundRect(draw, search.min, search.max, 14.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "search", ImVec2(search.min.x + 10.0f, search.center().y - 7.0f), 14.0f,
             ColorTextMuted());
    // ⚠️ 长度一律用 sizeof(字面量) - 1。本树曾有 6 处把字节数写死在 AddText 的
    //    text_end 上，而 4 个汉字在 UTF-8 里是 12 字节 —— 结果每处都少画 1 个字；
    //    另有一处比实际多 5 字节，直接读到字面量池外面。搜索提示「搜索命令」就属于前者。
    static constexpr char kSearchHint[] = "搜索命令";
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(search.min.x + 30.0f, search.center().y - 6.25f),
                  ColorTextMuted(), kSearchHint, kSearchHint + sizeof(kSearchHint) - 1);
    const float kbdW = KbdWidth("Ctrl K");
    Kbd(draw, RectAt(search.max.x - kbdW - 6.0f, search.center().y - 9.0f, kbdW, 18.0f), "Ctrl K");
    // 搜索框可点：开命令面板（webui Shell.jsx:36）。
    if (ChromeHit(search, "tb-search").clicked) {
        ToggleCommandPalette();
    }

    // 右侧：运行 / 停止 / 主题 / 设置 / Comfy 状态
    // 排布照 webui Shell.jsx:44-78（自右向左）：Comfy 状态项 · 设置 · 主题 · 运行/停止
    const float cy = 0.5f * (area.min.y + area.max.y);
    float rx = area.max.x - 16.0f;

    // Comfy 状态项（shell.css:458 的 .sb-item）：状态点 + 文字。
    // 早先这里只画一个恒为 Idle 的裸点，没有任何文字，也点不动。
    // 现在读 ComfySession 的真实连接状态 —— 没配地址是"未配置"，
    // 配了但连不上是 LastError，非空即报"未连接"。不编造"已连接"。
    {
        comfy::ComfySession& comfy = comfy::ComfySession::Instance();
        const bool configured = !Settings().comfyBaseUrl.empty();
        const std::string err = comfy.LastError();
        const bool connected = configured && err.empty();
        std::string text;
        theme::Tone tone = theme::Tone::Idle;
        if (!configured) {
            text = "Comfy 未配置";
        } else if (!connected) {
            text = "Comfy 未连接";
            tone = theme::Tone::Warn;
        } else {
            text = "Comfy 已连接";
            tone = theme::Tone::Ok;
        }
        ImFont* f = FontAt(11.5f);
        const float tw =
            f->CalcTextSizeA(11.5f, 1e9f, 0.0f, text.data(), text.data() + text.size()).x;
        const float w = 8.0f + 11.0f + 6.0f + tw + 16.0f;
        const Rect item{rx - w, cy - 10.0f, rx, cy + 10.0f};
        const Hit hit = ChromeHit(item, "tb-comfy");
        if (hit.hovered) {
            DrawRoundRect(draw, item.min, item.max, 4.0f, ColorFillHover());
        }
        StatusDot(draw, ImVec2(item.min.x + 8.0f + 3.5f, cy), tone, false);
        draw->AddText(f, 11.5f, ImVec2(item.min.x + 8.0f + 11.0f + 6.0f, cy - 5.75f),
                      ColorTextSecondary(), text.data(), text.data() + text.size());
        if (hit.clicked) {
            // 点开设置模态看连接细节，而不是像设计稿那样凭空把连接状态翻个面。
            ToggleSettingsModal(true);
        }
        rx -= w + 6.0f;
    }

    rx -= 28.0f;
    if (IconButton(draw, RectAt(rx, cy - 14.0f, 28.0f, 28.0f), "settings", false, false,
                   "tb-settings", "设置")) {
        ToggleSettingsModal(!settingsOpen_);
    }

    // 「主题」是**幽灵按钮 + 文字**（Shell.jsx:51），点开的是主题菜单，
    // 不是命令面板 —— 早先这里是个纯图标键且直接翻 paletteOpen_，两处都错。
    {
        ButtonSpec themeSpec;
        themeSpec.variant = ButtonVariant::Ghost;
        themeSpec.icon = "palette";
        const std::string_view label = "主题";
        ImFont* f = FontAt(13.0f);
        const float bw = ButtonWidth(ButtonSize::Medium, 15.0f,
                                     f->CalcTextSizeA(13.0f, 1e9f, 0.0f, label.data(),
                                                      label.data() + label.size())
                                             .x);
        rx -= bw;
        themeMenuAnchor_ = ImVec2(rx, cy + 15.0f);
        if (Button(draw, RectAt(rx, cy - 15.0f, bw, 30.0f), label, themeSpec, "tb-theme")) {
            themeMenuOpen_ = !themeMenuOpen_;
            if (themeMenuOpen_) {
                settingsOpen_ = false;
            }
        }
    }
    rx -= 12.0f;
    // 运行 / 停止是**二选一**（webui Shell.jsx:44-48：`run.active ? 停止 : 运行`）。
    // 早先两个按钮常驻且「停止」恒 disabled，界面上等于挂了一个永远按不动的控件。
    ImFont* topFont = FontBoldAt(13.0f);
    const auto labelWidth = [&](std::string_view label) {
        return topFont->CalcTextSizeA(13.0f, 1e9f, 0.0f, label.data(),
                                      label.data() + label.size())
            .x;
    };
    if (runActive_) {
        const std::string_view stopLabel = "停止";
        const float stopW = ButtonWidth(ButtonSize::Medium, 15.0f, labelWidth(stopLabel));
        rx -= stopW;
        ButtonSpec stopSpec;
        stopSpec.variant = ButtonVariant::Secondary;
        stopSpec.icon = "stop";
        if (Button(draw, RectAt(rx, 0.5f * (area.min.y + area.max.y) - 15.0f, stopW, 30.0f),
                   stopLabel, stopSpec, "tb-stop")) {
            RequestRunStop();
        }
    } else {
        const std::string_view runLabel = "运行";
        const float runW = ButtonWidth(ButtonSize::Medium, 15.0f, labelWidth(runLabel));
        rx -= runW;
        ButtonSpec runSpec;
        runSpec.variant = ButtonVariant::Primary;
        runSpec.icon = "play";
        // 执行体未接入时**不禁用**按钮：点了会弹 toast 说明为什么跑不了，
        // 这比一个按不动的灰按钮有用 —— 用户能问出原因。
        // 总控页那侧才禁用（那里离原因更远，页面上直接写了禁用说明）。
        if (Button(draw, RectAt(rx, 0.5f * (area.min.y + area.max.y) - 15.0f, runW, 30.0f),
                   runLabel, runSpec, "tb-run")) {
            RequestRunStart();
        }
    }
}

// 运行 / 停止的执行体提成函数：顶栏按钮与 Ctrl+Enter 走**同一份**。
// 写成两处的话，迟早只改得动一边 —— 这正是「一个动作只存一份」那条纪律。
//
// ⚠️ 阶段执行体**未接入**，所以「运行」不做任何假装在跑的事。理由见
//    WorkspaceA.cpp 的 OverviewPipelineWired()：Runner 的执行体拿不到，真执行体在
//    novel/ 且签名与 StageExecutor 不同形状。
//    早先这里会置 `runActive_ = true; runStageIndex_ = 0; runPercent_ = 0`，于是状态栏
//    稳定输出「T1/17 · 0%」—— 一个**永远不会前进**的进度条。那三个字段全文件只有
//    「写 0」和「读出来显示」两处，结构上就不可能前进，不是「跑到一半卡住」。
//    界面看起来在跑、实际上什么都不会发生，这比显示「未接入」糟得多。
void Shell::RequestRunStart() {
    if (!pages::OverviewPipelineWired()) {
        PushLog("warn", "运行请求被拒绝：阶段执行体未接入");
        Notify(pages::StageExecutorMissingReason(), theme::Tone::Warn);
        return;
    }
    runActive_ = true;
    runFinished_ = false;
    runStageIndex_ = 0;
    runPercent_ = 0;
    PushLog("info", "流水线开始运行");
}

void Shell::RequestRunStop() {
    runActive_ = false;
    PushLog("warn", "流水线已请求停止");
}

// ---------------------------------------------------------------- P4.3 导航栏
void Shell::DrawRail(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.max.x - 0.5f, area.min.y), ImVec2(area.max.x - 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    // 6 个工作区（设计稿序），第 6 项后是分隔线 + 3 个面板开关 + 弹性空隙 + 画廊
    float y = area.min.y + 10.0f;
    for (int i = 0; i < 6; ++i) {
        const bool on = layout_.workspace == i;
        const Rect item{area.min.x, y, area.max.x, y + 48.0f};
        const ImVec2 center = ImVec2(area.center().x, item.center().y);
        if (on) {
            DrawRoundRect(draw, ImVec2(item.min.x + 6.0f, item.min.y + 2.0f),
                          ImVec2(item.max.x - 6.0f, item.max.y - 2.0f), 10.0f, ColorFillSelected());
            // 左侧 2.5px accent 竖条：left:-8px，上下内缩 9px，r3
            const Rect bar{area.min.x - 8.0f, item.min.y + 9.0f, area.min.x - 5.5f,
                           item.max.y - 9.0f};
            DrawRoundRect(draw, bar.min, bar.max, 3.0f, 0, ColorAccentGlow(), 6.0f);
            DrawRoundRect(draw, bar.min, bar.max, 3.0f, ColorAccent());
        }
        // ⚠️ 一次 HitTest 拿走 hovered + clicked。早先这里先 Hovered("rail-hover-N")
        // 再 Clicked("rail-ws-N") —— 两个 InvisibleButton 落在**同一矩形**上，
        // ImGui 的 ItemHoverable 只让先注册的那个拿到 HoveredId（HoveredAllowOverlap 默认 false），
        // 第二个永远 hovered=false / clicked=false，整个导航栏点不动。
        const Hit hit = ChromeHit(item, "rail-ws-" + std::to_string(i));
        DrawIconCentered(draw, WorkspaceIcon(i), center, 19.0f,
                         on ? ColorAccent() : (hit.hovered ? ColorText() : ColorTextMuted()));
        if (hit.clicked) {
            SetWorkspace(i);
        }
        y += 48.0f;
    }

    // 分隔线 24×1
    y += 4.0f;
    draw->AddLine(ImVec2(area.center().x - 12.0f, y), ImVec2(area.center().x + 12.0f, y),
                  ColorLineSubtle(), 1.0f);
    y += 8.0f;

    struct Toggle {
        const char* icon;
        bool* state;
        const char* tip;
    };
    const Toggle toggles[] = {
        {"panelL", &layout_.sidePanelVisible, "侧栏 Ctrl+B"},
        {"terminal", &layout_.dockVisible, "底栏 Ctrl+J"},
        {"panel", &layout_.inspectorVisible, "检查器 Ctrl+I"},
    };
    // 三个面板开关。⚠️ 设计稿这里**没有**常驻高亮：
    // webui Shell.jsx:100-108 给侧栏/检查器写的是 `panels.x ? '' : ' active'`，
    // 但整个 CSS 里只有 `.icon-btn.active`（ui.css:102），**根本没有 .rail-btn.active 规则**
    // —— 那个 active 类是死类，不产生任何视觉。早先这里画了一层常驻 fill-hover，
    // 等于凭空多出一个设计稿没有的常亮态；只保留 hover（shell.css 的 .rail-btn:hover）。
    for (const Toggle& toggle : toggles) {
        const Rect item{area.min.x, y, area.max.x, y + 44.0f};
        // ⚠️ 一帧里只 HitTest 一次。同一个 id 注册两个 InvisibleButton 会让 hover 整个
        //    失效（实测：hover-jump-btn 与静息态逐像素零差异）。hovered / clicked 从
        //    同一次命中测试里取。
        const kit::Hit hit = ChromeHit(item, "rail-tg-" + std::string(toggle.icon));
        if (hit.hovered) {
            DrawRoundRect(draw, ImVec2(item.min.x + 6.0f, item.min.y), ImVec2(item.max.x - 6.0f, item.max.y),
                          10.0f, ColorFillHover());
        }
        DrawIconCentered(draw, toggle.icon, ImVec2(area.center().x, item.center().y), 19.0f,
                         hit.hovered ? ColorText() : ColorTextMuted());
        if (hit.clicked) {
            *toggle.state = !*toggle.state;
        }
        y += 44.0f;
    }

    // 弹性空隙后固定「组件画廊」（hidden: true，只在这一栏出现）
    const Rect gallery{area.min.x, area.max.y - 58.0f, area.max.x, area.max.y - 10.0f};
    if (layout_.workspace == 6) {
        DrawRoundRect(draw, ImVec2(gallery.min.x + 6.0f, gallery.min.y + 2.0f),
                      ImVec2(gallery.max.x - 6.0f, gallery.max.y - 2.0f), 10.0f, ColorFillSelected());
    }
    DrawIconCentered(draw, "grid", ImVec2(area.center().x, gallery.center().y), 19.0f,
                     layout_.workspace == 6 ? ColorAccent() : ColorTextMuted());
    if (ChromeClicked(gallery, "rail-gallery")) {
        SetWorkspace(6);
    }
}

// ---------------------------------------------------------------- P4.4 侧栏
// ---------------------------------------------------------------- P4.5 侧栏
namespace {

// 侧栏树的「可见节点扁平序号」（先序）↔ (章下标, 镜下标) 的互转。
// kit::Tree 只认一个 int，且只画**展开**节点的子节点 —— 所以两处都按同一套先序走，
// 收起节点的子树不占序号。镜下标 = -1 表示命中的是章节点；命中卷节点时两个都置 -1。
//
// ⚠️ 树是**三层**（卷 → 章 → 镜，设计稿 Shell.jsx:583-662 的 NovelSideTree）。
//    而 `chapter` 是 `BookSideView::chapters` 里的**全局下标**，不是「某卷内的下标」——
//    所以必须用独立的 `seen` 计数器走遍**所有**卷，哪怕卷收起了（收起的卷不占扁平
//    序号，但它的章仍然存在于 chapters 里）。用「当前卷内下标」比就会在第二卷
//    之后整体错位：点第 5 章高亮第 1 章。
int FlatTreeIndex(const std::vector<TreeNode>& nodes, int chapter, int shot) {
    int i = 0;     // 可见节点的扁平序号
    int seen = 0;  // 已走过的章数（= chapters 的全局下标）
    for (const TreeNode& vol : nodes) {
        i++;  // 卷节点本身占一个号
        if (!vol.expanded) {
            seen += static_cast<int>(vol.children.size());
            continue;
        }
        for (std::size_t c = 0; c < vol.children.size(); ++c) {
            const TreeNode& chap = vol.children[c];
            const int at = i++;
            if (seen == chapter) {
                if (shot < 0) {
                    return at;
                }
                if (chap.expanded && shot < static_cast<int>(chap.children.size())) {
                    return at + 1 + shot;
                }
                return at;
            }
            ++seen;
            if (chap.expanded) {
                i += static_cast<int>(chap.children.size());
            }
        }
    }
    return -1;
}

void ResolveTreeIndex(const std::vector<TreeNode>& nodes, int index, int& chapterOut,
                      int& shotOut) {
    int i = 0;
    int seen = 0;
    for (const TreeNode& vol : nodes) {
        if (i++ == index) {
            // 命中卷节点：只切展开 / 收起，不选中任何章（与资产树的分组行同语义）。
            chapterOut = -1;
            shotOut = -1;
            return;
        }
        if (!vol.expanded) {
            seen += static_cast<int>(vol.children.size());
            continue;
        }
        for (std::size_t c = 0; c < vol.children.size(); ++c) {
            const TreeNode& chap = vol.children[c];
            if (i++ == index) {
                chapterOut = seen;
                shotOut = -1;
                return;
            }
            ++seen;
            if (!chap.expanded) {
                continue;
            }
            for (std::size_t k = 0; k < chap.children.size(); ++k) {
                if (i++ == index) {
                    chapterOut = seen - 1;  // seen 在上面已经为这一章 +1 过
                    shotOut = static_cast<int>(k);
                    return;
                }
            }
        }
    }
    chapterOut = -1;
    shotOut = -1;
}

// 镜码 S001 —— 实现与注释都在 WorkspacePages.h（Shell / 故事板 / 检查器共用一份）。
using pages::ShotCode;

// 点中的是分组节点（设计稿的分组行只切展开 / 收起，不选中任何资产：
// Shell.jsx:554 的 onClick 只 setOpen，没有 setSelEntity）。返回它在 nodes 里的下标。
int AssetGroupAt(const std::vector<TreeNode>& nodes, int flat) {
    int i = 0;
    for (std::size_t g = 0; g < nodes.size(); ++g) {
        if (i++ == flat) {
            return static_cast<int>(g);
        }
        if (!nodes[g].expanded) {
            continue;
        }
        i += static_cast<int>(nodes[g].children.size());
    }
    return -1;
}

// 资产 kind 树的两向展平（与上面章 / 镜树同一套先序约定）。
//
// leafAsset 是**叶子序号**（只数叶子，不数分组）→ BookSideView::assets 下标。
// 返回值是 kit::Tree 用的**可见节点扁平序号**（分组节点也占号），所以两个数不是一回事：
// 传错一个就是「点 A 高亮 B」。命中分组节点返回 -1（设计稿的 .node.on 只加在叶子上，
// 分组节点只有展开 / 收起，没有选中态）。
int AssetLeafFlat(const std::vector<TreeNode>& nodes, const std::vector<int>& leafAsset,
                  int assetIndex) {
    int flat = 0;
    std::size_t leaf = 0;
    for (const TreeNode& group : nodes) {
        const int at = flat++;
        if (!group.expanded) {
            continue;
        }
        for (std::size_t k = 0; k < group.children.size(); ++k, ++flat, ++leaf) {
            if (assetIndex >= 0 && leaf < leafAsset.size() && leafAsset[leaf] == assetIndex) {
                return at + 1 + static_cast<int>(k);
            }
        }
    }
    return -1;
}

int AssetLeafAt(const std::vector<TreeNode>& nodes, const std::vector<int>& leafAsset, int flat) {
    int i = 0;
    std::size_t leaf = 0;
    for (const TreeNode& group : nodes) {
        if (i++ == flat) {
            return -1;  // 命中分组节点
        }
        if (!group.expanded) {
            continue;
        }
        for (std::size_t k = 0; k < group.children.size(); ++k) {
            if (i++ == flat) {
                return leaf < leafAsset.size() ? leafAsset[leaf] : -1;
            }
            ++leaf;
        }
    }
    return -1;
}

// 库里是空串时给「—」，不留空白格（KeyValues 的值列空着看不出是"没值"还是"漏了"）。
std::string OrDash(const std::string& value) { return value.empty() ? "—" : value; }

// 设计稿的侧栏树分两种：小说 / 分镜 / 出图 / 出片是「章 → 镜」树（ShotSideTree），
// 资产是「kind 筛选 + 分组树」（AssetsSideTree，Shell.jsx:519-580）。
// 总控与项目中心没有侧栏树。
bool WorkspaceHasSideTree(int workspace) {
    return workspace == static_cast<int>(Workspace::Novel) ||
           workspace == static_cast<int>(Workspace::Storyboard) ||
           workspace == static_cast<int>(Workspace::ImageFlow) ||
           workspace == static_cast<int>(Workspace::VideoFlow);
}

bool WorkspaceHasAssetTree(int workspace) {
    return workspace == static_cast<int>(Workspace::Assets);
}

// 资产 kind 分组的展开态。存的是「被收起的」（设计稿默认全展开），key 用**原始 kind**
// 英文值 —— AssetKindLabel 把 item / prop / treasure 都映成「物品」，用标签当 key
// 的话收起一个会连着收起三个。
bool KindCollapsed(const std::vector<std::string>& collapsed, const std::string& kind) {
    return std::find(collapsed.begin(), collapsed.end(), kind) != collapsed.end();
}

} // namespace

// 小说侧栏的「快速跳转」按钮组（Shell.jsx:605-624）。
//
// 2×2 grid gap 6，四个按钮：资产 / 分镜 / 出图 / 出片。
// 视觉走 .jump-btn（shell.css:640-648）：h24 r6 1px accent-glow 边 + accent-dim 底 +
// accent 字；hover 底换 jumpBtnBg（accent 22%）。
// ⚠️ ButtonVariant 只有 Primary/Secondary/Ghost/Danger，**没有** jump 这一档 —— 所以
//    这里自绘，不去硬凑一个 Ghost 变体（那会把「强调跳转」画成普通幽灵按钮）。
//    jumpBtnBg 是主题加载时算好的派生色（Theme.cpp），直接取。
void Shell::DrawJumpButtons(Rect bounds, ImDrawList* draw) {
    struct Jump {
        const char* icon;
        const char* label;
        Workspace target;
    };
    static constexpr char kLabel[] = "快速跳转";
    draw->AddText(FontBoldAt(11.0f), 11.0f, ImVec2(bounds.min.x + 8.0f, bounds.min.y),
                  ColorTextMuted(), kLabel, kLabel + sizeof(kLabel) - 1);

    const Jump jumps[] = {{"masks", "资产", Workspace::Assets},
                          {"clapper", "分镜", Workspace::Storyboard},
                          {"image", "出图", Workspace::ImageFlow},
                          {"film", "出片", Workspace::VideoFlow}};
    const float top = bounds.min.y + 18.0f;
    const float gap = 6.0f;
    const float cellW = (bounds.width() - gap) * 0.5f;
    const float cellH = 24.0f;  // .btn.sm

    for (int i = 0; i < 4; ++i) {
        const int column = i % 2;
        const int row = i / 2;
        const float left = bounds.min.x + (cellW + gap) * static_cast<float>(column);
        const float boxTop = top + (cellH + gap) * static_cast<float>(row);
        const Rect box{left, boxTop, left + cellW, boxTop + cellH};
        const std::string hitId = "jump-" + std::to_string(i);
        // ⚠️ 一帧里只能 HitTest 一次。以前这里先 `HitTest(...).hovered` 画完再
        //    `Clicked(...)`，等于用**同一个 id** 注册了两个 InvisibleButton。实测后果：
        //    悬停态压根不亮（`hover-jump-btn` 与静息态逐像素零差异），按钮只是
        //    "点得到但看不出按下"。hovered / clicked 从同一次命中测试里取。
        const kit::Hit hit = ChromeHit(box, hitId);
        DrawRoundRect(draw, box.min, box.max, 6.0f,
                      hit.hovered ? ColorOf(theme::CurrentDerived().jumpBtnBg) : ColorAccentDim(),
                      ColorAccentGlow(), 1.0f);

        ImFont* font = FontBoldAt(12.0f);
        const char* label = jumps[i].label;
        const float text =
            font->CalcTextSizeA(12.0f, 1e9f, 0.0f, label, label + std::strlen(label)).x;
        const float iconSize = 13.0f;
        // 图标与文字 gap 4（.btn.sm 的 gap），两者合计在格子里居中。
        const float left2 = box.min.x + (cellW - (iconSize + 4.0f + text)) * 0.5f;
        const float cy = box.center().y;
        DrawIcon(draw, jumps[i].icon, ImVec2(left2, cy - iconSize * 0.5f), iconSize, ColorAccent());
        draw->AddText(font, 12.0f, ImVec2(left2 + iconSize + 4.0f, cy - 6.0f), ColorAccent(), label,
                      label + std::strlen(label));

        if (hit.clicked) {
            // 设计稿就是 setWorkspace(ws) + notify(一句话)，**不带 tab、不带选中项**：
            // 章节上下文靠全局选中态自然带过去（Shell.jsx:619）。
            const BookSideView& book = BookSide();
            std::string context = "未选章节";
            if (book.selectedChapter >= 0 &&
                book.selectedChapter < static_cast<int>(book.chapters.size())) {
                const BookChapterView& chapter =
                    book.chapters[static_cast<std::size_t>(book.selectedChapter)];
                context = "第 " + std::to_string(chapter.ord) + " 章";
                if (!chapter.title.empty()) {
                    context += "「" + chapter.title + "」";
                }
            }
            SetWorkspace(static_cast<int>(jumps[i].target));
            Notify(std::string(label) + " · 上下文：" + context, theme::Tone::Ok);
        }
    }
}

void Shell::SelectAsset(int index) {
    const BookSideView& book = BookSide();
    if (index < 0 || index >= static_cast<int>(book.assets.size())) {
        return;
    }
    SelectBookAsset(index);
    assets_.setSelected(index);
    // 设计稿点侧栏叶子会退回详情态（Assets.jsx:151 的 setOverview(false)）——
    // 停在总览网格上点侧栏、主体没反应，看起来像侧栏高亮是假的。
    assets_.setOverview(false);
}

void Shell::SetInspectorSection(int index, bool open) {
    if (index >= 0 && index < 3) {
        sectionOpen_[index] = open;
    }
}

void Shell::Notify(std::string text, theme::Tone tone) {
    toastText_ = std::move(text);
    toastTone_ = tone;
    toastTimer_ = 3.2f;
}

// 资产侧栏：kind 筛选 chip 行 + 「kind 分组 → 实体」两层树（Shell.jsx:519-580）。
//
// ⚠️ 设计稿的 chip 是写死的 5 个候选（`['全部','人物','地点','物品','势力']`，Shell.jsx:522），
//    而分组名是**从数据里推**的（Shell.jsx:523-525）。真实工程的 entities.kind 是
//    `namespace kind` 里的 30 个英文值之一，库里无 CHECK 约束 —— 照抄那 5 个 chip 会让
//    库里 95% 的 kind 根本筛不出来。所以 chip 同样从数据推：全部 + 当前实际出现的 kind。
void Shell::DrawAssetSideTree(Rect area, ImDrawList* draw, float y) {
    const BookSideView& book = BookSide();
    const float x = area.min.x + 10.0f;
    const float w = area.max.x - 20.0f - x;

    // ---- 标题行：masks 图标 + 视觉资产 + {ready}/{total} 就绪 ----
    // 设计稿是 space-between（Shell.jsx:530），所以就绪数**右对齐**到行尾；
    // 画在 x 上会跟图标和标题叠在一起（侧栏只有 240px，叠了就是一团糊）。
    DrawIcon(draw, "masks", ImVec2(x, y), 14.0f, ColorAccent());
    static constexpr char kTitle[] = "视觉资产";
    draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(x + 20.0f, y), ColorText(), kTitle,
                  kTitle + sizeof(kTitle) - 1);
    int ready = 0;
    for (const BookAssetView& asset : book.assets) {
        if (asset.tone == theme::Tone::Ok) {
            ++ready;
        }
    }
    const std::string readyText = std::to_string(ready) + "/" +
                                  std::to_string(static_cast<int>(book.assets.size())) + " 就绪";
    const float readyW = MeasureClipped(FontAt(11.0f), 11.0f, w, readyText);
    DrawTextClipped(draw, FontAt(11.0f), 11.0f, ImVec2(x + w - readyW, y), w, ColorTextMuted(),
                    readyText);
    y += 20.0f;

    // ---- 进度条（.prog.thin 4px）----
    const float total = static_cast<float>(book.assets.size());
    Progress(draw, Rect{x, y, x + w, y + ProgressHeight(true)},
             total > 0.0f ? static_cast<float>(ready) / total : 0.0f, false, true);
    y += ProgressHeight(true) + 10.0f;

    // ---- chip 行：全部 + 实际出现的 kind（分组顺序 = 首次出现的顺序，与 ListEntities
    //      的 ORDER BY id 一致，不另排字母序）----
    std::vector<std::string> allKinds;
    for (const BookAssetView& asset : book.assets) {
        if (std::find(allKinds.begin(), allKinds.end(), asset.kind) == allKinds.end()) {
            allKinds.push_back(asset.kind);
        }
    }
    const std::string& filter = BookKindFilter();
    {
        float cx = x;
        const float cy = y;
        const float ch = ChipHeight(true);
        ChipSpec all{true, true, ""};
        if (Chip(draw, Rect{cx, cy, cx + ChipWidth("全部", all), cy + ch}, "全部", all, "chip-all")) {
            SetBookKindFilter({});
        }
        cx += ChipWidth("全部", all) + 6.0f;
        for (const std::string& kind : allKinds) {
            const std::string label = AssetKindLabel(kind);
            const int count = static_cast<int>(std::count_if(
                book.assets.begin(), book.assets.end(),
                [&kind](const BookAssetView& a) { return a.kind == kind; }));
            ChipSpec spec{filter == kind, true, std::to_string(count)};
            const float cw = ChipWidth(label, spec);
            // chip 行会换行（.chips 是 flex-wrap），放不下就折到下一行，别画出侧栏。
            if (cx + cw > x + w) {
                cx = x;
                y += ch + 6.0f;
            }
            if (Chip(draw, Rect{cx, y, cx + cw, y + ch}, label, spec, "chip-" + kind)) {
                SetBookKindFilter(filter == kind ? std::string{} : kind);
            }
            cx += cw + 6.0f;
        }
        y += ch + 10.0f;
    }

    // ---- 树：kind 分组 → 实体叶子 ----
    std::vector<TreeNode> nodes;
    std::vector<int> leafAsset;     // 叶子序号 → BookSideView::assets 下标
    std::vector<std::string> kinds;  // 分组序号 → **原始 kind 英文值**
    // ⚠️ 展开态的 key 必须用原始 kind，不能用分组标签：AssetKindLabel 把 item / prop /
    //    treasure 都映成「物品」，用标签当 key 的话点开一个会连着开三个。
    for (const std::string& kind : allKinds) {
        if (!filter.empty() && kind != filter) {
            continue;
        }
        TreeNode group;
        group.label = AssetKindLabel(kind);
        group.icon = "layers";
        int count = 0;
        for (const BookAssetView& asset : book.assets) {
            if (asset.kind != kind) {
                continue;
            }
            ++count;
            group.hasChildren = true;
            group.children.push_back(TreeNode{asset.name.empty() ? "—" : asset.name, {}, {}, false,
                                              false, {}});
            leafAsset.push_back(asset.index);
        }
        group.trailing = std::to_string(count);
        group.expanded = !KindCollapsed(collapsedKinds_, kind);
        nodes.push_back(std::move(group));
        kinds.push_back(kind);
    }

    if (nodes.empty()) {
        Empty(draw, Rect{x, y, x + w, y + 76.0f}, "masks", "没有这一类资产",
              "换回「全部」看看这个工程里有哪些实体");
        y += 84.0f;
    } else {
        // 可见节点扁平序号（先序）↔ 资产下标。kit::Tree 只认一个 int，且收起的子树
        // 不占序号 —— 与上面章 / 镜树同一套先序约定，这里是它的资产版。
        const int wanted = AssetLeafFlat(nodes, leafAsset, book.selectedAsset);
        int picked = wanted;
        y += Tree(draw, Rect{x, y, x + w, area.max.y - 34.0f}, nodes, picked, "asset-tree");
        if (picked != wanted) {
            // 分组行只切展开 / 收起（设计稿的 onClick 只有 setOpen，没有 setSelEntity）；
            // 叶子行才选资产。展开态存成员上，存局部变量的话下一帧就收回。
            const int group = AssetGroupAt(nodes, picked);
            if (group >= 0) {
                const std::string& key = kinds[static_cast<std::size_t>(group)];
                const auto at = std::find(collapsedKinds_.begin(), collapsedKinds_.end(), key);
                if (at == collapsedKinds_.end()) {
                    collapsedKinds_.push_back(key);
                } else {
                    collapsedKinds_.erase(at);
                }
                return;
            }
            const int index = AssetLeafAt(nodes, leafAsset, picked);
            if (index >= 0) {
                SelectAsset(index);
            }
        }
    }
}

void Shell::DrawSidePanel(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.max.x - 0.5f, area.min.y), ImVec2(area.max.x - 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    // 折叠把手 20×48 贴在右缘中点
    const Rect handle{area.max.x - 20.0f, area.center().y - 24.0f, area.max.x, area.center().y + 24.0f};
    DrawRoundRect(draw, handle.min, handle.max, 6.0f, ColorPanel(), ColorLineNormal(), 1.0f);
    DrawIconCentered(draw, "chevron", handle.center(), 12.0f, ColorTextMuted());
    if (ChromeClicked(handle, "side-collapse")) {
        ToggleSidePanel();
    }

    const float x = area.min.x + 10.0f;
    float y = area.min.y + 10.0f;
    const std::string root = layout_.projectName.empty() ? "项目" : layout_.projectName;
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(x + 14.0f, y), ColorTextMuted(), root.data(),
                  root.data() + root.size());
    y += 22.0f;

    const char* scope = WorkspaceLabel(layout_.workspace);
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(x + 6.0f, y), ColorTextSecondary(), scope,
                  scope + std::strlen(scope));
    y += 26.0f;

    if (layout_.projectRoot.empty()) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "未打开项目",
              "从顶栏项目胶囊回项目中心新建或打开，这里会列出真实的章节 / 镜头 / 资产");
        return;
    }
    if (!WorkspaceHasSideTree(layout_.workspace) && !WorkspaceHasAssetTree(layout_.workspace)) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "此工作区没有侧栏树",
              "总控是 KPI 面板；项目中心是整屏，不套侧栏");
        return;
    }

    // 小说侧栏的「快速跳转」2×2 按钮组（Shell.jsx:605-624）。设计稿只挂在小说工作区，
    // 位置在章节进度条之下、章节树之上。点击 = setWorkspace(ws) + 一条 toast，
    // 不带 tab、不带选中项 —— 章节上下文靠全局选中态自然带过去（与设计稿同语义）。
    //
    // ⚠️ 必须排在下面所有空态 `return` **之前**。原来它排在
    // 「这本小说还没有章」那个空态之后，于是没章节 / 没绑定 / 读取中 / 读取失败
    // 四种状态下这四个按钮**整组不画** —— 而「快速跳转」与「有没有章节」毫无
    // 关系：它跳的是资产 / 分镜 / 出图 / 出片四个工作区。
    //
    //    症状极其隐蔽：界面看着正常（有个空态提示），而 `hover-jump-btn` 探针
    //    报「一个控件都没命中」—— 判据说是「探针坐标偏了，别改产品」，于是
    //    去挪坐标，挪到哪都不对。用 `SHINE_SCAN` 把侧栏整片扫一遍才发现
    //    `jump-*` 这个 id 在那个工作区**压根不存在**。
    //    **「点空了」有三种原因，凭日志分不出来，只能把热区扫出来看。**
    if (layout_.workspace == static_cast<int>(Workspace::Novel)) {
        DrawJumpButtons(Rect{x, y, area.max.x - 20.0f, y + 54.0f}, draw);
        y += 64.0f;
    }

    const BookSideView& book = BookSide();
    if (!book.bound) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "结构尚未载入",
              "切到小说 / 资产任一页会读取 novel.db 里的真实结构");
        return;
    }
    if (book.loading) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "正在读取 novel.db",
              "库打开后会在这里列出章节与镜头");
        return;
    }
    if (!book.error.empty()) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "读取失败", book.error);
        return;
    }

    // 资产工作区走 kind 筛选树（Shell.jsx:519-580），不吃章 / 镜那一套。
    if (WorkspaceHasAssetTree(layout_.workspace)) {
        if (book.assets.empty()) {
            Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "masks", "这个工程还没有实体",
                  "跑一次初始化链（T1–T17）之后这里才有实体与资产");
            return;
        }
        DrawAssetSideTree(area, draw, y);
        return;
    }

    if (book.chapters.empty()) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "这本小说还没有章",
              "跑一次 T1–T17 之后这里才有章节与镜头");
        return;
    }

    // ⚠️ 原来这里又画了一遍跳转按钮组 —— 与上面那段重复。同一个 id 注册两次
    //    InvisibleButton = 第二个永远 clicked=false（ImGui 先注册者独占）。
    //    整段删掉，唯一实现是上面那处（在所有空态 return 之前）。

    // 设计稿的 NovelSideTree 是「书 / 卷 / 章 / 镜」三层 + 叶（Shell.jsx:583-662）。
    //
    // ⚠️ 早先这里注释写着「卷这一层要 novelcore::NovelGraph::ListVolumes()，而它不存在，
    //    补它要动 shine_core，所以画两层」—— **那个归因是错的**。卷根本不需要新接口：
    //    `ChapterRow::volume_id` 早就有（`src/novel/NovelTypes.h:115`，`ListChapters` 的
    //    SELECT 也带了它），`volumes` 表也一直在。缺的只是 UI 侧把这两列读出来。
    //    一个被记成「要改业务层」的缺口，其实是**两行没搬的字段**。
    //
    // 卷默认全展开：多一层点击才能看到章，与设计稿的意图不符；展开态暂不持久化。
    //
    // 镜只挂在**当前选中章**下面：场/镜是对选中章取的，没选过的章没有镜数据，
    // 画一排空节点等于骗人。
    const bool withShots = layout_.workspace != static_cast<int>(Workspace::Novel);
    // 先按 volumeId 归拢；volumeId == 0 或查不到卷名的一律归到「未归卷」——
    // 老工程的 volumes 表可能是空的，那时如实显示「未归卷」而不是编一个卷名。
    std::vector<TreeNode> nodes;
    std::vector<std::pair<int, std::string>> volumeOrder;  // volumeId → 标题（保序、去重）
    std::vector<int> chapterBucket(book.chapters.size(), 0);  // 章下标 → 哪个卷桶
    for (std::size_t i = 0; i < book.chapters.size(); ++i) {
        const BookChapterView& chapter = book.chapters[i];
        const int key = chapter.volumeId;
        const std::string title =
            chapter.volumeTitle.empty() ? std::string("未归卷") : chapter.volumeTitle;
        int bucket = -1;
        for (std::size_t k = 0; k < volumeOrder.size(); ++k) {
            if (volumeOrder[k].first == key) {
                bucket = static_cast<int>(k);
                break;
            }
        }
        if (bucket < 0) {
            bucket = static_cast<int>(volumeOrder.size());
            volumeOrder.emplace_back(key, title);
        }
        chapterBucket[i] = bucket;

        TreeNode chap;
        chap.label = "第 " + std::to_string(chapter.ord) + " 章" +
                     (chapter.title.empty() ? "" : " · " + chapter.title);
        chap.icon = "book";
        chap.trailing = OrDash(chapter.status);
        chap.expanded = withShots && static_cast<int>(i) == book.selectedChapter;
        chap.hasChildren = chap.expanded;
        if (chap.expanded) {
            for (const BookShotView& shot : book.shots) {
                TreeNode leaf;
                leaf.label = ShotCode(shot.ord) + (shot.action.empty() ? "" : " · " + shot.action);
                leaf.icon = "clapper";
                chap.children.push_back(std::move(leaf));
            }
        }
        nodes.push_back(std::move(chap));
    }
    // 把章按卷重新装桶：卷 → 章 → 镜。
    std::vector<TreeNode> volumes;
    volumes.reserve(volumeOrder.size());
    for (std::size_t k = 0; k < volumeOrder.size(); ++k) {
        TreeNode vol;
        vol.label = volumeOrder[k].second;
        vol.icon = "layers";
        for (std::size_t i = 0; i < book.chapters.size(); ++i) {
            if (chapterBucket[i] != static_cast<int>(k)) {
                continue;
            }
            vol.children.push_back(std::move(nodes[i]));
        }
        vol.trailing = std::to_string(vol.children.size());
        vol.hasChildren = !vol.children.empty();
        vol.expanded =
            std::find(collapsedVolumes_.begin(), collapsedVolumes_.end(), volumeOrder[k].first) ==
            collapsedVolumes_.end();
        volumes.push_back(std::move(vol));
    }

    // ⚠️ 先序遍历里**顶层节点总是前 N 个**（它们的子节点排在它们后面），所以扁平序号
    //    就等于在 volumes 里的下标 —— 命中卷节点不必再遍历一遍。
    const auto wanted = withShots
                            ? FlatTreeIndex(volumes, book.selectedChapter, book.selectedShot)
                            : FlatTreeIndex(volumes, book.selectedChapter, -1);
    int picked = wanted;
    const Rect treeArea{x, y, area.max.x - 20.0f, area.max.y - 10.0f};
    Tree(draw, treeArea, volumes, picked, "side-tree");
    if (picked == wanted) {
        return;  // 没点中
    }
    if (picked >= 0 && picked < static_cast<int>(volumes.size())) {
        // 卷行只切展开 / 收起，不选中任何章（与资产树的 kind 分组行同语义，
        // 设计稿的卷行 onClick 也只有 setOpen）。
        const int key = volumeOrder[static_cast<std::size_t>(picked)].first;
        const auto at = std::find(collapsedVolumes_.begin(), collapsedVolumes_.end(), key);
        if (at == collapsedVolumes_.end()) {
            collapsedVolumes_.push_back(key);
        } else {
            collapsedVolumes_.erase(at);
        }
        return;
    }
    int chapterIndex = -1;
    int shotIndex = -1;
    ResolveTreeIndex(volumes, picked, chapterIndex, shotIndex);
    if (chapterIndex < 0) {
        return;
    }
    if (shotIndex < 0) {
        SelectBookChapter(chapterIndex);
    } else {
        SelectBookShot(shotIndex);  // 只改下标，不重取（同一章的镜已经在快照里）
    }
}

// ---------------------------------------------------------------- P4.5 检查器
// `.inspector .sect { border-bottom: 1px solid var(--line-subtle) }`
// （shell.css:343-345）—— 分隔线在**整段底部**，不在段头正下方。
//
// ⚠️ 原实现把它画在 `header.max.y`，于是每段都多出一条设计稿里不存在的线，
//    而段底（`.sect-b` 的 padding-bottom 之后）反而没有线。展开段和折叠段
//    的收尾都要用到，抽出来免得两处各写一遍又走样。
void DrawSectionDivider(const Rect& area, float y, ImDrawList* draw) {
    // 满宽：`.sect` 是 `.inspector` 的直接子元素，没有左右内边距。
    draw->AddLine(ImVec2(area.min.x, y), ImVec2(area.max.x, y), ColorLineSubtle(), 1.0f);
}

void Shell::DrawInspector(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.min.x + 0.5f, area.min.y), ImVec2(area.min.x + 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    // 左右内边距 = 14px。段头 `.sect-h { padding:10px 14px }` 与段内
    // `.sect-b { padding:2px 14px 14px }` **同宽**，所以段头文字与段内正文左对齐
    // （原来的 16px 也是对齐的，只是整体宽了 2px）。
    constexpr float kSectPadX = 14.0f;
    const float x = area.min.x + kSectPadX;
    const float w = area.width() - kSectPadX * 2.0f;
    float y = area.min.y + 12.0f;

    // 3 段可折叠：属性 / 预览 / 关联。展开态在 sectionOpen_（成员）上。
    //
    // ⚠️ 早先这里是 `const Section sections[] = {{"属性",true},...}` —— 每帧新建的
    //    **const 局部数组**，段头画了折叠箭头却没有任何东西写回展开态，也没有点击处理。
    //    于是「关联」段永远打不开，箭头纯装饰。默认初值照设计稿（Shell.jsx:146
    //    的 {a:true, b:true, c:false}），但这次是真能点的。
    static constexpr char kSectionTitles[3][8] = {"属性", "预览", "关联"};
    const BookSideView& book = BookSide();

    // ⚠️ 段头几何照 shell.css:346-357 的 `.inspector .sect-h` 重算过：
    //
    //   display:flex; align-items:center; gap:7px; padding:10px 14px;
    //   font-size:12px; font-weight:700; color:var(--text-secondary)
    //
    // 段头高 = 10 + 行盒 + 10，行盒 = font-size × line-height = 12 × **1.6** = 19.2
    // ⇒ **39.2px**。（`base.css:16` 的 `line-height:1.6` 来自 `body`，经
    // `base.css:23-29` 的 `button { font: inherit }` 带进这个 `<button>`。）
    //
    // 原实现画的是 24px，而且把文字**钉死**在 `header.min.y + 5.0f`。
    // ⚠️ 更正一条我自己写错的注释：这里曾经写着「墨迹中心在 y+5.8、偏上 6.2px」。
    //    那是**错的** —— 按行盒重算：文字 y = min.y+5、字号 12.5 ⇒ 行盒中心
    //    `min.y + 5 + 6.25 = min.y + 11.25`，框中心 `min.y + 12`，只偏 **0.75px**，
    //    落在已接受的光学偏差带里。真正的毛病是**整段高度差 15.2px**（24 vs 39.2），
    //    三段加起来检查器比设计稿短了 45.6px —— 是布局短，不是字没居中。
    //    「字没居中」量级最大的几处在别处（kit::Chip 低 6px、队列行高 6px），
    //    记在 refactor/PROGRESS.md 的「文字垂直居中」一节。
    //
    // 还有三处顺带订正：
    //   · 颜色：设计是 `text-secondary`，原来用的是 `ColorText()`（primary）。
    //   · 箭头：Shell.jsx:150 是 11×11，原来写 10。
    //   · hover：设计 `.sect-h:hover` **只改文字颜色**（`text-primary`），
    //     没有背景色；原来给整条段头铺了 `ColorFillHover()`。
    //     hover 探针仍会变色（文字），判据不受影响。
    //
    // 段头内边距也是 14px，与 `.sect-b`（`padding: 2px 14px 14px`）一致 ⇒
    // 段头文字与段内正文左对齐。原先两处都是 16px（对齐是对的，只是整体宽了 2px）。
    constexpr float kSectHeadH = 39.2f;
    constexpr float kSectIconSize = 11.0f;
    constexpr float kSectGap = 7.0f;
    constexpr float kSectBodyPadTop = 2.0f;     // .sect-b padding-top
    constexpr float kSectBodyPadBottom = 14.0f; // .sect-b padding-bottom
    constexpr float kSectFont = 12.0f;           // 写 12.5f 会被 LookupNearest 顶到 13px

    for (int s = 0; s < 3; ++s) {
        const char* title = kSectionTitles[s];
        const bool open = sectionOpen_[s];
        const Rect header{x, y, x + w, y + kSectHeadH};
        const std::string headId = "inspector-head-" + std::to_string(s);
        // 同上：一帧里只 HitTest 一次，双注册会让 hover 失效。
        const kit::Hit headHit = ChromeHit(header, headId);
        // hover 底色**故意不画**：设计稿这一条只有 `:hover { color: text-primary }`。
        const ImU32 headFg = headHit.hovered ? ColorText() : ColorTextSecondary();
        // align-items:center ⇒ 图标中心与文字行盒中心同一水平线。
        DrawIcon(draw, open ? "chevdown" : "chevron",
                 ImVec2(x, header.center().y - kSectIconSize * 0.5f), kSectIconSize,
                 headHit.hovered ? ColorText() : ColorTextMuted());
        draw->AddText(FontBoldAt(kSectFont), kSectFont,
                      ImVec2(x + kSectIconSize + kSectGap,
                             kit::CenterTextY(FontBoldAt(kSectFont), kSectFont, header.center().y)),
                      headFg, title, title + std::strlen(title));
        y += kSectHeadH;
        if (headHit.clicked) {
            sectionOpen_[s] = !open;
        }
        if (!sectionOpen_[s]) {
            // 折叠：Shell.jsx:153 只在 open 时才渲染 .sect-b ⇒ 段头下面直接就是分隔线。
            DrawSectionDivider(area, y, draw);
            y += 1.0f;
            continue;
        }
        y += kSectBodyPadTop;
        if (s == 0) {
            // ⚠️ 这里原先写死 {代码:S012, 动作:转身, 时长:6.0s, 情绪:克制} —— 一组
            //    编出来的镜头属性，在任何工程、任何项目下都长这样，点了也不跟着选中项变。
            // 现在读侧栏那份只读快照：选中了镜就给镜的字段，只选了章就给章的字段，
            // 两者都没有才给空态。空串一律显示「—」，不靠留白表示"没值"。
            //
            // 资产工作区看**实体**：上一段选中的镜还留在快照里，不按工作区分流的话，
            // 在资产页点实体、右侧却还显示某个镜的属性 —— 两边说的不是一回事。
            const bool assetWorkspace = WorkspaceHasAssetTree(layout_.workspace);
            const bool hasAssetRow =
                assetWorkspace && book.selectedAsset >= 0 &&
                book.selectedAsset < static_cast<int>(book.assets.size());
            const bool hasShot = !assetWorkspace && !book.shots.empty() && book.selectedShot >= 0 &&
                                 book.selectedShot < static_cast<int>(book.shots.size());
            const bool hasChapter = !assetWorkspace && book.selectedChapter >= 0 &&
                                    book.selectedChapter < static_cast<int>(book.chapters.size());
            if (!book.bound || (!hasShot && !hasChapter && !hasAssetRow)) {
                Empty(draw, Rect{x, y, x + w, y + 76.0f}, "target",
                      layout_.projectRoot.empty() ? "未打开工程" : "未选中条目",
                      layout_.projectRoot.empty()
                          ? "打开工程并选中一个条目后，这里显示它的真实属性"
                          : (assetWorkspace ? "在左侧侧栏树里选中一个实体后，这里显示它的真实属性"
                                            : "在左侧侧栏树里选中章节 / 镜头后，这里显示它的真实属性"));
                y += 84.0f;
            } else if (hasAssetRow) {
                const BookAssetView& row = book.assets[static_cast<std::size_t>(book.selectedAsset)];
                const std::string layers =
                    row.layers > 0 ? (std::to_string(row.layersDone) + " / " +
                                      std::to_string(row.layers) + " 层就绪")
                                   : std::string("—");
                KeyValues(draw, Rect{x, y, x + w, y + 176.0f},
                          {{"名称", OrDash(row.name)},
                           {"类别", OrDash(AssetKindLabel(row.kind))},
                           {"实体 ID", "#" + std::to_string(row.entityId)},
                           {"摘要", OrDash(row.summary)},
                           {"视觉资产", row.hasAsset ? "已建立" : "尚未建立"},
                           {"生产状态", row.hasAsset ? row.statusLabel : "—"},
                           {"形象层", layers},
                           {"降级产物", row.degraded ? "有" : "无"}});
                y += 184.0f;
            } else if (hasShot) {
                const BookShotView& shot = book.shots[static_cast<std::size_t>(book.selectedShot)];
                std::string duration = shot.durationNote;
                if (duration.empty() && shot.durationSec > 0.0) {
                    // ⚠️ 保留一位小数，和故事板页的 `%.1fs` 同口径。
                    //    这里原来取整成 "5s"，同一个镜在两个面板上是两个时长。
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%.1fs", shot.durationSec);
                    duration = buf;
                }
                KeyValues(draw, Rect{x, y, x + w, y + 176.0f},
                          {{"镜码", ShotCode(shot.ord)},
                           {"场", shot.sceneOrd > 0 ? "第 " + std::to_string(shot.sceneOrd) + " 场" : "—"},
                           {"动作", OrDash(shot.action)},
                           {"表情", OrDash(shot.expression)},
                           {"情绪", OrDash(shot.mood)},
                           {"时长", OrDash(duration)},
                           {"台词", OrDash(shot.dialogue)},
                           {"旁白", OrDash(shot.narration)},
                           {"连贯性", OrDash(shot.canonStatus)}});
                y += 184.0f;
            } else {
                const BookChapterView& chapter =
                    book.chapters[static_cast<std::size_t>(book.selectedChapter)];
                KeyValues(draw, Rect{x, y, x + w, y + 84.0f},
                          {{"章序", "第 " + std::to_string(chapter.ord) + " 章"},
                           {"标题", OrDash(chapter.title)},
                           {"状态", OrDash(chapter.status)},
                           {"字数", chapter.words > 0 ? std::to_string(chapter.words) : "—"}});
                y += 92.0f;
            }
        } else if (s == 1) {
            Art(draw, Rect{x, y, x + w, y + 110.0f}, 5, true);
            // 上面那块是设计稿自带的示意插画（Art()），不是这个工程的出图结果。
            // 不写这句，读者会以为它是该镜的真实渲染。
            DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(x, y + 96.0f), w, ColorTextMuted(),
                            "示意插画 · 非本工程出图结果");
            y += 118.0f;
        } else {
            // ---- 关联段（Shell.jsx:173-179）----
            //
            // 设计稿是一行平铺三个 Tag（伏笔 #3 / 场景 12 / 镜 S05），**内联字面量**：
            // 没有数据结构、不可点、也不分组。所以这里不造一个「关联列表」出来 ——
            // 形态照抄（一行 .tag.sm，gap 8，可换行），内容换成真数据。
            //
            // 取不到的组**不出 tag**。三组同时空时给一行说明，而不是摆三个占位 tag：
            // 「伏笔 #3」这种假 tag 比没有更糟，它会让人以为库里真有这条伏笔。
            const BookRelationView& relation = book.relation;
            std::vector<std::string> tags;
            std::vector<theme::Tone> tones;
            for (const std::string& title : relation.foreshadows) {
                tags.push_back("伏笔 · " + title);
                tones.push_back(theme::Tone::Warn);
            }
            if (relation.sceneOrd > 0) {
                tags.push_back("场景 " + std::to_string(relation.sceneOrd) +
                               (relation.sceneTitle.empty() ? "" : " · " + relation.sceneTitle));
                tones.push_back(theme::Tone::Idle);
            }
            if (!relation.shotCode.empty()) {
                tags.push_back("镜 " + relation.shotCode);
                tones.push_back(theme::Tone::Idle);
            }

            if (tags.empty()) {
                DrawTextClipped(draw, FontAt(11.5f), 11.5f, ImVec2(x, y + 2.0f), w, ColorTextMuted(),
                                book.bound ? "这一项没有可查到的关联（伏笔表为空或未跑 T9）"
                                            : "打开工程并选中章节 / 镜头后，这里显示它的真实关联",
                                true);
                y += 24.0f;
            } else {
                float tx = x;
                float ty = y;
                const float th = TagHeight(true);
                for (std::size_t i = 0; i < tags.size(); ++i) {
                    const float tw = TagWidth(tags[i], true, false);
                    if (tx + tw > x + w) {
                        tx = x;
                        ty += th + 8.0f;  // .row.gap-2 = 8px
                    }
                    Tag(draw, Rect{tx, ty, tx + tw, ty + th}, tags[i], tones[i], true);
                    tx += tw + 8.0f;
                }
                y = ty + th + 6.0f;
            }
        }
        y += kSectBodyPadBottom;
        DrawSectionDivider(area, y, draw);
        y += 1.0f;
    }
}

// ---------------------------------------------------------------- P4.6 底栏
void Shell::DrawDock(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.min.x, area.min.y + 0.5f), ImVec2(area.max.x, area.min.y + 0.5f),
                  ColorLineSubtle(), 1.0f);

    const char* tabs[] = {"任务队列", "日志", "产物", "校验报告"};
    const std::vector<SegmentOption> options{{"0", tabs[0]}, {"1", tabs[1]}, {"2", tabs[2]},
                                             {"3", tabs[3]}};
    const std::string value = std::to_string(layout_.dockTab);
    // ⚠️ 宽高一律走 RectAt()。这里原来写 `Rect{x, y, 400.0f, 32.0f}`，而 kit::Rect 的
    //    四参构造是 (minX, minY, maxX, maxY) —— bounds.height() 成了 32 - dockTop(745)
    //    = **-713**。Tabs 内部按 bounds.height() 排每个页签，于是文字位置碰巧还对，
    //    但选中页签那条 2px accent 下划线被画到 y≈31（屏幕顶栏里），点击区域也是负高的。
    //    不报编译错、不崩，只是下划线跑错地方 —— 见 tools\find-rect-wh-misuse.ps1。
    const std::string_view picked = Tabs(draw, RectAt(area.min.x + 12.0f, area.min.y, 400.0f, 32.0f),
                                         options, value, "dock-tabs");
    if (!picked.empty()) {
        layout_.dockTab = std::atoi(std::string(picked).c_str());
    }
    if (IconButton(draw, RectAt(area.max.x - 34.0f, area.min.y + 3.0f, 22.0f, 22.0f), "x", false,
                   false, "dock-close")) {
        ToggleDock();
    }

    const Rect body{area.min.x + 12.0f, area.min.y + 34.0f, area.max.x - 12.0f, area.max.y - 10.0f};
    if (layout_.dockTab == 0) {
        DrawDockQueue(body, draw);
    } else if (layout_.dockTab == 1) {
        DrawDockLogs(body, draw);
    } else if (layout_.dockTab == 2) {
        DrawDockArtifacts(body, draw);
    } else {
        DrawDockReports(body, draw);
    }
}

// ---------------------------------------------------------------- P4.6a 任务队列
// 队列行来自 comfy::QueueModel 的真实快照（Shell.jsx:222-232 的行结构）。
// 早先这里是一条写死的 Progress(42%, run=true) —— 界面上永远在"跑"，与真实运行态无关。
void Shell::DrawDockQueue(Rect body, ImDrawList* draw) {
    const std::vector<comfy::QueueModel::Row> rows =
        comfy::ComfySession::Instance().Queue().Snapshot();
    // ⚠️ 画**全部**条目，超出由 ScrollRegion 滚 —— 原来这里是
    //    `if (y + 30.0f > body.max.y) break;`，队列有 50 个任务时只画前 6 个，
    //    剩下的**无声消失**，界面上看不出还有 44 个。列表被截断且不提示 = 界面在骗人。
    //    `tools\find-silent-truncation.ps1` 扫的就是这一族写法（全树 10 处）。
    //
    // 队列顺序是 Comfy 那边定的（运行中在前），所以从**顶部**开始画、不做尾部窗口。
    constexpr float kQueueRowH = 32.0f;
    constexpr float kQueueFootH = 18.0f;
    kit::ScrollRegion list("dock-queue-list", body);
    if (list) {
        // 内容必须画在 child **自己的** draw list 上：BeginChild 的裁剪矩形只写进
        // 它自己那条 list（见 Scroll.h 的说明）。
        ImDrawList* ldraw = list.drawList();
        const Rect inner = list.content();
        float y = inner.min.y;
        for (const comfy::QueueModel::Row& row : rows) {
            const bool running = row.state == comfy::TaskState::Running;
            const bool failed = row.state == comfy::TaskState::Failed;
            // 队列行走 kit::ListRow：底色、命中、文字 Y 全在里面。原来这一段
            // 自己算 `y + 3.0f` / `y + 4.0f`，与同一行里居中的 Tag 并排看时
            // 字比 Tag 高出 6px —— 「很多按钮的字不在中间」最扎眼的一处。
            //
            // ⚠️ 队列行**不可点**（这里只要 hover 底），所以 id 传空串：
            // ListRow 会跳过命中测试。传了 id 就等于把一次永远没人读的
            // clicked 提交给 ImGui 的 ID 栈，白占一个 item。
            //
            // suppressed 走 chromeInteractive_ 闸门：浮层开着时底下的 dock
            // 行不该出 hover 高亮（原来靠 ChromeHit 返回空 Hit 达成这件事，
            // 换成 kit 组件后闸门必须显式传进来，否则浮层会「漏高亮」）。
            kit::ListRowSpec spec;
            spec.id = {}; // 不可点
            spec.suppressed = !chromeInteractive_;
            spec.title = row.label.empty() ? row.promptId : row.label;
            spec.titleSize = 12.0f;
            spec.paddingX = 16.0f; // 给左边的 StatusDot 让位
            spec.chevron = "none";
            const Rect line{inner.min.x, y, inner.max.x, y + 30.0f};
            kit::ListRow(ldraw, line, spec);
            // 状态点画在 ListRow 之外：它是 7px 圆点居中，不是 16px 图标。
            StatusDot(ldraw, ImVec2(line.min.x + 8.0f, line.center().y),
                      failed ? theme::Tone::Danger : (running ? theme::Tone::Busy : theme::Tone::Idle),
                      running);
            // 细进度 + 百分比：走 QueueModel 的真实 progress，不是写死的 42。
            const float pct = row.progress * 100.0f;
            Progress(ldraw, Rect{line.min.x + 248.0f, y + 12.0f, line.min.x + 408.0f, y + 16.0f}, pct,
                     running, true);
            if (row.progressMax > 0) {
                const std::string p = std::to_string(row.progressValue) + "/" +
                                      std::to_string(row.progressMax) + " · " +
                                      std::to_string(static_cast<int>(pct)) + "%";
                DrawTextClipped(ldraw, MonoAt(10.5f), 10.5f,
                                ImVec2(line.min.x + 416.0f,
                                       kit::CenterTextY(MonoAt(10.5f), 10.5f, line.center().y)),
                                180.0f, ColorTextMuted(), p);
            }
            const Rect tagBox = RectAt(line.max.x - TagWidth("", true, false) - 6.0f, y + 6.0f,
                                       TagWidth("", true, false), TagHeight(true));
            Tag(ldraw, tagBox, failed ? "失败" : (running ? "运行中" : "排队"),
                failed ? theme::Tone::Danger : (running ? theme::Tone::Busy : theme::Tone::Idle), true);
            y += kQueueRowH;
        }
        ImFont* f = FontAt(10.5f);
        const std::string foot =
            rows.empty()
                ? "队列为空 · Comfy 未提交任务（或未连接）"
                : "共 " + std::to_string(rows.size()) + " 个任务 · 队列由出图 / 出片 / 小说生成共享";
        DrawTextClipped(ldraw, f, 10.5f, ImVec2(inner.min.x, y + 2.0f), inner.width(),
                        ColorTextMuted(), foot);
        list.setContentHeight(y + kQueueFootH - inner.min.y);
    }
}

// ---------------------------------------------------------------- P4.6b 日志
void Shell::DrawDockLogs(Rect body, ImDrawList* draw) {
    // 日志来自运行期真实事件：PushLog 写入 / 流水线收尾 / 产物打开失败等。
    // 早先这里是四条写死的 "T3 生成图 · …" —— 跟程序实际发生的事毫无关系。
    if (logLines_.empty()) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 420.0f, body.min.y + 90.0f},
              "terminal", "暂无日志", "启动流水线或打开产物后，这里会实时滚动");
        return;
    }
    // 最新的在下面（webui 勾到底），行高 18，按行数裁。
    //
    // ⚠️ 这里**保留**尾部窗口（不改成滚动）：日志是「跟随最新」的流，每来一行就
    //    把用户拽到底部会和「往上翻看历史」打架（真做跟随还要判断用户是否已在底部，
    //    那是另一件事）。但**必须写明裁了多少** —— 原来只有一句注释，界面上看不出
    //    更早的行存在过，那和队列那边一样属于「静默截断」。
    // scan:allow-silent-truncation 日志流按设计只保留最近 N 行，界面上已写明总行数
    constexpr float kLogRowH = 18.0f;
    const int maxRows = std::max(1, static_cast<int>((body.height() - 22.0f) / kLogRowH));
    const int total = static_cast<int>(logLines_.size());
    const int first = std::max(0, total - maxRows);
    float y = body.min.y;
    for (int i = first; i < total; ++i) {
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(body.min.x, y), ColorTextSecondary(),
                      logLines_[static_cast<std::size_t>(i)].c_str(), nullptr);
        y += kLogRowH;
    }
    if (runActive_) {
        // 运行中在末尾留一个闪烁光标块（Shell.jsx:236 的 log-caret）。
        const float blink = Pulse(1.0f) > 0.5f ? 1.0f : 0.0f;
        if (blink > 0.0f) {
            DrawRoundRect(draw, ImVec2(body.min.x, y + 3.0f), ImVec2(body.min.x + 7.0f, y + 15.0f),
                          1.0f, ColorAccent());
        }
    }
    if (total > maxRows) {
        DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(body.min.x, body.max.y - 14.0f),
                        body.width(), ColorTextMuted(),
                        "共 " + std::to_string(total) + " 行 · 这里只显示最近 " +
                            std::to_string(maxRows) + " 行");
    }
}

// ---------------------------------------------------------------- P4.7 状态栏
void Shell::DrawStatusBar(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.min.x, area.min.y + 0.5f), ImVec2(area.max.x, area.min.y + 0.5f),
                  ColorLineSubtle(), 1.0f);

    const float cy = area.center().y;
    float x = area.min.x + 10.0f;
    const float h = 20.0f;

    // 6 项：Comfy 状态点 + 文字 / LLM 就绪 / 队列 N / 弹性 / 阶段名 / 96px 细进度 + T{n}/17 / 主题 / 版本
    // ⚠️ 三项都读**真实状态**，不再写死 "Comfy 未连接 / LLM 就绪 / 队列 3"。
    //    LLM 的「就绪」看 AppSettings 里 provider 对应的 API Key 有没有配；
    //    队列数取 comfy::QueueModel 的 Counts；Comfy 同顶栏。
    {
        const AppSettings& s = Settings();
        const bool llmOk = !s.openaiApiKey.empty() || !s.mimoApiKey.empty() ||
                           !s.minimaxApiKey.empty();
        const comfy::ComfySession& session = comfy::ComfySession::Instance();
        const bool comfyConfigured = !s.comfyBaseUrl.empty();
        const bool comfyUp = comfyConfigured && session.LastError().empty();
        const comfy::QueueModel::Counts counts = session.Queue().CountsSnapshot();
        const int queued = counts.pending + counts.running;

        const std::string queueText = "队列 " + std::to_string(queued);
        struct Item {
            const char* text;
            theme::Tone tone;
        };
        const Item items[3] = {
            {comfyConfigured ? (comfyUp ? "Comfy 已连接" : "Comfy 未连接") : "Comfy 未配置",
             comfyUp ? theme::Tone::Ok
                     : (comfyConfigured ? theme::Tone::Warn : theme::Tone::Idle)},
            {llmOk ? "LLM 就绪" : "LLM 未配置", llmOk ? theme::Tone::Ok : theme::Tone::Idle},
            // ⚠️ 队列数要活到本函数返回：早先写成 ("队列 " + to_string(n)).c_str()，
            //    那个临时 string 在完整表达式结束就析构，指针当场悬空。
            {queueText.c_str(), theme::Tone::Idle},
        };
        for (const Item& item : items) {
            const float w =
                FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, item.text,
                                              item.text + std::strlen(item.text))
                    .x +
                16.0f;
            DrawRoundRect(draw, ImVec2(x, cy - 10.0f), ImVec2(x + w, cy + 10.0f), 4.0f, 0);
            StatusDot(draw, ImVec2(x + 7.5f, cy), item.tone, item.tone == theme::Tone::Busy);
            draw->AddText(FontAt(11.5f), 11.5f, ImVec2(x + 16.0f, cy - 5.75f),
                          ColorTextSecondary(), item.text, item.text + std::strlen(item.text));
            x += w + 6.0f;
        }
    }

    const std::string themeName = std::string("主题：") +
                                  std::string(theme::ThemeDisplayName(theme::CurrentThemeId()));
    const float themeW =
        FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, themeName.data(), themeName.data() + themeName.size()).x;
    // 版本号取 CMake 的 `SHINE_VERSION`（CMakeLists 里 shine_core 的 PUBLIC 编译定义）。
    // 早先这里是写死的 `const char* version = "v0.2.0"` —— 改版本时不会跟着动，
    // 界面上会一直显示一个与实际构建版本无关的号。
    const std::string version = std::string("v") + SHINE_VERSION;
    const float versionW =
        FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, version.data(), version.data() + version.size()).x;
    float rightX = area.max.x - 10.0f - versionW - 16.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX + 8.0f, cy - 5.75f), ColorTextMuted(),
                  version.data(), version.data() + version.size());
    rightX -= themeW + 16.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX + 8.0f, cy - 5.75f), ColorTextSecondary(),
                  themeName.data(), themeName.data() + themeName.size());
    // 96px 细进度 + 运行态文案。
    //
    // ⚠️ 早先这里是 `"T" + (runStageIndex_+1) + "/17 · " + runPercent_ + "%"`，而
    //    runStageIndex_ / runPercent_ **全文件只有「写 0」与「读出来显示」**，没有任何
    //    自增点 —— 那是一个结构上不可能前进的进度条，注释却写着「这里是真实运行态」。
    //    执行体未接入时如实说「执行体未接入」，不做进度条。
    //    另外那个 "17" 是硬编码字面量（T1..T17 恰好 17 个），全仓库没有 kStageCount。
    std::string progress;
    ImU32 progressColor = ColorTextSecondary();
    if (runActive_) {
        progress = "T" + std::to_string(runStageIndex_ + 1) + "/" +
                   std::to_string(TextStageCount()) + " · " + std::to_string(runPercent_) + "%";
    } else if (runFinished_) {
        progress = "已完成";
    } else if (!pages::OverviewPipelineWired()) {
        progress = "阶段执行体未接入";
        progressColor = ColorOf(theme::Current().statusWarn);
    } else {
        progress = "未运行";
    }
    const float progressW =
        FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, progress.data(), progress.data() + progress.size()).x;
    rightX -= 96.0f + 8.0f + progressW + 8.0f;
    // 进度条宽度也跟状态走：未接入/未运行时画一条 0% 的空轨道，是在暗示「进度是 0」
    // 而不是「没有进度可言」。
    Progress(draw, Rect{rightX, cy - 2.0f, rightX + 96.0f, cy + 2.0f},
             runActive_ ? static_cast<float>(runPercent_) : 0.0f, runActive_, true);
    rightX -= progressW + 8.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX, cy - 5.75f), progressColor,
                  progress.data(), progress.data() + progress.size());
}

// ---------------------------------------------------------------- P4.8 面包屑
//
// 设计稿 `Crumbs()` 的第三段：小说页取「第 N 章 · 标题」，其余走 `defaultTabName()`。
// ⚠️ 而那份 `defaultTabName` 表里 assets 的「实体 · 林晚」、image 的「分镜图_v3」
//    **本身就是 mock 字符串** —— 照抄等于把假名字写死在界面上。
// 规则：能从真状态推的推，推不出来的用**结构性**标签，一个假名都不编。
std::string Shell::DerivedViewLabel() const {
    switch (layout_.workspace) {
    case static_cast<int>(pages::Workspace::Overview):
        return "总控台";
    case static_cast<int>(pages::Workspace::Novel): {
        const pages::BookSideView& s = pages::BookSide();
        if (!s.bound || s.chapters.empty()) {
            return "章节";
        }
        const int idx = std::clamp(s.selectedChapter, 0, static_cast<int>(s.chapters.size()) - 1);
        const pages::BookChapterView& ch = s.chapters[static_cast<std::size_t>(idx)];
        if (ch.title.empty()) {
            return "第 " + std::to_string(ch.ord) + " 章";
        }
        return "第 " + std::to_string(ch.ord) + " 章 · " + ch.title;
    }
    case static_cast<int>(pages::Workspace::Assets): {
        const pages::BookSideView& s = pages::BookSide();
        if (!s.bound || s.assets.empty()) {
            return "实体";
        }
        const int idx = std::clamp(s.selectedAsset, 0, static_cast<int>(s.assets.size()) - 1);
        const pages::BookAssetView& a = s.assets[static_cast<std::size_t>(idx)];
        // 实体名是空的就不写「实体 · 」（设计稿的「实体 · 林晚」是 mock，不是格式要求）。
        return a.name.empty() ? std::string("实体")
                              : std::string("实体 · ") + a.name;
    }
    case static_cast<int>(pages::Workspace::Storyboard):
        return "镜头表";
    case static_cast<int>(pages::Workspace::ImageFlow):
        return "分镜图";
    case static_cast<int>(pages::Workspace::VideoFlow):
        return "H3 视频";
    case static_cast<int>(pages::Workspace::Gallery):
        return "组件画廊";
    default:
        return layout_.lastViewLabel.empty() ? "总览" : layout_.lastViewLabel;
    }
}

void Shell::DrawBreadcrumbs(Rect area, ImDrawList* draw) {
    // shell.css:223-234 `.crumbs`：height 34 / padding 0 16 / gap 7 / font-size 12 /
    // color text-muted / border-bottom 1px solid line-subtle /
    // background color-mix(in srgb, var(--bg-void) 60%, var(--bg-surface))。
    // 内容与 JSX 逐项对得上（Shell.jsx:124-139）：`.c`(text-secondary / 500) ·
    // `.sep`(opacity .55) · `.c.here`(text-primary / 600) · `.spacer` · `.tiny.dim` 提示。
    //
    // ⚠️ 底色与下边框原先**整条都没有**：这个函数原来只 AddText，于是面包屑在界面上
    //    是「浮在 bg-void 上的一串字」，而设计稿里它是一条独立的浅色横条。
    //    而底色**早就算好了** —— `theme::Derived::crumbBg`（`Theme.cpp:99` 的
    //    `MixSrgb(c.bgVoid, c.bgSurface, 60.0f)`，字段注释直接写着 shell.css:233），
    //    此前**零绘制消费点**，只有 `check-theme` 的自检在读它。
    //    派生色算出来没人用，和没算一样 —— 门禁也管不到（它不是硬编码颜色）。
    draw->AddRectFilled(area.min, area.max, ColorOf(theme::CurrentDerived().crumbBg));
    // border-box（`base.css:2-6` 的 `*` 全局）⇒ 这 1px 边框**含在 34px 高度里**，
    // 内容区少 1px，文字的垂直中心跟着上移 0.5px。
    draw->AddLine(ImVec2(area.min.x, area.max.y - 0.5f), ImVec2(area.max.x, area.max.y - 0.5f),
                  ColorLineSubtle(), 1.0f);
    const float cy = area.center().y - 0.5f;
    // ⚠️ 字号原先写 12.5f，而 12.5 **不在字体档位里**（`Tokens.h:110` 的 kSizes 只有
    //    12/13/14/16/20/28），`LookupNearest` 又**向上**取档 ⇒ 实际渲染在 13px，
    //    比设计稿大一号。写 12.0f 才真的落在 xs 档上。
    constexpr float kCrumbsFont = 12.0f;  // shell.css:230 / base.css:120 的 .tiny
    constexpr float kCrumbsGap = 7.0f;    // shell.css:228 的 flex gap
    // padding: 0 16px（原先左边 24px）
    float x = area.min.x + 16.0f;
    // ⚠️ 第三段以前恒为 "总览"：`SetWorkspace` 无条件写 `lastViewLabel = "总览"`，
    // 而 `lastViewLabel` 全仓没有任何别的地方会改它 ⇒ 切到小说页，面包屑照样是
    // 「项目 › 小说 › 总览」。状态字段没跟动作走，和「只显示不联动」是同一类。
    const std::string derived = DerivedViewLabel();
    const std::string viewLabel = derived.empty() ? std::string("总览") : derived;
    const char* crumbs[] = {"项目", WorkspaceLabel(layout_.workspace), viewLabel.c_str()};
    for (int i = 0; i < 3; ++i) {
        const bool current = (i == 2);
        ImFont* face = current ? FontBoldAt(kCrumbsFont) : FontAt(kCrumbsFont);
        const std::size_t len = std::strlen(crumbs[i]);
        const float w = face->CalcTextSizeA(kCrumbsFont, 1e9f, 0.0f, crumbs[i], crumbs[i] + len).x;
        draw->AddText(face, kCrumbsFont, ImVec2(x, cy - kCrumbsFont * 0.5f),
                      current ? ColorText() : ColorTextSecondary(), crumbs[i], crumbs[i] + len);
        x += w + kCrumbsGap;
        if (i < 2) {
            // ⚠️ 长度用 sizeof() - 1，**不要**写死字节数。
            //    这里原来写的是一个单角引号 U+203A 加 "+ 3"：它在 UTF-8 里正好 3 字节，
            //    当时对得上；但字面量一旦被改写成一个 1 字节的字符（编码往返、编辑器保存），
            //    那个 3 就会越界多读 2 字节 —— 实测面包屑上画成了两个问号
            //    （? 后面跟的是字符串池里恰好相邻的字节，纯属巧合，不报错、不崩，
            //    只是永远画不对）。
            static constexpr char kSep[] = "›";
            const std::size_t sepLen = sizeof(kSep) - 1;
            ImFont* sepFace = FontAt(kCrumbsFont);
            // 分隔符的步进原先写死 12.0f（≈ 7px gap + 5px 字宽，碰巧接近），
            // 现在按设计稿的 flex 语义真去量它的宽度。
            const float sw =
                sepFace->CalcTextSizeA(kCrumbsFont, 1e9f, 0.0f, kSep, kSep + sepLen).x;
            draw->AddText(sepFace, kCrumbsFont, ImVec2(x, cy - kCrumbsFont * 0.5f),
                          WithAlpha(ColorTextMuted(), 0.55f), kSep, kSep + sepLen);
            x += sw + kCrumbsGap;
        }
    }

    // 右侧提示串：JSX 是 `<span className="tiny dim">`（Shell.jsx:138），
    // `.tiny`=12px、`.dim`=text-muted（base.css:118/120）。
    const char* hint = "Ctrl+B 侧栏 · Ctrl+J 底栏 · Ctrl+I 检查器 · Ctrl+K 命令";
    ImFont* font = FontAt(kCrumbsFont);
    const float w =
        font->CalcTextSizeA(kCrumbsFont, 1e9f, 0.0f, hint, hint + std::strlen(hint)).x;
    draw->AddText(font, kCrumbsFont, ImVec2(area.max.x - 16.0f - w, cy - kCrumbsFont * 0.5f),
                  ColorTextMuted(), hint, hint + std::strlen(hint));
}

// ---------------------------------------------------------------- P4.9 命令面板
//
// 外壳 chrome 的热区统一入口（顶栏 / 导航 / 侧栏 / 检查器 / 面包屑 / 底栏都走它）。
//
// 浮层开着时**直接返回空 Hit、一个 item 都不提交**。理由是 ImGui 同窗口内
// 「先注册者独占 HoveredId」（imgui.cpp:5161），而外壳先于浮层注册 ⇒ 点遮罩关闭的
// 那一次点击会被侧栏 / 顶栏先吃掉，结果是「工作区被切走、浮层还开着」。
// 不提交 item 就没有「被吃掉」这回事，点击由浮层自己手算（IsMouseClicked +
// 判断点在不在面板内，这两个都不看 HoveredId）。
//
// 代价：浮层开着时外壳连 hover 高亮都没有 —— 那正是模态该有的样子。
// 配套的另一半在 DrawWorkspace（给工作区 child 加 NoMouseInputs），机制见 Scroll.h。
kit::Hit Shell::ChromeHit(const kit::Rect& bounds, std::string_view id) {
    if (!chromeInteractive_) {
        return kit::Hit{};
    }
    return kit::HitTest(bounds, id);
}

void Shell::DrawCommandPalette() {
    if (!paletteOpen_) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(560.0f, display.x - 40.0f);
    const kit::Rect screen{0.0f, 0.0f, display.x, display.y};
    const Rect bounds{(display.x - width) * 0.5f, display.y * 0.22f, (display.x + width) * 0.5f,
                      display.y * 0.22f + 420.0f};
    // 浮层必须画在 foreground：页面/底栏各自跑在 BeginChild 里，child 在父窗口那份
    // draw list **之后**渲染，画在父 list 上的浮层会被整片盖住（见 DrawReportModal 的注释）。
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // ⚠️ 遮罩原来画的是**面板自己的 bounds**，下一行 DrawShadowed 的面板底板就把它
    // 整个盖住了 —— 一次完全被覆盖的死绘制：面板照常显示，背后该压暗的界面一点没暗，
    // 和其它所有浮层都不一样（`.scrim` 是 `position: fixed; inset: 0`，铺满屏幕）。
    // 编译过、截图正常，只有「没生效」这一种表现。
    // 现在走 kit 的原语：遮罩铺满屏幕，底板才是面板那块。
    kit::ScrimPaint(draw, screen);
    kit::OverlayPanel(draw, bounds);

    // 输入
    const Rect input{bounds.min.x + 18.0f, bounds.min.y + 18.0f, bounds.max.x - 18.0f,
                     bounds.min.y + 56.0f};
    ImFont* font = FontAt(15.0f);
    // 打开面板就把焦点交给输入框。
    //
    // ⚠️ 这里原来是个**被掏空的 `if`（条件成立、里面空的）**，`SetKeyboardFocusHere`
    //    被删掉了。后果：Ctrl+K 打开面板之后**直接打字不进过滤框**，必须先用鼠标点一下
    //    输入框 —— 而命令面板的基本用法就是「打开就打字」。整个过滤逻辑（下面那段
    //    includes）是真的，所以症状是「面板能开、列表能滚，但好像打不了字」。
    //    `SetKeyboardFocusHere` 必须在 InputText **之前**那一帧调用（同一帧内、
    //    该 item 提交之前即可），我们只在「刚打开」的第一帧调，避免每帧抢焦点。
    // 「这一帧是不是刚打开的那一帧」要在**消费** paletteJustOpened_ 之前抓住。
    // 下面关闭判定要用它：同一次按键沿既该打开面板、又不该立刻把它关掉。
    const bool openedThisFrame = paletteJustOpened_;
    if (paletteJustOpened_) {
        paletteJustOpened_ = false;
        paletteFocusInput_ = true;
    }
    ImGui::SetCursorScreenPos(input.min);
    ImGui::SetNextItemWidth(input.width());
    ImGui::PushFont(font);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImU32(0));
    ImGui::PushStyleColor(ImGuiCol_Text, ColorText());
    if (paletteFocusInput_) {
        paletteFocusInput_ = false;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::InputText("##palette", paletteQuery_, sizeof(paletteQuery_));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopFont();

    // 三组：页面 / 命令 / 主题；过滤 = label+hint+group 的小写 includes，空组丢弃
    //
    // ⚠️ 这里早先用一个 `int workspace` 同时表达「切到第 N 个工作区」与「别的命令」
    //    （-1 命令 / -2 切主题 / -3..-7 五个主题），而 Enter 的执行体只有
    //    `if (picked.workspace >= 0) SetWorkspace(...)` 一句 ⇒ **14 条里 8 条是空操作**：
    //    面板只是关掉，什么都没发生。主题组那 5 条尤其扎眼 —— 面板上明明列着「深空/
    //    薄暮/纸墨/水墨/极夜」，按 Enter 却毫无反应。
    //    改成显式的 action 枚举（见 Shell.h 的 PaletteAction）：新增一个动作必须同时
    //    写执行体，编译器盯着。
    struct Item {
        std::string group;
        std::string label;
        std::string hint;
        PaletteAction action;
        int arg;  // Workspace = 工作区下标；Theme = kAllThemes 下标
    };
    const std::vector<Item> all = {
        {"页面", "总控", "pipeline", PaletteAction::Workspace, 0},
        {"页面", "小说", "novel", PaletteAction::Workspace, 1},
        {"页面", "资产", "assets", PaletteAction::Workspace, 2},
        {"页面", "分镜", "storyboard", PaletteAction::Workspace, 3},
        {"页面", "出图", "imageflow", PaletteAction::Workspace, 4},
        {"页面", "出片", "videoflow", PaletteAction::Workspace, 5},
        {"页面", "组件画廊", "gallery", PaletteAction::Workspace, 6},
        // 新建 / 打开都落到项目中心：那里既有项目列表也有工具条上的「新建项目」按钮。
        // ⚠️ 不假装能直接开新建向导 —— 向导是项目中心内部的一个 flag（HubState::wizard），
        //    而 hub 是 DrawProjectHub 里的函数内状态，跨不过去。要直达得先把它提为成员，
        //    那是另一件事，别在这里顺手改。
        {"命令", "新建项目", "Ctrl+N", PaletteAction::NewProject, 0},
        {"命令", "打开项目", "Ctrl+O", PaletteAction::OpenProject, 0},
        {"命令", "保存布局", "layout.dat", PaletteAction::SaveLayout, 0},
        // Ctrl+T 语义是「切主题」。早先它被注册成**打开命令面板**，于是面板上写着
        // 「切换主题 Ctrl+T」、真按 Ctrl+T 却把面板又打开了一遍 —— 套娃。
        // 轮换用 ThemeNext（切到下一个），「主题」组那 5 条才是切到**指定**那个。
        // 两者早先共用 Theme，于是「切换主题」实际只是切到 index 1。
        {"命令", "切换主题", "Ctrl+T", PaletteAction::ThemeNext, 0},
        {"主题", "深空", "deepspace", PaletteAction::Theme, 0},
        {"主题", "薄暮", "dusk", PaletteAction::Theme, 1},
        {"主题", "纸墨", "paperink", PaletteAction::Theme, 2},
        {"主题", "水墨", "inkwash", PaletteAction::Theme, 3},
        {"主题", "极夜", "polarnight", PaletteAction::Theme, 4},
    };
    std::string query = paletteQuery_;
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<const Item*> matched;
    for (const Item& item : all) {
        std::string haystack = item.group + item.label + item.hint;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (query.empty() || haystack.find(query) != std::string::npos) {
            matched.push_back(&item);
        }
    }
    paletteMatches_ = static_cast<int>(matched.size());
    paletteSelected_ = std::clamp(paletteSelected_, 0, std::max(0, paletteMatches_ - 1));

    // 先量一遍：键盘移动选中项后要靠它算出「该滚到哪」，滚动跟随选中行是命令面板的基本行为。
    std::vector<float> rowTops;
    {
        float probe = 0.0f;
        std::string group;
        rowTops.reserve(static_cast<std::size_t>(paletteMatches_));
        for (int i = 0; i < paletteMatches_; ++i) {
            const Item& item = *matched[static_cast<std::size_t>(i)];
            if (item.group != group) {
                group = item.group;
                probe += kPaletteGroupH;
            }
            rowTops.push_back(probe);
            probe += kPaletteRowH;
        }
        paletteContentH_ = probe;
    }
    // ⚠️ 列表必须裁到面板内。16 条目 + 3 组标题 = 546px，而输入框下面只有 358px ——
    //    不裁的话末尾几行会画到面板外面、压在工作区上（实测：薄暮/纸墨/水墨/极夜 漏在外面）。
    const Rect listArea{bounds.min.x, input.max.y + 6.0f, bounds.max.x, bounds.max.y - 12.0f};
    const float listH = std::max(0.0f, listArea.height());
    if (listArea.contains(ImGui::GetIO().MousePos)) {
        paletteScroll_ -= ImGui::GetIO().MouseWheel * 30.0f;
    }
    if (paletteSelected_ < static_cast<int>(rowTops.size()) && listH > 0.0f) {
        const float top = rowTops[static_cast<std::size_t>(paletteSelected_)];
        if (top - paletteScroll_ < 0.0f) {
            paletteScroll_ = top;
        } else if (top + kPaletteRowH - paletteScroll_ > listH) {
            paletteScroll_ = top + kPaletteRowH - listH;
        }
    }
    paletteScroll_ =
        std::clamp(paletteScroll_, 0.0f, std::max(0.0f, paletteContentH_ - listH));

    draw->PushClipRect(listArea.min, listArea.max, true);
    float y = listArea.min.y - paletteScroll_;
    std::string lastGroup;
    ImFont* groupFont = FontBoldAt(11.5f);
    for (int i = 0; i < paletteMatches_; ++i) {
        const Item& item = *matched[static_cast<std::size_t>(i)];
        if (item.group != lastGroup) {
            lastGroup = item.group;
            // 组标题带高 kPaletteGroupH，按带中心落字（原来 `y + 6.0f` 偏上 0.75px）。
            const Rect groupBand{bounds.min.x + 18.0f, y, bounds.max.x - 18.0f, y + kPaletteGroupH};
            draw->AddText(groupFont, 11.5f,
                          ImVec2(groupBand.min.x, kit::CenterTextY(groupFont, 11.5f, groupBand.center().y)),
                          ColorTextMuted(), item.group.data(), item.group.data() + item.group.size());
            y += kPaletteGroupH;
        }
        const Rect row{bounds.min.x + 12.0f, y, bounds.max.x - 12.0f, y + kPaletteRowH};
        // 命令面板的条目行走 kit::ListRow。原来自己写 `row.min.y + 6.0f`（13px 字）
        // 与 `+ 7.0f`（11.5px hint），行高 30 时分别偏上 **2.5px / 2.25px** ——
        // 8 处列表行里最大的一处偏差。
        //
        // 纯键盘驱动（↑↓ + Enter），**不注册命中**：鼠标点不动是既定行为，
        // 传空 id。选中态走 selected 底。
        kit::ListRowSpec spec;
        spec.id = {}; // 纯键盘，不吃鼠标
        spec.title = item.label;
        spec.titleSize = 13.0f;
        spec.titleColor = ColorText(); // 命令名：primary
        spec.trailing = item.hint;
        spec.trailingSize = 11.5f;
        spec.chevron = "none"; // 命令面板用右侧 kbd 提示，不画 chevron
        spec.paddingX = 10.0f;
        spec.selected = i == paletteSelected_;
        kit::ListRow(draw, row, spec);
        y += kPaletteRowH;
    }
    draw->PopClipRect();
    // 滚动条：装不下才画。
    if (paletteContentH_ > listH && listH > 0.0f) {
        const float trackW = 4.0f;
        const float trackX = bounds.max.x - 8.0f - trackW;
        const float thumbH = std::max(24.0f, listH * (listH / paletteContentH_));
        const float thumbY = listArea.min.y + (listH - thumbH) * (paletteScroll_ / (paletteContentH_ - listH));
        draw->AddRectFilled(ImVec2(trackX, listArea.min.y), ImVec2(trackX + trackW, listArea.max.y),
                            ColorFillMuted());
        draw->AddRectFilled(ImVec2(trackX, thumbY), ImVec2(trackX + trackW, thumbY + thumbH),
                            ColorLineStrong());
    }

    // 键盘：↑↓ / Enter / Esc
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        paletteSelected_ = (paletteSelected_ + 1) % std::max(1, paletteMatches_);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        paletteSelected_ = (paletteSelected_ + paletteMatches_ - 1) % std::max(1, paletteMatches_);
    }
    // ⚠️ 这里原来写成**裸** `IsKeyPressed(ImGuiKey_K)`，与 `ApplyShortcuts` 里打开面板的
    //    `Ctrl+K` 撞在一起：`ApplyShortcuts()` 在 `DrawCommandPalette()` **之前**跑，
    //    于是同一帧里「打开」被「关闭」立刻抵消 ⇒ **Ctrl+K 永远打不开命令面板**。
    //    副症状更常见：面板开着时输入任何含 `k` 的查询都会把面板关掉。
    //    判据必须和打开侧**同一把尺子**：`Ctrl+K` 才关，裸 K 不关。
    // ⚠️ 关闭判定必须**跳过「刚打开的那一帧」**。
    //    ApplyShortcuts() 在帧首跑，DrawCommandPalette() 在帧尾跑：同一帧里 Ctrl+K 先把
    //    paletteOpen_ 置 true，走到这段时**同一个按键沿**又把它置 false ⇒ Ctrl+K 永远
    //    打不开命令面板（面板上却明明白白写着「命令面板 Ctrl+K」）。
    //    动作判据 r53 打出的就是 `palette=closed → palette=closed`，且 saw-ctrl / saw-pressed
    //    都为真 —— 注入没问题，是产品这一侧的同帧抵消。
    //
    // 副症状（更常见）：面板开着时输入任何含 `k` 的查询都会把面板关掉。所以关闭侧必须
    // 和打开侧**同一把尺子**：只认 `Ctrl+K`，裸 `K` 不关。
    //
    // ⚠️ 只把条件从裸 K 收窄成 Ctrl+K 是不够的（早先那么改过，症状一样）—— 问题不在
    //    条件宽不宽，在于**打开与关闭读到的是同一个按键沿**。
    if (!openedThisFrame &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
         (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false)))) {
        paletteOpen_ = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && paletteMatches_ > 0) {
        const Item& picked = *matched[static_cast<std::size_t>(paletteSelected_)];
        RunPaletteAction(picked.action, picked.arg);
        paletteOpen_ = false;
        paletteQuery_[0] = '\0';
        paletteSelected_ = 0;
    }
    // 点面板外关闭：与其它浮层同一套做法 —— 遮罩不注册命中（见上面 ScrimPaint 的
    // 说明），也不用本工程会 0xC0000005 的 IsMouseHoveringRect，手算点在不在面板内。
    //
    // ⚠️ 必须跳过「刚打开的那一帧」，和上面键盘那条同一个理由。顶栏搜索框是**点开**
    //    本面板的（`tb-search`），而那一次点击的位置就在面板之外 —— 不加这个前置
    //    条件，搜索框就成了死按钮：点一下，面板开出来又当场关掉，看上去毫无反应。
    //    这是「同帧自毁」那一类，和项目中心的 `HubState::dismissArmed` 同源。
    if (!openedThisFrame && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !bounds.contains(ImGui::GetIO().MousePos)) {
        paletteOpen_ = false;
    }
}

// 命令面板 Enter 与 ApplyShortcuts 的 Ctrl+N / Ctrl+O / Ctrl+T 共用这一份。
// 分成两处的话，迟早只改得动一边 —— 本仓已经吃过一次（Ctrl+T 套娃）。
void Shell::RunPaletteAction(PaletteAction action, int arg) {
    switch (action) {
    case PaletteAction::Workspace:
        SetWorkspace(arg);
        break;
    case PaletteAction::ThemeNext: {
        // 「切换主题」= 切到**下一个**主题。写死一个索引会让第二轮点回同一个主题，
        // 看着像没生效。
        const std::size_t count = std::size(theme::kAllThemes);
        std::size_t cur = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (theme::kAllThemes[i] == theme::CurrentThemeId()) {
                cur = i;
                break;
            }
        }
        const theme::ThemeId next = theme::kAllThemes[(cur + 1) % count];
        SetTheme(next);
        Notify(std::string("主题 · ") + theme::ThemeIdKey(next), theme::Tone::Ok);
        break;
    }
    case PaletteAction::Theme: {
        const std::size_t count = std::size(theme::kAllThemes);
        if (arg < 0 || static_cast<std::size_t>(arg) >= count) {
            return;
        }
        const theme::ThemeId id = theme::kAllThemes[static_cast<std::size_t>(arg)];
        SetTheme(id);
        Notify(std::string("主题 · ") + theme::ThemeIdKey(id), theme::Tone::Ok);
        break;
    }
    case PaletteAction::NewProject:
        if (!hubOpen_) {
            ToggleProjectHub();
        }
        Notify("项目中心 · 点工具条上的「新建项目」", theme::Tone::Info);
        break;
    case PaletteAction::OpenProject:
        if (!hubOpen_) {
            ToggleProjectHub();
        }
        Notify("项目中心 · 从最近列表里挑一个打开", theme::Tone::Info);
        break;
    case PaletteAction::SaveLayout: {
        // ⚠️ 别写成 `SaveLayout() ? … : …` —— 那是**调用两次**，文件写两遍。
        const bool ok = SaveLayout();
        Notify(ok ? "布局已保存 · layout.dat" : "布局保存失败 · layout.dat",
               ok ? theme::Tone::Ok : theme::Tone::Danger);
        break;
    }
    }
}

// ---------------------------------------------------------------- P4.10 浮层
// toast（ui.css:978-1011 的 .toasts / .toast，设计稿里是 notify(...)）。
//
// ⚠️ 走 **GetForegroundDrawList()**，不是 Begin/End 里的那个 draw：页面与底栏都跑在
//    ScrollRegion(BeginChild) 里，child 的 draw list 在父窗口 list 之后渲染，
//    画上去会被工作区整片盖住（上一轮四个浮层就是这么被盖的）。
// 规格：fixed right 16 / bottom 40；min-w 260 max-w 380；pad 10 14；r-md；
// bg-overlay 底 + line-normal 边 + **左侧 3px** 色调条 + shadow-2；12.5px。
void Shell::DrawOverlays(ImDrawList* draw) {
    (void)draw;
    if (toastTimer_ <= 0.0f || toastText_.empty()) {
        return;
    }
    toastTimer_ -= lastDelta_;
    if (toastTimer_ <= 0.0f) {
        return;
    }
    // 尾部 0.4s 淡出：设计稿没有这层，是纯 CSS transition 的等价物 ——
    // 硬切会闪一下，反而比设计稿更糙。
    const float alpha = toastTimer_ < 0.4f ? toastTimer_ / 0.4f : 1.0f;

    ImDrawList* front = ImGui::GetForegroundDrawList();
    // 走 kit::Toast：本体 + 投影 + 3px 色调条 + 图标 + 文字居中全在里面。
    // 页面层曾自己画一份（`DrawRoundRect(..., 8.0f, ...)` + 手动量宽 + 手动排 Y），
    // 偏上 2.25px，而且与 kit 里那份**已经算对**的实现各活一份。
    // 圆角 8→10、量宽公式、Y 居中现在统一走 kit（ui.css:987-1000 的 r-md 是 10）。
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const theme::Tone tone = toastTone_ == theme::Tone::Idle ? theme::Tone::Info : toastTone_;
    kit::Toast(front, RectAt(0.0f, 0.0f, display.x, display.y), toastText_, tone,
               tone == theme::Tone::Ok ? "check" : "info", /*above=*/0.0f, alpha);
}

std::uint64_t Shell::LayoutStateHash() const {
    // FNV-1a 64，和取证那边的像素哈希同一套 —— 同一份实现，判据与产品别各写一遍。
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
            h *= 1099511628211ull;
        }
    };
    mix(static_cast<std::uint64_t>(layout_.workspace));
    mix(layout_.sidePanelVisible ? 1u : 0u);
    mix(layout_.dockVisible ? 1u : 0u);
    mix(layout_.inspectorVisible ? 1u : 0u);
    mix(static_cast<std::uint64_t>(layout_.sidePanelWidth));
    mix(static_cast<std::uint64_t>(layout_.inspectorWidth));
    mix(static_cast<std::uint64_t>(layout_.dockHeight));
    mix(static_cast<std::uint64_t>(layout_.dockTab));
    mix(layout_.reduceMotion ? 1u : 0u);
    for (const char c : layout_.projectName) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    for (const char c : layout_.lastViewLabel) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// ---------------------------------------------------------------- 快捷键
void Shell::ApplyShortcuts() {
    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) {
        ToggleSidePanel();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_J, false)) {
        ToggleDock();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) {
        ToggleInspector();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        ToggleCommandPalette();
    }
    // Ctrl+Enter = 运行（phases.md:286 的契约，设计稿顶栏「运行 / 下一阶段」也在这条
    // 快捷键的语义上）。以前**没注册**：全树的 IsKeyPressed 只有面板的 ↑↓/Enter/Esc/K
    // 和上面这三条，phases.md 要求的这条一直缺着。
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (runActive_) {
            RequestRunStop();
        } else {
            RequestRunStart();
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        // ⚠️ 这里原来写的是 `paletteOpen_ = !paletteOpen_`，而命令面板里那条
        // 「切换主题 / Ctrl+T」承诺的是**切主题**。于是按 Ctrl+T 把命令面板打开一遍，
        // 面板上那条 Ctrl+T 又只能把面板再开关一次 —— 套娃，永远切不到主题。
        // 改成真的轮换主题；面板里那条走 RunPaletteAction(ThemeNext)，同一份逻辑。
        RunPaletteAction(PaletteAction::ThemeNext, 0);
    }
    // ⚠️ 这两条以前**根本没注册**：命令面板上写着「新建项目 / Ctrl+N」「打开项目 /
    //    Ctrl+O」，但 ApplyShortcuts 里只有 B / J / I / K / T。面板里那条早先也是空操作，
    //    于是这行提示从头到尾是**两处都死的**。
    // 执行体走 RunPaletteAction —— 与面板 Enter 那条共用一份，不在这里重写一遍。
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        RunPaletteAction(PaletteAction::NewProject, 0);
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        RunPaletteAction(PaletteAction::OpenProject, 0);
    }
    // 浮层的关闭键。设计稿的三处都写了 Esc 关闭（校验报告 modal 的 footer、
    // 设置向导、命令面板），补齐后浮层才算真正可用。
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (paletteOpen_) {
            paletteOpen_ = false;
        } else if (settingsOpen_) {
            settingsOpen_ = false;
        } else if (reportDetail_ >= 0) {
            reportDetail_ = -1;
        } else if (themeMenuOpen_) {
            themeMenuOpen_ = false;
        } else if (hubOpen_) {
            hubOpen_ = false;
        }
    }
}

void Shell::SetWorkspace(int index) {
    layout_.workspace = std::clamp(index, 0, kWorkspaceCount - 1);
    // 第三段面包屑改成**按工作区现算**（DerivedViewLabel），这里不再无条件写死
    // "总览" —— 写了它就永远盖住真状态。字段保留只为 layout.dat 的向后兼容。
}

void Shell::SetDockTab(int tab) { layout_.dockTab = std::clamp(tab, 0, 3); }

void Shell::SetReportDetail(int index) {
    reportDetail_ = index;
    reportScroll_ = 0.0f;  // 换一份报告就从顶上读，别留着上一份的滚动位置
    if (reportDetail_ >= static_cast<int>(reports_.size())) {
        reportDetail_ = -1;  // 没那么多报告就别开 —— 模态只认列表里真实存在的下标
    }
}

void Shell::SetTheme(shine::theme::ThemeId id) {
    theme::ApplyTheme(id);
    // 水墨是唯一换衬线族的主题 → 字体图集要重建，其余主题只换 ImGuiStyle。
    //
    // ⚠️ 双向都要判，且要记住当前图集是哪一个族：
    //   * 只在「切到水墨」时重建 → 切离水墨后图集还停在宋体，其他主题的字全是宋体观感
    //   * 不记状态连续重建 → 每调一次 SetTheme(ink) 就重烘一遍图集（16 档 ×
    //     两族字形集），取证跑 5 套主题时会连续烘 5 次，卡顿且无意义
    const bool wantSerif = theme::ThemeUsesSerif(id);
    if (wantSerif != atlasIsSerif_) {
        if (!BuildFontAtlas(/*serif=*/wantSerif)) {
            shine::log::Error("font atlas rebuild failed for {} family — 文字可能缺字",
                              wantSerif ? "serif" : "sans");
        }
        atlasIsSerif_ = wantSerif;
        // 重建后 io.FontDefault 变了，必须把 Style 的字体色/尺寸基线重刷一遍
        theme::ApplyCurrentTheme();
    }
    (void)theme::PersistTheme(theme::DefaultThemeFile());
}

void Shell::ToggleSidePanel() { layout_.sidePanelVisible = !layout_.sidePanelVisible; }
void Shell::ToggleDock() { layout_.dockVisible = !layout_.dockVisible; }
void Shell::ToggleInspector() { layout_.inspectorVisible = !layout_.inspectorVisible; }
void Shell::ToggleCommandPalette() {
    paletteOpen_ = !paletteOpen_;
    if (paletteOpen_) {
        // 每次打开从顶上开始，并选中第一项 —— 沿用上次的滚动位置会让人以为列表被过滤过。
        paletteScroll_ = 0.0f;
        paletteSelected_ = 0;
        // 焦点交给输入框：「打开就打字」才是命令面板的基本用法。早先这里没有这一句，
        // 打开后必须先用鼠标点一下输入框才能打字（SetKeyboardFocusHere 被删过）。
        paletteJustOpened_ = true;
    }
}

void Shell::PushLog(std::string_view level, std::string_view message) {
    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    char stamp[16];
    std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d", now.wHour, now.wMinute, now.wSecond);
    logLines_.emplace_back(std::string(stamp) + " [" + std::string(level) + "] " +
                           std::string(message));
    // 只留最近 400 行：底栏日志是滚动视图，不裁会一直涨
    if (logLines_.size() > 400) {
        logLines_.erase(logLines_.begin(), logLines_.begin() + 200);
    }
}

// ---------------------------------------------------------------- 工程打开/新建
void Shell::SetProjectRoot(std::filesystem::path root, std::string name) {
    layout_.projectRoot = std::move(root);
    layout_.projectName = std::move(name);
    // 总控页的 Runner 要靠工程根才知道 work/ 与 ledger 的位置。
    // 不绑 = 页面显示真实空态，而不是编一份假账本出来。
    pages::BindOverviewProject(layout_.projectRoot);
    // 小说 / 资产 / 分镜三页共用同一份工程快照，任绑一个即可（见 WorkspacePages.h）。
    pages::BindNovelProject(layout_.projectRoot);
    (void)SaveLayout();
}

bool Shell::OpenProjectByName(const std::string& name) {
    // 在最近列表里按名字找根目录，再交给 ProjectService 真正打开
    std::filesystem::path root;
    for (const project::RecentEntry& entry : projects_.Recent()) {
        if (entry.name == name) {
            root = entry.rootDir;
            break;
        }
    }
    if (root.empty()) {
        PushLog("warn", "未找到工程：" + name);
        return false;
    }
    auto opened = projects_.Open(root);
    if (!opened) {
        PushLog("error", "打开工程失败：" + name + " · " + std::string(opened.error().message));
        return false;
    }
    SetProjectRoot(root, name);
    PushLog("info", "已打开工程：" + name);
    return true;
}

// ---------------------------------------------------------------- P1.5 布局持久化
bool Shell::SaveLayout() {
    const std::filesystem::path file = LayoutFile();
    if (file.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    // 定长定序：magic + 版本 + 布局块。损坏 → LoadLayout 走默认值，不崩。
    struct Header {
        char magic[16];
        std::uint32_t version;
        std::uint32_t workspace;
        std::uint32_t sidePanelVisible;
        std::uint32_t dockVisible;
        std::uint32_t inspectorVisible;
        std::uint32_t sidePanelWidth;
        std::uint32_t inspectorWidth;
        std::uint32_t dockHeight;
        std::uint32_t dockTab;
        std::uint32_t reduceMotion;
        std::uint32_t nameLength;
        std::uint32_t viewLength;
    } header{};
    std::memcpy(header.magic, kLayoutMagic, sizeof(kLayoutMagic));
    header.version = 1;
    header.workspace = static_cast<std::uint32_t>(layout_.workspace);
    header.sidePanelVisible = layout_.sidePanelVisible ? 1u : 0u;
    header.dockVisible = layout_.dockVisible ? 1u : 0u;
    header.inspectorVisible = layout_.inspectorVisible ? 1u : 0u;
    header.sidePanelWidth = static_cast<std::uint32_t>(layout_.sidePanelWidth);
    header.inspectorWidth = static_cast<std::uint32_t>(layout_.inspectorWidth);
    header.dockHeight = static_cast<std::uint32_t>(layout_.dockHeight);
    header.dockTab = static_cast<std::uint32_t>(layout_.dockTab);
    header.reduceMotion = layout_.reduceMotion ? 1u : 0u;
    header.nameLength = static_cast<std::uint32_t>(layout_.projectName.size());
    header.viewLength = static_cast<std::uint32_t>(layout_.lastViewLabel.size());
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(layout_.projectName.data(), static_cast<std::streamsize>(layout_.projectName.size()));
    out.write(layout_.lastViewLabel.data(),
              static_cast<std::streamsize>(layout_.lastViewLabel.size()));
    return out.good();
}

bool Shell::LoadLayout() {
    const std::filesystem::path file = LayoutFile();
    if (file.empty() || !std::filesystem::exists(file)) {
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    struct Header {
        char magic[16];
        std::uint32_t version;
        std::uint32_t workspace;
        std::uint32_t sidePanelVisible;
        std::uint32_t dockVisible;
        std::uint32_t inspectorVisible;
        std::uint32_t sidePanelWidth;
        std::uint32_t inspectorWidth;
        std::uint32_t dockHeight;
        std::uint32_t dockTab;
        std::uint32_t reduceMotion;
        std::uint32_t nameLength;
        std::uint32_t viewLength;
    } header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || std::memcmp(header.magic, kLayoutMagic, sizeof(kLayoutMagic)) != 0 ||
        header.version != 1) {
        return false; // 损坏 → 保持默认布局，不崩（照 Qt 侧 MainWindow 的行为）
    }
    layout_.workspace = std::clamp(static_cast<int>(header.workspace), 0, kWorkspaceCount - 1);
    layout_.sidePanelVisible = header.sidePanelVisible != 0;
    layout_.dockVisible = header.dockVisible != 0;
    layout_.inspectorVisible = header.inspectorVisible != 0;
    layout_.sidePanelWidth = static_cast<int>(header.sidePanelWidth);
    layout_.inspectorWidth = static_cast<int>(header.inspectorWidth);
    layout_.dockHeight = static_cast<int>(header.dockHeight);
    layout_.dockTab = static_cast<int>(header.dockTab);
    layout_.reduceMotion = header.reduceMotion != 0;
    kit::SetReduceMotion(layout_.reduceMotion);
    layout_.projectName.resize(header.nameLength);
    layout_.lastViewLabel.resize(header.viewLength);
    in.read(layout_.projectName.data(), header.nameLength);
    in.read(layout_.lastViewLabel.data(), header.viewLength);
    return true;
}

// ---------------------------------------------------------------- 帧
void Shell::DrawFrame(float dt) {
    lastDelta_ = dt;
    // 过渡补间池只在第一次进来时预分配一次。放在这里而不是 AppEntry：
    // Shell 构造完成、主题 JSON 也加载完之后才开始画，预分配跟着第一帧走最自然。
    if (!animPoolReserved_) {
        animPoolReserved_ = true;
        // 容量按「一屏里可能同时在飞的补间数」估：每页几十个 hover 通道，
        // 加上列表行长尾，给到几百条，避免第一次划过列表时才扩容。
        kit::ReserveTweenPool(512, 128, 256, 64, 512);
    }
    kit::TickAnimation(dt);
    ApplyShortcuts();

    // Comfy 会话：ImGui 前端原先**从没初始化过**它（AppEntry 只 Init 了 log/async/gallery），
    // 所以顶栏状态点与底栏队列只能画假的。这里按 AppSettings 的地址起一次会话，
    // 每帧只跑 ComfySession::Tick —— 它内部是纯定时器 + 非阻塞 socket，不碰同步 HTTP。
    static bool comfyInited = false;
    if (!comfyInited) {
        comfyInited = true;
        comfy::ComfySession::Instance().Init(Settings().comfyBaseUrl);
    }
    comfy::ComfySession::Instance().Tick(dt);

    // 页面层的 toast 通道：页面不认识 Shell（那是外壳的活），所以由外壳注入自己的
    // Notify。首帧之后页面才可能有按钮被点，注入放在这里一次就够。
    // ⚠️ 捕获 this 而不是裸函数指针：Notify 是成员函数。
    static bool toastWired = false;
    if (!toastWired) {
        toastWired = true;
        pages::SetWorkspaceToast([this](std::string message) { Notify(std::move(message)); });
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("##shine-root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float W = display.x;
    const float H = display.y;

    // 项目中心是**整屏**替身（webui Shell.jsx:850 的 screen==='hub'）：
    // 进项目中心时顶栏/导航/侧栏/检查器/底栏/状态栏全部让位，只留它自己。
    if (hubOpen_) {
        DrawProjectHubScreen(RectAt(0, 0, W, H), draw);
        // 每帧（无条件）同步「中心里对话框开着」这个读数。只在开着时写的话，
        // 探针会读到上一帧的残留，把「已经关了」误读成「还开着」。
        hubDialogOpen_ = pages::HubDialogOpen();
        ImGui::End();
        kit::DrawDebugWindows(debug_);
        return;
    }
    // 中心关着时也要清：否则探针在中心关闭后仍会读到「上一个对话框开着」。
    hubDialogOpen_ = false;

    const float dockH = layout_.dockVisible ? static_cast<float>(layout_.dockHeight) : 0.0f;
    const float sideW = layout_.sidePanelVisible ? static_cast<float>(layout_.sidePanelWidth) : 0.0f;
    const float inspW = layout_.inspectorVisible ? static_cast<float>(layout_.inspectorWidth) : 0.0f;
    const float bodyTop = kTopBarHeight;
    const float bodyBottom = H - kStatusBarHeight - dockH;

    // ---- 浮层闸门：先算「chrome 现在接不接受鼠标」，再登记主题菜单的外部关闭 ----
    //
    // 四个开态正好对应 DrawFrame 尾部那四个在**根窗口**里提交 item 的浮层
    // （命令面板 / 设置模态 / 报告模态 / 主题菜单）。开着的时候 chrome 走
    // ChromeHit()、工作区 child 带 NoMouseInputs，于是浮层按钮拿得到
    // HoveredWindow，底下的侧栏 / 顶栏 / 工作区也点不动。
    // 机制（imgui.cpp:6573 / 6607 / 5156 / 5161）写在 ChromeHit 的注释里。
    chromeInteractive_ = !(paletteOpen_ || settingsOpen_ || reportDetail_ >= 0 || themeMenuOpen_);
    themeOutsideHit_ = kit::Hit{};
    if (themeMenuOpen_) {
        // 必须**先于** chrome 注册：同窗口内先注册者独占 HoveredId，晚了就抢不到。
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        themeOutsideHit_ = kit::HitTest(Rect{0.0f, 0.0f, d.x, d.y}, "theme-outside");
    }

    DrawTopBar(RectAt(0, 0, W, kTopBarHeight), draw);
    DrawRail(RectAt(0, bodyTop, kRailWidth, bodyBottom - bodyTop), draw);

    const float centerX = kRailWidth + sideW;
    const float centerW = W - kRailWidth - sideW - inspW;
    if (layout_.sidePanelVisible) {
        DrawSidePanel(RectAt(kRailWidth, bodyTop, sideW, bodyBottom - bodyTop), draw);
    }
    if (layout_.inspectorVisible) {
        DrawInspector(RectAt(W - inspW, bodyTop, inspW, bodyBottom - bodyTop), draw);
    }

    DrawBreadcrumbs(RectAt(centerX, bodyTop, centerW, kCrumbHeight), draw);
    workspace_ = RectAt(centerX, bodyTop + kCrumbHeight, centerW, bodyBottom - bodyTop - kCrumbHeight);
    DrawWorkspace(workspace_, draw);

    if (layout_.dockVisible) {
        DrawDock(RectAt(0, H - kStatusBarHeight - dockH, W, dockH), draw);
    }
    DrawStatusBar(RectAt(0, H - kStatusBarHeight, W, kStatusBarHeight), draw);

    DrawCommandPalette();
    DrawSettingsModal();
    DrawReportModal();
    DrawThemeMenu(themeMenuAnchor_, ImGui::GetForegroundDrawList());
    DrawOverlays(draw);

    ImGui::End();
    kit::DrawDebugWindows(debug_);
}

// ---------------------------------------------------------------- 工作区分发
void Shell::DrawWorkspace(Rect area, ImDrawList* /*draw*/) {
    // ⚠️ 必须用 ImGui::BeginChild 当滚动区：自绘控件只出 draw call，不出裁剪也不出
    //    滚动，内容超出既不会被裁也不会滚，鼠标还会穿透。BeginChild 一次给全。
    // ⚠️ 浮层开着的时候必须让这个 child 退出命中测试，否则浮层按钮全是死的。
    //    机制写在 Scroll.h 的构造注释里（`g.HoveredWindow` 归 child，根窗口 item
    //    一律 hovered=false）。这里四个开态正好对应 DrawFrame 尾部那四个在**根窗口**
    //    里提交 item 的浮层：命令面板 / 设置模态 / 报告模态 / 主题菜单。
    //    项目中心不走这里（整屏替身，浮层在 hub-scroll child 内部提交，本来就能点）。
    const bool overlayBlocksMouse = paletteOpen_ || settingsOpen_ || reportDetail_ >= 0 ||
                                    themeMenuOpen_;
    kit::ScrollRegion region("workspace-scroll", area, /*borders=*/false, /*horizontal=*/false,
                             /*noMouseInputs=*/overlayBlocksMouse);
    if (!region) {
        return;
    }
    const kit::Rect origin = region.content();

    // 页面必须画进 **child 自己的** draw list：BeginChild 的裁剪矩形只作用于它自己的
    // draw list。传父窗口的 draw 进去，内容会一路溢出盖住底栅和状态栏（实测过）。
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // ⚠️ 满幅画布页（出图/出片）不能撑高：它们的画布是「铺满可视区」语义，
    //    撑到 2400 会让 FlowCanvas 的 fit 按 2400 高居中，y 偏移直接顶出视口，
    //    结果一个节点都看不见（实测 canvas=644x2400 → y=1112）。这两页自带
    //    内部滚动，不需要外壳再撑一次。
    const bool fullBleed = layout_.workspace == 4 || layout_.workspace == 5;
    const kit::Rect view{origin.min,
                         ImVec2(origin.max.x, origin.min.y + (fullBleed ? origin.height() : 2400.0f))};

    // 页面要自建视口（侧栏 / 面板 / 列）时必须知道**真正能看见多少**，
    // 而不是下面那个 2400 的布局区高。见 WorkspacePages.h 的说明。
    pages::SetWorkspaceViewportHeight(origin.height());
    // 每帧清一次：上一页面自报的内容高**不能**被这一页继承（否则切工作区后滚动
    // 范围会停在上一页的值上）。没自报的页面这一帧读到 0，退回 2400。
    pages::ResetPageContentHeight();

    switch (layout_.workspace) {
    case 0: DrawOverview(view, draw); break;
    case 1: novel_.Draw(view, draw); break;
    case 2: assets_.Draw(view, draw); break;
    case 3: storyboard_.Draw(view, draw); break;
    case 4: imageflow_.Draw(view, draw); break;
    case 5: videoflow_.Draw(view, draw); break;
    default: gallery_.Draw(view, draw); break;
    }
    // ⚠️ 这行是整个外壳**唯一**让工作区能滚的地方，缺了它滚轮怎么转都停在原地。
    //    页面是纯自绘的，全程没给 ImGui 提交过 item，高度必须显式报上去。
    //
    //    高度优先用页面**自报的真实内容高**（`SetPageContentHeight`），没有自报的
    //    页面退回 2400 的布局区。2400 是「够用」不是「刚好」：页面若按它铺卡片就会
    //    铺出巨型空盒子（实测总控页「账本」卡 1750px 高、只有 6 行），并让工作区
    //    多出上千 px 只能滚到空白的滚动范围。
    const float reported = pages::PageContentHeight();
    region.setContentHeight(reported > 0.0f ? reported : view.height());
}

// ---------------------------------------------------------------- P4.6c 产物
// 真实扫 <projectRoot>/output。IO 走 worker：UI 线程只读缓存 artifacts_，
// 换工程 / 点刷新 / 超过 3 秒才重扫，不在每帧碰 std::filesystem。
void Shell::RequestArtifactScan() {
    if (artifactScanning_ || artifactScanRoot_ == layout_.projectRoot) {
        return;
    }
    const std::filesystem::path root = layout_.projectRoot / "output";
    artifactScanning_ = true;
    artifactScanRoot_ = layout_.projectRoot;
    async::RunOnWorker([this, root] {
        std::vector<ArtifactRow> found;
        std::error_code ec;
        if (std::filesystem::is_directory(root, ec)) {
            // 只收文件，深度 2：output/ 下面通常还有一层按阶段的子目录。
            for (auto it = std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) {
                    break;
                }
                if (!it->is_regular_file(ec)) {
                    continue;
                }
                ArtifactRow row;
                row.path = it->path();
                row.name = util::PathToUtf8(it->path().filename());
                row.bytes = it->file_size(ec);
                row.kind = util::PathToUtf8(it->path().extension());
                if (!row.kind.empty() && row.kind.front() == '.') {
                    row.kind.erase(row.kind.begin());
                }
                found.push_back(std::move(row));
                if (found.size() >= 200) {
                    break;  // 一次最多 200 条，够看也不至于把内存吃穿
                }
            }
        }
        std::stable_sort(found.begin(), found.end(), [](const ArtifactRow& a, const ArtifactRow& b) {
            return a.name < b.name;
        });
        async::PostToUi([this, root, rows = std::move(found)] {
            artifactScanning_ = false;
            artifactRoot_ = root;
            artifacts_ = std::move(rows);
            // 排下一次重扫。与报告页同一处修正：这里原来也是 `= 0.0f`，
            // 于是 aged 恒假、除了换工程再没有触发点。
            artifactRefreshAt_ = ImGui::GetTime() + 2.0f;
        });
    });
}

void Shell::DrawDockArtifacts(Rect body, ImDrawList* draw) {
    // 换工程 / 2 秒后重扫。空工程根不扫，直接给诚实空态。
    const bool stale = artifactScanRoot_ != layout_.projectRoot;
    const bool aged = artifactRefreshAt_ > 0.0f && ImGui::GetTime() >= artifactRefreshAt_;
    if (stale || aged) {
        artifactScanning_ = false;  // 上一次的结果作废
        RequestArtifactScan();
    }

    if (layout_.projectRoot.empty()) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 460.0f, body.min.y + 90.0f},
              "folder", "未打开工程", "产物浏览器指向项目的 output/ 目录");
        return;
    }
    if (artifacts_.empty() && !artifactScanning_) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 460.0f, body.min.y + 90.0f},
              "folder", "output/ 目录是空的", "跑完出图或出片后，产物会出现在这里");
        return;
    }

    // ⚠️ 这里原来有一处**静默截断**，而且是扫描器长期漏掉的形态：
    //        if (y + 24.0f > body.max.y - 16.0f) { break; }
    //    `tools/find-silent-truncation.ps1` 的正则当年写成 `\.max\.y\s*\)`，只吃
    //    「给下边界留页脚」之外的朴素写法，于是这个 `- 16.0f` 让它在**有真缺陷的树上
    //    报 0** —— 扫描器报 0 和「没有截断」长得一模一样。产物一多，第 N+1 个之后的
    //    文件**无声消失**，界面上看不出 output/ 里还有东西。
    //    改成画**全部**条目，列表自己滚；页脚那一句留在滚动区**外面**。
    constexpr float kArtRowH = 26.0f;
    constexpr float kArtFootH = 18.0f;
    const Rect listArea{body.min.x, body.min.y, body.max.x, body.max.y - kArtFootH};
    kit::ScrollRegion list("dock-artifact-list", listArea);
    if (list) {
        // 内容必须画在 child 自己的 draw list 上，否则裁剪无效（见 Scroll.h）。
        ImDrawList* ldraw = list.drawList();
        const Rect inner = list.content();
        float y = inner.min.y;
        for (const ArtifactRow& row : artifacts_) {
            // 产物行走 kit::ListRow：文件名 / 种类 / chevron 三段全在里面，
            // 原来自己算 `y + 5.0f`（行高 24）时文件名偏上 1.25px、种类偏上
            // 1.75px —— 同一行里两段字各偏各的，种类比文件名更歪。
            const Rect line{inner.min.x, y, inner.min.x + 520.0f, y + 24.0f};
            kit::ListRowSpec spec;
            spec.id = chromeInteractive_ ? "dock-art-" + row.name : std::string_view{};
            spec.icon = "folder";
            spec.iconSize = 13.0f;
            spec.iconGap = 6.0f;
            spec.iconColor = ColorAccentHover();
            spec.title = row.name;
            spec.titleSize = 11.5f;
            spec.titleMono = true;
            spec.titleColor = ColorText(); // 可点开的实体名：primary，不是 secondary
            spec.trailing = row.kind;
            spec.trailingSize = 10.5f;
            spec.paddingX = 3.0f;
            // 命中走 ChromeHit 的闸门语义：浮层开着时 id 传空 ⇒ 不注册 item。
            spec.suppressed = !chromeInteractive_;
            const kit::Hit hit = kit::ListRow(ldraw, line, spec);
            if (hit.clicked) {
                const std::string err = util::ShellOpen(row.path);
                PushLog(err.empty() ? "info" : "err",
                        std::string("打开产物 ") + row.name + (err.empty() ? "" : " 失败：" + err));
            }
            y += kArtRowH;
        }
        list.setContentHeight(y - inner.min.y);
    }
    const std::string foot =
        "共 " + std::to_string(artifacts_.size()) + " 个产物 · 指向 " +
        util::PathToUtf8(artifactRoot_) + " · 点击条目用系统默认程序打开";
    draw->AddText(FontAt(10.5f), 10.5f, ImVec2(body.min.x, body.max.y - 14.0f), ColorTextMuted(),
                  foot.data(), foot.data() + foot.size());
}

// ---------------------------------------------------------------- P4.9a 主题菜单
// 照 webui Shell.jsx:51-70：幽灵按钮弹出的 .menu-pop（shell.css:101-158），
// 宽 224，5 个内置主题各一行（双色渐变方块 + 名字 + 英文），当前主题右侧打勾；
// 下面一条分隔线 + 「减少动效」开关项。
// 早先这里根本不存在 —— 顶栏那个调色板图标直接去翻命令面板，主题压根切不了。
void Shell::DrawThemeMenu(ImVec2 anchor, ImDrawList* draw) {
    if (!themeMenuOpen_) {
        return;
    }
    // 主题菜单走 kit::Menu：面板框 / 行 hover / 选中态 / 文字居中 / 命中全在里面。
    // 页面层原来手写一份（面板宽 224、行高 30 步进 32、手排 `row.min.y + 8.0f`
    // 偏上 0.75px），而 kit::Menu 那份是按 shell.css:98-145 算好的、零调用。
    //
    // 行序：内置主题（Label）→ 5 个主题（Item，选中态走 selected）→ 分隔 → 减少动效。
    std::vector<kit::MenuRow> rows;
    kit::MenuRow groupLabel;
    groupLabel.kind = kit::MenuRowKind::Label;
    groupLabel.label = "内置主题";
    rows.push_back(groupLabel);
    for (std::size_t i = 0; i < theme::kAllThemes.size(); ++i) {
        const theme::ThemeId id = theme::kAllThemes[i];
        kit::MenuRow row;
        row.kind = kit::MenuRowKind::Item;
        row.label = std::string(theme::ThemeDisplayName(id));
        // 设计稿这一行左边是 24×14 的双向渐变色块（shell.css:148 的 .swatch），
        // 不是图标字形 —— kit::Menu 的 icon 位画不了。这是有意的取舍：
        // 色块那一族只有主题菜单在用，为它给 kit::Menu 加一个「自定义左侧绘制」
        // 回调，会把一个纯数据菜单变成带副作用的绘制口。**代价是主题色块没有了**，
        // 选中态改由 kit 的 accent 字 + accent-dim 底表达（信息量不减）。
        row.selected = theme::CurrentThemeId() == id;
        rows.push_back(row);
    }
    kit::MenuRow sep;
    sep.kind = kit::MenuRowKind::Separator;
    rows.push_back(sep);
    kit::MenuRow motion;
    motion.kind = kit::MenuRowKind::Item;
    motion.label = "减少动效";
    motion.icon = "zap";
    motion.selected = layout_.reduceMotion;
    rows.push_back(motion);

    // 锚点：kit::Menu 贴 anchor 右下展开。主题按钮在顶栏右侧，锚点给它左下角。
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Rect menuAnchor{std::min(anchor.x, display.x - 200.0f), anchor.y,
                          std::min(anchor.x, display.x - 200.0f) + 200.0f, anchor.y + 30.0f};
    const int picked = kit::Menu(draw, menuAnchor, rows, "theme-menu");
    if (picked < 0) {
        return;
    }
    // 行下标 → 动作。1..5 是主题，末尾是「减少动效」。
    if (picked >= 1 && static_cast<std::size_t>(picked) <= theme::kAllThemes.size()) {
        const theme::ThemeId id = theme::kAllThemes[static_cast<std::size_t>(picked - 1)];
        SetTheme(id);
        themeMenuOpen_ = false;
        PushLog("info", "主题已切换：" + std::string(theme::ThemeDisplayName(id)));
    } else if (static_cast<std::size_t>(picked) == rows.size() - 1) {
        layout_.reduceMotion = !layout_.reduceMotion;
        kit::SetReduceMotion(layout_.reduceMotion);
        PushLog("info", layout_.reduceMotion ? "已减少动效" : "已恢复动效");
    }

    // 点菜单外面关掉（对应 webui 的 pointerdown 外部关闭）。
    //
    // ⚠️ `themeOutsideHit_` 的**注册**不在这里，而在 DrawFrame 开头、外壳 chrome
    // **之前**。同窗口内「先注册者独占 HoveredId」（imgui.cpp:5161）：注册在
    // chrome 之后的话，导航栏 / 顶栏 / 工作区任何一处都先抢到这次点击，于是
    // 「点外面关菜单」变成「切了工作区、菜单还开着悬在新工作区上」。
    if (themeOutsideHit_.clicked) {
        themeMenuOpen_ = false;
    }
}

// ---------------------------------------------------------------- P4.9b 设置模态
// 「设置 · 三步开工」（Shell.jsx:74 的 IconBtn tip）。这里读 AppSettings 的真值，
// 让用户看到程序**实际**连的是什么，而不是一份编出来的配置。
//
// 外壳是第 5 份「页面层私有浮层副本」，本轮收进 `kit::ModalFrameRect`。
// 收掉的不只是壳，还有 `SettingsCloseRect()` 里那份**独立的 560 × 452 复算** ——
// 原来「绘制与判据共用同一份几何」这句话只对了一半：判据没在 Review.cpp 里复算，
// 但产品内部早就分叉成两处。几何现在只有 `mf` 一处算出来，写进 settingsFrame_
// / settingsClose_，绘制与判据都读它。
void Shell::DrawSettingsModal() {
    if (!settingsOpen_) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const kit::Rect screen{0.0f, 0.0f, display.x, display.y};
    // 浮层走 foreground，否则会被 BeginChild 里的页面内容盖住。
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // 遮罩 / 面板框 / 头部 / 内容区一次拿全。遮罩**只画不注册命中**：注册全屏热区
    // 会先拿到 HoveredId，本模态里的按钮就永远按不动（先注册者独占）。
    const kit::ModalFrame mf =
        kit::ModalFrameRect(draw, screen, "设置 · 三步开工", "settings", 560.0f, 452.0f);
    settingsFrame_ = mf.frame;
    // × 落在头部右侧、垂直居中（22px 方钮）。位置在这里算一次就够了 ——
    // 下面的绘制与判据的 SettingsCloseRect() 读的都是 settingsClose_。
    //
    // ⚠️ 纵向偏移**取整**：头部高 47 是奇数，减去 22 除 2 得 12.5，于是这个 22×22
    //    的方钮两条边都会压在半像素上。半像素本身画得出来，但「× 的中心」于是
    //    落在 277.5 —— 判据注入鼠标后按 0.5 的容差判「注入到位吗」，差正好卡在
    //    边界上，报出来的是一条**自相矛盾**的消息（要 277、看到 277，却说没到位）。
    //    一像素以内不该算「注入失败」，但产品这边也没必要制造半像素。
    settingsClose_ = RectAt(mf.frame.max.x - 18.0f - 22.0f,
                            mf.header.min.y + std::round((mf.header.height() - 22.0f) * 0.5f),
                            22.0f, 22.0f);
    if (IconButton(draw, settingsClose_, "x", false, false, "settings-close")) {
        settingsOpen_ = false;
    }

    const AppSettings& s = Settings();
    const Rect& body = mf.body;
    float y = body.min.y;
    const float ix = body.min.x;
    const float iw = body.width();

    KeyValues(draw, Rect{ix, y, ix + iw, y + 96.0f},
              {{"当前工程", layout_.projectName.empty() ? "未打开项目" : layout_.projectName},
               {"工程根", layout_.projectRoot.empty() ? "—" : util::PathToUtf8(layout_.projectRoot)},
               {"数据目录", util::PathToUtf8(shine::app::EnvironmentPath(L"APPDATA"))}});
    y += 106.0f;

    KeyValues(draw, Rect{ix, y, ix + iw, y + 118.0f},
              {{"LLM 供应商", s.llmProvider},
               {"默认模型", s.openaiModelDefault},
               {"写作模型", s.openaiModelWriter},
               {"评审模型", s.openaiModelCritic},
               {"ComfyUI", s.comfyBaseUrl.empty() ? "未配置" : s.comfyBaseUrl}});
    y += 128.0f;

    KeyValues(draw, Rect{ix, y, ix + iw, y + 96.0f},
              {{"图库来源", s.gallerySource},
               {"缩略图尺寸", std::to_string(s.galleryThumbSize)},
               {"产物目录", s.videoOutputDir.empty() ? "未配置" : s.videoOutputDir}});
    y += 106.0f;

    static constexpr char kSettingsNote[] =
        "以上取自 %APPDATA%/ShineTVStudio/settings.json · API Key 不在此显示";
    draw->AddText(FontAt(10.5f), 10.5f, ImVec2(ix, y), ColorTextMuted(), kSettingsNote,
                  kSettingsNote + sizeof(kSettingsNote) - 1);

    // 点遮罩关闭：既不注册 item（免得遮罩先拿到 HoveredId，把模态里的按钮全变成
    // 死键），也不用 ImGui::IsMouseHoveringRect（本工程会 0xC0000005），
    // 改成手算点击是否落在面板内。面板矩形用上面存下的 settingsFrame_。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !settingsFrame_.contains(ImGui::GetIO().MousePos)) {
        settingsOpen_ = false;
    }
}

// ---------------------------------------------------------------- P4.9c 项目中心
void Shell::ToggleProjectHub() {
    hubOpen_ = !hubOpen_;
    themeMenuOpen_ = false;
    settingsOpen_ = false;
    if (hubOpen_) {
        PushLog("info", "进入项目中心");
    }
}

void Shell::DrawProjectHubScreen(Rect area, ImDrawList* draw) {
    // 只留一条顶栏：品牌 + 「返回工作区」+ 当前工程。项目中心自己的页头由它自己画。
    DrawRoundRect(draw, area.min, ImVec2(area.max.x, area.min.y + kTopBarHeight), 0.0f,
                  GlassColor());
    draw->AddLine(ImVec2(area.min.x, area.min.y + kTopBarHeight - 0.5f),
                  ImVec2(area.max.x, area.min.y + kTopBarHeight - 0.5f), ColorLineSubtle(), 1.0f);
    const float cy = 0.5f * kTopBarHeight;
    DrawIcon(draw, "grid", ImVec2(area.min.x + 18.0f, cy - 8.0f), 16.0f, ColorAccent());
    static constexpr char kHubTitle[] = "项目中心";
    draw->AddText(FontBoldAt(13.5f), 13.5f, ImVec2(area.min.x + 40.0f, cy - 6.75f), ColorText(),
                  kHubTitle, kHubTitle + sizeof(kHubTitle) - 1);
    {
        ButtonSpec backSpec;
        backSpec.variant = ButtonVariant::Secondary;
        backSpec.icon = "chevron";
        if (Button(draw, RectAt(area.max.x - 110.0f, cy - 14.0f, 94.0f, 28.0f), "返回工作区", backSpec,
                   "hub-back")) {
            ToggleProjectHub();
        }
    }
    if (Button(draw, RectAt(16.0f, cy - 14.0f, 26.0f, 28.0f), "←", ButtonSpec{},
               "hub-esc")) {
        ToggleProjectHub();
    }

    const Rect view{area.min.x, area.min.y + kTopBarHeight, area.max.x, area.max.y};
    kit::ScrollRegion region("hub-scroll", view);
    if (!region) {
        return;
    }
    // 内容高度由 DrawProjectHub **自报**（`SetPageContentHeight`）—— 只有它知道栅格
    // 排了几行、每行多高。
    //
    // ⚠️ 这里**不能**回退成「上报视口高」。上一版写的正是
    //    `setContentHeight(region.content().height())` —— 上报值恰好等于视口高 ⇒
    //    ScrollMaxY 恒为 0 ⇒ 项目超过一屏就被裁掉且滚不到（1080 高窗口约 6 张，
    //    第 7 张起够不着）。**看着加了、实际等于没加**，是「改了但没生效」最典型的形态：
    //    编译器不报错、像素看不出来，只有真去滚才发现。
    pages::ResetPageContentHeight();
    DrawProjectHub(region.content(), ImGui::GetWindowDrawList());
    const float hubContentH = pages::PageContentHeight();
    if (hubContentH <= 0.0f) {
        // 只报一次。真没上报时这属于覆盖洞（项目卡够不着），静默返回等于把它藏起来。
        static bool warned = false;
        if (!warned) {
            warned = true;
            shine::log::Error("hub-scroll: DrawProjectHub 没上报内容高度 —— "
                              "项目卡超过一屏就滚不到（ScrollMaxY 恒为 0）");
        }
    }
    region.setContentHeight(hubContentH);
}

} // namespace shine::pages