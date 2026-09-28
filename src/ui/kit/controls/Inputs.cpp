#include "ui/kit/controls/Inputs.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/motion/Tween.h"
#include "ui/kit/theme/Theme.h"

#include <QApplication>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPolygonF>
#include <QSlider>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace shine::widgets {
namespace {

// 自绘控件的状态色：disabled > pressed > hover > focus > normal
struct PaintState {
    bool hover = false;
    bool pressed = false;
    bool disabled = false;
    bool focus = false;
};

PaintState StateOf(const QWidget& w) {
    PaintState s;
    const QVariant v = w.property("shineState");
    const QString forced = v.toString();
    s.hover = forced == QStringLiteral("hover") || w.underMouse();
    s.pressed = forced == QStringLiteral("pressed");
    s.focus = forced == QStringLiteral("focus") || w.hasFocus();
    s.disabled = !w.isEnabled() || forced == QStringLiteral("disabled");
    return s;
}

} // namespace

// =================================================================== Field

Field::Field(const QString& label, LabelPos pos, QWidget* control, QWidget* parent)
    : QFrame(parent), control_(control) {
    SetKind(this, "field");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(4);

    auto* caption = new QLabel(label, this);
    SetKind(caption, "fieldlabel");

    if (pos == LabelPos::Top) {
        outer->addWidget(caption);
        outer->addWidget(control_);
    } else {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 8, 0);
        row->addWidget(caption);
        row->addWidget(control_, 1);
        outer->addLayout(row);
    }

    help_ = new QLabel(this);
    SetKind(help_, "fieldhelp");
    help_->setWordWrap(true);
    help_->hide();
    outer->addWidget(help_);

    error_ = new QLabel(this);
    SetKind(error_, "fielderror");
    error_->setWordWrap(true);
    error_->hide();
    outer->addWidget(error_);
}

void Field::SetHelp(const QString& text) {
    help_->setText(text);
    help_->setVisible(!text.isEmpty() && !HasError());
}

void Field::SetError(const QString& text) {
    error_->setText(text);
    error_->setVisible(!text.isEmpty());
    // error 态描边 status.danger（control 自带 error 属性）
    if (control_ != nullptr) {
        control_->setProperty("error", text.isEmpty() ? QString{} : QStringLiteral("true"));
        Repolish(control_);
    }
    help_->setVisible(!text.isEmpty() ? false : !help_->text().isEmpty());
}

bool Field::HasError() const { return error_ != nullptr && !error_->text().isEmpty(); }

// =============================================================== TextInput

TextInput::TextInput(QWidget* parent) : QFrame(parent) {
    SetKind(this, "inputframe");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    edit_ = new QLineEdit(this);
    SetKind(edit_, "input");
    edit_->setFocusPolicy(Qt::StrongFocus);
    row->addWidget(edit_, 1);

    clear_ = new QPushButton(QStringLiteral("×"), this);
    SetKind(clear_, "clearbutton");
    clear_->setFixedSize(20, 20);
    clear_->setCursor(Qt::PointingHandCursor);
    clear_->hide();
    row->addWidget(clear_);

    connect(edit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        clear_->setVisible(!t.isEmpty()); // 清空按钮
        if (on_changed_) {
            on_changed_(t);
        }
    });
    connect(clear_, &QPushButton::clicked, this, [this] { edit_->clear(); });
    edit_->installEventFilter(this);
}

bool TextInput::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == edit_ && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Escape && !edit_->text().isEmpty()) { // Esc 清空
            edit_->clear();
            return true;
        }
    }
    return QFrame::eventFilter(obj, ev);
}

void TextInput::SetText(const QString& t) { edit_->setText(t); }
QString TextInput::Text() const { return edit_->text(); }
void TextInput::SetPlaceholder(const QString& p) { edit_->setPlaceholderText(p); }

void TextInput::SetError(bool on) {
    edit_->setProperty("error", on ? QStringLiteral("true") : QString{});
    Repolish(edit_);
}

// ================================================================= TextArea

TextArea::TextArea(int maxChars, QWidget* parent) : QFrame(parent), limit_(maxChars) {
    SetKind(this, "field");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    edit_ = new QPlainTextEdit(this);
    SetKind(edit_, "textarea");
    edit_->setMinimumHeight(88);
    col->addWidget(edit_, 1);

    counter_ = new QLabel(this);
    SetKind(counter_, "counter");
    counter_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    col->addWidget(counter_);

    connect(edit_, &QPlainTextEdit::textChanged, this, &TextArea::RefreshCounter);
    RefreshCounter();
}

