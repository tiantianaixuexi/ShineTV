#pragma once
#include "flow/BatchRender.h"

#include <QWidget>

#include <filesystem>
#include <vector>

class QLabel;
class QVBoxLayout;

namespace shine::app {

class VideoTaskView : public QWidget {
  public:
    explicit VideoTaskView(QWidget* parent = nullptr);
    void SetShots(std::vector<std::pair<std::int64_t, QString>> shots);
    void SetFirstFrame(std::int64_t shot_id, const std::filesystem::path& path);
    void EnqueueAll();
    void ShowRunning();
    [[nodiscard]] QString Probe() const;

  private:
    void Rebuild();
    flow::BatchRenderQueue queue_;
    std::vector<std::pair<std::int64_t, QString>> shots_;
    std::vector<std::pair<std::int64_t, std::filesystem::path>> frames_;
    // 密集行列表（webui .dlist）：行由 Rebuild 逐个建、逐个销毁
    QWidget* list_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace shine::app
