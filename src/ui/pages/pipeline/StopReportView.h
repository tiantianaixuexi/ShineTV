#pragma once
#include "pipeline/StopPolicy.h"

#include <QWidget>

class QLabel;

namespace shine::app {

class StopReportView : public QWidget {
  public:
    explicit StopReportView(QWidget* parent = nullptr);
    void SetDecision(pipeline::StopDecision decision);
    // 自带标题开关：装进 SectionCard 时关掉，避免标题出现两次（同 GanttView）
    void SetOwnTitle(bool on);
    [[nodiscard]] QString Probe() const;

  private:
    pipeline::StopDecision decision_;
    QLabel* title_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
