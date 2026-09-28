#include "ui/pages/pipeline/GanttView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shine::app {

GanttView::GanttView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    title_ = widgets::SectionTitle(QStringLiteral("全书甘特 · 章节 × 阶段"), this);
    layout->addWidget(title_);
    table_ = new QTableWidget(this);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(table_, 1);
    SetChapters(3);
}

void GanttView::SetOwnTitle(bool on) {
    if (title_ != nullptr) {
        title_->setVisible(on);
    }
}

void GanttView::SetChapters(int count) {
    chapters_ = count;
    table_->setColumnCount(static_cast<int>(pipeline::AllStages().size()) + 1);
    table_->setRowCount(count);
    QStringList headers{QStringLiteral("章")};
    for (const auto& stage : pipeline::AllStages()) headers << QString::fromStdString(stage.code);
    table_->setHorizontalHeaderLabels(headers);
    for (int chapter = 0; chapter < count; ++chapter) {
        table_->setItem(chapter, 0, new QTableWidgetItem(QStringLiteral("第 %1 章").arg(chapter + 1)));
        for (std::size_t stage = 0; stage < pipeline::AllStages().size(); ++stage) {
            table_->setItem(chapter, static_cast<int>(stage) + 1, new QTableWidgetItem(QStringLiteral("待办")));
        }
    }
}

void GanttView::SetStageState(int chapter, pipeline::StageId stage, const QString& state) {
    if (chapter < 0 || chapter >= table_->rowCount()) return;
    const int column = static_cast<int>(stage) + 1;
    if (column < 0 || column >= table_->columnCount()) return;
    table_->setItem(chapter, column, new QTableWidgetItem(state));
}

QString GanttView::Probe() const {
    return QStringLiteral("chapters=%1; columns=%2").arg(chapters_).arg(table_->columnCount());
}

} // namespace shine::app
