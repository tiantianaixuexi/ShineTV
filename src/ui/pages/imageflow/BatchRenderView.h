#pragma once
// P07-S10：批量出图队列、进度、重试、降级账。
#include "flow/BatchRender.h"
#include <QString>
#include <utility>

#include <QWidget>

#include <vector>

class QLabel;
class QTableWidget;

namespace shine::app {

class BatchRenderView : public QWidget {
  public:
    explicit BatchRenderView(QWidget* parent = nullptr);
    void SetShots(std::vector<std::pair<std::int64_t, QString>> shots);
    void EnqueueAll();
    void RunMockBatch();
    void CancelAll();
    void ShowRunningDemo();
    [[nodiscard]] QString Probe() const;

  private:
    void Rebuild();
    void CompleteMock();

    flow::BatchRenderQueue queue_;
    std::vector<std::pair<std::int64_t, QString>> shots_;
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace shine::app
