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

#include "project/Project.h"

#include <array>
#include <filesystem>
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
    // 当前工程根。空 = 未打开项目。打开/新建项目后由 SetProjectRoot 写入，
    // 布局持久化也带上它（P1.5），重开程序能回到同一个工程。
    std::filesystem::path projectRoot;
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

    // 打开/新建项目后登记工程根：顶栏胶囊、总控页、后续各工作区都从这里取。
    // root 为空表示「未打开项目」——总控页会显示真实空态，不编数字。
    void SetProjectRoot(std::filesystem::path root, std::string name);
    [[nodiscard]] const std::filesystem::path& projectRoot() const { return layout_.projectRoot; }
    // 从持久化的项目注册表里挑一个打开（项目中心「打开」按钮与命令面板都走它）。
    bool OpenProjectByName(const std::string& name);

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
    // 往底栏「日志」页追加一行（"03:12:44 [info] 文本"）。
    void PushLog(std::string_view level, std::string_view message);

    ShellLayout layout_;
    // 项目服务实例：新建 / 打开 / 最近列表都走它（前端持有自己的实例，见 Project.h:139）。
    project::ProjectService projects_;
    kit::Rect workspace_;
    float toastTimer_ = 0.0f;
    std::vector<std::string> logLines_;
    bool paletteOpen_ = false;
    char paletteQuery_[128] = {};
    int paletteSelected_ = 0;
    int paletteMatches_ = 0;
    float lastDelta_ = 0.0f;
    kit::DebugWindows debug_;
    // 当前字体图集是不是衬线族。Host 初始化时已按启动主题建过一次，
    // 这里存同一份状态，SetTheme 只在**族变了**时重建图集。
    bool atlasIsSerif_ = false;

    // 流水线运行态（顶栏「运行 / 停止」二选一，webui Shell.jsx:44-48）。
    // Runner 是同步阻塞的，真正的执行放 worker；这里只存 UI 侧的状态。
    bool runActive_ = false;
    bool runFinished_ = false;
    int runStageIndex_ = 0; // 0..16，对应 T1..T17
    int runPercent_ = 0;

    // 六个工作区（各自持有交互态：选中的镜头/标签/页签）
    NovelPage novel_;
    AssetsPage assets_;
    StoryboardPage storyboard_;
    ImageFlowPage imageflow_;
    VideoFlowPage videoflow_;
    GalleryPage gallery_;
};

} // namespace shine::pages
