#pragma once
// P08 出片工作区：全屏节点画布 + 右侧浮动面板 + 底部连播胶片条。
//
// 结构对齐 webui（webui/src/views/VideoFlow.jsx）：
//   画布满铺整页
//   → 顶部浮动工具栏（提交前参数校验）
//   → 右侧浮动面板（可折叠）：首尾帧链 · 视频任务 · 成片
//   → 底部胶片条：按章连排镜头缩略 + 就绪态（点击查看）
#include "ui/kit/canvas/FlowCanvas.h"

#include <QWidget>

#include <filesystem>

class QLabel;
class QTabWidget;
class QListWidget;
class QPushButton;

namespace shine::app {

class ChainView;
class FinalCutView;
class VideoTaskView;
class FilmStrip;

class VideoFlowWorkspace : public QWidget {
  public:
    explicit VideoFlowWorkspace(QWidget* parent = nullptr);
    void LoadMock();
    void SetContext(std::filesystem::path db_path, std::filesystem::path project_dir);
    void Validate();
    void TogglePanel();
    [[nodiscard]] QString Probe() const;
    [[nodiscard]] QString ChainProbe() const;
    [[nodiscard]] QString TaskProbe() const;
    [[nodiscard]] QString FinalProbe() const;
    [[nodiscard]] QString FilmProbe() const;

    [[nodiscard]] shine::kit::FlowCanvas* Canvas() const noexcept { return canvas_; }
    [[nodiscard]] ChainView* Chain() const noexcept { return chain_; }
    [[nodiscard]] VideoTaskView* Tasks() const noexcept { return tasks_; }
    [[nodiscard]] FinalCutView* Final() const noexcept { return final_; }
    [[nodiscard]] FilmStrip* Film() const noexcept { return film_; }

  private:
    void BuildUi();
    std::filesystem::path db_path_;
    std::filesystem::path project_dir_;
    shine::kit::FlowCanvas* canvas_ = nullptr;
    ChainView* chain_ = nullptr;
    VideoTaskView* tasks_ = nullptr;
    FinalCutView* final_ = nullptr;
    FilmStrip* film_ = nullptr;
    QTabWidget* panel_stack_ = nullptr;
    QLabel* status_ = nullptr;
    QWidget* panel_ = nullptr;
    QPushButton* fold_btn_ = nullptr;
    bool panel_folded_ = false;
};

} // namespace shine::app
