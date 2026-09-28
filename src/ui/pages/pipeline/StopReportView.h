#pragma once
#include "pipeline/StopPolicy.h"

#include <QWidget>

class QLabel;

namespace shine::app {

class StopReportView : public QWidget {
  public:
    explicit StopReportView(QWidget* parent = nullptr);
    void SetDecision(pipeline::StopDecision decision);
    [[nodiscard]] QString Probe() const;

  private:
    pipeline::StopDecision decision_;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
