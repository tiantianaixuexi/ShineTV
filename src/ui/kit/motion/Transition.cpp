#include "ui/kit/motion/Transition.h"

#include "ui/kit/motion/Easing.h"

#include <QGraphicsOpacityEffect>
#include <QVariant>
#include <QWidget>

namespace shine::motion {

Tween* Transition::FadeIn(QWidget* w, int durationMs) {
    if (w == nullptr) {
        return nullptr;
    }
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    if (effect == nullptr) {
        effect = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(effect);
    }
    w->show();
    effect->setOpacity(ReduceMotion() ? 1.0 : 0.0);
    auto* tween = new Tween{theme::motion::kStandard, w};
    tween->Run(ReduceMotion() ? 1.0 : 0.0, 1.0, durationMs,
               [effect](const QVariant& v) { effect->setOpacity(v.toDouble()); });
    return tween;
}

Tween* Transition::FadeOut(QWidget* w, int durationMs) {
    if (w == nullptr) {
        return nullptr;
    }
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    if (effect == nullptr) {
        effect = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(effect);
    }
    auto* tween = new Tween{theme::motion::kExit, w};
    const double from = effect->opacity();
    tween->Run(from, 0.0, durationMs, [effect](const QVariant& v) { effect->setOpacity(v.toDouble()); });
    if (ReduceMotion()) {
        w->hide(); // 瞬时：直接隐藏，不留动画
    } else {
        QObject::connect(tween, &QVariantAnimation::finished, w, [w] { w->hide(); });
    }
    return tween;
}

Tween* Transition::SlideIn(QWidget* w, const QPoint& from, int durationMs) {
    if (w == nullptr) {
        return nullptr;
    }
    const QPoint target = w->pos();
    auto* tween = new Tween{theme::motion::kEmphasized, w};
    if (ReduceMotion()) {
        w->move(target);
        return tween;
    }
    w->move(from);
    tween->Run(from, target, durationMs, [w](const QVariant& v) { w->move(v.toPoint()); });
    return tween;
}

} // namespace shine::motion
