#pragma once
// P02-S7 判据工具（SHINE_THUMB_BENCH=1 / SHINE_VIEWER_SELFTEST=1）：
//   ThumbBench      —— 2 万张缩略图滚动基准 + 「不闪白」像素断言
//   ViewerSelfTest  —— ImageViewer 0.1–16× 锚点缩放正确性（锚点场景点漂移量测）
#include <string>

namespace shine::gallery {

[[nodiscard]] std::string ThumbBench();
[[nodiscard]] std::string ViewerSelfTest();

} // namespace shine::gallery
