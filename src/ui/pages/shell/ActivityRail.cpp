#include "ui/pages/shell/ActivityRail.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/WidgetCommon.h"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVariant>
#include <QVBoxLayout>

namespace shine::app {

namespace {
// 六个入口（UI.md §2.3）：图标为字符图标（IconButton 约定，P03 不引外部图标资源）
struct RailEntry {
    const char* icon;
    const char* tooltip;
};
inline constexpr RailEntry kEntries[] = {
    {"⌂", "总控 —— 全流程总控台"},   {"📖", "小说 —— 章节与设定"},
    {"🎭", "资产 —— 视觉资产库"},   {"🎬", "分镜 —— 故事板与镜头表"},
    {"🖼", "出图 —— ComfyUI 出图"}, {"🎞", "出片 —— 视频与成片"},
};
inline constexpr double kIndicatorW = 2.5; // UI.md §2.3：2.5px 指示条
} // namespace

ActivityRail::ActivityRail(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("activityRail"));
    setFixedWidth(56);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, theme::space::kSteps[4], 0, theme::space::kSteps[4]);
    lay->setSpacing(theme::space::kSteps[3]);
    for (const RailEntry& e : kEntries) {
        auto* btn = new shine::widgets::IconButton(QString::fromUtf8(e.icon),
                                                   QString::fromUtf8(e.tooltip),
                                                   shine::widgets::IconButton::Size::Md, this);
        btn->setFixedSize(40, 40);
        const int index = static_cast<int>(items_.size());
        connect(btn, &shine::widgets::IconButton::clicked, this, [this, index] { Select(index, true); });
        items_.push_back(btn);
        lay->addWidget(btn, 0, Qt::AlignHCenter);
    }
    lay->addStretch();
    items_[current_]->SetActive(true);
}

void ActivityRail::SetCurrent(int index, bool animated) {
    if (index < 0 || index >= static_cast<int>(items_.size()) || index == current_) {
        return;
    }
    Select(index, animated);
}

void ActivityRail::Select(int index, bool animated) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    items_[current_]->SetActive(false);
    current_ = index;
    items_[current_]->SetActive(true);

    const double targetY = items_[current_]->y() + items_[current_]->height() / 2.0 - 11.0;
    if (tween_ == nullptr) {
        tween_ = new shine::motion::Tween(theme::motion::kStandard, this);
    }
    const double from = indicator_y_;
    tween_->Run(from, targetY, theme::motion::kDurBaseMs,
                [this](const QVariant& v) {
                    indicator_y_ = v.toDouble();
                    update();
                });
    if (!animated || shine::motion::ReduceMotion()) {
        tween_->Settle();
    }
    if (on_changed_) {
        on_changed_(current_);
    }
}

void ActivityRail::paintEvent(QPaintEvent* ev) {
    QFrame::paintEvent(ev);
    QPainter p(this);
    // 指示条：2.5px × 22px，accent.primary（Token 取色，零字面色）
    p.fillRect(QRectF(0.0, indicator_y_, kIndicatorW, 22.0),
                shine::widgets::TokenQColor(theme::Current().accentPrimary));
}

void ActivityRail::resizeEvent(QResizeEvent* ev) {
    QFrame::resizeEvent(ev);
    if (indicator_y_ == 0.0 && !items_.empty()) {
        indicator_y_ = items_[current_]->y() + items_[current_]->height() / 2.0 - 11.0;
    }
}

} // namespace shine::app
