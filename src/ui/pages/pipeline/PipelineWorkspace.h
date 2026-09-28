#pragma once
#include "pipeline/Runner.h"

#include <QWidget>

#include <filesystem>

class QLabel;

namespace shine::app {

class GanttView;
class LedgerView;
class StopReportView;

class PipelineWorkspace : public QWidget {
  public:
    explicit PipelineWorkspace(QWidget* parent = nullptr);
    void LoadMock();
    void SetContext(std::filesystem::path root);
    void RunNext();
    void RunAll();
    [[nodiscard]] QString Probe() const;
    [[nodiscard]] QString GanttProbe() const;
    [[nodiscard]] QString LedgerProbe() const;
    [[nodiscard]] QString StopProbe() const;

    [[nodiscard]] GanttView* Gantt() const noexcept { return gantt_; }
    [[nodiscard]] LedgerView* Ledger() const noexcept { return ledger_; }
    [[nodiscard]] StopReportView* StopReport() const noexcept { return stop_; }

  private:
    void BuildUi();
    std::filesystem::path root_;
    pipeline::Runner runner_;
    GanttView* gantt_ = nullptr;
    LedgerView* ledger_ = nullptr;
    StopReportView* stop_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
