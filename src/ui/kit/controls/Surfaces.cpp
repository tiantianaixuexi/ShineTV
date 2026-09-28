#include "ui/kit/controls/Surfaces.h"

#include "ui/kit/motion/Easing.h"
#include "ui/kit/motion/Transition.h"
#include "ui/kit/motion/Tween.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/controls/Controls.h"
#include "ui/kit/controls/Inputs.h"

#include <QApplication>
#include <QCursor>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <deque>
#include <utility>

namespace shine::widgets {
namespace {

// 全局 toast 栈（≤3 条）
std::deque<QWidget*>& ToastStack() {
    static std::deque<QWidget*> s;
    return s;
}

void LayoutBottomRight(QWidget* w) {
    const QRect ag = w->screen()->availableGeometry();
    w->move(ag.right() - w->width() - 16, ag.bottom() - w->height() - 16);
}

} // namespace

// ==================================================================== Dialog

Dialog::Dialog(const QString& title, Size s, QWidget* parent) : QWidget(parent, Qt::Dialog) {
    SetKind(this, "dialog");
    setAttribute(Qt::WA_DeleteOnClose, true);
    const int w = s == Size::Sm ? 420 : (s == Size::Lg ? 800 : 560); // 规格尺寸（UI.md §2.1）
    setFixedWidth(w);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(20, 16, 20, 16);
    col->setSpacing(12);

    auto* t = new QLabel(title, this);
    SetKind(t, "dialogtitle");
    SetSemibold(t, true);
    col->addWidget(t);

    auto* body = new QVBoxLayout();
    body->setSpacing(8);
    body->setObjectName(QStringLiteral("shineDialogBody"));
    col->addLayout(body);
    body_ = body;

    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    col->addLayout(actions);
    actions_row_ = actions;

    resize(w, 240);
}

QVBoxLayout* Dialog::BodyLayout() { return body_; }

void Dialog::SetActions(const QString& primaryText, const QString& secondaryText,
                        std::function<void()> onPrimary, std::function<void()> onSecondary) {
    on_primary_ = std::move(onPrimary);
    auto* secondary = new Button(secondaryText, Button::Variant::Ghost, Button::Size::Md, this);
    primary_ = new Button(primaryText, Button::Variant::Primary, Button::Size::Md, this);
    actions_row_->addWidget(secondary);
    actions_row_->addWidget(primary_);
    connect(secondary, &Button::clicked, this, [this, cb = std::move(onSecondary)] {
        if (cb) {
            cb();
        }
        close();
    });
    connect(primary_, &Button::clicked, this, [this] {
        if (on_primary_) {
            on_primary_();
        }
        close();
    });
}

void Dialog::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Escape) { // Esc 关
        close();
        return;
    }
    if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter) { // Enter 主按钮
        if (primary_ != nullptr) {
            primary_->click();
        }
        return;
    }
    QWidget::keyPressEvent(ev);
}

// ===================================================================== Drawer

Drawer::Drawer(const QString& title, QWidget* parent) : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint) {
    SetKind(this, "drawer");
    setProperty("shineDrawer", true); // 无 Q_OBJECT，兄弟查找走属性标记
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFixedWidth(380); // 右侧 380px（UI.md §2.1）

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(16, 12, 16, 12);
    col->setSpacing(8);

    auto* row = new QHBoxLayout();
    auto* t = new QLabel(title, this);
    SetKind(t, "dialogtitle");
    SetSemibold(t, true);
    row->addWidget(t, 1);
    auto* closeBtn = new IconButton(QStringLiteral("×"), QStringLiteral("关闭"), IconButton::Size::Sm, this);
    row->addWidget(closeBtn);
    connect(closeBtn, &IconButton::clicked, this, &Drawer::CloseDrawer);
    col->addLayout(row);

    body_ = new QVBoxLayout();
    body_->setSpacing(8);
    col->addLayout(body_);
}

QVBoxLayout* Drawer::BodyLayout() { return body_; }

