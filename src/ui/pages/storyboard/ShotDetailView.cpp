#include "ui/pages/storyboard/ShotDetailView.h"

#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Inputs.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace shine::app {

ShotDetailView::ShotDetailView(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(theme::space::kSteps[1]);
    title_ = widgets::SectionTitle(QStringLiteral("镜头详情 · 未选择"), this);
    outer->addWidget(title_);
    performance_ = new QLabel(this);
    spatial_ = new QLabel(this);
    camera_ = new QLabel(this);
    references_ = new QLabel(this);
    for (QLabel* label : {performance_, spatial_, camera_, references_}) {
        label->setWordWrap(true);
        widgets::SetKind(label, "statedetail");
        outer->addWidget(label);
    }
    auto* beat_title = widgets::SectionTitle(QStringLiteral("Beat[] · 可编辑 JSON"), this);
    outer->addWidget(beat_title);
    timeline_ = new QPlainTextEdit(this);
    timeline_->setPlaceholderText(QStringLiteral("{\"duration_s\":3.0,\"beats\":[...]}"));
    outer->addWidget(timeline_, 1);
    auto* save = new widgets::Button(QStringLiteral("保存 Beat 时间轴"),
                                     widgets::Button::Variant::Primary,
                                     widgets::Button::Size::Sm, this);
    outer->addWidget(save);
    connect(save, &QPushButton::clicked, this, &ShotDetailView::SaveTimeline);
    Rebuild();
}

void ShotDetailView::SetShot(const novelcore::ShotRow& shot) {
    shot_ = shot;
    timeline_json_ = shot.timeline_json.empty() ? "{}" : shot.timeline_json;
    Rebuild();
}

void ShotDetailView::Rebuild() {
    title_->setText(QStringLiteral("镜头 S%1 · %2")
                        .arg(shot_.ord)
                        .arg(QString::fromStdString(shot_.action)));
    performance_->setText(QStringLiteral("表演 12 项：expression=%1；eyes=%2；breathing=%3；posture=%4；body=%5；hand=%6；head=%7；weight=%8；walk=%9；stop=%10；reaction=%11；pause=%12")
                              .arg(QString::fromStdString(shot_.expression),
                                   QStringLiteral("待填"), QStringLiteral("待填"),
                                   QStringLiteral("待填"), QStringLiteral("待填"),
                                   QStringLiteral("待填"), QStringLiteral("待填"),
                                   QStringLiteral("待填"), QStringLiteral("待填"),
                                   QStringLiteral("待填"), QStringLiteral("待填"),
                                   QStringLiteral("待填")));
    spatial_->setText(QStringLiteral("空间：前景/背景/位置/朝向取自 start_state_json 与 reference_json。\n%1")
                          .arg(QString::fromStdString(shot_.start_state_json)));
    camera_->setText(QStringLiteral("机位：camera_id=%1；composition_id=%2；lighting_id=%3；%4")
                         .arg(shot_.camera_id)
                         .arg(shot_.composition_id)
                         .arg(shot_.lighting_id)
                         .arg(QString::fromStdString(shot_.prompt_text)));
    references_->setText(QStringLiteral("引用：%1").arg(QString::fromStdString(shot_.reference_json)));
    timeline_->setPlainText(QString::fromStdString(timeline_json_));
}

void ShotDetailView::SaveTimeline() {
    timeline_json_ = timeline_->toPlainText().toStdString();
    if (on_timeline_) {
        on_timeline_(timeline_json_);
    }
}

QString ShotDetailView::DetailProbe() const {
    return QStringLiteral("shot=%1; performance=12; spatial=1; camera=1; beats=editable; references=1; timeline=%2")
        .arg(shot_.id)
        .arg(timeline_json_ == "{}" ? QStringLiteral("empty") : QStringLiteral("present"));
}

} // namespace shine::app
