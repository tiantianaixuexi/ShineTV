#pragma once
// P06-S5 单镜头详情：表演 12 项 / 空间 / 机位 / Beat[] / 引用。
#include "novel/NovelVisual.h"

#include <QWidget>

#include <functional>

class QLabel;
class QPlainTextEdit;

namespace shine::app {

class ShotDetailView : public QWidget {
  public:
    using TimelineHandler = std::function<void(std::string)>;
    explicit ShotDetailView(QWidget* parent = nullptr);

    void SetShot(const novelcore::ShotRow& shot);
    void SetTimelineHandler(TimelineHandler handler) { on_timeline_ = std::move(handler); }
    [[nodiscard]] QString DetailProbe() const;

  private:
    void Rebuild();
    void SaveTimeline();

    novelcore::ShotRow shot_;
    std::string timeline_json_ = "{}";
    TimelineHandler on_timeline_;
    QLabel* title_ = nullptr;
    QLabel* performance_ = nullptr;
    QLabel* spatial_ = nullptr;
    QLabel* camera_ = nullptr;
    QLabel* references_ = nullptr;
    QPlainTextEdit* timeline_ = nullptr;
};

} // namespace shine::app
