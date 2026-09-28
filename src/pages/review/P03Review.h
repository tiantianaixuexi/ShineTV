#pragma once
// shine::app —— P03-S11 多模态视觉评审截图包（UI.md §4 清单）。
// 一次跑完产出：hub-empty / hub-normal / wizard-step1..4 / workshop-empty /
// workshop-1280x720 / workshop-2560x1440 / palette-open / theme-dark / theme-light /
// firstrun 共 12 张 + shots-manifest.txt（文件名+字节数，取证用）。
// 约定（P02 取证教训）：目标目录先建、抓取前 repaint、报告落文件 + fflush。
#include <string>

namespace shine::app {

void SaveP03Review(const std::string& dirUtf8);

} // namespace shine::app
