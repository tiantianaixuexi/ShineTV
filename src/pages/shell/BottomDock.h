#pragma once
// shine::app —— 底栏（P03-S6）：四个 tab（任务队列/日志/产物/校验报告），整条可折叠（Ctrl+J）。
// 折叠动画走 motion.dur.base（MainWindow 统一补间高度）；内容页 P03 为占位。
#include <QFrame>

#include <functional>

class QLabel;
class QStackedWidget;

namespace shine::widgets {
class Tabs;
}

namespace shine::app {

class BottomDock : public QFrame {
  public:
    explicit BottomDock(QWidget* parent = nullptr);

    void SetCurrentTab(int index);
    [[nodiscard]] int CurrentTab() const;
    void SetOnTabChanged(std::function<void(int)> cb);

  private:
    shine::widgets::Tabs* tabs_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    std::function<void(int)> on_tab_changed_;
};

} // namespace shine::app
