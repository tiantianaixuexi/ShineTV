#include "ui/imgui/pages/Shell.h"

#include "comfy/ComfySession.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "ui/imgui/host/AppEnvironment.h"
#include "ui/imgui/kit/Fonts.h"
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
        if (HitTest(hit, "tb-brand").clicked) {
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
    if (HitTest(capsule, "tb-projchip").clicked) {
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
    if (HitTest(search, "tb-search").clicked) {
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
        const Hit hit = HitTest(item, "tb-comfy");
        if (hit.hovered) {
            DrawRoundRect(draw, item.min, item.max, 4.0f, ColorFillHover());
        }
        StatusDot(draw, ImVec2(item.min.x + 8.0f + 3.5f, cy), tone, false);
        draw->AddText(f, 11.5f, ImVec2(item.min.x + 8.0f + 11.0f + 6.0f, cy - 5.75f),
                      ColorTextSecondary(), text.data(), text.data() + text.size());
        if (hit.clicked) {
            // 点开设置模态看连接细节，而不是像设计稿那样凭空把连接状态翻个面。
            settingsOpen_ = true;
        }
        rx -= w + 6.0f;
    }

    rx -= 28.0f;
    if (IconButton(draw, RectAt(rx, cy - 14.0f, 28.0f, 28.0f), "settings", false, false,
                   "tb-settings", "设置")) {
        settingsOpen_ = !settingsOpen_;
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
            settingsOpen_ = false;
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
            runActive_ = false;
            PushLog("warn", "流水线已请求停止");
        }
    } else {
        const std::string_view runLabel = "运行";
        const float runW = ButtonWidth(ButtonSize::Medium, 15.0f, labelWidth(runLabel));
        rx -= runW;
        ButtonSpec runSpec;
        runSpec.variant = ButtonVariant::Primary;
        runSpec.icon = "play";
        if (Button(draw, RectAt(rx, 0.5f * (area.min.y + area.max.y) - 15.0f, runW, 30.0f),
                   runLabel, runSpec, "tb-run")) {
            runActive_ = true;
            runFinished_ = false;
            runStageIndex_ = 0;
            runPercent_ = 0;
            PushLog("info", "流水线开始运行");
        }
    }
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
        const Hit hit = kit::HitTest(item, "rail-ws-" + std::to_string(i));
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
        const bool hover = kit::Hovered(item, "rail-tg-" + std::string(toggle.icon));
        if (hover) {
            DrawRoundRect(draw, ImVec2(item.min.x + 6.0f, item.min.y), ImVec2(item.max.x - 6.0f, item.max.y),
                          10.0f, ColorFillHover());
        }
        DrawIconCentered(draw, toggle.icon, ImVec2(area.center().x, item.center().y), 19.0f,
                         hover ? ColorText() : ColorTextMuted());
        if (kit::Clicked(item, "rail-tg-" + std::string(toggle.icon))) {
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
    if (kit::Clicked(gallery, "rail-gallery")) {
        SetWorkspace(6);
    }
}

// ---------------------------------------------------------------- P4.4 侧栏
// ---------------------------------------------------------------- P4.5 侧栏
namespace {

// 侧栏树的「可见节点扁平序号」（先序）↔ (章下标, 镜下标) 的互转。
// kit::Tree 只认一个 int，且只画**展开**节点的子节点 —— 所以两处都按同一套先序走，
// 收起节点的子树不占序号。镜下标 = -1 表示命中的是章节点。
int FlatTreeIndex(const std::vector<TreeNode>& nodes, int chapter, int shot) {
    int i = 0;
    for (std::size_t c = 0; c < nodes.size(); ++c) {
        const int at = i++;
        if (static_cast<int>(c) == chapter) {
            if (shot < 0) {
                return at;
            }
            if (nodes[c].expanded) {
                return at + 1 + shot;
            }
            return at;
        }
        if (nodes[c].expanded) {
            i += static_cast<int>(nodes[c].children.size());
        }
    }
    return -1;
}

void ResolveTreeIndex(const std::vector<TreeNode>& nodes, int index, int& chapterOut,
                      int& shotOut) {
    int i = 0;
    for (std::size_t c = 0; c < nodes.size(); ++c) {
        if (i++ == index) {
            chapterOut = static_cast<int>(c);
            shotOut = -1;
            return;
        }
        if (!nodes[c].expanded) {
            continue;
        }
        for (std::size_t k = 0; k < nodes[c].children.size(); ++k) {
            if (i++ == index) {
                chapterOut = static_cast<int>(c);
                shotOut = static_cast<int>(k);
                return;
            }
        }
    }
    chapterOut = -1;
    shotOut = -1;
}

// 镜码 S001 —— 实现与注释都在 WorkspacePages.h（Shell / 故事板 / 检查器共用一份）。
using pages::ShotCode;

// 库里是空串时给「—」，不留空白格（KeyValues 的值列空着看不出是"没值"还是"漏了"）。
std::string OrDash(const std::string& value) { return value.empty() ? "—" : value; }

// 设计稿的侧栏树只挂在小说 / 分镜 / 出图 / 出片上（Shell.jsx 的 NovelSideTree 与
// ShotSideTree）；总控是 KPI 面板，资产是 kind 筛选树（下一轮）。
bool WorkspaceHasShotTree(int workspace) {
    return workspace == static_cast<int>(Workspace::Novel) ||
           workspace == static_cast<int>(Workspace::Storyboard) ||
           workspace == static_cast<int>(Workspace::ImageFlow) ||
           workspace == static_cast<int>(Workspace::VideoFlow);
}

} // namespace

void Shell::DrawSidePanel(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.max.x - 0.5f, area.min.y), ImVec2(area.max.x - 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    // 折叠把手 20×48 贴在右缘中点
    const Rect handle{area.max.x - 20.0f, area.center().y - 24.0f, area.max.x, area.center().y + 24.0f};
    DrawRoundRect(draw, handle.min, handle.max, 6.0f, ColorPanel(), ColorLineNormal(), 1.0f);
    DrawIconCentered(draw, "chevron", handle.center(), 12.0f, ColorTextMuted());
    if (kit::Clicked(handle, "side-collapse")) {
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
    if (!WorkspaceHasShotTree(layout_.workspace)) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "此工作区没有侧栏树",
              "总控是 KPI 面板；资产工作区的 kind 筛选树还没接（下轮）");
        return;
    }

    const BookSideView& book = BookSide();
    if (!book.bound) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "结构尚未载入",
              "切到小说 / 分镜任一页会读取 novel.db 里的真实章节");
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
    if (book.chapters.empty()) {
        Empty(draw, Rect{x, y, area.max.x - 10.0f, y + 132.0f}, "book", "这本小说还没有章",
              "跑一次 T1–T17 之后这里才有章节与镜头");
        return;
    }

    // ⚠️ 设计稿的 NovelSideTree 是「书 / 卷 / 章」三层（Shell.jsx:583-662）。卷这一层要
    //    novelcore::NovelGraph::ListVolumes()，而它**不存在**（只有 UpsertVolume）——
    //    补它要动 shine_core。所以这里画「书 / 章」两层，卷层级在进度文档里记为缺口。
    //
    //    镜只挂在**当前选中章**下面：场/镜是对选中章取的，没选过的章没有镜数据，
    //    画一排空节点等于骗人。
    const bool withShots = layout_.workspace != static_cast<int>(Workspace::Novel);
    std::vector<TreeNode> nodes;
    nodes.reserve(book.chapters.size());
    for (std::size_t i = 0; i < book.chapters.size(); ++i) {
        const BookChapterView& chapter = book.chapters[i];
        TreeNode node;
        node.label = "第 " + std::to_string(chapter.ord) + " 章" +
                     (chapter.title.empty() ? "" : " · " + chapter.title);
        node.icon = "book";
        node.trailing = OrDash(chapter.status);
        node.expanded = withShots && static_cast<int>(i) == book.selectedChapter;
        node.hasChildren = node.expanded;
        if (node.expanded) {
            for (const BookShotView& shot : book.shots) {
                TreeNode leaf;
                leaf.label = ShotCode(shot.ord) + (shot.action.empty() ? "" : " · " + shot.action);
                leaf.icon = "clapper";
                node.children.push_back(std::move(leaf));
            }
        }
        nodes.push_back(std::move(node));
    }

    const int wanted = withShots ? FlatTreeIndex(nodes, book.selectedChapter, book.selectedShot)
                                 : FlatTreeIndex(nodes, book.selectedChapter, -1);
    int picked = wanted;
    const Rect treeArea{x, y, area.max.x - 20.0f, area.max.y - 10.0f};
    Tree(draw, treeArea, nodes, picked, "side-tree");
    if (picked == wanted) {
        return;  // 没点中
    }
    int chapterIndex = -1;
    int shotIndex = -1;
    ResolveTreeIndex(nodes, picked, chapterIndex, shotIndex);
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
void Shell::DrawInspector(Rect area, ImDrawList* draw) {
    DrawRoundRect(draw, area.min, area.max, 0.0f, ColorSurface());
    draw->AddLine(ImVec2(area.min.x + 0.5f, area.min.y), ImVec2(area.min.x + 0.5f, area.max.y),
                  ColorLineSubtle(), 1.0f);

    const float x = area.min.x + 16.0f;
    const float w = area.width() - 32.0f;
    float y = area.min.y + 12.0f;

    // 3 段可折叠：属性 / 预览 / 关联
    struct Section {
        const char* title;
        bool open;
    };
    const Section sections[] = {{"属性", true}, {"预览", true}, {"关联", false}};
    for (const Section& section : sections) {
        const Rect header{x, y, x + w, y + 24.0f};
        DrawIcon(draw, section.open ? "chevdown" : "chevron", ImVec2(header.min.x, header.center().y - 5.0f),
                 10.0f, ColorTextMuted());
        draw->AddText(FontBoldAt(12.5f), 12.5f, ImVec2(header.min.x + 16.0f, header.min.y + 5.0f),
                      ColorText(), section.title, section.title + std::strlen(section.title));
        draw->AddLine(ImVec2(header.min.x, header.max.y), ImVec2(header.max.x, header.max.y),
                      ColorLineSubtle(), 1.0f);
        y += 26.0f;
        if (section.open) {
            // ⚠️ 这里原先写死 {代码:S012, 动作:转身, 时长:6.0s, 情绪:克制} —— 一组
            //    编出来的镜头属性，在任何工程、任何项目下都长这样，点了也不跟着选中项变。
            if (std::strcmp(section.title, "属性") == 0) {
                // 现在读侧栏那份只读快照：选中了镜就给镜的字段，只选了章就给章的字段，
                // 两者都没有才给空态。空串一律显示「—」，不靠留白表示"没值"。
                const BookSideView& book = BookSide();
                const bool hasShot = !book.shots.empty() && book.selectedShot >= 0 &&
                                     book.selectedShot < static_cast<int>(book.shots.size());
                const bool hasChapter = book.selectedChapter >= 0 &&
                                        book.selectedChapter <
                                            static_cast<int>(book.chapters.size());
                if (!book.bound || (!hasShot && !hasChapter)) {
                    Empty(draw, Rect{x, y, x + w, y + 76.0f}, "target",
                          layout_.projectRoot.empty() ? "未打开工程" : "未选中条目",
                          layout_.projectRoot.empty()
                              ? "打开工程并选中一个条目后，这里显示它的真实属性"
                              : "在左侧侧栏树里选中章节 / 镜头后，这里显示它的真实属性");
                    y += 84.0f;
                } else if (hasShot) {
                    const BookShotView& shot =
                        book.shots[static_cast<std::size_t>(book.selectedShot)];
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
                               {"场", "第 " + std::to_string(shot.sceneOrd) + " 场"},
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
            } else if (std::strcmp(section.title, "预览") == 0) {
                Art(draw, Rect{x, y, x + w, y + 110.0f}, 5, true);
                // 上面那块是设计稿自带的示意插画（Art()），不是这个工程的出图结果。
                // 不写这句，读者会以为它是该镜的真实渲染。
                DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(x, y + 96.0f), w, ColorTextMuted(),
                                "示意插画 · 非本工程出图结果");
                y += 118.0f;
            }
        }
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
    const std::string_view picked = Tabs(draw, Rect{area.min.x + 12.0f, area.min.y, 400.0f, 32.0f},
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
    float y = body.min.y;
    for (const comfy::QueueModel::Row& row : rows) {
        if (y + 30.0f > body.max.y) {
            break;
        }
        const bool running = row.state == comfy::TaskState::Running;
        const bool failed = row.state == comfy::TaskState::Failed;
        const Rect line{body.min.x, y, body.max.x, y + 30.0f};
        if (HitTest(line, "dock-q-" + row.promptId).hovered) {
            DrawRoundRect(draw, line.min, line.max, 6.0f, ColorFillHover());
        }
        StatusDot(draw, ImVec2(line.min.x + 4.0f, line.center().y),
                  failed ? theme::Tone::Danger : (running ? theme::Tone::Busy : theme::Tone::Idle),
                  running);
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(line.min.x + 16.0f, y + 3.0f), 220.0f,
                        ColorTextSecondary(),
                        row.label.empty() ? row.promptId : row.label);
        // 细进度 + 百分比：走 QueueModel 的真实 progress，不是写死的 42。
        const float pct = row.progress * 100.0f;
        Progress(draw, Rect{line.min.x + 248.0f, y + 12.0f, line.min.x + 408.0f, y + 16.0f}, pct,
                 running, true);
        if (row.progressMax > 0) {
            const std::string p = std::to_string(row.progressValue) + "/" +
                                  std::to_string(row.progressMax) + " · " +
                                  std::to_string(static_cast<int>(pct)) + "%";
            DrawTextClipped(draw, MonoAt(10.5f), 10.5f,
                            ImVec2(line.min.x + 416.0f, y + 4.0f), 180.0f, ColorTextMuted(), p);
        }
        const Rect tagBox = RectAt(line.max.x - TagWidth("", true, false) - 6.0f, y + 6.0f,
                                   TagWidth("", true, false), TagHeight(true));
        Tag(draw, tagBox, failed ? "失败" : (running ? "运行中" : "排队"),
            failed ? theme::Tone::Danger : (running ? theme::Tone::Busy : theme::Tone::Idle), true);
        y += 32.0f;
    }

    ImFont* f = FontAt(10.5f);
    const std::string foot = rows.empty()
                                 ? "队列为空 · Comfy 未提交任务（或未连接）"
                                 : "队列由出图 / 出片 / 小说生成共享";
    draw->AddText(f, 10.5f, ImVec2(body.min.x, y + 4.0f), ColorTextMuted(), foot.data(),
                  foot.data() + foot.size());
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
    // 最新的在下面（webui 勾到底），行高 18，超出按行数裁。
    const int maxRows = std::max(1, static_cast<int>((body.height() - 4.0f) / 18.0f));
    const int first = std::max(0, static_cast<int>(logLines_.size()) - maxRows);
    float y = body.min.y;
    for (int i = first; i < static_cast<int>(logLines_.size()); ++i) {
        draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(body.min.x, y), ColorTextSecondary(),
                      logLines_[static_cast<std::size_t>(i)].c_str(), nullptr);
        y += 18.0f;
    }
    if (runActive_) {
        // 运行中在末尾留一个闪烁光标块（Shell.jsx:236 的 log-caret）。
        const float blink = Pulse(1.0f) > 0.5f ? 1.0f : 0.0f;
        if (blink > 0.0f) {
            DrawRoundRect(draw, ImVec2(body.min.x, y + 3.0f), ImVec2(body.min.x + 7.0f, y + 15.0f),
                          1.0f, ColorAccent());
        }
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
    const char* version = "v0.2.0";
    const float versionW =
        FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, version, version + std::strlen(version)).x;
    float rightX = area.max.x - 10.0f - versionW - 16.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX + 8.0f, cy - 5.75f), ColorTextMuted(), version,
                  version + std::strlen(version));
    rightX -= themeW + 16.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX + 8.0f, cy - 5.75f), ColorTextSecondary(),
                  themeName.data(), themeName.data() + themeName.size());
    // 96px 细进度 + T{n}/17 · {pct}%
    // ⚠️ 这里是**真实运行态**，不是写死的 "T3/17 · 42%"。未运行时显示"未运行"，
    //    已完成显示"已完成"（webui Shell.jsx:330 三态），只有运行中才报阶段号。
    std::string progress = "未运行";
    if (runActive_) {
        progress = "T" + std::to_string(runStageIndex_ + 1) + "/17 · " +
                   std::to_string(runPercent_) + "%";
    } else if (runFinished_) {
        progress = "已完成";
    }
    const float progressW =
        FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, progress.data(), progress.data() + progress.size()).x;
    rightX -= 96.0f + 8.0f + progressW + 8.0f;
    Progress(draw, Rect{rightX, cy - 2.0f, rightX + 96.0f, cy + 2.0f},
             static_cast<float>(runPercent_), runActive_, true);
    rightX -= progressW + 8.0f;
    draw->AddText(FontAt(11.5f), 11.5f, ImVec2(rightX, cy - 5.75f), ColorTextSecondary(),
                  progress.data(), progress.data() + progress.size());
}