void Drawer::Open() {
    // 叠加：打开时下移已开的兄弟抽屉
    const QRect ag = screen()->availableGeometry();
    int slot = 0;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w != this && w->property("shineDrawer").toBool() && w->isVisible()) {
            ++slot;
        }
    }
    const int h = std::min(720, ag.height() - 80);
    setFixedHeight(h);
    const QPoint to{ag.right() - width() + 1 - slot * 24, ag.top() + 40 + slot * 24};
    show();
    raise();
    if (motion::ReduceMotion()) {
        move(to);
        return;
    }
    motion::Transition::SlideIn(this, to + QPoint(width(), 0), theme::motion::kDurBaseMs); // 滑入 motion.base
}

void Drawer::CloseDrawer() {
    if (motion::ReduceMotion()) {
        close();
        return;
    }
    // 滑出：Tween pos（motion.exit）
    auto* tween = new motion::Tween{theme::motion::kExit, this};
    const QPoint from = pos();
    tween->Run(from, from + QPoint(width(), 0), theme::motion::kDurFastMs,
               [this](const QVariant& v) { move(v.toPoint()); });
    connect(tween, &QVariantAnimation::finished, this, &QWidget::close);
}

// ====================================================================== Toast

void Toast::Show(const QString& text, Tone tone) {
    auto& stack = ToastStack();
    while (stack.size() >= 3) { // 可堆叠 3 条
        QWidget* old = stack.front();
        stack.pop_front();
        old->close();
        old->deleteLater();
    }

    auto* w = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    SetKind(w, "toast");
    const char* toneName = tone == Tone::Info        ? "info"
                           : tone == Tone::Success   ? "success"
                           : tone == Tone::Warning   ? "warning"
                                                     : "error";
    w->setProperty("tone", QString::fromLatin1(toneName));

    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(10, 8, 10, 8);
    row->setSpacing(8);
    auto* icon = new QLabel(tone == Tone::Success ? QStringLiteral("✔")
                           : tone == Tone::Warning ? QStringLiteral("!")
                           : tone == Tone::Error   ? QStringLiteral("✕")
                                                   : QStringLiteral("ℹ"),
                            w);
    SetKind(icon, "toasticon");
    icon->setProperty("tone", QString::fromLatin1(toneName));
    row->addWidget(icon);
    auto* label = new QLabel(text, w);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("background: transparent;"));
    row->addWidget(label, 1);

    w->adjustSize();
    w->setFixedWidth(std::max(240, w->sizeHint().width()));
    w->show();
    LayoutBottomRight(w);
    stack.push_back(w); // 新条目在最上（同位叠放，UI.md 只要求可堆叠 ≤3）

    const int ms = tone == Tone::Error ? 8000 : 4000; // error 8s 其余 4s
    QTimer::singleShot(ms, w, [w, &stack] {
        auto it = std::find(stack.begin(), stack.end(), w);
        if (it != stack.end()) {
            stack.erase(it);
        }
        w->close();
        w->deleteLater();
    });
}

// ==================================================================== Tooltip

namespace {

class TooltipFilter : public QObject {
  public:
    TooltipFilter(QObject* host, QString rich, QString shortcut)
        : QObject(host), rich_(std::move(rich)), shortcut_(std::move(shortcut)) {}

  protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        switch (ev->type()) {
            case QEvent::Enter:
                timer_.start(200); // 200ms 延迟（UI.md §2.1）
                break;
            case QEvent::Leave:
                timer_.stop();
                HideTip();
                break;
            case QEvent::MouseButtonPress:
                timer_.stop();
                HideTip();
                break;
            default:
                break;
        }
        return QObject::eventFilter(obj, ev);
    }

  private:
    void HideTip() {
        if (tip_ != nullptr) {
            tip_->close();
            tip_->deleteLater();
            tip_ = nullptr;
        }
    }

    void ShowTip() {
        QWidget* host = qobject_cast<QWidget*>(parent());
        if (host == nullptr) {
            return;
        }
        auto* tip = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
        SetKind(tip, "tooltip");
        tip->setAttribute(Qt::WA_DeleteOnClose);
        auto* row = new QHBoxLayout(tip);
        row->setContentsMargins(8, 4, 8, 4);
        row->setSpacing(6);
        auto* text = new QLabel(rich_, tip); // 富文本可写 <b>
        text->setTextFormat(Qt::RichText);
        text->setStyleSheet(QStringLiteral("background: transparent;"));
        row->addWidget(text);
        if (!shortcut_.isEmpty()) {
            row->addWidget(new Kbd(shortcut_, tip)); // 带快捷键
        }
        tip->adjustSize();
        tip->move(QCursor::pos() + QPoint(0, 20));
        tip->show();
        tip_ = tip;
    }

