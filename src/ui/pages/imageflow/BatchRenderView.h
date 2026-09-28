#pragma once
// P07-S10：批量出图队列、进度、重试、降级账。
#include "flow/BatchRender.h"
#include <QString>
#include <utility>

#include <QWidget>

#include <vector>

class QLabel;
class QVBoxLayout;

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
    // 密集行列表（webui .dlist）：行由 Rebuild 逐个建、逐个销毁
    QWidget* list_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace shine::app