// ---------------------------------------------------------------- P4.8 面包屑
void Shell::DrawBreadcrumbs(Rect area, ImDrawList* draw) {
    const float cy = area.center().y;
    float x = area.min.x + 24.0f;
    const char* crumbs[] = {"项目", WorkspaceLabel(layout_.workspace),
                            layout_.lastViewLabel.empty() ? "总览" : layout_.lastViewLabel.c_str()};
    for (int i = 0; i < 3; ++i) {
        const bool current = (i == 2);
        draw->AddText(current ? FontBoldAt(12.5f) : FontAt(12.5f), 12.5f, ImVec2(x, cy - 6.25f),
                      current ? ColorText() : ColorTextSecondary(), crumbs[i],
                      crumbs[i] + std::strlen(crumbs[i]));
        x += FontAt(12.5f)->CalcTextSizeA(12.5f, 1e9f, 0.0f, crumbs[i],
                                           crumbs[i] + std::strlen(crumbs[i]))
                  .x +
             8.0f;
        if (i < 2) {
            // ⚠️ 长度用 sizeof() - 1，**不要**写死字节数。
            //    这里原来写的是一个单角引号 U+203A 加 "+ 3"：它在 UTF-8 里正好 3 字节，
            //    当时对得上；但字面量一旦被改写成一个 1 字节的字符（编码往返、编辑器保存），
            //    那个 3 就会越界多读 2 字节 —— 实测面包屑上画成了两个问号
            //    （? 后面跟的是字符串池里恰好相邻的字节，纯属巧合，不报错、不崩，
            //    只是永远画不对）。
            static constexpr char kSep[] = "›";
            draw->AddText(FontAt(12.5f), 12.5f, ImVec2(x, cy - 6.25f),
                          WithAlpha(ColorTextMuted(), 0.55f), kSep, kSep + sizeof(kSep) - 1);
            x += 12.0f;
        }
    }

    // 右侧提示串
    const char* hint = "Ctrl+B 侧栏 · Ctrl+J 底栏 · Ctrl+I 检查器 · Ctrl+K 命令";
    ImFont* font = FontAt(11.5f);
    const float w = font->CalcTextSizeA(11.5f, 1e9f, 0.0f, hint, hint + std::strlen(hint)).x;
    draw->AddText(font, 11.5f, ImVec2(area.max.x - 24.0f - w, cy - 5.75f), ColorTextMuted(), hint,
                  hint + std::strlen(hint));
}

