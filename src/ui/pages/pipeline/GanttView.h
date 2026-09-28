#pragma once
#include "pipeline/StageMachine.h"

#include <QWidget>

class QTableWidget;

namespace shine::app {

class GanttView : public QWidget {
  public:
    explicit GanttView(QWidget* parent = nullptr);
    void SetChapters(int count);
    void SetStageState(int chapter, pipeline::StageId stage, const QString& state);
    [[nodiscard]] QString Probe() const;

  private:
    int chapters_ = 0;
    QTableWidget* table_ = nullptr;
};

} // namespace shine::app
