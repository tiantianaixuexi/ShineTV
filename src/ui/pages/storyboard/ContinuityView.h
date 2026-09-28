#pragma once
// P06-S6 V8 连续性 C1–C12 机器校验视图。
#include "novel/NovelContinuity.h"

#include <QWidget>

#include <filesystem>
#include <functional>
#include <vector>

class QGridLayout;
class QLabel;

namespace shine::widgets {
class Chip;
class Tag;
} // namespace shine::widgets

namespace shine::app {

class ContinuityView : public QWidget {
  public:
    using RunHandler = std::function<novelcore::ContinuityOutcome()>;
    explicit ContinuityView(QWidget* parent = nullptr);

    void SetContext(std::filesystem::path db_path, std::filesystem::path project_dir,
                    shine::novelcore::RowId chapter_id);
    bool RunCheck();
    [[nodiscard]] QString ContinuityProbe() const;

  protected:
    // webui .chips 是 flex + wrap；Qt Widgets 没有 flow layout，按可用宽度
    // 在 resizeEvent 里重排网格（与「QSS 没有 minmax/auto-fit → resizeEvent
    // 重算」是同一条既定做法）。
    void resizeEvent(QResizeEvent* event) override;

  private:
    void Rebuild();
    void RebuildChips();
    void ReflowChips();
    void ShowResult(const shine::novelcore::ContinuityOutcome& result);
    void ClearResult();

    std::filesystem::path db_path_;
    std::filesystem::path project_dir_;
    shine::novelcore::RowId chapter_id_ = 0;
    shine::novelcore::ContinuityOutcome result_;
    bool has_result_ = false;
    QLabel* status_ = nullptr;
    class QWidget* chips_ = nullptr; // C1–C12 药丸换行容器
    QGridLayout* chips_grid_ = nullptr;
    int chip_cols_ = 0;
    std::vector<shine::widgets::Chip*> chip_items_;
    shine::widgets::Tag* verdict_ = nullptr;
    class QWidget* list_ = nullptr;
};

} // namespace shine::app