// ---------------------------------------------------------------- P4.9 命令面板
void Shell::DrawCommandPalette() {
    if (!paletteOpen_) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(560.0f, display.x - 40.0f);
    const Rect bounds{(display.x - width) * 0.5f, display.y * 0.22f, (display.x + width) * 0.5f,
                      display.y * 0.22f + 420.0f};
    // 浮层必须画在 foreground：页面/底栏各自跑在 BeginChild 里，child 在父窗口那份
    // draw list **之后**渲染，画在父 list 上的浮层会被整片盖住（见 DrawReportModal 的注释）。
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    DrawRoundRect(draw, bounds.min, bounds.max, 14.0f, ColorScrim());
    DrawShadowed(draw, bounds.min, bounds.max, 14.0f, ColorOverlay(), ColorLineNormal(), 1.0f);

    // 输入
    const Rect input{bounds.min.x + 18.0f, bounds.min.y + 18.0f, bounds.max.x - 18.0f,
                     bounds.min.y + 56.0f};
    ImFont* font = FontAt(15.0f);
    if (ImGui::IsItemActive() || paletteQuery_[0] == '\0') {
    }
    // 直接用 ImGui 输入框承载中文输入法
    ImGui::SetCursorScreenPos(input.min);
    ImGui::SetNextItemWidth(input.width());
    ImGui::PushFont(font);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImU32(0));
    ImGui::PushStyleColor(ImGuiCol_Text, ColorText());
    ImGui::InputText("##palette", paletteQuery_, sizeof(paletteQuery_));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopFont();

    // 三组：页面 / 命令 / 主题；过滤 = label+hint+group 的小写 includes，空组丢弃
    struct Item {
        std::string group;
        std::string label;
        std::string hint;
        int workspace;
    };
    const std::vector<Item> all = {
        {"页面", "总控", "pipeline", 0},       {"页面", "小说", "novel", 1},
        {"页面", "资产", "assets", 2},        {"页面", "分镜", "storyboard", 3},
        {"页面", "出图", "imageflow", 4},     {"页面", "出片", "videoflow", 5},
        {"页面", "组件画廊", "gallery", 6},    {"命令", "新建项目", "Ctrl+N", -1},
        {"命令", "打开项目", "Ctrl+O", -1},    {"命令", "保存布局", "layout.dat", -1},
        {"命令", "切换主题", "Ctrl+T", -2},    {"主题", "深空", "deepspace", -3},
        {"主题", "薄暮", "dusk", -4},          {"主题", "纸墨", "paperink", -5},
        {"主题", "水墨", "inkwash", -6},       {"主题", "极夜", "polarnight", -7},
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
    if (ImGui::IsMouseHoveringRect(listArea.min, listArea.max, true)) {
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
    ImFont* labelFont = FontAt(13.0f);
    for (int i = 0; i < paletteMatches_; ++i) {
        const Item& item = *matched[static_cast<std::size_t>(i)];
        if (item.group != lastGroup) {
            lastGroup = item.group;
            draw->AddText(groupFont, 11.5f, ImVec2(bounds.min.x + 18.0f, y + 6.0f), ColorTextMuted(),
                          item.group.data(), item.group.data() + item.group.size());
            y += kPaletteGroupH;
        }
        const Rect row{bounds.min.x + 12.0f, y, bounds.max.x - 12.0f, y + kPaletteRowH};
        if (i == paletteSelected_) {
            DrawRoundRect(draw, row.min, row.max, 6.0f, ColorFillSelected());
        }
        draw->AddText(labelFont, 13.0f, ImVec2(row.min.x + 10.0f, row.min.y + 6.0f), ColorText(),
                      item.label.data(), item.label.data() + item.label.size());
        draw->AddText(FontAt(11.5f), 11.5f, ImVec2(row.max.x - 80.0f, row.min.y + 7.0f),
                      ColorTextMuted(), item.hint.data(), item.hint.data() + item.hint.size());
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
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        paletteOpen_ = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && paletteMatches_ > 0) {
        const Item& picked = *matched[static_cast<std::size_t>(paletteSelected_)];
        if (picked.workspace >= 0) {
            SetWorkspace(picked.workspace);
        }
        paletteOpen_ = false;
        paletteQuery_[0] = '\0';
        paletteSelected_ = 0;
    }
}

// ---------------------------------------------------------------- P4.10 浮层
void Shell::DrawOverlays(ImDrawList* draw) { (void)draw; }

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
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
        paletteOpen_ = !paletteOpen_;
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
    layout_.lastViewLabel = "总览";
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
        ImGui::End();
        kit::DrawDebugWindows(debug_);
        return;
    }

    const float dockH = layout_.dockVisible ? static_cast<float>(layout_.dockHeight) : 0.0f;
    const float sideW = layout_.sidePanelVisible ? static_cast<float>(layout_.sidePanelWidth) : 0.0f;
    const float inspW = layout_.inspectorVisible ? static_cast<float>(layout_.inspectorWidth) : 0.0f;
    const float bodyTop = kTopBarHeight;
    const float bodyBottom = H - kStatusBarHeight - dockH;

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
    kit::ScrollRegion region("workspace-scroll", area);
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

    switch (layout_.workspace) {
    case 0: DrawOverview(view, draw); break;
    case 1: novel_.Draw(view, draw); break;
    case 2: assets_.Draw(view, draw); break;
    case 3: storyboard_.Draw(view, draw); break;
    case 4: imageflow_.Draw(view, draw); break;
    case 5: videoflow_.Draw(view, draw); break;
    default: gallery_.Draw(view, draw); break;
    }
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

    float y = body.min.y;
    for (const ArtifactRow& row : artifacts_) {
        if (y + 24.0f > body.max.y - 16.0f) {
            break;
        }
        const Rect line{body.min.x, y, body.min.x + 520.0f, y + 24.0f};
        const Hit hit = HitTest(line, "dock-art-" + row.name);
        if (hit.hovered) {
            DrawRoundRect(draw, line.min, line.max, 6.0f, ColorFillHover());
        }
        DrawIcon(draw, "folder", ImVec2(line.min.x + 3.0f, line.center().y - 6.0f), 13.0f,
                 ColorAccentHover());
        DrawTextClipped(draw, MonoAt(11.5f), 11.5f, ImVec2(line.min.x + 22.0f, y + 5.0f), 300.0f,
                        ColorText(), row.name);
        DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(line.min.x + 330.0f, y + 5.0f), 90.0f,
                        ColorTextMuted(), row.kind);
        DrawIcon(draw, "chevron", ImVec2(line.max.x - 14.0f, line.center().y - 5.0f), 10.0f,
                 ColorTextMuted());
        if (hit.clicked) {
            const std::string err = util::ShellOpen(row.path);
            PushLog(err.empty() ? "info" : "err",
                    std::string("打开产物 ") + row.name + (err.empty() ? "" : " 失败：" + err));
        }
        y += 26.0f;
    }
    const std::string foot =
        "产物浏览器指向 " + util::PathToUtf8(artifactRoot_) + " · 点击条目用系统默认程序打开";
    draw->AddText(FontAt(10.5f), 10.5f, ImVec2(body.min.x, body.max.y - 14.0f), ColorTextMuted(),
                  foot.data(), foot.data() + foot.size());
}

