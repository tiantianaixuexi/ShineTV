#pragma once
// shine::pages::Shell —— 应用外壳（refactor/phases.md P4）
//
// 尺寸全部照 design-spec.md §4（100% 缩放下）：
//   TopBar 46 / Rail 56 / SidePanel 240 / Inspector 280 / Dock 190 / StatusBar 26 / Crumbs 34
//
// 布局是自绘的绝对栅格（不是 ImGui 的 dock）：设计稿的每一档宽高都有出处，
// 用 ImGui 的自动布局反而对不上。外壳本身只占一层全屏 ImGui 窗口，
// 子区域全部用 kit::Rect 传给各自的绘制函数。
#pragma once

#include "ui/imgui/kit/Debug.h"
#include "ui/imgui/kit/Widgets.h"
#include "ui/imgui/pages/Gallery.h"
#include "ui/imgui/pages/WorkspacePages.h"
#include "ui/imgui/theme/Theme.h"

#include <array>
#include <string>
#include <vector>

namespace shine::pages {

// 6 个工作区 + 组件画廊（Rail 第 10 项，hidden，只在这一栏出现）。
enum class Workspace {
    Overview = 0,   // 总控
    Novel,          // 小说
    Assets,         // 资产
    Storyboard,     // 分镜
    ImageFlow,      // 出图
    VideoFlow,      // 出片
    Gallery,        // 组件画廊
};

inline constexpr int kWorkspaceCount = 7;

[[nodiscard]] const char* WorkspaceIcon(int workspace);
[[nodiscard]] const char* WorkspaceLabel(int workspace);
[[nodiscard]] const char* WorkspaceSubtitle(int workspace);
// 数据通路目标（refactor/README 的模块表），取证 manifest 里要写清楚。
[[nodiscard]] const char* WorkspaceTarget(int workspace);

// 布局持久化（P1.5）：%APPDATA%/ShineTVStudio/layout.dat，magic shinetv-layout-1
struct ShellLayout {
    int workspace = 0;
    bool sidePanelVisible = true;
    bool dockVisible = true;
    bool inspectorVisible = true;
    int sidePanelWidth = 240;
    int inspectorWidth = 280;
    int dockHeight = 190;
    int dockTab = 0;
    std::string projectName;
    std::string lastViewLabel;
    bool reduceMotion = false;
};

class Shell {
public:
    void DrawFrame(float dt);

    // 取证 / 快捷键用
    void SetWorkspace(int index);
    [[nodiscard]] int workspace() const { return layout_.workspace; }
    void SetTheme(shine::theme::ThemeId id);
    void ToggleSidePanel();
    void ToggleDock();
    void ToggleInspector();
    void ToggleCommandPalette();
    [[nodiscard]] kit::Rect workspaceRect() const { return workspace_; }

    // 布局持久化
    [[nodiscard]] bool LoadLayout();
    [[nodiscard]] bool SaveLayout();

private:
    void DrawTopBar(kit::Rect area, ImDrawList* draw);
    void DrawRail(kit::Rect area, ImDrawList* draw);
    void DrawSidePanel(kit::Rect area, ImDrawList* draw);
    void DrawInspector(kit::Rect area, ImDrawList* draw);
    void DrawDock(kit::Rect area, ImDrawList* draw);
    void DrawStatusBar(kit::Rect area, ImDrawList* draw);
    void DrawBreadcrumbs(kit::Rect area, ImDrawList* draw);
    void DrawWorkspace(kit::Rect area, ImDrawList* draw);
    void DrawCommandPalette();
    void DrawOverlays(ImDrawList* draw);
    void ApplyShortcuts();

    ShellLayout layout_;
    kit::Rect workspace_;
    float toastTimer_ = 0.0f;
    std::vector<std::string> logLines_;
    bool paletteOpen_ = false;
    char paletteQuery_[128] = {};
    int paletteSelected_ = 0;
    int paletteMatches_ = 0;
    float lastDelta_ = 0.0f;
    kit::DebugWindows debug_;

    // 六个工作区（各自持有交互态：选中的镜头/标签/页签）
    NovelPage novel_;
    AssetsPage assets_;
    StoryboardPage storyboard_;
    ImageFlowPage imageflow_;
    VideoFlowPage videoflow_;
    GalleryPage gallery_;
};

} // namespace shine::pages