    QString rich_;
    QString shortcut_;
    QTimer timer_;
    QWidget* tip_ = nullptr;

  public:
    void Arm() {
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, &TooltipFilter::ShowTip);
    }
};

} // namespace

void Tooltip::Attach(QWidget* host, const QString& richText, const QString& shortcutText) {
    if (host == nullptr) {
        return;
    }
    auto* filter = new TooltipFilter(host, richText, shortcutText);
    filter->Arm();
    host->installEventFilter(filter);
}

// =============================================================== SectionCard

SectionCard::SectionCard(const QString& title, QWidget* parent) : QFrame(parent) {
    SetKind(this, "sectioncard");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // ── 标题栏：整行可点，折叠/展开 ──
    head_ = new QPushButton(this);
    head_->setFlat(true);
    head_->setCheckable(true);
    head_->setCursor(Qt::PointingHandCursor);
    head_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    head_->setToolTip(QStringLiteral("点击折叠/展开本块"));
    SetKind(head_, "sectionhead");
    auto* headRow = new QHBoxLayout(head_);
    headRow->setContentsMargins(theme::space::kSteps[4], theme::space::kSteps[2],
                                theme::space::kSteps[4], theme::space::kSteps[2]);
    headRow->setSpacing(theme::space::kSteps[2]);
    chevron_ = new QLabel(head_);
    SetKind(chevron_, "sectionchevron");
    headRow->addWidget(chevron_);
    title_ = new QLabel(title, head_);
    SetKind(title_, "sectiontitle");
    SetSemibold(title_, true);
    headRow->addWidget(title_, 1);
    subtitle_ = new ElidedLabel(QString{}, head_);
    SetKind(subtitle_, "sectionsub");
    subtitle_->SetExpandable(false); // 纯展示位：只省略 + hover 全文，不抢点击折叠
    subtitle_->hide();
    headRow->addWidget(subtitle_, 0);
    col->addWidget(head_);

    connect(head_, &QPushButton::clicked, this, [this] {
        if (collapsible_) {
            SetExpanded(!expanded_);
        }
    });

    body_ = new QWidget(this);

    // ── 内容区 ──
    // 布局交给 body_ 的构造器安装；事后再 setLayout() 会触发
    // "QLayout: Attempting to add QLayout to QWidget which already has a layout"。
    body_lay_ = new QVBoxLayout(body_);
    body_lay_->setContentsMargins(theme::space::kSteps[4], theme::space::kSteps[3],
                                  theme::space::kSteps[4], theme::space::kSteps[4]);
    body_lay_->setSpacing(theme::space::kSteps[2]);
    col->addWidget(body_);

    ApplyExpanded();
}

QVBoxLayout* SectionCard::BodyLayout() const { return body_lay_; }

void SectionCard::SetSubtitle(const QString& text) {
    subtitle_->SetFullText(text);
    subtitle_->setVisible(!text.isEmpty());
}

void SectionCard::SetTitle(const QString& text) {
    title_->setText(text);
    head_->setToolTip(QStringLiteral("%1\n点击折叠/展开本块").arg(text));
}

void SectionCard::SetContentMinWidth(int px) {
    if (body_ != nullptr) {
        body_->setMinimumWidth(px);
    }
}

void SectionCard::SetCollapsible(bool on) {
    collapsible_ = on;
    head_->setEnabled(true);
    ApplyExpanded();
}

void SectionCard::SetExpanded(bool on) {
    if (expanded_ == on) {
        return;
    }
    expanded_ = on;
    ApplyExpanded();
}

void SectionCard::ApplyExpanded() {
    chevron_->setText(collapsible_ ? (expanded_ ? QStringLiteral("▾") : QStringLiteral("▸"))
                                   : QString{});
    chevron_->setVisible(collapsible_);
    body_->setVisible(expanded_);
    head_->setChecked(collapsible_ && expanded_);
}

} // namespace shine::widgets
