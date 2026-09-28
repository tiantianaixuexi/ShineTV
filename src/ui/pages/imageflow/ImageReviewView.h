#pragma once
// P07-S11：固定清单图像评审与重跑入口。
#include "flow/ImageReview.h"

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace shine::app {

class ImageReviewView : public QWidget {
  public:
    explicit ImageReviewView(QWidget* parent = nullptr);
    void SetInput(const flow::ImageReviewInput& input, const std::string& report_path);
    void SetReviewer(flow::ImageReviewFn reviewer) { reviewer_ = std::move(reviewer); }
    void Run();
    [[nodiscard]] QString Probe() const;

  private:
    void Rebuild();

    flow::ImageReviewInput input_;
    std::string report_path_;
    flow::ImageReviewFn reviewer_;
    flow::ImageReviewReport report_;
    bool has_report_ = false;
    QLabel* image_ = nullptr;
    // 五项固定清单（webui .checklist）：行由 Rebuild 逐个建、逐个销毁
    QWidget* checklist_ = nullptr;
    QVBoxLayout* checklist_lay_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace shine::app
