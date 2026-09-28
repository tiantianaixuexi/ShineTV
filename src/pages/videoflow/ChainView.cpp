#include "pages/videoflow/ChainView.h"

#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shine::app {

ChainView::ChainView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = new QLabel(QStringLiteral("首尾帧链式 · ChainView"), this);
    widgets::SetKind(title, "statetitle");
    widgets::SetSemibold(title, true);
    layout->addWidget(title);
    status_ = new QLabel(QStringLiteral("暂无链式关系"), this);
    widgets::SetKind(status_, "statedetail");
    layout->addWidget(status_);
    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("上一镜"), QStringLiteral("下一镜"),
                                       QStringLiteral("状态"), QStringLiteral("策略"), QStringLiteral("处理建议")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);
}

void ChainView::SetChain(flow::VideoChain chain) {
    chain_ = std::move(chain);
    Rebuild();
}

void ChainView::Rebuild() {
    table_->setRowCount(static_cast<int>(chain_.links.size()));
    for (int row = 0; row < static_cast<int>(chain_.links.size()); ++row) {
        const auto& link = chain_.links[static_cast<std::size_t>(row)];
        const std::array<QString, 5> values{QString::number(link.from_shot), QString::number(link.to_shot),
                                           link.connected ? QStringLiteral("✔ 已连接") : QStringLiteral("⚠ 断链"),
                                           QString::fromStdString(link.strategy),
                                           QString::fromStdString(link.detail)};
        for (int col = 0; col < values.size(); ++col) {
            table_->setItem(row, col, new QTableWidgetItem(values[static_cast<std::size_t>(col)]));
        }
    }
    status_->setText(QString::fromStdString(chain_.Describe()));
}

QString ChainView::Probe() const {
    return QString::fromStdString(chain_.Describe());
}

} // namespace shine::app