// ---------------------------------------------------------------- P4.6d 校验报告
//
// 真值源：novelcore::RunContinuityChecks 落盘的 <project>/work/ch<NNN>/v08_continuity.json
// —— V8 连续性，也就是 pipeline T12 / T15 的产出，字段见 NovelContinuity.cpp:467。
//
// 早先这一页只给诚实空态，紧邻的 DrawReportModal 更糟：它是个**永远打不开的空壳** ——
// 驱动它的 reportDetail_ 全代码只有 `= -1` 初始化与复位，从没有任何地方把它设成 ≥0，
// 于是那个模态框一次都没出现过，Esc 逐层关闭里的那一层也永远走不到。
// 现在列表按章列真报告，点行走模态看逐项结论。

namespace {

// work/ch<NNN> → NNN。返回 0 = 不是按章组织的报告目录（跳过，不拿它当一份空报告充数）。
int ChapterOrdFromDir(const std::filesystem::path& dir) {
    const std::string name = util::PathToUtf8(dir.filename());
    if (name.size() < 3 || name.compare(0, 2, "ch") != 0) {
        return 0;
    }
    int ord = 0;
    for (std::size_t i = 2; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        ord = ord * 10 + (name[i] - '0');
    }
    return ord;
}

} // namespace

theme::Tone Shell::SeverityTone(std::string_view severity) {
    if (severity == "high") {
        return theme::Tone::Danger;
    }
    if (severity == "medium") {
        return theme::Tone::Warn;
    }
    return theme::Tone::Idle;  // low（设计稿给的是 var(--text-muted)）
}

theme::Tone Shell::ReportTone(const ChapterReport& report) {
    if (report.failed > 0) {
        return theme::Tone::Danger;
    }
    if (report.unverified > 0) {
        return theme::Tone::Warn;  // 数据不足：既不算通过，也别粉饰成失败
    }
    return theme::Tone::Ok;
}

