#pragma once
// P07 出图工作区：全屏节点画布 + 右侧浮动参数面板。
//
// 结构对齐 webui（webui/src/views/ImageFlow.jsx）：
//   画布满铺整页（canvas-page）
//   → 顶部浮动工具栏：导入 / 导出 / 提交前校验 / 批量出图
//   → 右侧浮动面板（可折叠）：绑定 · 批量出图 · 图评审 · 结果
//   → 面板底部常驻 ComfyUI 健康条（连接状态 + 队列 + 释放 VRAM）
//
// 画布不再与面板分栏争宽度：面板浮在画布之上，折叠时画布拿到整幅，
// 展开时画布自动收窄让出面板位置。
#include "flow/FlowBinder.h"
#include "flow/ImageReview.h"
#include "ui/kit/canvas/FlowCanvas.h"

#include <QWidget>

#include <filesystem>
#include <string>
#include <vector>

class QLabel;
class QStackedWidget;
class QTabWidget;
class QPushButton;

namespace shine::app {

class BatchRenderView;
class BindingView;
class ComfyPanel;
class ImageReviewView;
class RenderResultView;

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
    [[nodiscard]] QString ResultProbe() const;

    [[nodiscard]] shine::kit::FlowCanvas* Canvas() const noexcept { return canvas_; }
    [[nodiscard]] BatchRenderView* Batch() const noexcept { return batch_; }
    [[nodiscard]] BindingView* Binding() const noexcept { return binding_; }
    [[nodiscard]] ImageReviewView* Review() const noexcept { return review_; }
    [[nodiscard]] ComfyPanel* Comfy() const noexcept { return comfy_; }
    // 浮动面板开合（面板头部的折叠按钮）
    void TogglePanel();

  private:
    void BuildUi();
    void RefreshGraphFromHost();
    void SetStatus(const QString& text);
    void SwitchPanelTab(int index);
    [[nodiscard]] std::vector<flow::BindingShotContext> ReadShots() const;

    std::filesystem::path db_path_;
    std::filesystem::path project_dir_;
    shine::kit::FlowCanvas* canvas_ = nullptr;
    ComfyPanel* comfy_ = nullptr;
    BindingView* binding_ = nullptr;
    BatchRenderView* batch_ = nullptr;
    ImageReviewView* review_ = nullptr;
    RenderResultView* result_ = nullptr; // 「结果」页签：当前镜头成图 + 视频就绪
    QTabWidget* panel_stack_ = nullptr;
    QLabel* status_ = nullptr;
    QWidget* panel_ = nullptr;
    QPushButton* fold_btn_ = nullptr;
    bool panel_folded_ = false;
};

} // namespace shine::app