void TextArea::SetText(const QString& t) { edit_->setPlainText(t); }
QString TextArea::Text() const { return edit_->toPlainText(); }
void TextArea::SetLimit(int n) {
    limit_ = n;
    RefreshCounter();
}

bool TextArea::IsOverLimit() const { return edit_->toPlainText().size() > limit_; }

void TextArea::RefreshCounter() {
    const int n = edit_->toPlainText().size();
    const bool over = n > limit_; // 超限变 status.danger
    counter_->setText(QStringLiteral("%1 / %2").arg(n).arg(limit_));
    counter_->setProperty("error", over ? QStringLiteral("true") : QString{});
    edit_->setProperty("error", over ? QStringLiteral("true") : QString{});
    Repolish(counter_);
    Repolish(edit_);
}

// =================================================================== Select

Select::Select(bool searchable, bool multi, QWidget* parent)
    : QFrame(parent), searchable_(searchable), multi_(multi) {
    SetKind(this, "input");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(4);

    summary_ = new QPushButton(this);
    // 外观全部交给全局 QSS（selectbutton 段）。内联 `color: palette(text)` 读的是
    // QPalette 而非 Token：应用此前不下发调色板，深色主题下它就是系统黑 → 深底黑字。
    SetKind(summary_, "selectbutton");
    summary_->setCursor(Qt::PointingHandCursor);
    // ClickFocus：弹层只能点开（Select 无键盘处理），因此不放进 Tab 链 ——
    // 否则对话框打开时焦点默认落在摘要按钮上，白画一圈 accent 焦点环，看着像选中态。
    summary_->setFocusPolicy(Qt::ClickFocus);
    row->addWidget(summary_, 1);

    auto* caret = new QLabel(QStringLiteral("▾"), this);
    caret->setStyleSheet(QStringLiteral("background: transparent;"));
    row->addWidget(caret);

    connect(summary_, &QPushButton::clicked, this, [this] { TogglePopup(); });
}

void Select::SetItems(std::vector<Item> items) {
    items_ = std::move(items);
    SyncSummary();
}

void Select::SetPlaceholder(const QString& p) { summary_->setText(p); }

std::vector<QString> Select::Checked() const {
    std::vector<QString> out;
    for (const Item& it : items_) {
        if (it.checked) {
            out.push_back(it.text);
        }
    }
    return out;
}

void Select::mousePressEvent(QMouseEvent* ev) {
    QFrame::mousePressEvent(ev);
    TogglePopup();
}

void Select::SyncSummary() {
    QString s;
    for (const Item& it : items_) {
        if (it.checked) {
            if (!s.isEmpty()) {
                s += QStringLiteral("、");
            }
            s += it.text;
        }
    }
    summary_->setText(s);
}

void Select::TogglePopup() {
    if (popup_ != nullptr) {
        popup_->close();
        return;
    }
    BuildPopup(QString{});
}

void Select::BuildPopup(const QString& filter) {
    auto* popup = new QWidget(nullptr, Qt::Popup);
    popup_ = popup;
    popup->setAttribute(Qt::WA_DeleteOnClose);
    SetKind(popup, "selectpopup");
    auto* col = new QVBoxLayout(popup);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(2);

    QLineEdit* find = nullptr;
    if (searchable_) {
        find = new QLineEdit(popup);
        SetKind(find, "input");
        find->setPlaceholderText(QStringLiteral("搜索…"));
        find->setClearButtonEnabled(true);
        col->addWidget(find);
    }

    std::vector<std::pair<QPushButton*, std::size_t>> rows;
    std::vector<QLabel*> groups;

    QString last_group;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        Item& it = items_[i];
        if (!filter.isEmpty() && !it.text.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        if (!it.group.isEmpty() && it.group != last_group) {
            last_group = it.group;
            auto* g = new QLabel(it.group, popup);
            SetKind(g, "selectgroup");
            col->addWidget(g);
            groups.push_back(g);
        }
        const QString mark = it.checked ? QStringLiteral("✓ ") : QString{};
        auto* row = new QPushButton(mark + it.text, popup);
        SetKind(row, "selectitem");
        row->setProperty("checked", it.checked ? QStringLiteral("true") : QString{});
        row->setCursor(Qt::PointingHandCursor);
        QObject::connect(row, &QPushButton::clicked, popup, [this, popup, i] {
            if (multi_) {
                items_[i].checked = !items_[i].checked;
                SyncSummary();
                popup->close();
                BuildPopup(QString{});
            } else {
                for (Item& x : items_) {
                    x.checked = false;
                }
                items_[i].checked = true;
                SyncSummary();
                popup->close();
            }
            if (on_changed_) {
                on_changed_();
            }
        });
        col->addWidget(row);
        rows.emplace_back(row, i);
    }
    if (find != nullptr) {
        QObject::connect(find, &QLineEdit::textChanged, popup, [rows, groups](const QString& t) {
            // 原地过滤（不重建弹层，避免输入焦点被拆）；rows 已填充后按值捕获
            for (const auto& [row, idxUnused] : rows) {
                Q_UNUSED(idxUnused);
                row->setVisible(t.isEmpty() || row->text().contains(t, Qt::CaseInsensitive));
            }
            for (QLabel* g : groups) {
                g->setVisible(t.isEmpty());
            }
        });
    }
    popup->adjustSize();
    popup->move(mapToGlobal(QPoint(0, height() + 2)));
    popup->show();
    popup->raise();
}

