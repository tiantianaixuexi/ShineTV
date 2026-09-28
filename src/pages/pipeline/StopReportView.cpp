#include "pages/pipeline/StopReportView.h"

#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"

#include <QLabel>
#include <QVBoxLayout>

namespace shine::app {

StopReportView::StopReportView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = new QLabel(QStringLiteral("停止报告 · stop_report.md"), this);
    widgets::SetKind(title, "statetitle");
    widgets::SetSemibold(title, true);
    layout->addWidget(title);
    status_ = new QLabel(QStringLiteral("当前未触发停止条件"), this);
    status_->setWordWrap(true);
    widgets::SetKind(status_, "statedetail");
    layout->addWidget(status_);
}

void StopReportView::SetDecision(pipeline::StopDecision decision) {
    decision_ = std::move(decision);
    status_->setText(decision_.stop ? QStringLiteral("%1：%2").arg(QString::fromStdString(decision_.rule),
                                                                    QString::fromStdString(decision_.reason))
                                   : QStringLiteral("当前未触发停止条件"));
}

QString StopReportView::Probe() const {
    return QStringLiteral("stop=%1; rule=%2; reason=%3")
        .arg(decision_.stop ? QStringLiteral("1") : QStringLiteral("0"))
        .arg(QString::fromStdString(decision_.rule))
        .arg(QString::fromStdString(decision_.reason));
}

} // namespace shine::app
