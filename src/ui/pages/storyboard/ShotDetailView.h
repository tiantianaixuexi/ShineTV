#pragma once
// P06-S5 单镜头详情：表演 12 项 / 空间 / 机位 / Beat[] / 引用。
#include "novel/NovelVisual.h"

#include <QWidget>

#include <array>
#include <functional>

class QGridLayout;
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
    // webui ui.css:1015 .kv 的固定行数：设计稿 7 行（动作/空间/表演/机位/光线/
    // 时长/情绪），Qt 端多一行「引用」承载 reference_json（P06-S5 的不变式）。
    inline static constexpr int kKvRows = 8;
    void Rebuild();
    void SaveTimeline();

    novelcore::ShotRow shot_;
    std::string timeline_json_ = "{}";
    TimelineHandler on_timeline_;
    QLabel* title_ = nullptr;
    QLabel* sub_ = nullptr;
    QGridLayout* kv_ = nullptr;
    std::array<QLabel*, kKvRows> keys_{};
    std::array<QLabel*, kKvRows> values_{};
    QPlainTextEdit* timeline_ = nullptr;
};

} // namespace shine::app
