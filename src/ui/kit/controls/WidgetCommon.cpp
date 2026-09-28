#include "ui/kit/controls/WidgetCommon.h"

#include "ui/kit/theme/CssColor.h"
#include "ui/kit/theme/Theme.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHash>
#include <QMouseEvent>
#include <QStyle>

#include <algorithm>
#include <functional>

namespace shine::widgets {

QColor TokenQColor(std::uint32_t rgba) {
    return QColor{static_cast<int>((rgba >> 24) & 0xFF), static_cast<int>((rgba >> 16) & 0xFF),
                  static_cast<int>((rgba >> 8) & 0xFF), static_cast<int>(rgba & 0xFF)};
}

void Repolish(QWidget* w) {
    if (w == nullptr) {
        return;
    }
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

void SetForcedState(QWidget* w, State s) {
    if (w == nullptr) {
        return;
    }
    const char* v = "";
    switch (s) {
        case State::Hover:
            v = "hover";
            break;
        case State::Pressed:
            v = "pressed";
            break;
        case State::Focus:
            v = "focus";
            break;
        case State::Normal:
        case State::Disabled:
        default:
            v = "";
            break;
    }
    const QString sv = s == State::Disabled ? QStringLiteral("disabled") : QString::fromLatin1(v);
    w->setEnabled(s != State::Disabled); // 禁用天然级联
    // shineState 向后代传播：复合件（TextInput 等）内层控件也要进入对应态
    std::function<void(QWidget*)> apply = [&](QWidget* x) {
        x->setProperty("shineState", sv);
        Repolish(x);
        for (QObject* c : x->children()) {
            if (auto* cw = qobject_cast<QWidget*>(c)) {
                apply(cw);
            }
        }
    };
    apply(w);
}

void SetKind(QWidget* w, const char* kind) {
    if (w != nullptr) {
        w->setProperty("shineKind", QString::fromLatin1(kind));
        Repolish(w);
    }
}

void SetVariant(QWidget* w, const char* variant) {
    if (w != nullptr) {
        w->setProperty("shineVariant", QString::fromLatin1(variant));
        Repolish(w);
    }
}

void SetSizeAttr(QWidget* w, const char* size) {
    if (w != nullptr) {
        w->setProperty("shineSize", QString::fromLatin1(size));
        Repolish(w);
    }
}

void SetSemibold(QWidget* w, bool on) {
    if (w != nullptr) {
        w->setProperty("shineWeight", on ? QStringLiteral("semibold") : QString{});
        Repolish(w);
    }
}

void SetTextColor(QWidget* w, std::uint32_t token) {
    if (w != nullptr) {
        w->setStyleSheet(QStringLiteral("color:%1;").arg(shine::widget::CssRgb(token)));
    }
}

QLabel* SectionTitle(const QString& text, QWidget* parent, bool semibold) {
    auto* label = new QLabel(text, parent);
    SetKind(label, "statetitle");
    if (semibold) {
        SetSemibold(label, true);
    }
    return label;
}

// ============================== ElidedLabel ==============================

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent) : QLabel(parent) {
    full_ = text;
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setToolTip(text);
    if (expandable_) {
        setCursor(Qt::PointingHandCursor);
    }
    Recompute();
}

void ElidedLabel::SetFullText(const QString& full) {
    full_ = full;
    setToolTip(full);
    Recompute();
}

void ElidedLabel::SetExpanded(bool on) {
    if (!expandable_ || expanded_ == on) {
        return;
    }
    expanded_ = on;
    Recompute();
    updateGeometry();
    update();
}

void ElidedLabel::Refresh() { Recompute(); }

void ElidedLabel::Recompute() {
    if (expanded_) {
        setWordWrap(true);
        QLabel::setText(full_);
        setMinimumWidth(0);
        setMinimumHeight(0); // 高度交给 wordWrap 的 sizeHint
        updateGeometry();
        return;
    }
    setWordWrap(false);
    const int avail = std::max(0, width() - 2);
    shown_ = fontMetrics().elidedText(full_, Qt::ElideRight, avail);
    QLabel::setText(shown_);
    setToolTip(full_); // 省略时 hover 出全文
}

void ElidedLabel::resizeEvent(QResizeEvent* ev) {
    QLabel::resizeEvent(ev);
    Recompute();
}

void ElidedLabel::mouseReleaseEvent(QMouseEvent* ev) {
    if (expandable_ && ev->button() == Qt::LeftButton && rect().contains(ev->position().toPoint())) {
        SetExpanded(!expanded_);
        ev->accept();
        return;
    }
    QLabel::mouseReleaseEvent(ev);
}

// ================================================================ 阴影实现
namespace {

// 阴影的几何参数，逐条对齐 webui/src/styles/tokens.css：
//   --shadow-1      = 0 1px 2px rgba(0,0,0,.35), 0 4px 16px rgba(0,0,0,.30)
//   --shadow-2      = 0 12px 40px rgba(0,0,0,.45)
//   --shadow-accent = 0 4px 20px rgba(accent,.28)
// 一条 CSS box-shadow 能叠多层，QGraphicsDropShadowEffect 只出一层，
// 这里取「扩散占主导」的那一层（shadow-1 取第二层的 16px）。
struct ShadowSpec {
    int offsetX;
    int offsetY;
    int blurRadius;
};

constexpr ShadowSpec kShadowSm{0, 1, 16};
constexpr ShadowSpec kShadowLg{0, 12, 40};
constexpr ShadowSpec kShadowAccent{0, 4, 20};

[[nodiscard]] QColor ShadowColorOf(ShadowLevel level) {
    const theme::ColorToken& c = theme::Current();
    switch (level) {
        case ShadowLevel::Sm:
            return TokenQColor(c.shadow1);
        case ShadowLevel::Lg:
            return TokenQColor(c.shadow2);
        case ShadowLevel::Accent:
            return TokenQColor(c.shadowAccent);
        case ShadowLevel::None:
        default:
            return QColor{};
    }
}

[[nodiscard]] ShadowSpec ShadowGeomOf(ShadowLevel level) {
    switch (level) {
        case ShadowLevel::Sm:
            return kShadowSm;
        case ShadowLevel::Lg:
            return kShadowLg;
        case ShadowLevel::Accent:
            return kShadowAccent;
        case ShadowLevel::None:
        default:
            return {0, 0, 0};
    }
}

// 主题切换时重挂阴影：ThemeService 是纯静态类没有信号，这里靠 QApplication
// 每次换肤会发的 ThemeChange 事件（见 ApplyQss 里的 QEvent::ThemeChange）。
// 记住每个控件的档位，换肤后按新主题色重挂一次。
QHash<QWidget*, ShadowLevel>& ShadowRegistry() {
    static QHash<QWidget*, ShadowLevel> registry;
    return registry;
}

class ShadowRefresher : public QObject {
  public:
    explicit ShadowRefresher(QObject* parent = nullptr) : QObject(parent) {}