// =================================================================== Slider

Slider::Slider(bool withTicks, double min, double max, const QString& unit, QWidget* parent)
    : QFrame(parent), unit_(unit) {
    SetKind(this, "field");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setProperty("shineKind", QStringLiteral("slider")); // QSS 用 QSlider::groove[shineKind]
    const bool isInt = (unit.isEmpty() && min == static_cast<int>(min) && max == static_cast<int>(max));
    scale_ = isInt ? 1.0 : 100.0;
    slider_->setRange(static_cast<int>(min * scale_), static_cast<int>(max * scale_));
    if (withTicks) { // 刻度
        slider_->setTickPosition(QSlider::TicksBelow);
        slider_->setTickInterval(std::max(1, static_cast<int>((max - min) * scale_ / 5)));
    }
    row->addWidget(slider_, 1);

    value_ = new QLabel(this);
    SetKind(value_, "slidervalue");
    row->addWidget(value_);

    connect(slider_, &QSlider::valueChanged, this, [this](int) {
        RefreshLabel();
        if (on_changed_) {
            on_changed_(Value());
        }
    });
    RefreshLabel();
}

void Slider::SetValue(double v) {
    slider_->setValue(static_cast<int>(v * scale_));
    RefreshLabel();
}

double Slider::Value() const { return slider_->value() / scale_; }

void Slider::RefreshLabel() {
    const double v = Value();
    const QString num = scale_ == 1.0 ? QString::number(static_cast<int>(v))
                                      : QString::number(v, 'f', 2);
    value_->setText(unit_.isEmpty() ? num : num + QStringLiteral(" ") + unit_); // 值 + 单位
}

// =============================================================== NumberInput

NumberInput::NumberInput(bool isFloat, double min, double max, QWidget* parent)
    : QFrame(parent), is_float_(isFloat), min_(min), max_(max) {
    SetKind(this, "field");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    auto* minus = new QPushButton(QStringLiteral("−"), this);
    SetKind(minus, "button");
    SetVariant(minus, "ghost");
    SetSizeAttr(minus, "sm");
    minus->setFixedWidth(24);
    connect(minus, &QPushButton::clicked, this, [this] { Step(-1); });
    row->addWidget(minus);

    edit_ = new QLineEdit(this);
    SetKind(edit_, "input");
    if (is_float_) {
        edit_->setValidator(new QDoubleValidator(min_, max_, 2, edit_));
    } else {
        edit_->setValidator(new QIntValidator(static_cast<int>(min_), static_cast<int>(max_), edit_));
    }
    edit_->setAlignment(Qt::AlignCenter);
    connect(edit_, &QLineEdit::editingFinished, this, [this] {
        if (on_changed_) {
            on_changed_(Value());
        }
    });
    row->addWidget(edit_, 1);

    auto* plus = new QPushButton(QStringLiteral("+"), this);
    SetKind(plus, "button");
    SetVariant(plus, "ghost");
    SetSizeAttr(plus, "sm");
    plus->setFixedWidth(24);
    connect(plus, &QPushButton::clicked, this, [this] { Step(+1); });
    row->addWidget(plus);

    col->addLayout(row);

    auto* hint = new QLabel(QStringLiteral("范围 %1 – %2")
                                .arg(is_float_ ? QString::number(min_, 'f', 1) : QString::number(static_cast<int>(min_)))
                                .arg(is_float_ ? QString::number(max_, 'f', 1) : QString::number(static_cast<int>(max_))),
                            this); // 范围提示
    SetKind(hint, "fieldhelp");
    col->addWidget(hint);

    SetValue(min_);
}

