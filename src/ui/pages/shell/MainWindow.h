#pragma once
// shine::app —— 主窗口（P03）：以项目为中心的两态外壳。
//   启动首屏 = ProjectHubView（项目列表）；打开项目后 = 工坊三区
//   （顶栏 / 活动栏+中央区+右侧检查器 / 底栏 / 状态栏），
//   中央区吃掉全部剩余宽度并有硬下限（kCenterMinW），右侧检查器默认收起、
//   Ctrl+I 或顶栏按钮开合，底栏 Ctrl+J 开合（动画 motion.base）。
//   布局持久化：%APPDATA%/ShineTVStudio/layout.dat（geometry/state + 分栏比例 + 标签状态 +
//   最近项目根）；文件被手改坏 → 回退默认布局 + Toast（P03 风险表），不崩。
#include "ui/pages/shell/StatusBar.h"
#include "ui/kit/motion/Tween.h"
#include "project/Project.h"

#include <QMainWindow>

#include <filesystem>

class QCloseEvent;
class QSplitter;
class QStackedWidget;
class QTabBar;

namespace shine::widgets {
class Drawer; // ShowStatusDetail 返回抽屉本体（Surfaces.h 里定义）
}

namespace shine::app {

class ActivityRail;
class Breadcrumb;
class BottomDock;
class CommandPalette;
class NovelWorkspace;
class ProjectHubView;
class AssetWorkspace;
class ImageFlowWorkspace;
class VideoFlowWorkspace;
class PipelineWorkspace;
class StoryboardWorkspace;
class RightPanel;
class TopBar;

class MainWindow : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // —— 自动化 / 验收接口（P03 证据、截图包、验收脚本用；产品代码不用）——
    [[nodiscard]] ProjectHubView* Hub() const { return hub_; }
    [[nodiscard]] CommandPalette* Palette() const { return palette_; }
    [[nodiscard]] ImageFlowWorkspace* ImageFlowPage() const;
    [[nodiscard]] VideoFlowWorkspace* VideoFlowPage() const;
    [[nodiscard]] StatusBar* StateBar() const { return status_bar_; }
    [[nodiscard]] project::ProjectService& Service() { return svc_; }
    [[nodiscard]] QString CurrentProjectName() const;
    [[nodiscard]] QString LayoutProbe() const; // 布局探针文本（两次运行比对持久化）
    [[nodiscard]] bool LayoutRestored() const { return layout_restored_; }
    bool OpenProjectPath(const std::filesystem::path& rootDir); // false = 失败（已 Toast）
    void EnterProject(const project::ProjectRef& ref);          // 进入工坊并接线
    void ShowHubPage();
    void ShowWorkshop();
    void SetTestLayout(); // 固定的非默认布局（验收：重启后逐项还原）
    void PersistNow() { SaveLayout(); }
    // 验收设施：折叠判定（Ctrl+I / Ctrl+J 同路径）与状态栏详情抽屉（S8 判据）。
    // 返回抽屉本体（Drawer 是独立顶层，外部抓帧须抓它；导航型项返回 nullptr）。
    void ToggleInspector();
    void ToggleBottomDock();
    [[nodiscard]] widgets::Drawer* ShowStatusDetail(StatusItem item);
    // P04 自动化：活动栏切工作区；取当前标签页里的小说工作区页（无则 nullptr）
    void SwitchWorkspace(int index);
    [[nodiscard]] NovelWorkspace* NovelPage() const;
    [[nodiscard]] AssetWorkspace* AssetPage() const;
    [[nodiscard]] StoryboardWorkspace* StoryboardPage() const;

  protected:
    void closeEvent(QCloseEvent* ev) override;

  private:
    void BuildHub();
    void BuildWorkshop();
    void BuildPaletteCommands();
    void CloseProjectToHub();
    void RefreshStatusBar();
    void UpdateBreadcrumb();
    void SaveLayout();
    void RestoreLayout();
    [[nodiscard]] std::filesystem::path LayoutFile() const;
    // P04：文档页装配（标题「小说」→ NovelWorkspace；其余仍为占位页）
    void EnsureAssetDocTab();
    void EnsureStoryboardDocTab();
    void LoadStoryboardPages(const project::ProjectRef& ref);
    void EnsureImageDocTab();
    void LoadImagePages(const project::ProjectRef& ref);
    void EnsureVideoDocTab();
    void LoadVideoPages(const project::ProjectRef& ref);
    void LoadAssetPages(const project::ProjectRef& ref);
    QWidget* MakeDocPage(const QString& title);
    void AddDocTab(const QString& title);
    void EnsureNovelDocTab();
    void LoadNovelPages(const project::ProjectRef& ref);

    project::ProjectService svc_;

    QStackedWidget* pages_ = nullptr;
    ProjectHubView* hub_ = nullptr;
    QWidget* workshop_ = nullptr;

    TopBar* top_bar_ = nullptr;
    ActivityRail* rail_ = nullptr;
    RightPanel* right_ = nullptr;
    BottomDock* bottom_ = nullptr;
    StatusBar* status_bar_ = nullptr;
    Breadcrumb* crumb_ = nullptr;
    CommandPalette* palette_ = nullptr;

    // 三区：hsplit_ 只有两格 = [中央区, 右侧检查器]；活动栏是固定宽的兄弟节点。
    static constexpr int kCenterMinW = 720; // 中央区硬下限：低于此值左中右会互相压扁
    QSplitter* hsplit_ = nullptr;
    QTabBar* doc_tabs_ = nullptr;
    QStackedWidget* doc_stack_ = nullptr;

    shine::motion::Tween* inspector_tween_ = nullptr;
    shine::motion::Tween* bottom_tween_ = nullptr;
    bool inspector_visible_ = false; // 检查器默认收起
    bool bottom_visible_ = true;
    int inspector_last_w_ = 320;
    int bottom_last_h_ = 220;
    bool layout_restored_ = true;
    std::filesystem::path last_project_root_;
};

} // namespace shine::app
