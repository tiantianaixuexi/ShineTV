#include "ui/pages/imageflow/RenderResultView.h"

#include "ui/kit/images/Grid.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Feedback.h"

#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace shine::app {

RenderResultView::RenderResultView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme::space::kSteps[2]);

    thumb_ = new QLabel(this);
    thumb_->setAlignment(Qt::AlignCenter);
    thumb_->setMinimumHeight(180);
    thumb_->setWordWrap(true);
    widgets::SetKind(thumb_, "art");
    layout->addWidget(thumb_);

    facts_ = new QLabel(this);
    facts_->setWordWrap(true);
    widgets::SetKind(facts_, "statedetail");
    layout->addWidget(facts_);

    assets_ = new QLabel(this);
    assets_->setWordWrap(true);
    widgets::SetKind(assets_, "statemeta");
    layout->addWidget(assets_);

    rerun_ = new widgets::Button(QStringLiteral("重跑本镜出图"), widgets::Button::Variant::Secondary,
                                 widgets::Button::Size::Sm, this);
    rerun_->setToolTip(QStringLiteral("把这个镜头重新排进出图队列"));
    layout->addWidget(rerun_);
    layout->addStretch();

    Rebuild();
}

void RenderResultView::SetEntry(const Entry& entry) {
    entry_ = entry;
    Rebuild();
}

void RenderResultView::Rebuild() {
    // 成图：有已解码图就显示；没有就显式空态 —— 不画占位假图。
    if (!entry_.image.isNull()) {
        const QPixmap pix = QPixmap::fromImage(entry_.image).scaled(
            520, 340, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        thumb_->setPixmap(pix);
    } else {
        thumb_->setPixmap(QPixmap());
        thumb_->setText(entry_.code.isEmpty() ? QStringLiteral("未选择镜头")
                                              : QStringLiteral("%1 · %2\n尚未出图")
                                                    .arg(entry_.code, entry_.status));
    }

    facts_->setText(QStringLiteral("镜号 %1\n章节 %2\n场景 %3\n状态 %4\nPrompt %5")
                        .arg(entry_.code, entry_.chapter, entry_.scene, entry_.status, entry_.prompt));

    const bool has_video = !entry_.videoPath.isEmpty();
    assets_->setText(has_video ? QStringLiteral("视频：%1（已出片）").arg(entry_.videoPath)
                               : QStringLiteral("视频：尚未出片（出图工作区不产视频）"));
}

QString RenderResultView::Probe() const {
    return QStringLiteral("code=%1; status=%2; image=%3; video=%4")
        .arg(entry_.code, entry_.status,
             entry_.image.isNull() ? QStringLiteral("none") : QStringLiteral("ready"),
             entry_.videoPath.isEmpty() ? QStringLiteral("none") : QStringLiteral("ready"));
}

} // namespace shine::app
