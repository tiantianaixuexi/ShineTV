#pragma once
// shine::widgets —— 导航/布局（P02-S5，UI.md §2.1）：Tabs / Toolbar / Splitter。
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <functional>
#include <vector>

#include <QFrame>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QWidget>

class QHBoxLayout;
class QPushButton;
class QSplitterHandle;

namespace shine::widgets {

// Tabs —— 下划线指示条；指示条滑动 motion.base/emphasized
class Tabs : public QFrame {
  public:
    explicit Tabs(const QStringList& items, QWidget* parent = nullptr);

    void SetCurrent(int index, bool animated = true);
    [[nodiscard]] int Current() const { return current_; }
    void SetOnChanged(std::function<void(int)> cb) { on_changed_ = std::move(cb); }

  protected:
    void resizeEvent(QResizeEvent* ev) override;
    void showEvent(QShowEvent* ev) override;

  private:
    void SlideTo(int index, bool animated);
    void PlaceIndicator(int x, int w, bool animated);

    QHBoxLayout* row_ = nullptr;
    std::vector<QPushButton*> items_;
    QFrame* indicator_ = nullptr;
    int current_ = 0;
    std::function<void(int)> on_changed_;
};

// Toolbar —— 水平；分组 + 分隔线 + 溢出折叠（溢出项收进「更多」弹层）
class Toolbar : public QFrame {
  public:
    explicit Toolbar(QWidget* parent = nullptr);

    void AddGroup(const std::vector<QWidget*>& items); // 组间自动加分隔线
    void AddSpacer();

  protected:
    void resizeEvent(QResizeEvent* ev) override;

  private:
    void Reflow();

    QHBoxLayout* row_ = nullptr;
    std::vector<QWidget*> items_; // 全部可见件（含分隔线）
    QPushButton* more_ = nullptr;
    std::vector<QWidget*> overflow_;
};

// Splitter —— 水平 / 垂直；4px 把手 + 悬停高亮；可折叠（双击把手折叠/展开）
class Splitter : public QSplitter {
  public:
    explicit Splitter(Qt::Orientation o, QWidget* parent = nullptr);

    void SetCollapsible(bool on);
    void Collapse(int index, bool collapse = true); // 折叠 = 把该侧收成 0

  protected:
    QSplitterHandle* createHandle() override;

  private:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void ToggleCollapse(int index);
    bool collapsible_ = true;
};

} // namespace shine::widgets
