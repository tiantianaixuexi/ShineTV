#include "pages/shell/Breadcrumb.h"

#include "widget/theme/Token.h"
#include "widget/controls/Controls.h"

#include <QHBoxLayout>
#include <QLabel>

namespace shine::app {

Breadcrumb::Breadcrumb(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("breadcrumb"));
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(theme::space::kSteps[3], theme::space::kSteps[1],
                            theme::space::kSteps[3], theme::space::kSteps[1]);
    lay->setSpacing(theme::space::kSteps[1]);
    lay->addStretch();
}

void Breadcrumb::SetPath(const QStringList& crumbs) {
    crumbs_ = crumbs;
    Rebuild();
}

void Breadcrumb::Rebuild() {
    auto* lay = qobject_cast<QHBoxLayout*>(layout());
    // 清掉旧段（保留尾部 stretch）
    while (lay->count() > 1) {
        QLayoutItem* item = lay->takeAt(0);
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (int i = 0; i < crumbs_.size(); ++i) {
        if (i > 0) {
            auto* sep = new QLabel(QStringLiteral("/"), this);
            sep->setObjectName(QStringLiteral("crumbSep"));
            lay->addWidget(sep);
        }
        // 末段是当前位置（不可点），前面的段可点回
        if (i + 1 == crumbs_.size()) {
            auto* cur = new QLabel(crumbs_[i], this);
            shine::widgets::SetSemibold(cur, true);
            lay->addWidget(cur);
        } else {
            auto* btn = new shine::widgets::Button(crumbs_[i],
                                                   shine::widgets::Button::Variant::Ghost,
                                                   shine::widgets::Button::Size::Sm, this);
            connect(btn, &shine::widgets::Button::clicked, this, [this, i] {
                if (on_pick_) {
                    on_pick_(i);
                }
            });
            lay->addWidget(btn);
        }
    }
    lay->addStretch();
}

} // namespace shine::app