std::string Shell::ReportSummary(const ChapterReport& report) {
    // 三态分开报，不合并成一个数：failed 与 unverified 是两回事。
    // ⚠️ failed==0 且 unverified>0 时**不能**写「全部通过」—— 那 2 条根本没核对过。
    std::string s;
    if (report.failed > 0) {
        s = std::to_string(report.failed) + " 未过 · ";
    } else if (report.unverified > 0) {
        s = "无未过项 · ";
    } else {
        s = "全部通过 · ";
    }
    if (report.unverified > 0) {
        s += std::to_string(report.unverified) + " 未核对 · ";
    }
    s += std::to_string(report.pairs) + " 镜对";
    return s;
}

std::vector<Shell::CheckRow> Shell::BuildCheckRows(const ChapterReport& report) {
    std::vector<CheckRow> rows;
    rows.reserve(report.issues.size() + report.notes.size());
    for (const ReportIssue& issue : report.issues) {
        CheckRow row;
        row.code = issue.code;
        row.level = issue.severity;
        row.levelTone = SeverityTone(issue.severity);
        // 结论口径照设计稿 Shell.jsx:292：未过且 level=low 记「提示」，否则记「未过」。
        const bool hint = issue.severity == "low";
        row.verdict = hint ? "提示" : "未过";
        row.verdictTone = hint ? theme::Tone::Warn : theme::Tone::Danger;
        row.detail = issue.detail;
        rows.push_back(std::move(row));
    }
    // notes 形如「C3：数据不足…」—— 冒号前是规则码，后面是人话。
    // ⚠️ 拆不出「码：说明」就整条丢掉：宁可少一行，也不编一个码出来顶账。
    for (const std::string& note : report.notes) {
        const std::size_t sep = note.find("：");
        if (sep == std::string::npos || sep == 0) {
            continue;
        }
        CheckRow row;
        row.code = note.substr(0, sep);
        row.level = "-";
        row.levelTone = theme::Tone::Idle;
        row.verdict = "未核对";
        row.verdictTone = theme::Tone::Info;
        row.detail = note.substr(sep + 1);
        rows.push_back(std::move(row));
    }
    return rows;
}

bool Shell::ParseContinuityReport(const std::filesystem::path& file, ChapterReport& out) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
    if (doc == nullptr) {
        return false;  // 损坏的报告当没有 —— 不猜、不补
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    const char* stage = yyjson_get_str(yyjson_obj_get(root, "stage"));
    if (stage == nullptr || std::string_view{stage} != "V8") {
        yyjson_doc_free(doc);
        return false;  // work/ 下别的 JSON —— 不当校验报告充数
    }
    const auto num = [&root](const char* key) {
        return static_cast<int>(yyjson_get_int(yyjson_obj_get(root, key)));
    };
    out.chapterId = num("chapter_id");
    out.shots = num("shots");
    out.pairs = num("pairs");
    out.rulesChecked = num("rules_checked");
    out.failed = num("failed");
    out.unverified = num("unverified");
    if (yyjson_val* issues = yyjson_obj_get(root, "issues"); yyjson_is_arr(issues)) {
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(issues, &iter);
        for (yyjson_val* item = nullptr; (item = yyjson_arr_iter_next(&iter)) != nullptr;) {
            const auto str = [item](const char* key) {
                const char* raw = yyjson_get_str(yyjson_obj_get(item, key));
                return raw != nullptr ? std::string(raw) : std::string{};
            };
            ReportIssue issue;
            issue.code = str("code");
            issue.severity = str("severity");
            issue.detail = str("detail");
            out.issues.push_back(std::move(issue));
        }
    }
    if (yyjson_val* notes = yyjson_obj_get(root, "notes"); yyjson_is_arr(notes)) {
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(notes, &iter);
        for (yyjson_val* item = nullptr; (item = yyjson_arr_iter_next(&iter)) != nullptr;) {
            if (const char* raw = yyjson_get_str(item); raw != nullptr) {
                out.notes.emplace_back(raw);
            }
        }
    }
    yyjson_doc_free(doc);
    return true;
}

void Shell::RequestReportScan() {
    if (layout_.projectRoot.empty()) {
        return;
    }
    const std::filesystem::path root = layout_.projectRoot / "work";
    reportScanning_ = true;
    reportScanRoot_ = layout_.projectRoot;
    async::RunOnWorker([this, root] {
        std::vector<ChapterReport> found;
        std::error_code ec;
        if (std::filesystem::is_directory(root, ec)) {
            for (auto it = std::filesystem::directory_iterator(root, ec);
                 it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (ec) {
                    break;
                }
                if (!it->is_directory(ec)) {
                    continue;
                }
                const int ord = ChapterOrdFromDir(it->path());
                if (ord <= 0) {
                    continue;
                }
                const std::filesystem::path file = it->path() / "v08_continuity.json";
                ChapterReport report;
                if (!ParseContinuityReport(file, report)) {
                    continue;  // 目录在但报告没落盘 / 损坏 —— 不拿它凑一条空报告
                }
                report.chapterOrd = ord;
                report.path = util::PathToUtf8(file);
                found.push_back(std::move(report));
            }
        }
        std::stable_sort(found.begin(), found.end(),
                         [](const ChapterReport& a, const ChapterReport& b) {
                             return a.chapterOrd < b.chapterOrd;
                         });
        async::PostToUi([this, rows = std::move(found)] {
            reportScanning_ = false;
            reports_ = std::move(rows);
            // 排下一次重扫的时点。⚠️ 这一行以前写的是 `= 0.0f`，于是
            // DrawDockReports 里的 `aged` 恒为假 —— 换工程之外再也没有第二个触发点，
            // 跑完一个阶段后这一页会一直停在「尚无校验报告」。这里给真的时点。
            reportRefreshAt_ = ImGui::GetTime() + 2.0f;
            // 重扫后别让模态指着一条已经没了的报告。-1（本来就没打开）保持 -1。
            if (reportDetail_ >= static_cast<int>(reports_.size())) {
                reportDetail_ = reports_.empty() ? -1 : 0;
            }
        });
    });
}

void Shell::ExportReport(int index) {
    if (index < 0 || index >= static_cast<int>(reports_.size())) {
        return;
    }
    const ChapterReport report = reports_[static_cast<std::size_t>(index)];
    if (report.path.empty()) {
        return;
    }
    const std::filesystem::path src(report.path);
    // 逐字节复制原报告，不重新序列化 —— 序列化会把排版和未知字段抹掉。
    // 写盘走 worker：UI 线程不做同步 IO。
    async::RunOnWorker([this, src] {
        const std::optional<std::string> bytes = util::ReadFileBytes(src);
        if (!bytes.has_value()) {
            async::PostToUi([this, src] {
                PushLog("err", "导出校验报告失败：读不到 " + util::PathToUtf8(src));
            });
            return;
        }
        const std::filesystem::path dst = layout_.projectRoot / "output" / src.filename();
        const bool ok = util::WriteFileEnsuredDir(dst, *bytes);
        async::PostToUi([this, dst, ok] {
            PushLog(ok ? "info" : "err",
                    ok ? "已导出校验报告 " + util::PathToUtf8(dst)
                       : "导出校验报告失败：写不进 " + util::PathToUtf8(dst));
        });
    });
}

