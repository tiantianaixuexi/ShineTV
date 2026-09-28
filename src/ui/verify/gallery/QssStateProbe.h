#pragma once
// QssStateProbe —— QSS 动态属性态强制机制探针（P02-S5 诊断件，SHINE_QSS_PROBE=1）。
// 用同一套 QssBuilder 生成的样式做 A/B/C 三种属性设置时机实验，逐个输出脸行像素色，
// 定位 [shineState="..."] 选择器为何（不）匹配。零依赖诊断，永久保留作回归哨兵。
#include <string>

namespace shine::gallery {

[[nodiscard]] std::string QssStateProbe();

// P02-S6 判据基准：万行 DataTable 滚动流畅性（SHINE_TABLE_BENCH=1）。
// 10 000 行 × 随机滚动 200 帧计时；<2000ms 记 PASS。
[[nodiscard]] std::string TableBench();

} // namespace shine::gallery
