#pragma once
// shine::motion::Transition —— 控件级常用过场（P02-S4）
//
// 页面零散动画归零：入场/出场/位移只准走这里；时长默认取 motion.dur.* token，
// 「减少动效」时 Tween 自动瞬时完成（fade 也不播）。
#include "ui/kit/motion/Tween.h"
#include "ui/kit/theme/Token.h"

#include <QPoint>

class QWidget;

namespace shine::motion {

class Transition {
  public:
    // 淡入（显示 + 透明度 0→1）；返回可托管给 MotionScope 的 Tween
    [[nodiscard]] static Tween* FadeIn(QWidget* w, int durationMs = theme::motion::kDurBaseMs);
    // 淡出（透明度 1→0，完成后隐藏）
    [[nodiscard]] static Tween* FadeOut(QWidget* w, int durationMs = theme::motion::kDurFastMs);
    // 位移入场（from → 当前位置）
    [[nodiscard]] static Tween* SlideIn(QWidget* w, const QPoint& from,
                                        int durationMs = theme::motion::kDurSlowMs);
};

} // namespace shine::motion