void NumberInput::SetValue(double v) {
    v = std::clamp(v, min_, max_);
    edit_->setText(is_float_ ? QString::number(v, 'f', 2)
                             : QString::number(static_cast<int>(std::lround(v))));
}

double NumberInput::Value() const {
    bool ok = false;
    const double v = edit_->text().toDouble(&ok);
    return ok ? std::clamp(v, min_, max_) : min_;
}

void NumberInput::Step(int dir) {
    const double cur = Value();
    SetValue(is_float_ ? cur + dir * 1.0 : cur + dir);
    if (on_changed_) {
        on_changed_(Value());
    }
}

// ==================================================================== Toggle

Toggle::Toggle(QWidget* parent) : QWidget(parent) {
    SetKind(this, "toggle");
    setFixedSize(36, 20);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    knob_t_ = 0.0;
}

void Toggle::SetChecked(bool on) {
    if (checked_ == on) {
        return;
    }
    checked_ = on;
    motion::Tween t{theme::motion::kStandard}; // knob 滑动 motion.fast
    t.Run(knob_t_, on ? 1.0 : 0.0, theme::motion::kDurFastMs,
          [this](const QVariant& v) {
              knob_t_ = v.toDouble();
              update();
          });
    if (motion::ReduceMotion()) {
        knob_t_ = on ? 1.0 : 0.0;
        update();
    }
    if (on_toggled_) {
        on_toggled_(on);
    }
}

void Toggle::paintEvent(QPaintEvent* ev) {
    QWidget::paintEvent(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const theme::ColorToken& t = theme::Current();
    const PaintState s = StateOf(*this);
    // 勾选态也要能区分 hover / pressed（五态可辨）
    const QColor off = TokenQColor(s.pressed ? t.bgSurface : (s.hover ? t.bgElevated : t.bgPanel));
    const QColor on = TokenQColor(s.disabled ? t.statusIdle
                        : (s.pressed ? t.accentSecondary : (s.hover ? t.accentPrimaryHover : t.accentPrimary)));
    const QColor track = checked_ ? on : off;
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(TokenQColor(s.focus ? t.accentPrimary : t.lineNormal));
    p.setBrush(track);
    p.drawRoundedRect(r, 10, 10);

    const double kx = 2.0 + knob_t_ * (r.width() - 16.0);
    p.setPen(TokenQColor(t.lineNormal));
    p.setBrush(TokenQColor(t.bgSurface)); // knob 高亮块
    p.drawEllipse(QRectF(r.x() + kx, r.y() + 2, 16, 16));
}

void Toggle::mousePressEvent(QMouseEvent* ev) {
    QWidget::mousePressEvent(ev);
    SetChecked(!checked_);
    setFocus(Qt::OtherFocusReason);
}

void Toggle::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Space || ev->key() == Qt::Key_Return) {
        SetChecked(!checked_);
    } else {
        QWidget::keyPressEvent(ev);
    }
}

// ================================================================== Checkbox

Checkbox::Checkbox(const QString& text, QWidget* parent) : QWidget(parent), text_(text) {
    SetKind(this, "check");
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(24);
    setMinimumWidth(fontMetrics().horizontalAdvance(text_) + 32); // 自绘宽度下限（防塌缩）
}

void Checkbox::SetChecked(bool on) {
    checked_ = on;
    partial_ = false;
    update();
    if (on_toggled_) {
        on_toggled_(on);
    }
}

void Checkbox::SetPartial(bool on) {
    partial_ = on;
    update();
}

