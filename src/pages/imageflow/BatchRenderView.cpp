#include "pages/imageflow/BatchRenderView.h"

#include "widget/theme/Theme.h"
#include "widget/controls/Controls.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <algorithm>
#include <array>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shine::app {

BatchRenderView::BatchRenderView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = new QLabel(QStringLiteral("批量出图 · 队列与降级账"), this);
    widgets::SetKind(title, "statetitle");
    widgets::SetSemibold(title, true);
    layout->addWidget(title);
    auto* bar = new QHBoxLayout;
    auto* enqueue = new widgets::Button(QStringLiteral("全部入队"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, this);
    auto* mock = new widgets::Button(QStringLiteral("演示运行"), widgets::Button::Variant::Secondary,
                                     widgets::Button::Size::Sm, this);
    auto* cancel = new widgets::Button(QStringLiteral("中断 / 清队列"), widgets::Button::Variant::Danger,
                                       widgets::Button::Size::Sm, this);
    bar->addWidget(enqueue);
    bar->addWidget(mock);
    bar->addWidget(cancel);
    layout->addLayout(bar);
    summary_ = new QLabel(QStringLiteral("没有待出图的镜头"), this);
    widgets::SetKind(summary_, "statedetail");
    layout->addWidget(summary_);
    table_ = new QTableWidget(this);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("镜头"), QStringLiteral("流程"),
                                       QStringLiteral("状态"), QStringLiteral("进度"), QStringLiteral("产物"),
                                       QStringLiteral("错误 / 降级")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);
    connect(enqueue, &QPushButton::clicked, this, &BatchRenderView::EnqueueAll);
    connect(mock, &QPushButton::clicked, this, &BatchRenderView::RunMockBatch);
    connect(cancel, &QPushButton::clicked, this, &BatchRenderView::CancelAll);
}

void BatchRenderView::SetShots(std::vector<std::pair<std::int64_t, QString>> shots) {
    shots_ = std::move(shots);
    queue_.Clear();
    Rebuild();
}

void BatchRenderView::EnqueueAll() {
    queue_.Clear();
    for (std::size_t i = 0; i < shots_.size(); ++i) {
        queue_.Add(shots_[i].first, shots_[i].second.toStdString(), "分镜图_v3",
                   flow::BatchPriority::SceneImage);
    }
    Rebuild();
}

void BatchRenderView::ShowRunningDemo() {
    if (queue_.Snapshot().empty()) EnqueueAll();
    for (const auto& job : queue_.Snapshot()) {
        if (job.state == flow::BatchState::Pending) {
            queue_.StartNext();
            break;
        }
    }
    Rebuild();
}

void BatchRenderView::RunMockBatch() {
    if (queue_.Snapshot().empty()) EnqueueAll();
    // Snapshot is a value; drive the actual queue deterministically for offline review.
    while (queue_.StartNext()) {
        const auto jobs = queue_.Snapshot();
        const auto running = std::find_if(jobs.begin(), jobs.end(), [](const flow::BatchJob& j) {
            return j.state == flow::BatchState::Running;
        });
        if (running == jobs.end()) break;
        const bool degraded = running->id % 3 == 0;
        queue_.Complete(running->id, degraded ? 1 : 2,
                        degraded ? "no_reference：缺少参考图，已走文生图降级" : "");
    }
    Rebuild();
}

void BatchRenderView::CancelAll() {
    queue_.CancelAll();
    Rebuild();
}

void BatchRenderView::CompleteMock() { RunMockBatch(); }

void BatchRenderView::Rebuild() {
    const auto jobs = queue_.Snapshot();
    table_->setRowCount(static_cast<int>(jobs.size()));
    for (int row = 0; row < static_cast<int>(jobs.size()); ++row) {
        const auto& job = jobs[static_cast<std::size_t>(row)];
        const std::array<QString, 7> values{
            QString::number(job.id), QString::fromStdString(job.label),
            QString::fromStdString(job.flow_name),
            job.state == flow::BatchState::Pending ? QStringLiteral("排队")
                : job.state == flow::BatchState::Running ? QStringLiteral("运行中")
                : job.state == flow::BatchState::Done ? QStringLiteral("完成")
                : job.state == flow::BatchState::Degraded ? QStringLiteral("降级")
                : job.state == flow::BatchState::Failed ? QStringLiteral("失败")
                : QStringLiteral("已中断"),
            QStringLiteral("%1%").arg(job.progress), QString::number(job.artifacts),
            QString::fromStdString(job.error.empty() ? job.degradation : job.error)};
        for (int col = 0; col < values.size(); ++col) {
            table_->setItem(row, col, new QTableWidgetItem(values[static_cast<std::size_t>(col)]));
        }
    }
    summary_->setText(QString::fromStdString(queue_.Describe()) +
                      (queue_.DegradationLedger().empty() ? QStringLiteral(" · 无降级")
                                                          : QStringLiteral(" · 降级账 %1 条")
                                                                .arg(queue_.DegradationLedger().size())));
}

QString BatchRenderView::Probe() const {
    return QStringLiteral("jobs=%1; pending=%2; ledger=%3; %4")
        .arg(queue_.Snapshot().size())
        .arg(queue_.PendingCount())
        .arg(queue_.DegradationLedger().size())
        .arg(QString::fromStdString(queue_.Describe()));
}

} // namespace shine::app
