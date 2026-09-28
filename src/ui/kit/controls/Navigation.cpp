#include "ui/kit/controls/Navigation.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/motion/Tween.h"
#include "ui/kit/theme/Theme.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitterHandle>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

namespace shine::widgets {

// ====================================================================== Tabs

Tabs::Tabs(const QStringList& items, QWidget* parent) : QFrame(parent) {
    SetKind(this, "tabs");
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(8, 0, 8, 0);
    row_->setSpacing(2);

    int i = 0;
    for (const QString& it : items) {
        auto* b = new QPushButton(it, this);
        SetKind(b, "tab");
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        connect(b, &QPushButton::clicked, this, [this, i] { SetCurrent(i); });
        items_.push_back(b);
        row_->addWidget(b);
        ++i;
    }
    row_->addStretch(1);

    indicator_ = new QFrame(this);
    SetKind(indicator_, "tabindicator");
    indicator_->setFixedHeight(2);
    indicator_->setParent(this);
    indicator_->lower();
    indicator_->show();

    if (!items_.empty()) {
        SetCurrent(0, false);
    }
}

void Tabs::SetCurrent(int index, bool animated) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    current_ = index;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        items_[static_cast<std::size_t>(i)]->setProperty(
            "selected", i == index ? QStringLiteral("true") : QString{});
        Repolish(items_[static_cast<std::size_t>(i)]);
    }
    SlideTo(index, animated);
    if (on_changed_) {
        on_changed_(index);
    }
}

void Tabs::SlideTo(int index, bool animated) {
    QPushButton* b = items_[static_cast<std::size_t>(index)];
    PlaceIndicator(b->x(), b->width(), animated);
}

void Tabs::PlaceIndicator(int x, int w, bool animated) {
    const int y = height() - 2;
    if (!animated || motion::ReduceMotion()) {
        indicator_->setGeometry(x, y, w, 2);
        return;
    }
    // 指示条滑动 motion.base + emphasized（UI.md §2.1）
    auto* t = new motion::Tween{theme::motion::kEmphasized, this};
    const QRect from = indicator_->geometry();
    const QRect to{x, y, w, 2};
    t->Run(from, to, theme::motion::kDurBaseMs,
           [this](const QVariant& v) { indicator_->setGeometry(v.toRect()); });
}

void Tabs::resizeEvent(QResizeEvent* ev) {
    QFrame::resizeEvent(ev);
    if (!items_.empty()) {
        SlideTo(current_, false);
    }
}

void Tabs::showEvent(QShowEvent* ev) {
    QFrame::showEvent(ev);
    if (!items_.empty()) {
        SlideTo(current_, false);
    }
}

// =================================================================== Toolbar

Toolbar::Toolbar(QWidget* parent) : QFrame(parent) {
    SetKind(this, "toolbar");
    setMinimumHeight(40);
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(8, 4, 8, 4);
    row_->setSpacing(6);

    more_ = new QPushButton(QStringLiteral("⋯ 更多"), this);
    SetKind(more_, "button");
    SetVariant(more_, "ghost");
    SetSizeAttr(more_, "sm");
    more_->hide();
    connect(more_, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        for (QWidget* w : overflow_) {
            if (auto* b = qobject_cast<QPushButton*>(w)) {
                menu.addAction(b->text());
            }
        }
        menu.exec(more_->mapToGlobal(QPoint(0, more_->height())));
    });
}

void Toolbar::AddGroup(const std::vector<QWidget*>& items) {
    if (!items_.empty()) {
        auto* sep = new QFrame(this);
        sep->setFixedWidth(1);
        sep->setMinimumHeight(20);
        SetKind(sep, "toolseparator");
        row_->addWidget(sep);
        items_.push_back(sep);
    }
    for (QWidget* w : items) {
        row_->addWidget(w);
        items_.push_back(w);
    }
    row_->addWidget(more_); // 更多按钮恒在末尾
    more_->raise();
}

void Toolbar::AddSpacer() {
    row_->addStretch(1);
    items_.push_back(nullptr); // 占位标记（不参与溢出计算）
}

void Toolbar::resizeEvent(QResizeEvent* ev) {
    QFrame::resizeEvent(ev);
    Reflow();
}

void Toolbar::Reflow() {
    // 溢出折叠：放不下的尾部项收进「更多」
    overflow_.clear();
    const int limit = width() - more_->sizeHint().width() - 24;
    int used = 0;
    bool overflowing = false;
    for (QWidget* w : items_) {
        if (w == nullptr) {
            used = limit; // spacer 之后全部视为溢出候选
            continue;
        }
        used += w->sizeHint().width() + 6;
        const bool hide = used > limit;
        w->setVisible(!hide);
        if (hide) {
            overflowing = true;
            if (qobject_cast<QPushButton*>(w) != nullptr) {
                overflow_.push_back(w);
            }
        }
    }
    more_->setVisible(overflowing);
}

// =================================================================== Splitter

QSplitterHandle* Splitter::createHandle() {
    QSplitterHandle* h = QSplitter::createHandle();
    h->installEventFilter(this); // 双击折叠/展开
    return h;
}

Splitter::Splitter(Qt::Orientation o, QWidget* parent) : QSplitter(o, parent) {
    setHandleWidth(4); // 4px 把手（UI.md §2.1）
    setChildrenCollapsible(true);
    SetKind(this, "splitter");
    setStyleSheet(QStringLiteral("")); // 交由全局 QSS（shineKind 选择器）
}

void Splitter::SetCollapsible(bool on) {
    collapsible_ = on;
    setChildrenCollapsible(on);
}

void Splitter::Collapse(int index, bool collapse) {
    if (index < 0 || index >= count()) {
        return;
    }
    QList<int> sizes;
    for (int i = 0; i < count(); ++i) {
        sizes.push_back(i == index ? (collapse ? 0 : 120) : 120);
    }
    setSizes(sizes);
}

void Splitter::ToggleCollapse(int index) {
    const QList<int> s = sizes();
    if (index < 0 || index >= s.size()) {
        return;
    }
    Collapse(index, s[index] > 0); // 双击把手折叠/展开
}

bool Splitter::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::MouseButtonDblClick && collapsible_) {
        for (int i = 0; i < count() - 1; ++i) { // 把手数 = 子件数 - 1
            if (obj == handle(i)) {
                ToggleCollapse(i);
                return true;
            }
        }
    }
    return QSplitter::eventFilter(obj, ev);
}

} // namespace shine::widgets
