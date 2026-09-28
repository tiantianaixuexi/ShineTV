#include "ui/pages/pipeline/LedgerView.h"

#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace shine::app {
namespace {

// views.css:94-111 账本表格：th 可点排序（阶段 / 产物），td 里
// 阶段 = text-primary w600、哈希 = .table .num（mono muted）、产物 = .mono、
// 降级 = 无则 dim「—」有则 Tag tone=warn。
constexpr int kSortRole = Qt::UserRole + 51; // 阶段列按阶段序号排，不按字符串

// 阶段中文名取自真实阶段表（pipeline::AllStages），不写死
[[nodiscard]] QString StageName(pipeline::StageId id) {
    for (const auto& def : pipeline::AllStages()) {
        if (def.id == id) {
            return QString::fromStdString(def.name);
        }
    }
    return QString{};
}

} // namespace

LedgerView::LedgerView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    title_ = widgets::SectionTitle(QStringLiteral("账本 · 成本 / 调用 / 降级"), this);
    layout->addWidget(title_);

    empty_ = new widgets::EmptyState(
        QStringLiteral("≡"), QStringLiteral("还没有账本记录"),
        QStringLiteral("每跑完一个阶段会落一条：输入哈希、产物路径与降级说明。"), QString{}, this);
    layout->addWidget(empty_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("阶段"), QStringLiteral("输入哈希"), QStringLiteral("产物"), QStringLiteral("降级")});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    // Overview.jsx:96-99 阶段 / 产物两列表头可点排序
    table_->setSortingEnabled(true);
    layout->addWidget(table_, 1);

    // views.css:112 表格下方的 tiny dim 汇总行
    summary_ = new QLabel(this);
    widgets::SetKind(summary_, "statemeta");
    summary_->setStyleSheet(QStringLiteral("font-size: 12px;"));
    layout->addWidget(summary_);

    RefreshEmpty();
}

void LedgerView::SetOwnTitle(bool on) {
    if (title_ != nullptr) {
        title_->setVisible(on);
    }
}

void LedgerView::SetLedger(const pipeline::Ledger& ledger, const pipeline::Budget& budget) {
    rows_ = ledger.Entries();
    budget_ = budget;
    Render();
}

void LedgerView::SetLedger(const std::vector<pipeline::LedgerEntry>& entries,
                           const pipeline::Budget& budget) {
    rows_ = entries;
    budget_ = budget;
    Render();
}

void LedgerView::RefreshEmpty() {
    const bool empty = rows_.empty();
    table_->setVisible(!empty);
    empty_->setVisible(empty);
}

void LedgerView::Render() {
    entries_ = static_cast<int>(rows_.size());
    calls_ = budget_.llm_calls;

    const bool was_sorted = table_->isSortingEnabled();
    table_->setSortingEnabled(false);
    table_->setRowCount(entries_);
    for (int row = 0; row < entries_; ++row) {
        const auto& entry = rows_[static_cast<std::size_t>(row)];

        // 阶段：text-primary + w600（Overview.jsx:104 这一列是唯一的正文色重）
        auto* stage = new QTableWidgetItem(
            QStringLiteral("%1 %2").arg(QString::fromStdString(pipeline::StageCode(entry.stage)),
                                        StageName(entry.stage)));
        QFont stage_font = stage->font();
        stage_font.setWeight(QFont::DemiBold);
        stage->setFont(stage_font);
        stage->setForeground(widgets::TokenQColor(theme::Current().textPrimary));
        stage->setData(kSortRole, static_cast<int>(entry.stage));
        stage->setToolTip(QStringLiteral("阶段 %1")
                              .arg(QString::fromStdString(pipeline::StageCode(entry.stage))));
        table_->setItem(row, 0, stage);

        // 输入哈希：.table .num = mono + muted
        auto* hash = new QTableWidgetItem(QString::fromStdString(entry.input_hash));
        QFont mono = hash->font();
        mono.setFamilies({QString::fromStdString(std::string(theme::font::kMonoFamily))});
        mono.setPixelSize(11);
        hash->setFont(mono);
        hash->setForeground(widgets::TokenQColor(theme::Current().textMuted));
        table_->setItem(row, 1, hash);

        // 产物：.mono；单元格只显示文件名，全路径进 tooltip（账本里的落盘路径是真数据）
        const std::filesystem::path out_path{entry.output_path};
        auto* out = new QTableWidgetItem(QString::fromStdString(out_path.filename().string()));
        QFont mono_out = out->font();
        mono_out.setFamilies({QString::fromStdString(std::string(theme::font::kMonoFamily))});
        mono_out.setPixelSize(12);
        out->setFont(mono_out);
        out->setToolTip(QString::fromStdString(entry.output_path));
        table_->setItem(row, 2, out);

        // 降级：无则 dim「—」，有则 Tag warn（Overview.jsx:107）
        if (entry.degradation.empty() || entry.degradation == "-") {
            auto* none = new QTableWidgetItem(QStringLiteral("—"));
            none->setForeground(widgets::TokenQColor(theme::Current().textMuted));
            table_->setItem(row, 3, none);
        } else {
            auto* tag = new widgets::Tag(QString::fromStdString(entry.degradation), "warn", false, table_);
            table_->setCellWidget(row, 3, tag);
        }
    }
    table_->setSortingEnabled(was_sorted);

    // views.css:112：阶段产物 / LLM 调用 / 高档 / 镜头 / 估算成本（真实预算值）
    summary_->setText(QStringLiteral("阶段产物 %1 · LLM 调用 %2 · 高档 %3 · 镜头 %4 · 估算成本 ¥%5")
                          .arg(entries_)
                          .arg(budget_.llm_calls)
                          .arg(budget_.high_quality_calls)
                          .arg(budget_.shots)
                          .arg(budget_.cost, 0, 'f', 2));
    RefreshEmpty();
}

QString LedgerView::Probe() const {
    return QStringLiteral("entries=%1; calls=%2").arg(entries_).arg(calls_);
}

} // namespace shine::app
