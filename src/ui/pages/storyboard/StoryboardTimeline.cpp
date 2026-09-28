#include "ui/pages/storyboard/StoryboardTimeline.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"
#include "util/Encoding.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QDrag>
#include <QMouseEvent>
#include <QPixmap>
#include <QLabel>
#include <QMimeData>
#include <QScrollArea>
#include <QScrollBar>
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
    auto* title = widgets::SectionTitle(QStringLiteral("故事板时间线 · Scene → Sequence → Shot"), this);
    outer->addWidget(title);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    host_ = new QWidget(scroll);
    row_ = new QHBoxLayout(host_);
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(theme::space::kSteps[2]);
    row_->addStretch(1);
    scroll->setWidget(host_);
    outer->addWidget(scroll);
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
        auto* card = new widgets::Card(widgets::Card::Variant::Outlined, host_);
        card->setFixedWidth(150);
        card->setAcceptDrops(true);
        card->setProperty("shotOrd", shot.ord);
        card->installEventFilter(this);
        card->setCursor(Qt::OpenHandCursor);
        auto* body = card->BodyLayout();
        auto* ord = widgets::SectionTitle(QStringLiteral("S%1").arg(shot.ord), card);
        body->addWidget(ord);
        auto* placeholder = new QLabel(QStringLiteral("▧\n待出图"), card);
        placeholder->setFixedHeight(88);
        placeholder->setAlignment(Qt::AlignCenter);
        widgets::SetKind(placeholder, "field");
        body->addWidget(placeholder);
        const QString duration = shot.duration_note.empty()
                                     ? QStringLiteral("时长未定义")
                                     : QString::fromStdString(shot.duration_note);
        auto* meta = new QLabel(QStringLiteral("%1\n%2").arg(duration,
                                       QString::fromStdString(shot.mood)), card);
        meta->setWordWrap(true);
        widgets::SetKind(meta, "statedetail");
        body->addWidget(meta);
        row_->addWidget(card);
    }
    row_->addStretch(1);
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
