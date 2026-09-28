#pragma once
// 全书甘特：行 = 章，列 = 阶段链（真实阶段表，见 pipeline::AllStages）。
#include "pipeline/StageMachine.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace shine::widgets {
class EmptyState;
} // namespace shine::widgets

namespace shine::app {

class GanttView : public QWidget {
  public:
    explicit GanttView(QWidget* parent = nullptr);
    void SetChapters(int count);
    void SetStageState(int chapter, pipeline::StageId stage, const QString& state);
    // 自带标题开关：本视图可以被直接放进 SectionCard（卡片已有标题栏），
    // 也可以独立使用带自己的标题。默认 true = 独立使用。
    // 总控台把它装进分区卡片，所以关掉，避免同一个标题出现两次。
    void SetOwnTitle(bool on);
    [[nodiscard]] QString Probe() const;

  protected:
    // 换肤后重建取自 token 的表头 / 表格局部样式
    void changeEvent(QEvent* ev) override;

  private:
    void ApplyQss();
    void RefreshEmpty();

    int chapters_ = 0;
    QLabel* title_ = nullptr;
    shine::widgets::EmptyState* empty_ = nullptr;
    QTableWidget* table_ = nullptr;
    // setStyleSheet() 自身会派发 QEvent::StyleChange，而 changeEvent() 对 StyleChange
    // 也要重刷 —— 不防重入就是 setStyleSheet → StyleChange → ApplyQss → setStyleSheet
    // 的无限递归，进程直接 0xC00000FD（栈溢出）崩在 MainWindow 构造期。
    bool applying_qss_ = false;
};

} // namespace shine::app
