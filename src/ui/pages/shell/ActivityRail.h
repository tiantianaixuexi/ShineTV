#pragma once
// shine::app —— 活动栏（P03-S7）：六个工作区入口（总控/小说/资产/分镜/出图/出片）。
// 每项 IconButton + tooltip；选中项左侧 2.5px accent.primary 指示条，切换时滑动过渡。
// 指示条自绘（颜色取 theme::Current()，源码零字面色），时长取 motion.dur.base。
#include "ui/kit/motion/Tween.h"
#include "ui/kit/controls/Controls.h"

#include <functional>
#include <vector>

#include <QFrame>
#include <QStringList>

namespace shine::app {

// 六个工作区的显示名 —— **唯一来源**。活动栏是它们在 UI 上的归属控件，
// 标签页标题 / 面包屑 / 命令面板 / 外壳下标全部从这里取，改一处即可。
// （原先挂在 SidePanel 上，但 SidePanel 只是一块 P03 占位面板，已随三区重构删除。）
const QStringList& WorkspaceNames();

class ActivityRail : public QFrame {
  public:
    explicit ActivityRail(QWidget* parent = nullptr);

    void SetCurrent(int index, bool animated = true);
    [[nodiscard]] int Current() const { return current_; }
    void SetOnChanged(std::function<void(int)> cb) { on_changed_ = std::move(cb); }

    // webui shell.css:99-108：工作区入口之后是一条分隔线 + 三个面板开关
    // （侧栏 / 底栏 / 检查器）。设计稿把它们放在活动栏而不是顶栏，这里照搬。
    void SetOnToggleSide(std::function<void()> cb) { on_toggle_side_ = std::move(cb); }
    void SetOnToggleDock(std::function<void()> cb) { on_toggle_dock_ = std::move(cb); }
    void SetOnToggleInspector(std::function<void()> cb) { on_toggle_inspector_ = std::move(cb); }
    // 三个开关态跟随外壳开合（active = fill.selected + accent 字，见 shell.css:188）
    void SetPanelActive(bool side, bool dock, bool inspector);

  protected:
    void paintEvent(QPaintEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

  private:
    void Select(int index, bool animated);
    // 指示条顶边：当前项按钮垂直居中再上下各缩进 11px（= 22px 高的一半）
    [[nodiscard]] double IndicatorTopFor(int index) const;

    std::vector<shine::widgets::IconButton*> items_;
    // 面板开关独立于工作区入口：不参与 Selected()/指示条，只表达「面板开着没有」
    std::vector<shine::widgets::IconButton*> panels_;
    int current_ = 0;
    double indicator_y_ = 0.0; // 当前指示条顶边（像素，滑动补间值）
    shine::motion::Tween* tween_ = nullptr;
    std::function<void(int)> on_changed_;
    std::function<void()> on_toggle_side_;
    std::function<void()> on_toggle_dock_;
    std::function<void()> on_toggle_inspector_;
};

} // namespace shine::app
