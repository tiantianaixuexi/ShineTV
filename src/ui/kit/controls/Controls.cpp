#include "ui/kit/controls/Controls.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/motion/Tween.h"
#include "ui/kit/theme/Theme.h"

#include <QHBoxLayout>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegion>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOptionButton>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
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

// ============================================================= StatusDot

StatusDot::StatusDot(const char* tone, QWidget* parent) : QWidget(parent) {
    SetKind(this, "statusdot");
    setFixedSize(DiameterPx(), DiameterPx()); // ui.css .dot 7×7
    // 1.6s 一个周期（ui.css:171）。定时器步长取 40ms：相位连续推进，
    // 由 paintEvent 插值出实际半径/透明度，不会在两个离散帧之间跳。
    timer_.setInterval(40);
    connect(&timer_, &QTimer::timeout, this, [this] {
        phase_ += 40.0 / 1600.0; // 40ms / 1.6s
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
        }
        update();
    });
    SetTone(tone);
}

void StatusDot::SetPulse(bool on) {
    pulse_ = on;
    if (on && !motion::ReduceMotion()) {
        phase_ = 0.0;
        timer_.start();
    } else {
        timer_.stop(); // 「减少动效」：静态点，语义仍是「运行中」
    }
    update();
}

void StatusDot::SetTone(const char* tone) {
    tone_ = QString::fromLatin1(tone);
    update();
}

