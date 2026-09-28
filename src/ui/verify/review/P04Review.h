#pragma once
#include <string>

namespace shine::app {

// P04-S11：生成 build/_shots/P04/ 全套小说工作区截图与 shots-manifest.txt。
// 评审批次由 SHINE_P04_REVIEW 指定；函数在截图链完成后退出进程。
void SaveP04Review(const std::string& dirUtf8);

} // namespace shine::app
