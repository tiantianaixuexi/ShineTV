#pragma once
// shine::app —— Win32 文件对话框（IFileOpenDialog / IFileSaveDialog）
// 说明：只做「选一个文件 / 选一个目录」这一件事，返回 **UTF-8** 路径；取消返回空串。
// shell32 / ole32 / uuid 已在 CMake 里链接，无需额外库。
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shine::app {

using FileFilter = std::pair<std::string, std::string>; // {显示名, "*.json;*.txt"}

[[nodiscard]] std::string OpenFileDialog(std::string_view title, const std::vector<FileFilter>& filters,
                                        std::string_view initialDir = {});
[[nodiscard]] std::string SaveFileDialog(std::string_view title, std::string_view defaultName,
                                         const std::vector<FileFilter>& filters,
                                         std::string_view initialDir = {});
// G-S4/S5：选目录（FOS_PICKFOLDERS）；initialDir 为空则不定位
[[nodiscard]] std::string SelectFolderDialog(std::string_view title, std::string_view initialDir = {});

} // namespace shine::app