  protected:
    bool eventFilter(QObject* watched, QEvent* ev) override {
        if (ev->type() == QEvent::ThemeChange && watched == qApp) {
            for (auto it = ShadowRegistry().begin(); it != ShadowRegistry().end(); ++it) {
                ApplyShadow(it.key(), it.value());
            }
        }
        return QObject::eventFilter(watched, ev);
    }
};

ShadowRefresher& Refresher() {
    static ShadowRefresher refresher;
    return refresher;
}

} // namespace

void ApplyShadow(QWidget* w, ShadowLevel level) {
    if (w == nullptr) {
        return;
    }
    if (level == ShadowLevel::None) {
        w->setGraphicsEffect(nullptr);
        ShadowRegistry().remove(w);
        return;
    }
    // 一个控件只能挂一个 effect：先摘掉旧的再挂新的，否则 setGraphicsEffect
    // 会让先前那个失效（Qt 只允许一个）。
    if (auto* old = qobject_cast<QGraphicsDropShadowEffect*>(w->graphicsEffect())) {
        old->deleteLater();
    }
    const ShadowSpec spec = ShadowGeomOf(level);
    auto* effect = new QGraphicsDropShadowEffect(w);
    effect->setColor(ShadowColorOf(level));
    effect->setBlurRadius(spec.blurRadius);
    effect->setOffset(spec.offsetX, spec.offsetY);
    w->setGraphicsEffect(effect);
    ShadowRegistry()[w] = level;

    // 订阅 ThemeChange（首次调用时装一次即可）
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        Refresher().installEventFilter(qApp);
    }
}

void ApplyShadowOnThemeChange(QWidget* w, ShadowLevel level) {
    ApplyShadow(w, level);
}

} // namespace shine::widgets
