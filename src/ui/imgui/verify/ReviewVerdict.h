#pragma once
// shine::imguiverify::detail —— manifest 收尾与 overall 判定
//
// 这一族是**唯一**能决定 overall 的地方。它读的是各族判据在 Session 上留下的计数，
// 不重新推断、不二次抓图：
//   * 判据二：同一工作区在 5 套主题下的像素两两不同（主题真的切了没有）；
//   * 判据三：受控图之间不许出现逐字节相同的两张（每张都拍到了它承诺的状态）；
//   * 判据四：全程不得出现反向矩形 / 同帧重叠热区（画得出与点得动）；
//   * 各族探针的分数与四条 worker 等待结论，外加抓图失败与滚动未到位。
//
// ⚠️ 收尾三行的**顺序**是脚本契约（scripts/run_reviews.ps1:56/61/66）：
//    `# shots: …` → `last-inverted-rect=…`（仅在有反向矩形时）→ `overall=PASS|FAIL`。
//    字段名、顺序、数值格式一个都不能动，脚本按正则读它们。
//
// ⚠️ 判据的结论**原样**交给调用方（Session::result.pass）。不许调用方拿
//    captured / failed 二次推算退出码 —— 那正是 Review.h 里记下来的那次假绿。

#include "ui/imgui/verify/ReviewSession.h"

namespace shine::imguiverify::detail {

// 判据二/三/四 → overall 判定 → 写收尾三行 → 打 verdict 日志。
// 之后由 Review.cpp 返回 Session::result。
void FinishVerdict(Session& session);

} // namespace shine::imguiverify::detail
