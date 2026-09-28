#pragma once
// P06-S9：生成 build/_shots/P06/ 全套分镜工作区截图与 manifest。
#include <filesystem>

namespace shine::app {

void SaveP06Review(const std::filesystem::path& dir);

} // namespace shine::app
