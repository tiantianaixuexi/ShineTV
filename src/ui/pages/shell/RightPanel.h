#pragma once
// shine::app —— 右栏（P03-S6 七区之一）：三个可折叠段（属性 / 预览 / 生成参数）。
// 落点清单以当前源码为准；页面行为与布局验证见 docs/40-operations/verification.md。
#include <QFrame>

namespace shine::app {

class RightPanel : public QFrame {
  public:
    explicit RightPanel(QWidget* parent = nullptr);
};

} // namespace shine::app
