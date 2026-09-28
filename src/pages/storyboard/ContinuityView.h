#pragma once
// P06-S6 V8 连续性 C1–C12 机器校验视图。
#include "novel/NovelContinuity.h"

#include <QWidget>

#include <filesystem>
#include <functional>
#include <vector>

class QLabel;

namespace shine::app {

class ContinuityView : public QWidget {
  public:
    using RunHandler = std::function<novelcore::ContinuityOutcome()>;
    explicit ContinuityView(QWidget* parent = nullptr);

    void SetContext(std::filesystem::path db_path, std::filesystem::path project_dir,
                    shine::novelcore::RowId chapter_id);
    bool RunCheck();
    [[nodiscard]] QString ContinuityProbe() const;

  private:
    void Rebuild();
    void ShowResult(const shine::novelcore::ContinuityOutcome& result);
    void ClearResult();

    std::filesystem::path db_path_;
    std::filesystem::path project_dir_;
    shine::novelcore::RowId chapter_id_ = 0;
    shine::novelcore::ContinuityOutcome result_;
    bool has_result_ = false;
    QLabel* status_ = nullptr;
    class QWidget* list_ = nullptr;
};

} // namespace shine::app
