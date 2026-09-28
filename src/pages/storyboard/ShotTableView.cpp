#include "pages/storyboard/ShotTableView.h"

#include "widget/data/Table.h"
#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"
#include "widget/controls/Inputs.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QTableView>
#include <QVBoxLayout>

namespace shine::app {

ShotTableView::ShotTableView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);
    auto* title = new QLabel(QStringLiteral("镜头表 · 千行虚拟滚动 / 列宽按 id 持久化"), this);
    widgets::SetKind(title, "statetitle");
    widgets::SetSemibold(title, true);
    outer->addWidget(title);

    auto* command = new QWidget(this);
    auto* command_layout = new QHBoxLayout(command);
    command_layout->setContentsMargins(0, 0, 0, 0);
    mood_input_ = new widgets::TextInput(command);
    mood_input_->SetPlaceholder(QStringLiteral("批量情绪，例如：紧张"));
    command_layout->addWidget(mood_input_, 1);
    auto* batch = new widgets::Button(QStringLiteral("应用到选中镜头"),
                                      widgets::Button::Variant::Secondary,
                                      widgets::Button::Size::Sm, command);
    command_layout->addWidget(batch);
    outer->addWidget(command);

    table_ = new data::DataTable(QStringLiteral("storyboard.shots"), this);
    table_->SetColumns({
        {QStringLiteral("ord"), QStringLiteral("#"), 52},
        {QStringLiteral("shot"), QStringLiteral("镜号"), 72},
        {QStringLiteral("action"), QStringLiteral("动作"), 220},
        {QStringLiteral("mood"), QStringLiteral("情绪"), 120},
        {QStringLiteral("duration"), QStringLiteral("时长"), 90},
        {QStringLiteral("status"), QStringLiteral("状态"), 100},
        {QStringLiteral("edit"), QStringLiteral("操作"), 80},
    });
    table_->SetSelectable(data::DataTable::Select::Multi);
    table_->SetActionColumn(QStringLiteral("操作"), [this](int row) { EditSelected(row); });
    outer->addWidget(table_, 1);

    connect(batch, &QPushButton::clicked, this, [this] {
        if (on_batch_mood_ && mood_input_ != nullptr) {
            const std::vector<int> selected = table_->SelectedRows();
            std::vector<novelcore::RowId> ids;
            ids.reserve(selected.size());
            for (int row : selected) {
                if (row >= 0 && row < static_cast<int>(shots_.size())) {
                    ids.push_back(shots_[static_cast<std::size_t>(row)].id);
                }
            }
            if (!ids.empty()) {
                on_batch_mood_(ids, mood_input_->Text());
            }
        }
    });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            EditSelected(index.row());
        }
    });
}

void ShotTableView::SetShots(std::vector<novelcore::ShotRow> shots) {
    shots_ = std::move(shots);
    Rebuild();
}

void ShotTableView::Rebuild() {
    if (table_ == nullptr) {
        return;
    }
    std::vector<std::vector<QString>> rows;
    rows.reserve(shots_.size());
    for (const auto& shot : shots_) {
        rows.push_back({QString::number(shot.ord), QStringLiteral("S%1").arg(shot.ord),
                        QString::fromStdString(shot.action), QString::fromStdString(shot.mood),
                        QString::fromStdString(shot.duration_note),
                        QString::fromStdString(shot.canon_status), QStringLiteral("编辑")});
    }
    table_->SetRows(rows);
}

void ShotTableView::EditSelected(int row) {
    if (row < 0 || row >= static_cast<int>(shots_.size()) || !on_update_) {
        return;
    }
    const auto& shot = shots_[static_cast<std::size_t>(row)];
    bool ok = false;
    const QString action = QInputDialog::getText(this, QStringLiteral("编辑镜头"),
                                                 QStringLiteral("动作"), QLineEdit::Normal,
                                                 QString::fromStdString(shot.action), &ok);
    if (!ok) return;
    const QString mood = QInputDialog::getText(this, QStringLiteral("编辑镜头"),
                                              QStringLiteral("情绪"), QLineEdit::Normal,
                                              QString::fromStdString(shot.mood), &ok);
    if (!ok) return;
    const QString duration = QInputDialog::getText(this, QStringLiteral("编辑镜头"),
                                                   QStringLiteral("时长备注"), QLineEdit::Normal,
                                                   QString::fromStdString(shot.duration_note), &ok);
    if (!ok) return;
    const ShotEdit edit{.id = shot.id, .action = action, .mood = mood,
                        .duration_note = duration};
    on_update_(edit);
}

void ShotTableView::ApplyMoodToSelection(const QString& mood) {
    if (!on_batch_mood_ || table_ == nullptr) {
        return;
    }
    std::vector<novelcore::RowId> ids;
    for (int row : table_->SelectedRows()) {
        if (row >= 0 && row < static_cast<int>(shots_.size())) {
            ids.push_back(shots_[static_cast<std::size_t>(row)].id);
        }
    }
    if (!ids.empty()) {
        on_batch_mood_(ids, mood);
    }
}

QString ShotTableView::TableProbe() const {
    return QStringLiteral("rows=%1; columns=7; virtual=1; widths=id; edit=%2; batch=%3")
        .arg(static_cast<int>(shots_.size()))
        .arg(on_update_ ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(on_batch_mood_ ? QStringLiteral("1") : QStringLiteral("0"));
}

} // namespace shine::app
