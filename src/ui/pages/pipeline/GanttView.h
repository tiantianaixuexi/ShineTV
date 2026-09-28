#pragma once
#include "pipeline/StageMachine.h"

#include <QWidget>

class QLabel;
class QTableWidget;

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

  private:
    int chapters_ = 0;
    QLabel* title_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace shine::app
