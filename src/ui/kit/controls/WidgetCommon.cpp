#include "ui/kit/controls/WidgetCommon.h"

#include "ui/kit/theme/CssColor.h"

#include <QEvent>
#include <QStyle>

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

} // namespace shine::widgets
