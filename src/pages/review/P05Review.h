#pragma once
#include <string>

namespace shine::app {

// P05-S9：生成资产工作区截图包与 manifest；由 SHINE_P05_REVIEW 触发。
void SaveP05Review(const std::string& dirUtf8);

} // namespace shine::app
