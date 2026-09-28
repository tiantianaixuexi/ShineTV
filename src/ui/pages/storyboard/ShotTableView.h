#pragma once
// P06-S4 镜头表：虚拟化行、列 id 宽度持久化、编辑与批量情绪命令。
#include "novel/NovelVisual.h"

#include <QWidget>

#include <functional>
#include <vector>

namespace shine::data {
class DataTable;
}
namespace shine::widgets {
class TextInput;
}

namespace shine::app {

class ShotTableView : public QWidget {
  public:
    struct ShotEdit {
        novelcore::RowId id = 0;
        QString action;
        QString mood;
        QString duration_note;
    };
    using UpdateHandler = std::function<void(const ShotEdit&)>;
    using BatchMoodHandler = std::function<void(const std::vector<novelcore::RowId>&, const QString&)>;

    explicit ShotTableView(QWidget* parent = nullptr);

    void SetShots(std::vector<novelcore::ShotRow> shots);
    void SetOnUpdate(UpdateHandler handler) { on_update_ = std::move(handler); }
    void SetOnBatchMood(BatchMoodHandler handler) { on_batch_mood_ = std::move(handler); }
    void ApplyMoodToSelection(const QString& mood);
    [[nodiscard]] QString TableProbe() const;

  private:
    void Rebuild();
    void EditSelected(int row = -1);

    std::vector<novelcore::ShotRow> shots_;
    UpdateHandler on_update_;
    BatchMoodHandler on_batch_mood_;
    shine::data::DataTable* table_ = nullptr;
    shine::widgets::TextInput* mood_input_ = nullptr;
};

} // namespace shine::app
