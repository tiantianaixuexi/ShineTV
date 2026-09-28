#pragma once
// shine::motion::Easing —— 动效曲线 + 「减少动效」全局开关（P02-S4）
//
// 判据（S4）：时长与缓动全部从 motion.* token 取（widget/theme/Token.h，总纲 §2.3）；
// 「减少动效」打开后全部动效退化为瞬时（P02 §6-4：任何动画不再出现）。
#include "widget/theme/Token.h"

#include <string>

namespace shine::motion {

// 3 条曲线 = 总纲 §2.3 的 motion.ease.* token
enum class Curve { Standard, Emphasized, Exit };
[[nodiscard]] const theme::motion::Ease& CurveOf(Curve c);

// 三次贝塞尔求值：进度 x∈[0,1] → 缓动后 y（牛顿迭代解 t，二分兜底）
[[nodiscard]] double CubicBezierY(const theme::motion::Ease& ease, double x);

// 「减少动效」全局开关（S8 样式编辑器持久化；SHINE_REDUCE_MOTION=1 可强制打开）
void SetReduceMotion(bool on);
[[nodiscard]] bool ReduceMotion();
void InitializeMotion(); // 读环境变量（QApplication 构造后调用一次）

// 同屏动画上限（P02 风险表 R7）：满员后新动效直接瞬时完成
inline constexpr int kMaxConcurrent = 8;

// S4 自检（SHINE_MOTION_SELFTEST=1）：曲线端点 / 时长 token / 减少动效瞬时 / MotionScope 回收
[[nodiscard]] bool SelfTest(std::string* report);

} // namespace shine::motion
