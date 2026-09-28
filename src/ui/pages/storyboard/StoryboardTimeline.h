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
    // 当前选中镜头（对应 webui 的 selShot）：卡片亮 accent 边 + accent 辉光
    void SetSelectedShot(novelcore::RowId id);
    void SetOnSelectShot(std::function<void(novelcore::RowId)> handler) {
        on_select_ = std::move(handler);
    }
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
    std::function<void(novelcore::RowId)> on_select_;
    novelcore::RowId selected_id_ = 0;

    QPoint drag_start_;
    int drag_ord_ = 0;
    void Rebuild();
    // 卡片态合成：选中 = accent 边（QSS 的 [selected="true"]）+ accent 辉光，
    // 悬停 = shadow-1，拖拽中不加辉光（.dragging 的半透明由拖拽像素图承担）
    void ApplyCardStates();
    // duration_note（"3.5s" / "4.1"）→ 时长条百分比，webui 口径 min(100, secs/6*100)
    static int SecondsOf(const std::string& note);
};

} // namespace shine::app
