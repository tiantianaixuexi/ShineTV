#pragma once
// shine::app —— 右侧检查器（三区骨架的第 3 区，默认收起，Ctrl+I 或顶栏按钮开合）。
//
// 定位：**只显示当前选中有对象的真实数据**。没有对象时显示空状态，不显示任何
// 占位/路线图文案（上一版这里硬编码着「预览区（P05/P07 接入…）」「后端 mock」
// 之类的占位串，既是假信息，也在主区被重复画了一遍）。
//
// 页面通过 AddSection() 往里放自己的段；外壳不预置任何内容。
#include "ui/kit/controls/WidgetCommon.h"

#include <QFrame>
#include <QString>

class QVBoxLayout;
class QScrollArea;
class QStackedWidget;

namespace shine::app {

class RightPanel : public QFrame {
  public:
    explicit RightPanel(QWidget* parent = nullptr);

    // 页面往检查器里放一段（自带标题栏，返回该段本体便于页面后续更新）。
    // 重复调用同名 title 会替换旧段。
    QWidget* AddSection(const QString& title, QWidget* body);

    // 没有选中对象时显示的空状态（页面可调 SetSelection 来切换）
    void SetSelection(const QString& what); // 空串 = 无选中
    void ClearSections();

  private:
    QScrollArea* scroll_ = nullptr;       // .inspector overflow-y: auto
    QStackedWidget* stack_ = nullptr;
    QWidget* empty_ = nullptr;      // 无选中时的空状态页
    QWidget* host_ = nullptr;       // 有选中时的段容器
    QVBoxLayout* host_lay_ = nullptr; // host_ 的布局（末尾恒有一个 stretch）
    class QLabel* empty_text_ = nullptr;
};

} // namespace shine::app
