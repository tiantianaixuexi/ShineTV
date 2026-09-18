#include "novel/NovelProjects.h"

#include <fmt/format.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "core/Log.h"
#include "core/Settings.h"
#include "db/sqlite/SqliteDb.h"
#include "novel/NovelDb.h"
#include "util/Encoding.h"
#include "util/File.h"

namespace shine::novelcore {
namespace {

std::vector<ProjectInfo> g_items;

[[nodiscard]] bool IsValidName(std::string_view name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (const char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

} // namespace

std::filesystem::path RootDir() {
    const auto& s = Settings().novelRootDir;
    if (!s.empty()) {
        return util::PathFromUtf8(s);
    }
    // 默认：%APPDATA%\ShineTVStudio\novels
    wchar_t appdata[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return std::filesystem::path{L"novels"};
    }
    return std::filesystem::path{appdata} / L"ShineTVStudio" / L"novels";
}

void Refresh() {
    g_items.clear();
    const auto root = RootDir();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (!std::filesystem::is_directory(root, ec)) {
        log::Warn("novel root not a dir: {}", util::PathToUtf8(root));
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_directory()) {
            continue;
        }
        ProjectInfo info;
        info.dir = entry.path();
        info.name = util::PathToUtf8(entry.path().filename());
        info.dbPath = entry.path() / "novel.db";
        info.hasDb = std::filesystem::exists(info.dbPath, ec);
        g_items.push_back(std::move(info));
    }
    log::Info("novel projects scanned: {} (root={})", g_items.size(), util::PathToUtf8(root));
}

const std::vector<ProjectInfo>& Items() {
    return g_items;
}

std::string CreateProject(std::string_view name) {
    if (!IsValidName(name)) {
        return "书名不能为空，且不能包含 / \\ : * ? \" < > |";
    }
    const auto root = RootDir();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    const auto dir = root / util::PathFromUtf8(std::string{name});
    if (std::filesystem::exists(dir, ec)) {
        return "已存在同名小说工程";
    }
    if (!std::filesystem::create_directories(dir, ec) && ec) {
        return fmt::format("创建目录失败: {}", ec.message());
    }

    // schema v3 由 NovelDb 迁移负责（建全表 + schemaVersion=3）
    if (auto r = NovelDb::Instance().Open(dir / "novel.db"); !r) {
        return fmt::format("初始化 novel.db 失败: {}", r.error().message);
    }
    NovelDb::Instance().Close();
    Refresh();
    log::Info("novel project created: {}", util::PathToUtf8(dir));
    return {};
}

} // namespace shine::novelcore
