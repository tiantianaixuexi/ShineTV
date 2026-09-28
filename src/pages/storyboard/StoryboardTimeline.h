#pragma once
// P06-S3 Scene → Sequence → Shot 故事板时间线：镜头占位/缩略图、时长与拖拽重排。
#include "novel/NovelVisual.h"

#include <QWidget>

#include <functional>
#include <vector>

class QScrollArea;
class QHBoxLayout;
class QWidget;
#include <QPoint>

namespace shine::app {

class StoryboardTimeline : public QWidget {
  public:
    using ReorderHandler = std::function<void(const std::vector<novelcore::RowId>&)>;

    explicit StoryboardTimeline(QWidget* parent = nullptr);

    void SetScene(const novelcore::SceneRow& scene, std::vector<novelcore::ShotRow> shots);
    void SetOnReorder(ReorderHandler handler) { on_reorder_ = std::move(handler); }
    [[nodiscard]] QString TimelineProbe() const;

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  private:
    QWidget* host_ = nullptr;
    QHBoxLayout* row_ = nullptr;
    novelcore::SceneRow scene_;
    std::vector<novelcore::ShotRow> shots_;
    ReorderHandler on_reorder_;

    QPoint drag_start_;
    int drag_ord_ = 0;
    void Rebuild();
};

} // namespace shine::app
