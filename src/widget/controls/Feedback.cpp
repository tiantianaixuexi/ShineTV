#include "widget/controls/Feedback.h"

#include "widget/theme/Theme.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace shine::widgets {

// =============================================================== ProgressBar

ProgressBar::ProgressBar(QWidget* parent) : QProgressBar(parent) {
    SetKind(this, "progressbar");
    setRange(0, 100);
    setValue(0);
    setTextVisible(true);
    setMinimumHeight(8);
}

void ProgressBar::SetIndeterminate(bool on) {
    indeterminate_ = on;
    if (on) {
        setRange(0, 0); // Qt 忙碌模式（无数字跳动）
    } else {
        setRange(0, 100);
        setValue(value() == 0 ? 0 : value());
    }
}

void ProgressBar::SetInlineText(const QString& t) {
    setFormat(t.isEmpty() ? QStringLiteral("%p%") : t); // 内嵌文字
}

void ProgressBar::SetState(const char* state) {
    setProperty("state", QString::fromLatin1(state));
    Repolish(this);
}

// ================================================================ EmptyState

EmptyState::EmptyState(const QString& icon, const QString& title, const QString& subtitle,
                       const QString& actionText, QWidget* parent)
    : QFrame(parent) {
    SetKind(this, "emptystate");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(32, 24, 32, 24);
    col->setSpacing(8);
    col->setAlignment(Qt::AlignCenter);

    auto* ic = new QLabel(icon, this);
    SetKind(ic, "stateicon");
    ic->setAlignment(Qt::AlignCenter);
    col->addWidget(ic, 0, Qt::AlignCenter);

    auto* t = new QLabel(title, this);
    SetKind(t, "statetitle");
    SetSemibold(t, true);
    t->setAlignment(Qt::AlignCenter);
    col->addWidget(t, 0, Qt::AlignCenter);

    auto* s = new QLabel(subtitle, this);
    SetKind(s, "statesub");
    s->setAlignment(Qt::AlignCenter);
    s->setWordWrap(true);
    col->addWidget(s, 0, Qt::AlignCenter);

    // 主行动按钮：必须给出下一步（UI.md §2.1）
    auto* action = new Button(actionText.isEmpty() ? QStringLiteral("开始") : actionText,
                              Button::Variant::Primary, Button::Size::Md, this);
    connect(action, &Button::clicked, this, [this] {
        if (on_action_) {
            on_action_();
        }
    });
    col->addSpacing(4);
    col->addWidget(action, 0, Qt::AlignCenter);
}

// ================================================================ ErrorState

ErrorState::ErrorState(const QString& title, const QString& detail, QWidget* parent) : QFrame(parent) {
    SetKind(this, "errorstate");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(32, 24, 32, 24);
    col->setSpacing(8);
    col->setAlignment(Qt::AlignCenter);

    auto* ic = new QLabel(QStringLiteral("⚠"), this);
    SetKind(ic, "stateicon");
    ic->setAlignment(Qt::AlignCenter);
    col->addWidget(ic, 0, Qt::AlignCenter);

    auto* t = new QLabel(title, this);
    SetKind(t, "statetitle");
    SetSemibold(t, true);
    t->setAlignment(Qt::AlignCenter);
    col->addWidget(t, 0, Qt::AlignCenter);

    auto* detailBox = new QLabel(detail, this);
    SetKind(detailBox, "statedetail");
    detailBox->setWordWrap(true);
    detailBox->setTextFormat(Qt::PlainText); // Error.detail 明文展示
    detailBox->hide();                       // 默认折叠
    col->addWidget(detailBox);

    auto* row = new QHBoxLayout();
    row->setSpacing(8);
    row->addStretch(1);
    auto* retry = new Button(QStringLiteral("重试"), Button::Variant::Primary, Button::Size::Md, this);
    connect(retry, &Button::clicked, this, [this] {
        if (on_retry_) {
            on_retry_();
        }
    });
    row->addWidget(retry);
    auto* more = new Button(QStringLiteral("查看详情"), Button::Variant::Ghost, Button::Size::Md, this);
    more->setCheckable(true);
    connect(more, &Button::clicked, this, [detailBox, more] {
        detailBox->setVisible(more->isChecked()); // 折叠 Error.detail
    });
    row->addWidget(more);
    row->addStretch(1);
    col->addLayout(row);
}

} // namespace shine::widgets
