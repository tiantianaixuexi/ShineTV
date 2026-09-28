#pragma once
// P08：出片流程画布、链式视图、任务队列与成片管理。
#include "widget/canvas/FlowCanvas.h"

#include <QWidget>

#include <filesystem>

class QLabel;

namespace shine::app {

class ChainView;
class FinalCutView;
class VideoTaskView;

class VideoFlowWorkspace : public QWidget {
  public:
    explicit VideoFlowWorkspace(QWidget* parent = nullptr);
    void LoadMock();
    void SetContext(std::filesystem::path db_path, std::filesystem::path project_dir);
    void Validate();
    [[nodiscard]] QString Probe() const;
    [[nodiscard]] QString ChainProbe() const;
    [[nodiscard]] QString TaskProbe() const;
    [[nodiscard]] QString FinalProbe() const;

    [[nodiscard]] shine::kit::FlowCanvas* Canvas() const noexcept { return canvas_; }
    [[nodiscard]] ChainView* Chain() const noexcept { return chain_; }
    [[nodiscard]] VideoTaskView* Tasks() const noexcept { return tasks_; }
    [[nodiscard]] FinalCutView* Final() const noexcept { return final_; }

  private:
    void BuildUi();
    std::filesystem::path db_path_;
    std::filesystem::path project_dir_;
    shine::kit::FlowCanvas* canvas_ = nullptr;
    ChainView* chain_ = nullptr;
    VideoTaskView* tasks_ = nullptr;
    FinalCutView* final_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
