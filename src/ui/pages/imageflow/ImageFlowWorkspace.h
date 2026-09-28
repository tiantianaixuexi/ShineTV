#pragma once
// P07：流程画布、绑定、批量出图、图评审与 Comfy 面板的组合工作区。
#include "flow/FlowBinder.h"
#include "flow/ImageReview.h"
#include "ui/kit/canvas/FlowCanvas.h"

#include <QWidget>

#include <filesystem>
#include <string>
#include <vector>

class QLabel;

namespace shine::app {

class BatchRenderView;
class BindingView;
class ComfyPanel;
class ImageReviewView;

class ImageFlowWorkspace : public QWidget {
  public:
    explicit ImageFlowWorkspace(QWidget* parent = nullptr);
    ~ImageFlowWorkspace() override;

    void SetContext(std::filesystem::path db_path, std::filesystem::path project_dir);
    void SetBaseUrl(const QString& url);
    void LoadMock();
    void LoadFromStoryboard();
    void ImportApiJson(const QString& text);
    bool ExportApiJson(const QString& path);
    void Validate();
    void RunMock();
    [[nodiscard]] QString Probe() const;
    [[nodiscard]] QString GraphProbe() const;
    [[nodiscard]] QString ReviewProbe() const;
    [[nodiscard]] QString BatchProbe() const;
    [[nodiscard]] QString BindingProbe() const;
    [[nodiscard]] QString ComfyProbe() const;

    [[nodiscard]] shine::kit::FlowCanvas* Canvas() const noexcept { return canvas_; }
    [[nodiscard]] BatchRenderView* Batch() const noexcept { return batch_; }
    [[nodiscard]] BindingView* Binding() const noexcept { return binding_; }
    [[nodiscard]] ImageReviewView* Review() const noexcept { return review_; }
    [[nodiscard]] ComfyPanel* Comfy() const noexcept { return comfy_; }

  private:
    void BuildUi();
    void RefreshGraphFromHost();
    void SetStatus(const QString& text);
    [[nodiscard]] std::vector<flow::BindingShotContext> ReadShots() const;

    std::filesystem::path db_path_;
    std::filesystem::path project_dir_;
    shine::kit::FlowCanvas* canvas_ = nullptr;
    ComfyPanel* comfy_ = nullptr;
    BindingView* binding_ = nullptr;
    BatchRenderView* batch_ = nullptr;
    ImageReviewView* review_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
