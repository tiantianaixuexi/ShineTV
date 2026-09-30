#pragma once
// shine::imguiverify::detail —— 场景脚本：按既定顺序把状态摆好、逐张抓图
//
// 两个入口，顺序由 Review.cpp 的 RunReview 决定：
//   * RunCoverageSweep —— 静息态覆盖：当前主题下 7 个工作区各一张（第一段），
//     再加 6 个业务工作区 × 5 套主题（第二段，phases.md P6.3 的 30 组）。
//   * RunControlledStates —— 剩下的全部受控图：把工程打开、页签/条目/浮层一个个
//     显式摆到它承诺的状态再拍（第〇段未命中前的第三~第八段）。
//
// ⚠️ 这一族**只负责摆状态与抓图**，不负责判「摆对了没有」—— 那是
//    ReviewVerdict.cpp 的事。等待 worker 回投的结论既进 manifest 也进 overall，
//    但结论在哪儿读、怎么合，判据文件说了算。
//
// ⚠️ 段内的编号（第三段/第四段/…）是取证日志里的原编号，**故意保留**：
//    出问题时要按编号对着历史截图与 manifest 讨论。文件的物理顺序和编号顺序
//    曾经不一致过（第六段的代码排在第七段前面），拆文件时不要顺手"整理"编号。

#include "ui/imgui/verify/ReviewSession.h"

namespace shine::imguiverify::detail {

// 第一段 + 第二段：静息态覆盖扫描。跑完把主题还原成判据开始前那套。
void RunCoverageSweep(Session& session);

// 第三~第八段 + toast：受控状态取证。fixture 造不出来时整段跳过。
void RunControlledStates(Session& session);

} // namespace shine::imguiverify::detail
