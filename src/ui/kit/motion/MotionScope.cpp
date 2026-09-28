#include "ui/kit/motion/MotionScope.h"

#include "ui/kit/motion/Tween.h"

namespace shine::motion {

MotionScope::~MotionScope() {
    for (Tween* t : owned_) {
        if (t == nullptr) {
            continue;
        }
        t->Settle(); // 先落终态（保证视觉一致），再回收
        t->stop();
        t->deleteLater();
    }
    owned_.clear();
}

void MotionScope::Adopt(Tween* t) {
    if (t != nullptr) {
        owned_.push_back(t);
    }
}

std::size_t MotionScope::Count() const { return owned_.size(); }

} // namespace shine::motion