void Shell::DrawDockReports(Rect body, ImDrawList* draw) {
    // 换工程 / 2 秒后重扫（与 DrawDockArtifacts 同一套判定，不发明第二份）。
    const bool stale = reportScanRoot_ != layout_.projectRoot;
    const bool aged = reportRefreshAt_ > 0.0f && ImGui::GetTime() >= reportRefreshAt_;
    if (stale || aged) {
        reportScanning_ = false;  // 上一次的结果作废
        RequestReportScan();
    }
    if (layout_.projectRoot.empty()) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 520.0f, body.min.y + 90.0f},
              "target", "未打开工程", "打开工程并跑完 T12 / T15 后，逐项校验报告会在此列出");
        return;
    }
    if (reports_.empty() && !reportScanning_) {
        Empty(draw, Rect{body.min.x, body.min.y + 6.0f, body.min.x + 620.0f, body.min.y + 100.0f},
              "target", "尚无校验报告", "校验只比较状态，不让 LLM 自行猜测连续性 · 跑完 T12 / T15 后在此查看逐项结论");
        return;
    }

    // 行 = code(70) | name(90) | 结论 Tag | 尾部 chevron。设计稿这一组是 flex 行
    // （Shell.jsx:255-262）**没有列头**，所以这里不套 DataTable，也不加排序。
    float y = body.min.y;
    for (std::size_t i = 0; i < reports_.size(); ++i) {
        if (y + 26.0f > body.max.y - 20.0f) {
            break;
        }
        const ChapterReport& report = reports_[i];
        const Rect line{body.min.x, y, body.min.x + 560.0f, y + 26.0f};
        // 一个矩形只 HitTest 一次：hover 高亮与点击读同一个 Hit。
        const Hit hit = HitTest(line, "dock-report-" + std::to_string(i));
        if (hit.hovered) {
            DrawRoundRect(draw, line.min, line.max, 6.0f, ColorFillHover());
        }
        std::string code = "ch" + std::to_string(report.chapterOrd);
        while (code.size() < 5) {
            code.insert(code.begin() + 2, '0');
        }
        DrawTextClipped(draw, MonoAt(10.5f), 10.5f, ImVec2(line.min.x + 4.0f, y + 8.0f), 62.0f,
                        ColorTextMuted(), code);
        DrawTextClipped(draw, FontAt(12.5f), 12.5f, ImVec2(line.min.x + 74.0f, y + 6.0f), 150.0f,
                        ColorTextSecondary(), "连续性 C1-C12");
        const std::string summary = ReportSummary(report);
        Tag(draw, RectAt(line.min.x + 232.0f, y + 5.0f, TagWidth(summary, true, false),
                         TagHeight(true)),
            summary, ReportTone(report), /*small=*/true);
        DrawIcon(draw, "chevron", ImVec2(line.max.x - 16.0f, line.center().y - 5.0f), 10.0f,
                 ColorTextMuted());
        if (hit.clicked) {
            reportDetail_ = static_cast<int>(i);
        }
        y += 26.0f;
    }
    const std::string foot = "报告来自 work/ch<NNN>/v08_continuity.json · 点击任一行查看逐项结论（检查 / 级别 / 结论 / 详情）";
    DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(body.min.x, body.max.y - 14.0f),
                    body.width(), ColorTextMuted(), foot);
}

