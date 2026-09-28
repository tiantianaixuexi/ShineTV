#include "ui/pages/videoflow/VideoTaskView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"

#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shine::app {

VideoTaskView::VideoTaskView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("视频任务 · ShotVideo(20)"), this);
    layout->addWidget(title);
    auto* enqueue = new widgets::Button(QStringLiteral("全部入队"), widgets::Button::Variant::Primary,
                                       widgets::Button::Size::Sm, this);
    auto* run = new widgets::Button(QStringLiteral("演示运行"), widgets::Button::Variant::Secondary,
                                    widgets::Button::Size::Sm, this);
    layout->addWidget(enqueue);
    layout->addWidget(run);
    summary_ = new QLabel(QStringLiteral("没有待出片的镜头"), this);
    widgets::SetKind(summary_, "statedetail");
    layout->addWidget(summary_);
    table_ = new QTableWidget(this);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("镜头"), QStringLiteral("首帧"),
                                       QStringLiteral("状态"), QStringLiteral("进度"), QStringLiteral("产物")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);
    connect(enqueue, &QPushButton::clicked, this, &VideoTaskView::EnqueueAll);
    connect(run, &QPushButton::clicked, this, &VideoTaskView::ShowRunning);
}

void VideoTaskView::SetShots(std::vector<std::pair<std::int64_t, QString>> shots) {
    shots_ = std::move(shots);
    queue_.Clear();
    Rebuild();
}

void VideoTaskView::SetFirstFrame(std::int64_t shot_id, const std::filesystem::path& path) {
    frames_.emplace_back(shot_id, path);
    Rebuild();
}

void VideoTaskView::EnqueueAll() {
    queue_.Clear();
    for (const auto& [id, label] : shots_) {
        queue_.Add(id, label.toStdString(), "H3 视频", flow::BatchPriority::ShotVideo);
    }
    Rebuild();
}

void VideoTaskView::ShowRunning() {
    if (queue_.Snapshot().empty()) EnqueueAll();
    if (queue_.StartNext()) Rebuild();
}

void VideoTaskView::Rebuild() {
    const auto jobs = queue_.Snapshot();
    table_->setRowCount(static_cast<int>(jobs.size()));
    for (int row = 0; row < static_cast<int>(jobs.size()); ++row) {
        const auto& job = jobs[static_cast<std::size_t>(row)];
        QString thumb = QStringLiteral("未加载");
        for (const auto& [id, path] : frames_) {
            if (id != job.shot_id) continue;
            QImage image(QString::fromStdString(path.string()));
            thumb = image.isNull() ? QStringLiteral("首帧缺失") : QStringLiteral("QImage %1×%2").arg(image.width()).arg(image.height());
        }
        const QString state = job.state == flow::BatchState::Pending ? QStringLiteral("排队")
            : job.state == flow::BatchState::Running ? QStringLiteral("运行中") : QStringLiteral("完成");
        const std::array<QString, 6> values{QString::number(job.id), QString::fromStdString(job.label), thumb,
                                           state, QStringLiteral("%1%").arg(job.progress), QString::number(job.artifacts)};
        for (int col = 0; col < values.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(values[static_cast<std::size_t>(col)]));
    }
    summary_->setText(QString::fromStdString(queue_.Describe()));
}

QString VideoTaskView::Probe() const {
    return QStringLiteral("jobs=%1; frames=%2; %3")
        .arg(queue_.Snapshot().size())
        .arg(frames_.size())
        .arg(QString::fromStdString(queue_.Describe()));
}

} // namespace shine::app
