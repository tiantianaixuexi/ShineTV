#include "ui/pages/storyboard/StoryboardTimeline.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "util/Encoding.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QDrag>
#include <QMouseEvent>
#include <QPixmap>
#include <QLabel>
#include <QMimeData>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>

namespace shine::app {
namespace {
constexpr const char* kShotMime = "application/x-shine-shot-ord";
}

StoryboardTimeline::StoryboardTimeline(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);
    auto* head = new QWidget(this);
    auto* head_row = new QHBoxLayout(head);
    head_row->setContentsMargins(0, 0, 0, 0);
    head_row->setSpacing(theme::space::kSteps[1]);
    auto* title = widgets::SectionTitle(QStringLiteral("故事板时间线 · Scene → Sequence → Shot"), head);
    head_row->addWidget(title);
    head_row->addStretch(1);
    auto* hint = new QLabel(QStringLiteral("按住卡片拖动 · 时长条按比例"), head);
    widgets::SetKind(hint, "statemeta");
    head_row->addWidget(hint);
    outer->addWidget(head);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    host_ = new QWidget(scroll);
    row_ = new QHBoxLayout(host_);
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(theme::space::kSteps[2]);
    row_->addStretch(1);
    scroll->setWidget(host_);
    outer->addWidget(scroll, 1);
    // 标题行 ~22 + 间距 4 + 卡片 148 + 滚动条余量 8 ≈ 182
    // 不给下限的话卡片会被压到底部时长行只剩一半（实测截图）。
    setMinimumHeight(186);
    setAcceptDrops(true);
}

void StoryboardTimeline::SetScene(const novelcore::SceneRow& scene,
                                  std::vector<novelcore::ShotRow> shots) {
    scene_ = scene;
    shots_ = std::move(shots);
    Rebuild();
}

void StoryboardTimeline::Rebuild() {
    if (row_ == nullptr) {
        return;
    }
    while (QLayoutItem* item = row_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    for (const auto& shot : shots_) {
        // webui .tl-card：w128 / r10 / fill-muted 底 / 缩略 72px / 正文 p7 9 9
        auto* card = new QFrame(host_);
        widgets::SetKind(card, "tlcard");
        card->setFixedWidth(128);
        // 内容高度下限（设计稿 .tl-card 高度由内容撑开）：
        //   缩略 72 + tinfo(上7 下9) + tcode ~15 + gap2 + taction ~17 + gap2
        //        + tdur(mt6 + ~15) + 上下边框 2 ≈ 147
        // 宿主滚动区在窄窗口下会把卡片压到 sizeHint 以下，底部时长行被裁掉，
        // 这里显式兜底。
        card->setMinimumHeight(148);
        card->setAcceptDrops(true);
        card->setProperty("shotOrd", shot.ord);
        card->installEventFilter(this);
        card->setCursor(Qt::OpenHandCursor);
        auto* body = new QVBoxLayout(card);
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(0);

        auto* thumb = new QLabel(QStringLiteral("▧\n待出图"), card);
        widgets::SetKind(thumb, "tlthumb");
        thumb->setFixedHeight(72);
        thumb->setAlignment(Qt::AlignCenter);
        body->addWidget(thumb);

        auto* info = new QWidget(card);
        auto* il = new QVBoxLayout(info);
        il->setContentsMargins(9, 7, 9, 9);
        il->setSpacing(2);

        auto* code = new QLabel(QStringLiteral("S%1").arg(shot.ord), info);
        widgets::SetKind(code, "tlcode");
        il->addWidget(code);

        auto* action = new widgets::ElidedLabel(QString::fromStdString(shot.action), info);
        action->SetExpandable(false);
        il->addWidget(action);

        // 时长行：细进度条（按秒数 / 6s 归一）+ 时长文字
        auto* dur_row = new QWidget(info);
        auto* dr = new QHBoxLayout(dur_row);
        dr->setContentsMargins(0, 6, 0, 0); // webui .tdur margin-top: 6px
        dr->setSpacing(6);
        const QString duration = shot.duration_note.empty()
                                     ? QStringLiteral("—")
                                     : QString::fromStdString(shot.duration_note);
        auto* bar = new widgets::ProgressBar(dur_row);
        bar->setMinimumWidth(30);
        // webui .tdur 是「纯条 + 时长文字」：<div className="prog thin grow"> 里
        // 只有填充色块，没有 %p 文本。ProgressBar 默认 setTextVisible(true)，
        // 在 6px 高的条里会画出 "11%" 并把行撑高、把卡片底边顶出去（实测截图
        // 里时长条被卡片下沿裁掉一半）。这里显式关掉。
        bar->setTextVisible(false);
        bar->setValue(SecondsOf(shot.duration_note));
        dr->addWidget(bar, 1);
        auto* dur = new QLabel(duration, dur_row);
        widgets::SetKind(dur, "tldur");
        dr->addWidget(dur);
        il->addWidget(dur_row);

        body->addWidget(info);
        row_->addWidget(card);
    }
    row_->addStretch(1);
}

int StoryboardTimeline::SecondsOf(const std::string& note) {
    bool ok = false;
    const double seconds = QString::fromStdString(note).toDouble(&ok);
    if (!ok || seconds <= 0.0) {
        return 0;
    }
    // webui .tl-card .tdur：min(100, (secs / 6) * 100)
    return std::min(100, static_cast<int>(seconds / 6.0 * 100.0 + 0.5));
}

void StoryboardTimeline::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(kShotMime)) {
        event->acceptProposedAction();
    }
}

bool StoryboardTimeline::eventFilter(QObject* watched, QEvent* event) {
    auto* card = qobject_cast<QWidget*>(watched);
    if (card == nullptr) {
        return QWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            drag_start_ = mouse->position().toPoint();
            drag_ord_ = card->property("shotOrd").toInt();
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if ((mouse->buttons() & Qt::LeftButton) &&
            (mouse->position().toPoint() - drag_start_).manhattanLength() > 8) {
            auto* mime = new QMimeData;
            mime->setData(kShotMime, QByteArray::number(drag_ord_));
            auto* drag = new QDrag(card);
            drag->setMimeData(mime);
            drag->setPixmap(card->grab());
            drag->exec(Qt::MoveAction);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void StoryboardTimeline::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat(kShotMime) || !on_reorder_) {
        return;
    }
    bool ok = false;
    const int source = event->mimeData()->data(kShotMime).toInt(&ok);
    const QWidget* target = qobject_cast<QWidget*>(event->source());
    if (!ok || target == nullptr) {
        return;
    }
    const int targetOrd = target->property("shotOrd").toInt();
    std::vector<novelcore::RowId> ids;
    ids.reserve(shots_.size());
    novelcore::RowId sourceId = 0;
    novelcore::RowId targetId = 0;
    for (const auto& shot : shots_) {
        ids.push_back(shot.id);
        if (shot.ord == source) sourceId = shot.id;
        if (shot.ord == targetOrd) targetId = shot.id;
    }
    if (sourceId == 0 || targetId == 0 || sourceId == targetId) {
        return;
    }
    auto sourcePos = std::find(ids.begin(), ids.end(), sourceId);
    ids.erase(sourcePos);
    auto targetPos = std::find(ids.begin(), ids.end(), targetId);
    ids.insert(targetPos, sourceId);
    on_reorder_(ids);
    event->acceptProposedAction();
}

QString StoryboardTimeline::TimelineProbe() const {
    return QStringLiteral("scene=%1; shots=%2; draggable=1; placeholder=1")
        .arg(scene_.id)
        .arg(static_cast<int>(shots_.size()));
}

} // namespace shine::app