void StatusDot::paintEvent(QPaintEvent* ev) {
    QWidget::paintEvent(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // tone → status.* token；空 tone 走 accent（设计稿 .dot 默认随状态给色）
    const theme::ColorToken& c = theme::Current();
    QColor col = TokenQColor(c.accentPrimary);
    if (tone_ == QLatin1String("ok")) {
        col = TokenQColor(c.statusOk);
    } else if (tone_ == QLatin1String("warn")) {
        col = TokenQColor(c.statusWarn);
    } else if (tone_ == QLatin1String("danger")) {
        col = TokenQColor(c.statusDanger);
    } else if (tone_ == QLatin1String("idle")) {
        col = TokenQColor(c.statusIdle);
    } else if (tone_ == QLatin1String("busy")) {
        col = TokenQColor(c.statusBusy);
    }

    const QPointF center{width() / 2.0, height() / 2.0};
    const double d = DiameterPx();

    if (pulse_ && timer_.isActive()) {
        // pulse-dot：外扩的光晕环 + 本体轻微缩放，循环相位 0→1
        // 光晕半径 1.0→1.9 直径、alpha 0.35→0（本体的淡出尾迹）
        const double t = phase_;
        const double glow = d * (1.0 + 0.9 * t);
        QColor halo = col;
        halo.setAlphaF(0.35 * (1.0 - t));
        p.setBrush(halo);
        p.setPen(Qt::NoPen);
        p.drawEllipse(center, glow / 2.0, glow / 2.0);

        // 本体随相位轻微呼吸（1.0 → 0.86 → 1.0），用正弦模拟 CSS 的 keyframes 起伏
        const double breathe = 1.0 - 0.14 * (0.5 - 0.5 * std::cos(2.0 * M_PI * t));
        const double body = d * breathe;
        p.setBrush(col);
        p.drawEllipse(center, body / 2.0, body / 2.0);
        return;
    }

    p.setBrush(col);
    p.setPen(Qt::NoPen);
    p.drawEllipse(center, d / 2.0, d / 2.0);
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

void Button::CaptureBackdrop() {
    // 把按钮自身排除后再抓父容器：父控件 render() 会连同子控件一起绘制，
    // 若按钮在画面上，抓回来的就是「已经含按钮」的图 —— 缩小后那圈露边仍是
    // 旧按钮的像素，残影照旧。hide/show 发生在同一个事件处理里、绘制入队之前，
    // 因此不会看到中间态（也不会触发按下取消，按钮的按压态由鼠标事件维护）。
    //
    // ⚠️ 隐藏控件会把焦点交出去（setVisible(true) 不会还回来）。键盘导航过的
    // 按钮按下瞬间焦点本来就在自己身上，所以这里显式存取一次；QAbstractButton
    // 随后的 mousePressEvent 也会再次 setFocus，两者不冲突。
    QWidget* p = parentWidget();
    backdrop_ = QPixmap();
    if (p == nullptr || !isVisible() || size().isEmpty()) {
        return;
    }
    QWidget* focus_before = focusWidget();
    setVisible(false);
    QPixmap shot(size());
    shot.fill(Qt::transparent);
    p->render(&shot, QPoint(-pos()), QRegion(rect()));
    setVisible(true);
    if (focus_before != nullptr && focus_before != this) {
        focus_before->setFocus();
    }
    backdrop_ = shot;
}

void Button::AnimatePress(double scale) {
    // 复用同一个 Tween 实例：Tween 不自删（见 Tween.h），每次交互 new 一个会
    // 随交互次数单调累积。Run() 内部会 restart()，直接重跑即可。
    if (press_tween_ == nullptr) {
        press_tween_ = new motion::Tween{theme::motion::kStandard, this};
    }
    const double from = press_scale_;
    press_tween_->Run(from, scale, theme::motion::kDurFastMs, [this](const QVariant& v) {
        press_scale_ = v.toDouble();
        // 回到 1.0 后背景图失效，走正常绘制路径
        if (press_scale_ >= 0.999) {
            backdrop_ = QPixmap();
        }
        update();
    });
}

void Button::paintEvent(QPaintEvent* ev) {
    // backdrop 尺寸必须与当前控件一致：loading 切换会 setFixedWidth()，若此时
    // 正在按压，尺寸已变而图还是旧的，缩放定位会整体偏移 —— 直接放弃该帧缩放。
    const bool can_scale = press_scale_ < 0.999 && !backdrop_.isNull() &&
                           backdrop_.size() == size();
    if (!can_scale) {
        QPushButton::paintEvent(ev);
        return;
    }
    // 先铺「按钮背后的内容」，再把按钮本体以中心为基准缩放绘制。
    // 走 QStyle 而不是基类 paintEvent：基类会走 Qt 自己的绘制路径，
    // 而我们要的是把 CE_PushButton 画进一个已被平移/缩放的 painter。
    QPainter p(this);
    p.drawPixmap(0, 0, backdrop_);
    QStyleOptionButton opt;
    initStyleOption(&opt);
    p.save();
    p.translate(width() / 2.0, height() / 2.0);
    p.scale(press_scale_, press_scale_);
    p.translate(-width() / 2.0, -height() / 2.0);
    style()->drawControl(QStyle::CE_PushButton, &opt, &p, this);
    p.restore();
}

void Button::mousePressEvent(QMouseEvent* ev) {
    // webui `.btn:disabled { transform: none }`：禁用态不缩放
    if (isEnabled() && !isCheckable()) {
        CaptureBackdrop();
        AnimatePress(0.97); // webui ui.css:23
    }
    QPushButton::mousePressEvent(ev);
}

void Button::mouseReleaseEvent(QMouseEvent* ev) {
    QPushButton::mouseReleaseEvent(ev);
    if (press_scale_ < 0.999) {
        AnimatePress(1.0);
    }
}

// ============================================================= IconButton

IconButton::IconButton(const QString& iconText, const QString& tooltip, Size s, QWidget* parent)
    : QPushButton(iconText, parent) {
    SetKind(this, "iconbutton");
    SetSizeAttr(this, s == Size::Sm ? "sm" : "md");
    setToolTip(tooltip.isEmpty() ? iconText : tooltip); // 必带 tooltip（UI.md §1 无障碍）
    // webui ui.css .icon-btn 28×28 / .icon-btn.sm 22×22（box-sizing: border-box，含 1px 边）
    setFixedSize(s == Size::Sm ? 22 : 28, s == Size::Sm ? 22 : 28);
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
    // 抬升/回落过渡对齐 CSS transition 的 var(--dur-2)=200ms（此前用 motion.fast）
    auto* t = new motion::Tween{theme::motion::kStandard, this};
    const QPoint from = pos();
    t->Run(from, from + QPoint(0, dy), theme::motion::kDurBaseMs,
           [this](const QVariant& v) { move(v.toPoint()); });
}

void Card::enterEvent(QEnterEvent* ev) {
    QFrame::enterEvent(ev);
    if (!lifted_) {
        lifted_ = true;
        // webui ui.css:196 .card.hoverable:hover = translateY(-2px)，过渡走 motion.base
        Lift(-2);
        ApplyShadow(this, ShadowLevel::Sm); // webui 同一规则还带 shadow-1（QSS 无 box-shadow）
    }
}

void Card::leaveEvent(QEvent* ev) {
    QFrame::leaveEvent(ev);
    if (lifted_) {
        lifted_ = false;
        Lift(2);
        ApplyShadow(this, ShadowLevel::None);
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
        setFixedSize(7, 7); // webui ui.css .dot：7×7 正圆
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

Chip::Chip(const QString& text, const char* tone, QWidget* parent) : QPushButton(parent) {
    SetKind(this, "chip");
    if (tone != nullptr && *tone != '\0') {
        setProperty("tone", QString::fromLatin1(tone));
    }
    setCursor(Qt::PointingHandCursor);
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(text);
    base_text_ = text;
    // webui .chip 是 inline-flex + gap 6 的**双元素**结构：
    //   {文字}<span className="cnt">{计数}</span>
    // QPushButton 会自绘 text()，一旦再挂子控件，两者抢同一块矩形
    // （按钮文字被布局裁掉、计数压在文字上）。所以按钮自身文字必须清空，
    // 全部内容交给内部布局的两个 QLabel 承载。
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(11, 0, 11, 0); // webui .chip padding: 0 11px
    row_->setSpacing(6);                     // webui .chip gap: 6px
    label_ = new QLabel(text, this);
    SetKind(label_, "chiplabel");
    row_->addWidget(label_, 0, Qt::AlignVCenter);
    count_label_ = new QLabel(this);
    SetKind(count_label_, "chipcount");
    count_label_->hide();
    row_->addWidget(count_label_, 0, Qt::AlignVCenter);
    setText(QString{});
    // 无障碍/自动化取值：findChild 与 tooltip 仍能拿到完整文案
    setAccessibleName(text);
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
    updateGeometry(); // 标签/计数文字变化后让父布局重排
}

// 尺寸取内部布局（标签 + 计数胶囊）+ 边框，QSS min-height:26 保证药丸高度。
QSize Chip::SizeHintFromContent() const {
    if (row_ == nullptr) {
        return {0, 26};
    }
    // 不能直接用 row_->sizeHint()：布局未激活时它返回的是上一次激活时的缓存
    // （或无效默认值），此时标签刚 setText 完、布局还没重算，宽度会偏小，
    // 上游再按这个宽度摆放就会裁字。改成直接把两个子标签的 hint 手动累加——
    // 它们就是全部内容，且各自 hint 永远是即时的。
    int w = row_->contentsMargins().left() + row_->contentsMargins().right() + 2; // +2 边框
    int h = 26;
    for (const QWidget* child : {static_cast<const QWidget*>(label_),
                                 static_cast<const QWidget*>(count_label_)}) {
        if (child == nullptr || child->isHidden()) {
            continue;
        }
        w += child->sizeHint().width();
        h = std::max(h, child->sizeHint().height() + 2);
    }
    if (label_ != nullptr && count_label_ != nullptr && !count_label_->isHidden()) {
        w += row_->spacing(); // 标签与计数胶囊之间有 gap
    }
    return {w, h};
}

QSize Chip::sizeHint() const { return SizeHintFromContent(); }

QSize Chip::minimumSizeHint() const { return SizeHintFromContent(); }

void Chip::SetOn(bool on) {
    if (on_ == on) {
        return;
    }
    on_ = on;
    Apply();
}

void Chip::SetBaseText(const QString& text) {
    base_text_ = text;
    setToolTip(count_ < 0 ? text : QStringLiteral("%1 · %2 项").arg(text).arg(count_));
    setAccessibleName(text);
    if (label_ != nullptr) {
        label_->setText(text);
    }
    SetCount(count_);
}

// 计数独立成胶囊（webui .chip .cnt），与标签并列而非并入文字。
void Chip::SetCount(int n) {
    count_ = n;
    if (count_label_ == nullptr) {
        return;
    }
    if (n < 0) {
        count_label_->setText(QString{});
        count_label_->hide();
    } else {
        count_label_->setText(QString::number(n));
        count_label_->show();
    }
    setToolTip(n < 0 ? base_text_
                     : QStringLiteral("%1 · %2 项").arg(base_text_).arg(n));
    updateGeometry();
}

} // namespace shine::widgets