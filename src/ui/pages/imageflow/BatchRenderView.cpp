#include "ui/pages/imageflow/BatchRenderView.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include "ui/layout/QtLayout.h"

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在本页根控件（objectName=batchView）上，与 kit 全局 QSS 隔离。
// 逐条对应 webui views.css:290–320 的 .dlist 段：
//   .drow  p8 2 + 底部发丝线 + hover fill.hover
//   .q-prog w90（进度条列宽由布局给，样式走 kit 的 progressbar）
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#batchView *[shineKind=\"drow\"]:hover { background-color: %1; }\n"
               "QWidget#batchView QLabel[rowRole=\"idx\"] { color: %2; font-size: 11px; }\n"
               "QWidget#batchView QLabel[rowRole=\"code\"] { color: %3; font-size: 11px; font-weight: 700; }\n"
               "QWidget#batchView QLabel[rowRole=\"dim\"] { color: %4; font-size: 11px; }\n"
               "QWidget#batchView QLabel[rowRole=\"pct\"] { color: %4; font-size: 11px; }\n")
        .arg(shine::widget::CssRgb(t.fillHover), shine::widget::CssRgb(t.textMuted),
             shine::widget::CssRgb(t.accentPrimary), shine::widget::CssRgb(t.textMuted));
}


} // namespace

BatchRenderView::BatchRenderView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("batchView"));
    setStyleSheet(PageQss());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("批量出图 · 队列与降级账"), this);
    layout->addWidget(title);
    // webui BatchList：按钮行在列表之上，未连接时补一条 warn 提示
    auto* action_row = new QWidget(this);
    auto* bar = new QHBoxLayout(action_row);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(theme::space::kSteps[1]);
    auto* enqueue = new widgets::Button(QStringLiteral("全部入队"), widgets::Button::Variant::Primary,
                                        widgets::Button::Size::Sm, this);
    auto* mock = new widgets::Button(QStringLiteral("演示运行"), widgets::Button::Variant::Secondary,
                                     widgets::Button::Size::Sm, this);
    auto* cancel = new widgets::Button(QStringLiteral("中断 / 清队列"), widgets::Button::Variant::Danger,
                                       widgets::Button::Size::Sm, this);
    bar->addWidget(enqueue);
    bar->addWidget(mock);
    bar->addWidget(cancel);
    bar->addStretch(1);
    layout->addWidget(action_row);

    // 密集行列表（.dlist）
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_ = new QWidget(scroll);
    list_lay_ = new QVBoxLayout(list_);
    list_lay_->setContentsMargins(0, 0, 0, 0);
    list_lay_->setSpacing(0);
    list_lay_->addStretch(1);
    scroll->setWidget(list_);
    layout->addWidget(scroll, 1);

    summary_ = new QLabel(QStringLiteral("没有待出图的镜头"), this);
    widgets::SetKind(summary_, "statemeta");
    layout->addWidget(summary_);
    connect(enqueue, &QPushButton::clicked, this, &BatchRenderView::EnqueueAll);
    connect(mock, &QPushButton::clicked, this, &BatchRenderView::RunMockBatch);
    connect(cancel, &QPushButton::clicked, this, &BatchRenderView::CancelAll);
    widgets::RefreshOnThemeChange(this, [this] { setStyleSheet(PageQss()); });
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
    util::ClearLayout(list_lay_, 1); // 末尾常驻 addStretch(1)，保留
    const auto jobs = queue_.Snapshot();
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const auto& job = jobs[i];
        const bool last = i + 1 == jobs.size();
        // 状态四态：排队 idle / 运行中 busy / 完成 ok / 降级 warn / 失败 danger / 已中断 idle
        const char* tone = "idle";
        QString text = QStringLiteral("排队");
        if (job.state == flow::BatchState::Running) {
            tone = "busy";
            text = QStringLiteral("运行中");
        } else if (job.state == flow::BatchState::Done) {
            tone = "ok";
            text = QStringLiteral("完成");
        } else if (job.state == flow::BatchState::Degraded) {
            tone = "warn";
            text = QStringLiteral("降级");
        } else if (job.state == flow::BatchState::Failed) {
            tone = "danger";
            text = QStringLiteral("失败");
        } else if (job.state == flow::BatchState::Cancelled) {
            tone = "idle";
            text = QStringLiteral("已中断");
        }

        auto* row = new QWidget(list_);
        widgets::SetKind(row, "drow");
        if (last) row->setProperty("shineKind", QString{});
        auto* col = new QVBoxLayout(row);
        col->setContentsMargins(2, theme::space::kSteps[1], 2, theme::space::kSteps[1]);
        col->setSpacing(3);

        auto* line = new QHBoxLayout;
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(theme::space::kSteps[2]);
        auto* idx = new QLabel(QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), row);
        idx->setProperty("rowRole", QStringLiteral("idx"));
        idx->setFixedWidth(18);
        auto* code = new QLabel(QString::fromStdString(job.label), row);
        code->setProperty("rowRole", QStringLiteral("code"));
        code->setFixedWidth(46);
        auto* prog = new widgets::ProgressBar(row);
        // webui .q-prog w90：队列行里的进度条是固定 90px 的细条
        prog->setFixedWidth(90);
        prog->setTextVisible(false);
        prog->setValue(job.progress);
        prog->SetState(job.state == flow::BatchState::Failed ? "error"
                          : job.state == flow::BatchState::Degraded ? "idle" : "");
        auto* pct = new QLabel(QStringLiteral("%1%").arg(job.progress), row);
        pct->setProperty("rowRole", QStringLiteral("pct"));
        pct->setFixedWidth(32);
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* tag = new widgets::Tag(text, tone, false, row);
        line->addWidget(idx, 0);
        line->addWidget(code, 0);
        line->addWidget(prog, 1);
        line->addWidget(pct, 0);
        line->addWidget(tag, 0);
        col->addLayout(line);

        // 降级 / 错误说明作为 dsub 单行省略（webui BatchList 末行的 tiny dim 说明）
        const QString note = job.error.empty()
                                 ? (job.degradation.empty()
                                        ? QStringLiteral("%1 · 产物 %2").arg(QString::fromStdString(job.flow_name))
                                                                          .arg(job.artifacts)
                                        : QString::fromStdString(job.degradation))
                                 : QString::fromStdString(job.error);
        if (!note.isEmpty()) {
            auto* sub = new widgets::ElidedLabel(note, row);
            sub->setProperty("rowRole", QStringLiteral("dim"));
            sub->SetExpandable(false);
            col->addWidget(sub);
        }
        list_lay_->insertWidget(list_lay_->count() - 1, row);
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