void Shell::DrawReportModal() {
    if (reportDetail_ < 0) {
        return;
    }
    if (reportDetail_ >= static_cast<int>(reports_.size())) {
        reportDetail_ = -1;  // 重扫后这条没了 —— 别让模态指着一个不存在的下标
        return;
    }
    const ChapterReport& report = reports_[static_cast<std::size_t>(reportDetail_)];
    const std::vector<CheckRow> rows = BuildCheckRows(report);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // ⚠️ 必须是 **foreground** draw list，不能用 GetWindowDrawList()。
    //    外壳的页面 / 底栏各自跑在 ScrollRegion(BeginChild) 里，而 child 是在
    //    父窗口那份 draw list **之后**渲染的 —— 画在父 list 上的浮层会被整片盖住。
    //    实测症状：遮罩和面板都画了，但工作区的 KPI 卡片压在模态上面，
    //    截图上「模态」只剩一条表头带，manifest 还写着 saved。
    //    本仓 Tooltip 早就用同一招并在 Widgets.cpp:793 记了原因。
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    // 设计稿内联 width 640 / height min(74vh,660)（Shell.jsx:272）—— 尺寸原样保留。
    const float w = 640.0f;
    const float h = std::min(660.0f, display.y * 0.74f);
    const Rect bounds{(display.x - w) * 0.5f, (display.y - h) * 0.5f, (display.x + w) * 0.5f,
                      (display.y + h) * 0.5f};
    DrawRoundRect(draw, ImVec2(0.0f, 0.0f), ImVec2(display.x, display.y), 0.0f, ColorScrim());
    DrawShadowed(draw, bounds.min, bounds.max, 14.0f, ColorOverlay(), ColorLineNormal(), 1.0f);
    // 点遮罩关闭。⚠️ 这里**不能**用 kit::HitTest：全屏遮罩若先注册成 InvisibleButton，
    // 它会先拿到 HoveredId，模态里所有按钮（同一帧、位置落在遮罩内）永远 hovered=false。
    // 改用不注册 item 的 IsMouseHoveringRect。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseHoveringRect(bounds.min, bounds.max, true)) {
        reportDetail_ = -1;
    }

    const float headerH = 46.0f;
    const float footerH = 12.0f * 2.0f + ButtonHeight(ButtonSize::Medium) + 1.0f;

    // ---- .modal-h（ui.css:955-965）：padding 14/18 + gap 10 + 1px 下边 ----
    // ⚠️ 标题里的规则组写作 "C1-C12"（ASCII 连字符）而不是设计稿的 en dash：
    //    U+2013 不在图集的字形基线里，用它会画成缺字形方块。
    const std::string title = "连续性 C1-C12 · 第 " + std::to_string(report.chapterOrd) + " 章";
    DrawIcon(draw, "target", ImVec2(bounds.min.x + 18.0f, bounds.min.y + 15.0f), 17.0f, ColorAccent());
    DrawTextClipped(draw, FontBoldAt(14.0f), 14.0f, ImVec2(bounds.min.x + 45.0f, bounds.min.y + 15.0f),
                    260.0f, ColorText(), title);
    ButtonSpec exportSpec;
    exportSpec.variant = ButtonVariant::Ghost;
    exportSpec.size = ButtonSize::Small;
    exportSpec.icon = "download";
    const std::string exportLabel = "导出 JSON";
    const float exportW = ButtonWidth(ButtonSize::Small, 13.0f,
                                       FontAt(10.5f)->CalcTextSizeA(
                                           10.5f, 1e9f, 0.0f, exportLabel.data(),
                                           exportLabel.data() + exportLabel.size())
                                           .x);
    if (Button(draw, RectAt(bounds.max.x - 18.0f - 22.0f - 8.0f - exportW, bounds.min.y + 12.0f,
                            exportW, ButtonHeight(ButtonSize::Small)),
               exportLabel, exportSpec, "report-export")) {
        ExportReport(reportDetail_);
    }
    if (IconButton(draw, RectAt(bounds.max.x - 18.0f - 22.0f, bounds.min.y + 12.0f, 22.0f, 22.0f),
                   "x", false, false, "report-close")) {
        reportDetail_ = -1;
    }
    draw->AddLine(ImVec2(bounds.min.x, bounds.min.y + headerH - 0.5f),
                  ImVec2(bounds.max.x, bounds.min.y + headerH - 0.5f), ColorLineSubtle(), 1.0f);

    // ---- .modal-b（ui.css:966-969）：fill-muted 底 + padding 18 + 纵向滚 ----
    //
    // ⚠️ 这里**必须**用 draw->PushClipRect 自己裁，**不能**用 ScrollRegion（BeginChild）。
    //    BeginChild 会另开一个 ImGuiWindow，而 ImGui 的渲染次序是「后建的先画」——
    //    这个 child 就沉到了工作区那个 child 底下，父 draw list 上的部分（面板底、
    //    标题栏、表头）还看得见，落在 child 里的表体却被工作区整片盖住。
    //    实测症状：模态只剩一条表头带，下面全是工作区的 KPI 卡片。
    //    模态是单帧定尺的浮层，本来就不需要独立窗口 —— 全留在同一个 draw list 上，
    //    z 序由调用顺序决定，不用赌 ImGui 的窗口次序。
    const Rect body{bounds.min.x, bounds.min.y + headerH, bounds.max.x, bounds.max.y - footerH};
    const float tableX = bounds.min.x + 18.0f;
    const float tableW = w - 36.0f;
    const float headH = TableHeaderHeight(true);
    const float rowH = TableRowHeight(true);
    const float summaryH = TagHeight(true) + 10.0f;
    const float contentH =
        18.0f + summaryH + headH + (rows.empty() ? rowH : static_cast<float>(rows.size()) * rowH) + 18.0f;
    const float maxScroll = std::max(0.0f, contentH - body.height());
    if (ImGui::IsMouseHoveringRect(body.min, body.max, true)) {
        reportScroll_ -= ImGui::GetIO().MouseWheel * 40.0f;
    }
    reportScroll_ = std::clamp(reportScroll_, 0.0f, maxScroll);

    draw->PushClipRect(body.min, body.max, true);
    // .modal-b 的底是 --fill-muted（Shell.jsx:280 内联），不是模态框的 overlay。
    draw->AddRectFilled(body.min, body.max, ColorFillMuted());
    float y = body.min.y + 18.0f - reportScroll_;

    // 摘要 Tag，tone=info（设计稿 Shell.jsx:281-283）。
    const std::string summary = ReportSummary(report) + " · 阈值：high 未过即 FAIL";
    Tag(draw, RectAt(tableX, y, TagWidth(summary, true, false), TagHeight(true)), summary,
        theme::Tone::Info, /*small=*/true);
    y += summaryH;

    // 表头。设计稿那张表**渲染 5 个 td 却只写了 4 个标题**（Shell.jsx:285 vs 289-293），
    // 照抄会留下一列没有表头的裸格子；这里保留它的 4 个标题，但都落在有内容的列上。
    const float codeW = 56.0f;
    const float levelW = 88.0f;
    const float verdictW = 88.0f;
    const float levelX = tableX + codeW;
    const float verdictX = levelX + levelW;
    const float detailX = verdictX + verdictW;
    const float detailW = std::max(60.0f, tableX + tableW - detailX);
    draw->AddRectFilled(ImVec2(tableX, y), ImVec2(tableX + tableW, y + headH), ColorPanel());
    ImFont* headFont = FontBoldAt(11.5f);
    for (const auto& [x, titleText] :
         {std::pair{tableX, "检查"}, std::pair{levelX, "级别"}, std::pair{verdictX, "结论"},
          std::pair{detailX, "详情"}}) {
        DrawTextClipped(draw, headFont, 11.5f, ImVec2(x + 8.0f, y + 7.0f), 200.0f, ColorTextMuted(),
                        titleText);
    }
    draw->AddLine(ImVec2(tableX, y + headH - 0.5f), ImVec2(tableX + tableW, y + headH - 0.5f),
                  ColorLineNormal(), 1.0f);
    y += headH;

    if (rows.empty()) {
        // 没有 issue 也没有 note = 这一章一条都没判出来。照实说，别写成「全部通过」。
        DrawTextClipped(draw, FontAt(12.0f), 12.0f, ImVec2(tableX + 8.0f, y + 7.0f), tableW - 16.0f,
                        ColorTextMuted(),
                        report.pairs > 0 ? "本组没有记下任何不一致项" : "没有可比较的镜对");
        y += rowH;
    }
    for (const CheckRow& row : rows) {
        const Rect line{tableX, y, tableX + tableW, y + rowH};
        if (ImGui::IsMouseHoveringRect(line.min, line.max, true)) {
            draw->AddRectFilled(line.min, line.max, ColorFillHover());
        }
        DrawTextClipped(draw, MonoAt(11.5f), 11.5f, ImVec2(line.min.x + 8.0f, y + 7.0f), codeW - 16.0f,
                        ColorAccent(), row.code);
        Tag(draw, RectAt(levelX + 8.0f, y + 6.0f, TagWidth(row.level, true, false), TagHeight(true)),
            row.level, row.levelTone, /*small=*/true);
        Tag(draw,
            RectAt(verdictX + 8.0f, y + 6.0f, TagWidth(row.verdict, true, false), TagHeight(true)),
            row.verdict, row.verdictTone, /*small=*/true);
        // 详情超宽出省略号，全文挂 tooltip（.ellipsis 的等价物）。
        const float detailTextX = detailX + 8.0f;
        const float detailDrawn = DrawTextClipped(draw, FontAt(12.0f), 12.0f,
                                                  ImVec2(detailTextX, y + 7.0f), detailW - 16.0f,
                                                  ColorTextSecondary(), row.detail);
        if (detailDrawn >= detailW - 16.0f - 0.5f) {
            Tooltip(line, row.detail);
        }
        draw->AddLine(ImVec2(tableX, y + rowH - 0.5f), ImVec2(tableX + tableW, y + rowH - 0.5f),
                      ColorLineSubtle(), 1.0f);
        y += rowH;
    }
    draw->PopClipRect();

    // 滚动条：内容装不下时才画（thumb = 可见比例，track = 主体高度）。
    if (maxScroll > 0.0f) {
        const float trackW = 4.0f;
        const float trackX = bounds.max.x - 18.0f - trackW;
        const float thumbH = std::max(24.0f, body.height() * (body.height() / contentH));
        const float thumbY =
            body.min.y + (body.height() - thumbH) * (reportScroll_ / maxScroll);
        draw->AddRectFilled(ImVec2(trackX, body.min.y), ImVec2(trackX + trackW, body.max.y),
                            ColorFillMuted());
        draw->AddRectFilled(ImVec2(trackX, thumbY), ImVec2(trackX + trackW, thumbY + thumbH),
                            ColorLineStrong());
    }

    // ---- .modal-f（ui.css:970-975）：padding 12/18 + 1px 上边 ----
    draw->AddLine(ImVec2(bounds.min.x, bounds.max.y - footerH + 0.5f),
                  ImVec2(bounds.max.x, bounds.max.y - footerH + 0.5f), ColorLineSubtle(), 1.0f);
    DrawTextClipped(draw, FontAt(10.5f), 10.5f, ImVec2(bounds.min.x + 18.0f, bounds.max.y - 12.0f - 14.0f),
                    w - 120.0f, ColorTextMuted(),
                    "校验只比较状态，不让 LLM 自行猜测连续性 · Esc 关闭");
    ButtonSpec closeSpec;
    closeSpec.variant = ButtonVariant::Ghost;
    if (Button(draw, RectAt(bounds.max.x - 18.0f - 72.0f, bounds.max.y - 12.0f - ButtonHeight(ButtonSize::Medium),
                            72.0f, ButtonHeight(ButtonSize::Medium)),
               "关闭", closeSpec, "report-dismiss")) {
        reportDetail_ = -1;
    }
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
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float w = 224.0f;
    const float h = 34.0f + static_cast<float>(theme::kAllThemes.size()) * 32.0f + 9.0f + 32.0f;
    float x = std::min(anchor.x, display.x - w - 8.0f);
    float y = std::min(anchor.y, display.y - h - 8.0f);
    const Rect bounds{x, y, x + w, y + h};

    DrawShadowed(draw, bounds.min, bounds.max, 10.0f, ColorOverlay(), ColorLineNormal(), 1.0f);

    static constexpr char kThemeLabel[] = "内置主题";
    draw->AddText(FontBoldAt(10.5f), 10.5f, ImVec2(bounds.min.x + 12.0f, bounds.min.y + 10.0f),
                  ColorTextMuted(), kThemeLabel, kThemeLabel + sizeof(kThemeLabel) - 1);
    float iy = bounds.min.y + 30.0f;
    for (std::size_t i = 0; i < theme::kAllThemes.size(); ++i) {
        const theme::ThemeId id = theme::kAllThemes[i];
        const theme::ColorToken& t = theme::ThemeColorsOf(id);
        const bool on = theme::CurrentThemeId() == id;
        const Rect row{bounds.min.x + 4.0f, iy, bounds.max.x - 4.0f, iy + 30.0f};
        const Hit hit = HitTest(row, "theme-item-" + std::to_string(i));
        if (hit.hovered || on) {
            DrawRoundRect(draw, row.min, row.max, 6.0f, on ? ColorFillSelected() : ColorFillHover());
        }
        // 24×14 双向渐变方块：accent → accent-2（shell.css:148 的 .swatch）
        const Rect swatch{row.min.x + 10.0f, row.center().y - 7.0f, row.min.x + 34.0f,
                          row.center().y + 7.0f};
        DrawHGradient(draw, swatch.min, swatch.max, 4.0f, ColorOf(t.accentPrimary),
                      ColorOf(t.accentSecondary));
        const std::string_view name = theme::ThemeDisplayName(id);
        draw->AddText(FontAt(12.5f), 12.5f, ImVec2(row.min.x + 44.0f, row.min.y + 8.0f),
                      on ? ColorText() : ColorTextSecondary(), name.data(), name.data() + name.size());
        if (on) {
            DrawIcon(draw, "check", ImVec2(row.max.x - 22.0f, row.center().y - 6.0f), 13.0f,
                     ColorAccent());
        }
        if (hit.clicked) {
            SetTheme(id);
            themeMenuOpen_ = false;
            PushLog("info", "主题已切换：" + std::string(name));
        }
        iy += 32.0f;
    }

    draw->AddLine(ImVec2(bounds.min.x + 10.0f, iy), ImVec2(bounds.max.x - 10.0f, iy),
                  ColorLineSubtle(), 1.0f);
    iy += 9.0f;
    {
        const Rect row{bounds.min.x + 4.0f, iy, bounds.max.x - 4.0f, iy + 30.0f};
        const Hit hit = HitTest(row, "theme-motion");
        if (hit.hovered || layout_.reduceMotion) {
            DrawRoundRect(draw, row.min, row.max, 6.0f,
                          layout_.reduceMotion ? ColorFillSelected() : ColorFillHover());
        }
        DrawIcon(draw, "zap", ImVec2(row.min.x + 10.0f, row.center().y - 7.0f), 14.0f,
                 ColorTextSecondary());
        static constexpr char kMotionLabel[] = "减少动效";
        draw->AddText(FontAt(12.5f), 12.5f, ImVec2(row.min.x + 32.0f, row.min.y + 8.0f),
                      ColorTextSecondary(), kMotionLabel, kMotionLabel + sizeof(kMotionLabel) - 1);
        if (layout_.reduceMotion) {
            DrawIcon(draw, "check", ImVec2(row.max.x - 22.0f, row.center().y - 6.0f), 13.0f,
                     ColorAccent());
        }
        if (hit.clicked) {
            layout_.reduceMotion = !layout_.reduceMotion;
            kit::SetReduceMotion(layout_.reduceMotion);
            PushLog("info", layout_.reduceMotion ? "已减少动效" : "已恢复动效");
        }
    }

    // 点菜单外面关掉（对应 webui 的 pointerdown 外部关闭）。
    const Hit outside = HitTest(Rect{0.0f, 0.0f, display.x, display.y}, "theme-outside");
    if (outside.clicked) {
        themeMenuOpen_ = false;
    }
}

