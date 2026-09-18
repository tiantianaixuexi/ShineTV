#pragma once
// shine::gallery::FileActions —— 图库文件动作（G-S11 S1）
//
// 剪贴板/资源管理器/回收站。删除到回收站**不做确认**（调用方弹窗后才调）。
#include <filesystem>
#include <string>
#include <vector>

namespace shine::gallery::file_actions {

// 剪贴板写 UTF-8 文本（UI 线程）
void CopyText(std::string_view utf8);
void CopyPaths(const std::vector<std::filesystem::path>& paths); // 每行一个 UTF-8 路径
void CopyNames(const std::vector<std::filesystem::path>& paths); // 每行一个文件名

// 资源管理器中定位（多选时定位第一个）
[[nodiscard]] bool RevealInExplorer(const std::filesystem::path& path, std::string* error = nullptr);

// 删除到回收站（FOF_ALLOWUNDO）。**调用前必须已二次确认**。
[[nodiscard]] bool Recycle(const std::vector<std::filesystem::path>& paths, std::string* error = nullptr);

} // namespace shine::gallery::file_actions
