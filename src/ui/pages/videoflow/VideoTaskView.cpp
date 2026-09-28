#include "ui/pages/videoflow/VideoTaskView.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <array>

namespace shine::app {
namespace {

// —— 页面专属 QSS ——
// 只挂在本页根控件（objectName=taskView）上，与 kit 全局 QSS 隔离。
// 逐条对应 webui views.css:290–320 与 VideoFlow.jsx TaskList：
//   .drow   p8 2 + 底部发丝线 + hover fill.hover
//   镜号    mono 11px accent w700
//   首帧态  tiny dim（就绪 / 缺失）
[[nodiscard]] QString PageQss() {
    const theme::ColorToken& t = theme::Current();
    return QStringLiteral(
               "QWidget#taskView *[shineKind=\"drow\"]:hover { background-color: %1; }\n"
               "QWidget#taskView QLabel[rowRole=\"code\"] { color: %2; font-size: 11px; font-weight: 700; }\n"
               "QWidget#taskView QLabel[rowRole=\"dim\"] { color: %3; font-size: 11px; }\n"
               "QWidget#taskView QLabel[rowRole=\"pct\"] { color: %3; font-size: 11px; }\n")
        .arg(shine::widget::CssRgb(t.fillHover), shine::widget::CssRgb(t.accentPrimary),
             shine::widget::CssRgb(t.textMuted));
}

// 换肤后重挂页面 QSS：ThemeService 是纯静态类，靠 qApp 发的 ThemeChange 事件感知。
class PageStyleRefresher : public QObject {
  public:
    explicit PageStyleRefresher(QWidget* page, QObject* parent) : QObject(parent), page_(page) {
        qApp->installEventFilter(this);
    }
    ~PageStyleRefresher() override { qApp->removeEventFilter(this); }

  protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (ev->type() == QEvent::ThemeChange && watched == qApp) page_->setStyleSheet(PageQss());
        return QObject::eventFilter(watched, ev);
    }

  private:
    QWidget* page_;
};

} // namespace

VideoTaskView::VideoTaskView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("taskView"));
    setStyleSheet(PageQss());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[1]);

    // webui TaskList：按钮行（全部入队 / 停止）在列表之上
    auto* action_row = new QWidget(this);
    auto* actions = new QHBoxLayout(action_row);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(theme::space::kSteps[1]);
    auto* enqueue = new widgets::Button(QStringLiteral("全部入队"), widgets::Button::Variant::Primary,
                                        widgets::Button::Size::Sm, this);
    auto* run = new widgets::Button(QStringLiteral("演示运行"), widgets::Button::Variant::Secondary,
                                    widgets::Button::Size::Sm, this);
    actions->addWidget(enqueue);
    actions->addWidget(run);
    actions->addStretch(1);
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

    summary_ = new QLabel(QStringLiteral("没有待出片的镜头"), this);
    widgets::SetKind(summary_, "statemeta");
    layout->addWidget(summary_);
    connect(enqueue, &QPushButton::clicked, this, &VideoTaskView::EnqueueAll);
    connect(run, &QPushButton::clicked, this, &VideoTaskView::ShowRunning);
    new PageStyleRefresher(this, this);
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
    while (list_lay_->count() > 1) {
        QLayoutItem* item = list_lay_->takeAt(0);
        if (item == nullptr) break;
        if (QWidget* row = item->widget()) row->deleteLater();
        delete item;
    }
    const auto jobs = queue_.Snapshot();
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const auto& job = jobs[i];
        const bool last = i + 1 == jobs.size();
        const char* tone = "idle";
        QString text = QStringLiteral("排队");
        if (job.state == flow::BatchState::Running) {
            tone = "busy";
            text = QStringLiteral("运行中");
        } else if (job.state == flow::BatchState::Done) {
            tone = "ok";
            text = QStringLiteral("完成");
        } else if (job.state == flow::BatchState::Failed) {
            tone = "danger";
            text = QStringLiteral("失败");
        } else if (job.state == flow::BatchState::Degraded) {
            tone = "warn";
            text = QStringLiteral("降级");
        } else if (job.state == flow::BatchState::Cancelled) {
            tone = "idle";
            text = QStringLiteral("已中断");
        }

        // 首帧来源：只记路径与存在性，**不在 UI 线程读盘解码**
        QString first = QStringLiteral("首帧缺失");
        QString first_path;
        for (const auto& [id, path] : frames_) {
            if (id != job.shot_id) continue;
            first = QStringLiteral("首帧就绪");
            first_path = QString::fromStdString(path.string());
        }

        auto* row = new QWidget(list_);
        widgets::SetKind(row, "drow");
        if (last) row->setProperty("shineKind", QString{});
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(2, theme::space::kSteps[1], 2, theme::space::kSteps[1]);
        lay->setSpacing(theme::space::kSteps[2]);

        auto* code = new QLabel(QString::fromStdString(job.label), row);
        code->setProperty("rowRole", QStringLiteral("code"));
        code->setFixedWidth(30);
        auto* frame = new QLabel(first, row);
        frame->setProperty("rowRole", QStringLiteral("dim"));
        frame->setFixedWidth(62);
        auto* prog = new widgets::ProgressBar(row);
        prog->setTextVisible(false);
        prog->setValue(job.progress);
        prog->SetState(job.state == flow::BatchState::Failed ? "error"
                          : job.state == flow::BatchState::Degraded ? "idle" : "");
        auto* pct = new QLabel(QStringLiteral("%1%").arg(job.progress), row);
        pct->setProperty("rowRole", QStringLiteral("pct"));
        pct->setFixedWidth(32);
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* tag = new widgets::Tag(text, tone, false, row);
        row->setToolTip(first_path.isEmpty() ? QStringLiteral("首帧缺失：%1").arg(QString::fromStdString(job.label))
                                             : QStringLiteral("首帧：%1").arg(first_path));
        lay->addWidget(code, 0);
        lay->addWidget(frame, 0);
        lay->addWidget(prog, 1);
        lay->addWidget(pct, 0);
        lay->addWidget(tag, 0);
        list_lay_->insertWidget(list_lay_->count() - 1, row);
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
