#include "ui/imgui/pages/Shell.h"

#include "core/Log.h"
#include "ui/app/AppEnvironment.h"
#include "ui/imgui/kit/Fonts.h"
#include "ui/imgui/kit/Scroll.h"
#include "ui/imgui/pages/Gallery.h"
#include "ui/imgui/pages/WorkspacePages.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
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
    x = capsule.max.x + 16.0f;

    // 搜索触发器 260×28 胶囊 + 右侧 Kbd "Ctrl K"
    const Rect search{x, 0.5f * (area.min.y + area.max.y) - 14.0f, x + 260.0f,
                      0.5f * (area.min.y + area.max.y) + 14.0f};
    DrawRoundRect(draw, search.min, search.max, 14.0f, ColorFillMuted(), ColorLineNormal(), 1.0f);
    DrawIcon(draw, "search", ImVec2(search.min.x + 10.0f, search.center().y - 7.0f), 14.0f,
             ColorTextMuted());
    draw->AddText(FontAt(12.5f), 12.5f, ImVec2(search.min.x + 30.0f, search.center().y - 6.25f),
                  ColorTextMuted(), "搜索项目 / 章节 / 镜头", "搜索项目 / 章节 / 镜头" + 27);
    const float kbdW = KbdWidth("Ctrl K");
    Kbd(draw, RectAt(search.max.x - kbdW - 6.0f, search.center().y - 9.0f, kbdW, 18.0f), "Ctrl K");

    // 右侧：运行 / 停止 / 主题 / 设置 / Comfy 状态点
    float rx = area.max.x - 16.0f;
    // Comfy 状态点
    StatusDot(draw, ImVec2(rx - 4.0f, 0.5f * (area.min.y + area.max.y)), theme::Tone::Idle, false);
    rx -= 16.0f;
    rx -= 28.0f;
    if (IconButton(draw, RectAt(rx, 0.5f * (area.min.y + area.max.y) - 14.0f, 28.0f, 28.0f),
                   "settings", false, false, "tb-settings", "设置")) {
        layout_.reduceMotion = !layout_.reduceMotion;
        SetReduceMotion(layout_.reduceMotion);
    }
    rx -= 8.0f;
    rx -= 28.0f;
    if (IconButton(draw, RectAt(rx, 0.5f * (area.min.y + area.max.y) - 14.0f, 28.0f, 28.0f),
                   "palette", paletteOpen_, false, "tb-theme", "主题")) {
        paletteOpen_ = !paletteOpen_;
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
        DrawIconCentered(draw, WorkspaceIcon(i), center, 19.0f,
                         on ? ColorAccent() : (kit::Hovered(item, "rail-hover-" + std::to_string(i)) ? ColorAccent() : ColorTextMuted()));
        if (kit::Clicked(item, "rail-ws-" + std::to_string(i))) {
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
    for (const Toggle& toggle : toggles) {
        const Rect item{area.min.x, y, area.max.x, y + 44.0f};
        if (*toggle.state) {
            DrawRoundRect(draw, ImVec2(item.min.x + 6.0f, item.min.y), ImVec2(item.max.x - 6.0f, item.max.y),
                          10.0f, ColorFillHover());
        }
        DrawIconCentered(draw, toggle.icon, ImVec2(area.center().x, item.center().y), 19.0f,
                         *toggle.state ? ColorAccent() : ColorTextMuted());
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

    // 树：按工作区换内容
    const float x = area.min.x + 10.0f;
    const float w = area.width() - 30.0f;
    float y = area.min.y + 10.0f;
    const std::string_view root = "项目";
    draw->AddText(FontBoldAt(11.5f), 11.5f, ImVec2(x + 14.0f, y), ColorTextMuted(), root.data(),
                  root.data() + root.size());
    y += 22.0f;

    struct TreeRow {
        const char* label;
        int depth;
        bool active;
    };
    std::vector<TreeRow> rows;
    switch (layout_.workspace) {
    case 0:
        rows = {{"运行", 0, true}, {"T1–T17 阶段", 1, false}, {"预算与账本", 1, false},
                {"停止条件", 1, false}};
        break;
    case 1:
        rows = {{"第 1 章 · 雨夜", 0, true}, {"第 2 章 · 旧桥", 0, false}, {"世界设定", 0, false},
                {"角色", 1, false}, {"地理", 1, false}, {"初始化 I1–I16", 0, false}};
        break;
    case 2:
        rows = {{"角色", 0, true}, {"场景", 0, false}, {"道具", 0, false}, {"风格库", 0, false}};
        break;
    case 3:
        rows = {{"S001–S012", 0, true}, {"连续性 C1–C12", 0, false}, {"故事板时间线", 0, false}};
        break;
    case 4:
    case 5:
        rows = {{"节点图", 0, true}, {"批量队列", 0, false}, {"评审记录", 0, false}};
        break;
    default:
        rows = {{"基础控件", 0, true}, {"数据展示", 0, false}, {"容器与导航", 0, false},
                {"画布与图像", 0, false}};
        break;
    }

    for (const TreeRow& row : rows) {
        const float indent = 10.0f * static_cast<float>(row.depth);
        const Rect rowRect{x + indent, y, x + w + 10.0f, y + 26.0f};
        if (row.active) {
            DrawRoundRect(draw, rowRect.min, rowRect.max, 6.0f, ColorFillSelected());
        }
        if (row.depth == 1) {
            DrawIcon(draw, "chevdown", ImVec2(rowRect.min.x + 2.0f, rowRect.center().y - 5.0f), 10.0f,
                     ColorTextMuted());
        }
        draw->AddText(FontAt(12.5f), 12.5f, ImVec2(rowRect.min.x + 16.0f, rowRect.min.y + 6.0f),
                      row.active ? ColorText() : ColorTextSecondary(), row.label,
                      row.label + std::strlen(row.label));
        y += 28.0f;
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
            if (std::strcmp(section.title, "属性") == 0) {
                KeyValues(draw, Rect{x, y, x + w, y + 84.0f},
                          {{"代码", "S012"}, {"动作", "转身"}, {"时长", "6.0s"}, {"情绪", "克制"}});
                y += 92.0f;
            } else if (std::strcmp(section.title, "预览") == 0) {
                Art(draw, Rect{x, y, x + w, y + 110.0f}, 5, true);
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
    if (layout_.dockTab == 1) {
        // 日志行：等宽 11.5px，级别色 ok/warn/err，尾部 7×12 闪烁 accent 光标块
        const char* levels[] = {"info", "warn", "err", "info"};
        const char* lines[] = {"T3 生成图 · 提交 Comfy 队列 4 张",
                               "T3 生成图 · 队列等待 12s", "T4 评审 · 3 项低于阈值",
                               "T5 落库 · 写入 project/derived.json"};
        float y = body.min.y;
        for (int i = 0; i < 4; ++i) {
            ImU32 color = ColorTextSecondary();
            if (std::strcmp(levels[i], "warn") == 0) {
                color = ColorOf(theme::Current().statusWarn);
            } else if (std::strcmp(levels[i], "err") == 0) {
                color = ColorOf(theme::Current().statusDanger);
            }
            draw->AddText(MonoAt(11.5f), 11.5f, ImVec2(body.min.x, y), color, lines[i],
                          lines[i] + std::strlen(lines[i]));
            y += 18.0f;
        }
        const float blink = Pulse(1.0f) > 0.5f ? 1.0f : 0.0f;
        if (blink > 0.0f) {
            DrawRoundRect(draw, ImVec2(body.min.x + 2.0f, y - 14.0f),
                          ImVec2(body.min.x + 9.0f, y - 2.0f), 1.0f, ColorAccent());
        }
    } else if (layout_.dockTab == 2) {
        for (int i = 0; i < 3; ++i) {
            const Rect row{body.min.x, body.min.y + 16.0f * static_cast<float>(i), body.min.x + 260.0f,
                           body.min.y + 16.0f * static_cast<float>(i) + 16.0f};
            DrawTextClipped(draw, FontAt(12.0f), 12.0f, row.min, 260.0f, ColorTextSecondary(),
                            "artifacts/S012_v3.png");
        }
    } else if (layout_.dockTab == 0) {
        Progress(draw, Rect{body.min.x, body.min.y, body.min.x + 320.0f, body.min.y + 6.0f}, 42.0f,
                 true, false);
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
    StatusDot(draw, ImVec2(x + 3.5f, cy), theme::Tone::Idle, false);
    x += 12.0f;
    const char* items[] = {"Comfy 未连接", "LLM 就绪", "队列 0"};
    for (const char* item : items) {
        const float w =
            FontAt(11.5f)->CalcTextSizeA(11.5f, 1e9f, 0.0f, item, item + std::strlen(item)).x + 16.0f;
        DrawRoundRect(draw, ImVec2(x, cy - 10.0f), ImVec2(x + w, cy + 10.0f), 4.0f, 0);
        draw->AddText(FontAt(11.5f), 11.5f, ImVec2(x + 8.0f, cy - 5.75f), ColorTextSecondary(), item,
                      item + std::strlen(item));
        x += w + 6.0f;
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
            draw->AddText(FontAt(12.5f), 12.5f, ImVec2(x, cy - 6.25f),
                          WithAlpha(ColorTextMuted(), 0.55f), "›", "›" + 3);
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
    ImDrawList* draw = ImGui::GetWindowDrawList();

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

    float y = input.max.y + 6.0f;
    std::string lastGroup;
    ImFont* groupFont = FontBoldAt(11.5f);
    ImFont* labelFont = FontAt(13.0f);
    for (int i = 0; i < paletteMatches_; ++i) {
        const Item& item = *matched[static_cast<std::size_t>(i)];
        if (item.group != lastGroup) {
            lastGroup = item.group;
            draw->AddText(groupFont, 11.5f, ImVec2(bounds.min.x + 18.0f, y + 6.0f), ColorTextMuted(),
                          item.group.data(), item.group.data() + item.group.size());
            y += 22.0f;
        }
        const Rect row{bounds.min.x + 12.0f, y, bounds.max.x - 12.0f, y + 28.0f};
        if (i == paletteSelected_) {
            DrawRoundRect(draw, row.min, row.max, 6.0f, ColorFillSelected());
        }
        draw->AddText(labelFont, 13.0f, ImVec2(row.min.x + 10.0f, row.min.y + 6.0f), ColorText(),
                      item.label.data(), item.label.data() + item.label.size());
        draw->AddText(FontAt(11.5f), 11.5f, ImVec2(row.max.x - 80.0f, row.min.y + 7.0f),
                      ColorTextMuted(), item.hint.data(), item.hint.data() + item.hint.size());
        y += 30.0f;
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
}

void Shell::SetWorkspace(int index) {
    layout_.workspace = std::clamp(index, 0, kWorkspaceCount - 1);
    layout_.lastViewLabel = "总览";
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
void Shell::ToggleCommandPalette() { paletteOpen_ = !paletteOpen_; }

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

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("##shine-root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float W = display.x;
    const float H = display.y;
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

} // namespace shine::pages
