#include "widget/motion/Tween.h"

#include "widget/motion/Easing.h"

#include <unordered_set>
#include <utility>

namespace shine::motion {
namespace {

std::unordered_set<Tween*>& ActiveSet() {
    static std::unordered_set<Tween*> s;
    return s;
}

} // namespace

Tween::Tween(const theme::motion::Ease& ease, QObject* parent)
    : QVariantAnimation(parent), ease_(ease) {
    // 连接只做一次（Qt::UniqueConnection 对 lambda 无效）
    connect(this, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) {
                if (setter_) {
                    setter_(v);
                }
            });
    connect(this, &QVariantAnimation::finished, this, [this] {
        if (active_) {
            ActiveSet().erase(this);
            active_ = false;
        }
    });
}

Tween::~Tween() {
    if (active_) {
        ActiveSet().erase(this);
        active_ = false;
    }
}

int Tween::ActiveCount() { return static_cast<int>(ActiveSet().size()); }

QVariant Tween::interpolated(const QVariant& from, const QVariant& to, double progress) const {
    return QVariantAnimation::interpolated(from, to, CubicBezierY(ease_, progress));
}

void Tween::Run(const QVariant& from, const QVariant& to, int durationMs, Setter setter) {
    setter_ = std::move(setter);
    end_ = to;

    // 瞬时条件：减少动效 / 非法时长 / 同屏动画满员（风险表 R7）
    if (ReduceMotion() || durationMs <= 0 || ActiveCount() >= kMaxConcurrent) {
        if (setter_) {
            setter_(end_);
        }
        return;
    }

    if (!active_) {
        ActiveSet().insert(this);
        active_ = true;
    }
    setStartValue(from);
    setEndValue(to);
    setDuration(durationMs); // 时长恒来自 motion.dur.* token（调用方传 token）
    start();
}

void Tween::Settle() {
    stop();
    if (active_) {
        ActiveSet().erase(this);
        active_ = false;
    }
    if (setter_) {
        setter_(end_);
    }
}

} // namespace shine::motion
