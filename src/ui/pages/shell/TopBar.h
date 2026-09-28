#pragma once
// shine::app —— 顶栏（对齐 webui/src/styles/shell.css:18-155）：
//   [品牌 ▸站名] [项目名胶囊 ▾] [🔍 搜索命令 Ctrl K] …… [▶运行 ⏹停止] [🎨主题] [⚙]
// 主题菜单承接 P02-S8 全部能力（5 内置 + 自定义 + 样式编辑器 + 减少动效），
// 内置主题每项带 26×16 色卡（取该主题自身的 accent token）。
#include <QFrame>
#include <QString>

#include <functional>

class QPushButton;
class QWidget;

namespace shine::widgets {
class Button;
}

namespace shine::app {

class TopBar : public QFrame {
  public:
    explicit TopBar(QWidget* parent = nullptr);

    void SetProjectName(const QString& name); // 空 = 未打开项目
    void SetOnShowHub(std::function<void()> cb);          // 「项目列表」
    void SetOnCloseProject(std::function<void()> cb);     // 「关闭项目」
    void SetOnRevealProject(std::function<void()> cb);    // 「在资源管理器中显示」
    void SetOnOpenPalette(std::function<void()> cb);      // Ctrl+K 搜索
    void SetOnToggleSidePanel(std::function<void()> cb);  // Ctrl+B 左侧导航开合
    void SetOnToggleInspector(std::function<void()> cb);  // Ctrl+I 右侧检查器开合
    void SetOnRun(std::function<void()> cb);
    void SetOnStop(std::function<void()> cb);
    void SetOnSettings(std::function<void()> cb);
    void SetOnThemeChanged(std::function<void()> cb);     // 换肤后（状态栏主题名刷新）

    void SetSidePanelActive(bool on); // 侧栏开合时按钮态跟着走
    void SetInspectorActive(bool on); // 检查器开合时按钮态跟着走
    void PopupThemeMenu(); // 主题菜单（状态栏「主题名」点击复用，避免两处菜单分叉）

  private:
    void ShowThemeMenu(const QPoint& globalPos);

    QPushButton* brand_ = nullptr;         // 站名（点击回项目列表）
    QWidget* proj_chip_ = nullptr;         // 项目名胶囊外壳（色点 + 文字）
    QWidget* proj_dot_ = nullptr;          // 胶囊里的 accent 色点
    QPushButton* palette_ = nullptr;       // 搜索位（260×28）
    shine::widgets::Button* project_btn_ = nullptr;
    shine::widgets::Button* theme_btn_ = nullptr;
    shine::widgets::Button* side_btn_ = nullptr;
    shine::widgets::Button* inspector_btn_ = nullptr;
    bool has_project_ = false;
    std::function<void()> on_show_hub_;
    std::function<void()> on_close_project_;
    std::function<void()> on_reveal_project_;
    std::function<void()> on_open_palette_;
    std::function<void()> on_toggle_side_panel_;
    std::function<void()> on_toggle_inspector_;
    std::function<void()> on_run_;
    std::function<void()> on_stop_;
    std::function<void()> on_settings_;
    std::function<void()> on_theme_changed_;
};

} // namespace shine::app
