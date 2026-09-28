#include "ui/kit/controls/Feedback.h"

#include "ui/kit/theme/Theme.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace shine::widgets {

// =============================================================== ProgressBar

ProgressBar::ProgressBar(QWidget* parent) : QProgressBar(parent) {
    SetKind(this, "progressbar");
    setRange(0, 100);
    setValue(0);
    setTextVisible(true);
    // 高度交给 QSS 的 min/max-height: 6px（webui .prog）。此处不再 setMinimumHeight(8)，
    // 否则会压过 QSS 的 6px，比设计稿高一档。
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
    col->setContentsMargins(20, 40, 20, 40); // webui .empty padding: 40px 20px
    col->setSpacing(10);                      // .empty gap: 10px
    col->setAlignment(Qt::AlignCenter);

    // .glyph：52×52 圆角虚线方框，框内才是图标（QSS 的 emptyglyph 负责框）
    auto* glyph = new QFrame(this);
    SetKind(glyph, "emptyglyph");
    auto* glyphLay = new QHBoxLayout(glyph);
    glyphLay->setContentsMargins(0, 0, 0, 0);
    auto* ic = new QLabel(icon, glyph);
    SetKind(ic, "stateicon");
    ic->setAlignment(Qt::AlignCenter);
    glyphLay->addWidget(ic);
    col->addWidget(glyph, 0, Qt::AlignCenter);

    auto* t = new QLabel(title, this);
    SetKind(t, "emptytitle"); // .empty .title：f13 w600 text-secondary
    t->setAlignment(Qt::AlignCenter);
    t->setWordWrap(true);
    t->setMaximumWidth(360); // 同 subtitle：宽度上限让折行点可预期
    col->addWidget(t, 0, Qt::AlignCenter);

    auto* s = new QLabel(subtitle, this);
    SetKind(s, "statesub");
    s->setAlignment(Qt::AlignCenter);
    s->setWordWrap(true);
    // ⚠️ 必须给宽度上限：wordWrap 的 QLabel 在 Qt::AlignCenter 下会按
    // 「父容器当前宽度」折行，若父容器此刻还没完成布局（宽度接近 0 或过大），
    // 换行位置算错就会把整行文字挤成乱码（实测截图里「检测片段默认
    // 校验状态」压成不可读的一团）。给固定上限让折行点可预期。
    s->setMaximumWidth(360);
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
    col->setContentsMargins(20, 40, 20, 40); // 与 EmptyState 同一套 .empty 几何
    col->setSpacing(10);
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
