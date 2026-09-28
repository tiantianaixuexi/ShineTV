#pragma once
// shine::motion::Tween —— 值补间（P02-S4）
//
// 时长/缓动只准来自 motion.* token；「减少动效」或同屏动画满 kMaxConcurrent 时
// 同步落到终值（瞬时），保证 P02 §6-4「任何动画不再出现」。
// 生命周期：不自删 —— 谁创建谁回收（Transition 挂父 + deleteLater；MotionScope 析构回收）。
#include "ui/kit/theme/Token.h"

#include <functional>

#include <QVariant>
#include <QVariantAnimation>

namespace shine::motion {

class Tween final : public QVariantAnimation {
  public:
    using Setter = std::function<void(const QVariant&)>;

    explicit Tween(const theme::motion::Ease& ease = theme::motion::kStandard, QObject* parent = nullptr);

    // 开始补间；instant 条件命中时同步把终值喂给 setter 后直接返回
    void Run(const QVariant& from, const QVariant& to, int durationMs, Setter setter);

    // 立即落到终值并停（MotionScope 析构 / 手动收尾用）
    void Settle();

    [[nodiscard]] static int ActiveCount();

    ~Tween() override;

  protected:
    // 进度先过贝塞尔缓动，再交给 Qt 按类型插值
    [[nodiscard]] QVariant interpolated(const QVariant& from, const QVariant& to,
                                        double progress) const override;

  private:
    theme::motion::Ease ease_;
    Setter setter_;
    QVariant end_;
    bool active_ = false;
};

} // namespace shine::motion