void Checkbox::paintEvent(QPaintEvent* ev) {
    QWidget::paintEvent(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const theme::ColorToken& t = theme::Current();
    const PaintState s = StateOf(*this);

    QRectF box{0, (height() - 16) / 2.0, 16, 16};
    const bool filled = checked_ || partial_;
    const QColor fillColor = TokenQColor(s.disabled ? t.statusIdle
                                : (s.pressed ? t.accentSecondary : (s.hover ? t.accentPrimaryHover : t.accentPrimary)));
    p.setPen(TokenQColor(s.disabled ? t.statusIdle : (s.focus ? t.accentPrimary : t.lineNormal)));
    p.setBrush(filled ? fillColor : TokenQColor(s.hover ? t.bgElevated : t.bgPanel));
    p.drawRoundedRect(box, 3, 3);

    if (partial_) {
        p.setPen(QPen(TokenQColor(t.accentPrimaryFg), 2));
        p.drawLine(box.x() + 4, box.center().y(), box.x() + 12, box.center().y());
    } else if (checked_) {
        p.setPen(QPen(TokenQColor(t.accentPrimaryFg), 2));
        p.drawPolyline(QPolygonF{{box.x() + 3.5, box.center().y()},
                                 {box.x() + 7, box.y() + 11},
                                 {box.x() + 12.5, box.y() + 5}});
    }
    p.setPen(TokenQColor(s.disabled ? t.statusIdle : t.textPrimary));
    p.drawText(QRectF(24, 0, width() - 24, height()), Qt::AlignVCenter | Qt::AlignLeft, text_);
}

void Checkbox::mousePressEvent(QMouseEvent* ev) {
    QWidget::mousePressEvent(ev);
    SetChecked(!checked_);
    setFocus(Qt::OtherFocusReason);
}

void Checkbox::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Space) {
        SetChecked(!checked_);
    } else {
        QWidget::keyPressEvent(ev);
    }
}

// ===================================================================== Radio

Radio::Radio(const QString& text, QWidget* parent) : QWidget(parent), text_(text) {
    SetKind(this, "radio");
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(24);
    setMinimumWidth(fontMetrics().horizontalAdvance(text_) + 32); // 自绘宽度下限（防塌缩）
}

void Radio::SetChecked(bool on) {
    checked_ = on;
    update();
    if (on_toggled_) {
        on_toggled_(on);
    }
}

void Radio::paintEvent(QPaintEvent* ev) {
    QWidget::paintEvent(ev);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const theme::ColorToken& t = theme::Current();
    const PaintState s = StateOf(*this);

    QRectF box{0, (height() - 16) / 2.0, 16, 16};
    const QColor fillColor = TokenQColor(s.disabled ? t.statusIdle
                                : (s.pressed ? t.accentSecondary : (s.hover ? t.accentPrimaryHover : t.accentPrimary)));
    p.setPen(TokenQColor(s.disabled ? t.statusIdle : (s.focus ? t.accentPrimary : t.lineNormal)));
    p.setBrush(checked_ ? fillColor : TokenQColor(s.hover ? t.bgElevated : t.bgPanel));
    p.drawEllipse(box);
    if (checked_) {
        p.setPen(Qt::NoPen);
        p.setBrush(TokenQColor(t.accentPrimaryFg));
        p.drawEllipse(box.adjusted(5, 5, -5, -5));
    }
    p.setPen(TokenQColor(s.disabled ? t.statusIdle : t.textPrimary));
    p.drawText(QRectF(24, 0, width() - 24, height()), Qt::AlignVCenter | Qt::AlignLeft, text_);
}

void Radio::mousePressEvent(QMouseEvent* ev) {
    QWidget::mousePressEvent(ev);
    SetChecked(true);
    setFocus(Qt::OtherFocusReason);
}

void Radio::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Space) {
        SetChecked(true);
    } else {
        QWidget::keyPressEvent(ev);
    }
}

// ================================================================= SearchBox

SearchBox::SearchBox(QWidget* parent) : QFrame(parent) {
    SetKind(this, "input");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 0, 2, 0);
    row->setSpacing(4);

    auto* icon = new QLabel(QStringLiteral("🔍"), this); // 前缀放大镜
    SetKind(icon, "searchicon");
    row->addWidget(icon);

    edit_ = new QLineEdit(this);
    edit_->setFrame(false);
    edit_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    edit_->setClearButtonEnabled(true);
    row->addWidget(edit_, 1);

    debounce_.setSingleShot(true);
    debounce_.setInterval(200); // 防抖 200ms（UI.md §2.1）
    connect(&debounce_, &QTimer::timeout, this, [this] {
        if (on_search_) {
            on_search_(edit_->text());
        }
    });
    connect(edit_, &QLineEdit::textChanged, this, [this](const QString&) { debounce_.start(); });
    edit_->installEventFilter(this);
}

void SearchBox::SetPlaceholder(const QString& p) { edit_->setPlaceholderText(p); }

bool SearchBox::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == edit_ && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Escape && !edit_->text().isEmpty()) { // Esc 清空
            edit_->clear();
            return true;
        }
    }
    return QFrame::eventFilter(obj, ev);
}

} // namespace shine::widgets
