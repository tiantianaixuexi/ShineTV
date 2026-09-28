#pragma once
#include "flow/BatchRender.h"

#include <QWidget>

#include <filesystem>
#include <vector>

class QLabel;
class QTableWidget;

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
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace shine::app
