#pragma once
// 小说工程列表：根目录下每个子目录 = 一本小说（含 novel.db）
#include <filesystem>
#include <string>
#include <vector>

namespace shine::novelcore {

struct ProjectInfo {
    std::string name;
    std::filesystem::path dir;
    std::filesystem::path dbPath;
    bool hasDb = false;
};

// 根目录：Settings().novelRootDir，空则 %APPDATA%\ShineTVStudio\novels
[[nodiscard]] std::filesystem::path RootDir();

// 刷新扫描
void Refresh();
[[nodiscard]] const std::vector<ProjectInfo>& Items();

// 创建目录 + 初始化 novel.db；失败返回错误文案
[[nodiscard]] std::string CreateProject(std::string_view name);

} // namespace shine::novelcore