// ---------------------------------------------------------------- P4.9b 设置模态
// 「设置 · 三步开工」（Shell.jsx:74 的 IconBtn tip）。这里读 AppSettings 的真值，
// 让用户看到程序**实际**连的是什么，而不是一份编出来的配置。
void Shell::DrawSettingsModal() {
    if (!settingsOpen_) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // 同 DrawReportModal：浮层走 foreground，否则会被 BeginChild 里的页面内容盖住。
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float w = 560.0f;
    const float h = 452.0f;
    const Rect bounds{(display.x - w) * 0.5f, (display.y - h) * 0.5f, (display.x + w) * 0.5f,
                      (display.y + h) * 0.5f};

    const Hit scrim;
    DrawRoundRect(draw, ImVec2(0.0f, 0.0f), ImVec2(display.x, display.y), 0.0f, ColorScrim());
    DrawShadowed(draw, bounds.min, bounds.max, 14.0f, ColorOverlay(), ColorLineNormal(), 1.0f);

    DrawIcon(draw, "settings", ImVec2(bounds.min.x + 16.0f, bounds.min.y + 15.0f), 16.0f,
             ColorAccent());
    static constexpr char kSettingsTitle[] = "设置 · 三步开工";
    draw->AddText(FontBoldAt(14.0f), 14.0f, ImVec2(bounds.min.x + 40.0f, bounds.min.y + 14.0f),
                  ColorText(), kSettingsTitle, kSettingsTitle + sizeof(kSettingsTitle) - 1);
    if (IconButton(draw, RectAt(bounds.max.x - 34.0f, bounds.min.y + 10.0f, 22.0f, 22.0f), "x",
                   false, false, "settings-close")) {
        settingsOpen_ = false;
    }
    draw->AddLine(ImVec2(bounds.min.x, bounds.min.y + 44.0f), ImVec2(bounds.max.x, bounds.min.y + 44.0f),
                  ColorLineSubtle(), 1.0f);

    const AppSettings& s = Settings();
    float y = bounds.min.y + 58.0f;
    const float ix = bounds.min.x + 20.0f;
    const float iw = bounds.width() - 40.0f;

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

    // 点遮罩关闭：同样不能用 kit::HitTest（全屏遮罩会先抢 HoveredId，
    // 把模态里的按钮变成死键）。用不注册 item 的 IsMouseHoveringRect。
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseHoveringRect(bounds.min, bounds.max, true)) {
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
    DrawProjectHub(region.content(), ImGui::GetWindowDrawList());
}

} // namespace shine::p⚠️