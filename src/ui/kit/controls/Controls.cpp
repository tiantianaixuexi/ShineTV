#include "ui/kit/controls/Controls.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/motion/Tween.h"
#include "ui/kit/theme/Theme.h"

#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVariant>
#include <QVBoxLayout>

#include <cmath>
#include <utility>
#include <vector>

namespace shine::widgets {

// ================================================================ Spinner

Spinner::Spinner(Size s, QWidget* parent) : QWidget(parent), size_(s) {
    SetKind(this, "spinner");
    const int d = DiameterPx();
    setFixedSize(d, d);
    timer_.setInterval(theme::motion::kDurFastMs / 4); // 节律取 motion token
    connect(&timer_, &QTimer::timeout, this, [this] {
        angle_ = std::fmod(angle_ + 15.0, 360.0);
        update();
    });
    SetRunning(true);
}

int Spinner::DiameterPx() const {
    switch (size_) {
        case Size::Sm:
            return 14;
        case Size::Lg:
            return 32;
        case Size::Md:
        default:
            return 20;
    }
}

void Spinner::SetRunning(bool on) {
    if (on && !motion::ReduceMotion()) {
        timer_.start();
    } else {
        timer_.stop();
    }
}

void Spinner::paintEvent(QPaintEvent* ev) {
    QWidget::paintEvent(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor col = TokenQColor(theme::Current().accentPrimary);
    QPen pen{col};
    pen.setWidthF(size_ == Size::Lg ? 3.0 : 2.0);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const QPointF c{width() / 2.0, height() / 2.0};
    p.translate(c);
    p.rotate(angle_);
    const double r = (DiameterPx() - pen.widthF()) / 2.0;
    p.drawArc(QRectF{-r, -r, 2 * r, 2 * r}, 0, 90 * 16);
}

// ================================================================= Button

Button::Button(const QString& text, Variant v, Size s, QWidget* parent) : QPushButton(text, parent) {
    SetKind(this, "button");
    switch (v) {
        case Variant::Primary:
            SetVariant(this, "primary");
            break;
        case Variant::Danger:
            SetVariant(this, "danger");
            break;
        case Variant::Ghost:
            SetVariant(this, "ghost");
            break;
        case Variant::Secondary:
        default:
            SetVariant(this, "secondary");
            break;
    }
    SetSizeAttr(this, s == Size::Sm ? "sm" : (s == Size::Lg ? "lg" : "md"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void Button::SetLoading(bool on) {
    if (on == loading_) {
        return;
    }
    loading_ = on;
    if (on) {
        fixed_w_ = width();
        setFixedWidth(fixed_w_); // 保持宽度（不跳动）
        saved_text_ = text();
        setText(QString{});
        setEnabled(false); // 禁点
        spinner_ = new Spinner(Spinner::Size::Sm, this);
        spinner_->show();
        PlaceSpinner();
    } else {
        setEnabled(true);
        setText(saved_text_);
        setMinimumWidth(0);
        setMaximumWidth(16777215);
        if (spinner_ != nullptr) {
            spinner_->deleteLater();
            spinner_ = nullptr;
        }
        loading_ = false;
    }
}

void Button::resizeEvent(QResizeEvent* ev) {
    QPushButton::resizeEvent(ev);
    PlaceSpinner();
}

void Button::PlaceSpinner() {
    if (spinner_ != nullptr) {
        const int d = spinner_->DiameterPx();
        spinner_->move((width() - d) / 2, (height() - d) / 2);
    }
}

// ============================================================= IconButton

IconButton::IconButton(const QString& iconText, const QString& tooltip, Size s, QWidget* parent)
    : QPushButton(iconText, parent) {
    SetKind(this, "iconbutton");
    SetSizeAttr(this, s == Size::Sm ? "sm" : "md");
    setToolTip(tooltip.isEmpty() ? iconText : tooltip); // 必带 tooltip（UI.md §1 无障碍）
    setFixedSize(s == Size::Sm ? 24 : 32, s == Size::Sm ? 24 : 32);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void IconButton::SetActive(bool on) {
    active_ = on;
    setProperty("active", on ? QStringLiteral("true") : QString{});
    Repolish(this);
}

// ==================================================================== Card

Card::Card(Variant v, QWidget* parent) : QFrame(parent) {
    SetKind(this, "card");
    SetVariant(this, v == Variant::Flat ? "flat" : (v == Variant::Elevated ? "elevated" : "outlined"));
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    body_ = new QVBoxLayout();
    body_->setContentsMargins(16, 12, 16, 12);
    body_->setSpacing(8);
    outer->addLayout(body_);
    setAttribute(Qt::WA_Hover, true);
}

void Card::SetAccent(bool on) {
    if (on && accent_ == nullptr) {
        accent_ = new QFrame(this);
        accent_->setFixedWidth(3);
        SetKind(accent_, "cardaccent");
        qobject_cast<QHBoxLayout*>(layout())->insertWidget(0, accent_);
    } else if (!on && accent_ != nullptr) {
        accent_->deleteLater();
        accent_ = nullptr;
    }
}

QVBoxLayout* Card::BodyLayout() { return body_; }

void Card::Lift(int dy) {
    auto* t = new motion::Tween{theme::motion::kStandard, this};
    const QPoint from = pos();
    t->Run(from, from + QPoint(0, dy), theme::motion::kDurFastMs, // motion.fast
           [this](const QVariant& v) { move(v.toPoint()); });
}

void Card::enterEvent(QEnterEvent* ev) {
    QFrame::enterEvent(ev);
    if (!lifted_) {
        lifted_ = true;
        Lift(-1); // hover 抬升 1px
    }
}

void Card::leaveEvent(QEvent* ev) {
    QFrame::leaveEvent(ev);
    if (lifted_) {
        lifted_ = false;
        Lift(1);
    }
}

void Card::mousePressEvent(QMouseEvent* ev) {
    QFrame::mousePressEvent(ev);
    if (on_click_) {
        on_click_();
    }
}

// ===================================================================== Tag

Tag::Tag(const QString& text, const char* tone, bool removable, QWidget* parent) : QFrame(parent) {
    SetKind(this, "tag");
    if (tone != nullptr && *tone != '\0') {
        setProperty("tone", QString::fromLatin1(tone));
        Repolish(this);
    }
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    label_ = new QLabel(text, this);
    label_->setStyleSheet(QStringLiteral("background: transparent;"));
    row->addWidget(label_);
    if (removable) {
        auto* x = new QPushButton(QStringLiteral("×"), this);
        SetKind(x, "clearbutton");
        x->setFixedSize(16, 16);
        x->setCursor(Qt::PointingHandCursor);
        connect(x, &QPushButton::clicked, this, [this] {
            if (on_remove_) {
                on_remove_();
            }
        });
        row->addWidget(x);
    }
}

// ==================================================================== Badge

Badge::Badge(QWidget* parent) : QLabel(parent) {
    SetKind(this, "badge");
    setAlignment(Qt::AlignCenter);
    SetCount(0);
}

void Badge::SetCount(int n) {
    setProperty("dot", QString{});
    if (n < 0) {
        hide();
        return;
    }
    show();
    setText(n > 99 ? QStringLiteral("99+") : QString::number(n)); // 超 99 → 99+
    Repolish(this);
}

void Badge::SetDot(bool on) {
    if (on) {
        setProperty("dot", QStringLiteral("true"));
        setText(QString{});
        setFixedSize(8, 8);
        show();
        Repolish(this);
    } else {
        setMinimumSize(0, 0);
        setMaximumSize(16777215, 16777215);
        SetCount(0);
    }
}

void Badge::SetTone(const char* tone) {
    setProperty("tone", QString::fromLatin1(tone));
    Repolish(this);
}

// ===================================================================== Kbd

Kbd::Kbd(const QString& keys, QWidget* parent) : QLabel(keys, parent) {
    SetKind(this, "kbd");
    setAlignment(Qt::AlignCenter);
}

// =============================================================== Segmented

Segmented::Segmented(const QStringList& items, QWidget* parent) : QFrame(parent) {
    SetKind(this, "segmented");
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(2);
    int i = 0;
    for (const QString& it : items) {
        if (i >= 4) {
            break; // 2–4 段（UI.md §2.1）
        }
        auto* b = new QPushButton(it, this);
        SetKind(b, "segment");
        b->setCursor(Qt::PointingHandCursor);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::StrongFocus);
        connect(b, &QPushButton::clicked, this, [this, i] { Select(i); });
        items_.push_back(b);
        row_->addWidget(b);
        ++i;
    }
    if (!items_.empty()) {
        Select(0);
    }
}

void Segmented::SetCurrent(int index) { Select(index); }

void Segmented::Select(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    current_ = index;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        items_[static_cast<std::size_t>(i)]->setChecked(i == index);
        items_[static_cast<std::size_t>(i)]->setProperty(
            "selected", i == index ? QStringLiteral("true") : QString{});
        Repolish(items_[static_cast<std::size_t>(i)]);
    }
    if (on_changed_) {
        on_changed_(index);
    }
}

// ======================================================================= Chip

Chip::Chip(const QString& text, const char* tone, QWidget* parent) : QPushButton(text, parent) {
    SetKind(this, "chip");
    if (tone != nullptr && *tone != '\0') {
        setProperty("tone", QString::fromLatin1(tone));
    }
    setCursor(Qt::PointingHandCursor);
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(text);
    base_text_ = text;
    connect(this, &QPushButton::clicked, this, [this] {
        on_ = !on_;
        Apply();
        if (on_toggled_) {
            on_toggled_(on_);
        }
    });
    Apply();
}

void Chip::Apply() {
    setChecked(on_);
    setProperty("on", on_ ? QStringLiteral("true") : QString{});
    Repolish(this);
}

void Chip::SetOn(bool on) {
    if (on_ == on) {
        return;
    }
    on_ = on;
    Apply();
}

void Chip::SetBaseText(const QString& text) {
    base_text_ = text;
    setToolTip(text);
    SetCount(count_);
}

// 计数直接进按钮文字：QPushButton 一旦挂 QLayout，子控件会和它自绘的文字抢位置
// （文字被布局裁掉、计数压在文字上）。webui 的 .chip .cnt 是独立小胶囊，
// Qt 这里退化为「标签 + 空格 + 计数」，语义一致、外观略简。
void Chip::SetCount(int n) {
    count_ = n;
    setText(n < 0 ? base_text_
                  : QStringLiteral("%1 %2").arg(base_text_).arg(n));
    setToolTip(n < 0 ? base_text_
                     : QStringLiteral("%1 · %2 项").arg(base_text_).arg(n));
}

} // namespace shine::widgets