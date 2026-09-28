#include "ui/pages/shell/Breadcrumb.h"

#include "ui/kit/theme/Token.h"
#include "ui/kit/controls/Controls.h"

#include <QHBoxLayout>
#include <QLabel>

namespace shine::app {

Breadcrumb::Breadcrumb(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("breadcrumb"));
    setFixedHeight(34); // webui shell.css .crumbs：h34
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 0, 16, 0); // webui .crumbs：padding 0 16
    lay->setSpacing(7);
}

void Breadcrumb::SetPath(const QStringList& crumbs) {
    crumbs_ = crumbs;
    Rebuild();
}

void Breadcrumb::Rebuild() {
    auto* lay = qobject_cast<QHBoxLayout*>(layout());
    // 清掉全部旧段（构造期与历次 Rebuild 都可能留下 stretch，必须一并清掉，
    // 否则段落在两个 stretch 之间被推到中间，看起来像"面包屑浮在顶栏中央"）。
    while (lay->count() > 0) {
        QLayoutItem* item = lay->takeAt(0);
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (int i = 0; i < crumbs_.size(); ++i) {
        if (i > 0) {
            auto* sep = new QLabel(QStringLiteral("›"), this);
            sep->setObjectName(QStringLiteral("crumbSep"));
            lay->addWidget(sep);
        }
        // 末段是当前位置（不可点），前面的段可点回
        if (i + 1 == crumbs_.size()) {
            auto* cur = new QLabel(crumbs_[i], this);
            cur->setObjectName(QStringLiteral("crumbHere")); // 当前段：text-primary + w600
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
