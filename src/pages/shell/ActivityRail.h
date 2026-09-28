#pragma once
// shine::app —— 活动栏（P03-S7）：六个工作区入口（总控/小说/资产/分镜/出图/出片）。
// 每项 IconButton + tooltip；选中项左侧 2.5px accent.primary 指示条，切换时滑动过渡。
// 指示条自绘（颜色取 theme::Current()，源码零字面色），时长取 motion.dur.base。
#include "widget/motion/Tween.h"
#include "widget/controls/Controls.h"

#include <functional>
#include <vector>

#include <QFrame>

namespace shine::app {

class ActivityRail : public QFrame {
  public:
    explicit ActivityRail(QWidget* parent = nullptr);

    void SetCurrent(int index, bool animated = true);
    [[nodiscard]] int Current() const { return current_; }
    void SetOnChanged(std::function<void(int)> cb) { on_changed_ = std::move(cb); }

  protected:
    void paintEvent(QPaintEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

  private:
    void Select(int index, bool animated);

    std::vector<shine::widgets::IconButton*> items_;
    int current_ = 0;
    double indicator_y_ = 0.0; // 当前指示条顶边（像素，滑动补间值）
    shine::motion::Tween* tween_ = nullptr;
    std::function<void(int)> on_changed_;
};

} // namespace shine::app
