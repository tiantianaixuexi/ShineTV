#include "ui/pages/videoflow/FilmStrip.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace shine::app {

FilmStrip::FilmStrip(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(theme::space::kSteps[2], theme::space::kSteps[1],
                              theme::space::kSteps[2], theme::space::kSteps[1]);
    outer->setSpacing(theme::space::kSteps[1]);

    auto* head = new QHBoxLayout;
    auto* title = shine::widgets::SectionTitle(QStringLiteral("连播胶片条"), this);
    head->addWidget(title);
    head->addStretch(1);
    summary_ = new QLabel(this);
    shine::widgets::SetKind(summary_, "statesub");
    head->addWidget(summary_);
    outer->addLayout(head);

    row_ = new QHBoxLayout;
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(theme::space::kSteps[1]);
    outer->addLayout(row_);

    Rebuild();
}

void FilmStrip::SetCells(std::vector<Cell> cells) {
    cells_ = std::move(cells);
    Rebuild();
}

void FilmStrip::Rebuild() {
    // 清空旧格：布局项逐个摘掉后销毁（控件本身由 QPushButton 父级链管）
    while (true) {
        QLayoutItem* item = row_->takeAt(0);
        if (item == nullptr) {
            break;
        }
        delete item;
    }
    int ready = 0;
    for (int i = 0; i < static_cast<int>(cells_.size()); ++i) {
        const Cell& cell = cells_[static_cast<std::size_t>(i)];
        ready += cell.ready ? 1 : 0;

        auto* btn = new QPushButton(this);
        btn->setFlat(true);
        btn->setMinimumWidth(112);
        btn->setCursor(cell.ready ? Qt::PointingHandCursor : Qt::ArrowCursor);
        shine::widgets::SetKind(btn, "filmcell");
        auto* lay = new QVBoxLayout(btn);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(2);

        auto* thumb = new QLabel(btn);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setFixedHeight(64);
        if (!cell.thumb.isNull()) {
            thumb->setPixmap(QPixmap::fromImage(cell.thumb).scaled(
                104, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        } else {
            // 未出片：显式空态，不复用别的镜头的图
            thumb->setText(cell.ready ? QStringLiteral("无首帧") : QStringLiteral("待出片"));
            shine::widgets::SetKind(thumb, "statemeta");
        }
        lay->addWidget(thumb);

        auto* meta = new QLabel(
            QStringLiteral("%1 · %2").arg(cell.code, cell.ready ? cell.duration : QStringLiteral("—")),
            btn);
        meta->setAlignment(Qt::AlignCenter);
        shine::widgets::SetKind(meta, cell.ready ? "statemeta" : "statesub");
        lay->addWidget(meta);

        if (cell.ready) {
            connect(btn, &QPushButton::clicked, this, [this, i] {
                if (on_picked_) on_picked_(i);
            });
            btn->setToolTip(QStringLiteral("查看 %1").arg(cell.videoPath));
        } else {
            btn->setEnabled(false);
            btn->setToolTip(QStringLiteral("%1 尚未出片").arg(cell.code));
        }
        row_->addWidget(btn, 0, Qt::AlignTop);
    }
    row_->addStretch(1);
    summary_->setText(QStringLiteral("%1 / %2 个镜头已出片").arg(ready).arg(cells_.size()));
}

int FilmStrip::ReadyCount() const {
    int ready = 0;
    for (const Cell& c : cells_) {
        ready += c.ready ? 1 : 0;
    }
    return ready;
}

QString FilmStrip::Probe() const {
    return QStringLiteral("cells=%1; ready=%2").arg(cells_.size()).arg(ReadyCount());
}

} // namespace shine::app
