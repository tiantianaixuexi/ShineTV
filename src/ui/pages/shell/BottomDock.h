#pragma once
// shine::app —— 底栏（对齐 webui/src/styles/shell.css:372-443）：
//   四个 tab（任务队列 / 日志 / 产物 / 校验报告）+ 行尾收起按钮，整条可折叠（Ctrl+J）。
// 「日志」页接真实日志缓冲（core/Log.h 的线程安全快照，按 Version() 增量追加，
// 底部自动跟随）；其余三页尚无数据源，保持占位说明。折叠动画走 motion.dur.base
// （MainWindow 统一补间高度）。
#include <QFrame>

#include <cstdint>
#include <functional>

class QLabel;
class QStackedWidget;
class QVBoxLayout;

namespace shine {
namespace log {
struct Line;
} // namespace log
} // namespace shine

namespace shine::widgets {
class Tabs;
class IconButton;
}

namespace shine::app {

class BottomDock : public QFrame {
  public:
    explicit BottomDock(QWidget* parent = nullptr);

    void SetCurrentTab(int index);
    [[nodiscard]] int CurrentTab() const;
    void SetOnTabChanged(std::function<void(int)> cb);
    void SetOnClose(std::function<void()> cb); // 行尾 ✕（Ctrl+J 同路径）

  private:
    [[nodiscard]] QWidget* MakeLogView();
    static void AppendLogLine(QVBoxLayout* lay, const shine::log::Line& line);

    shine::widgets::Tabs* tabs_ = nullptr;
    shine::widgets::IconButton* close_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    std::function<void(int)> on_tab_changed_;
    std::function<void()> on_close_;
};

} // namespace shine::app
