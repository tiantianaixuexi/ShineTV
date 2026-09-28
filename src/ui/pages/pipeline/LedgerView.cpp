#include "ui/pages/pipeline/LedgerView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shine::app {

LedgerView::LedgerView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    title_ = widgets::SectionTitle(QStringLiteral("账本 · 成本 / 调用 / 降级"), this);
    layout->addWidget(title_);
    summary_ = new QLabel(QStringLiteral("暂无账本"), this);
    widgets::SetKind(summary_, "statedetail");
    layout->addWidget(summary_);
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({QStringLiteral("阶段"), QStringLiteral("输入哈希"), QStringLiteral("产物"), QStringLiteral("降级")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);
}

void LedgerView::SetOwnTitle(bool on) {
    if (title_ != nullptr) {
        title_->setVisible(on);
    }
}

void LedgerView::SetLedger(const pipeline::Ledger& ledger, const pipeline::Budget& budget) {
    entries_ = static_cast<int>(ledger.Entries().size());
    calls_ = budget.llm_calls;
    table_->setRowCount(entries_);
    for (int row = 0; row < entries_; ++row) {
        const auto& entry = ledger.Entries()[static_cast<std::size_t>(row)];
        const std::array<QString, 4> values{QString::fromStdString(pipeline::StageCode(entry.stage)),
                                               QString::fromStdString(entry.input_hash),
                                               QString::fromStdString(entry.output_path),
                                               QString::fromStdString(entry.degradation)};
        for (int col = 0; col < values.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(values[static_cast<std::size_t>(col)]));
    }
    summary_->setText(QStringLiteral("阶段产物 %1 · LLM 调用 %2 · 高档 %3 · 镜头 %4 · 估算成本 %5")
                          .arg(entries_).arg(budget.llm_calls).arg(budget.high_quality_calls)
                          .arg(budget.shots).arg(budget.cost));
}

QString LedgerView::Probe() const {
    return QStringLiteral("entries=%1; calls=%2").arg(entries_).arg(calls_);
}

} // namespace shine::app
