#include "core/Settings.h"
#include "util/File.h"
#include "util/Encoding.h"
#include "core/Log.h"
#include "util/Reflect.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace shine {
namespace {

AppSettings g_settings;

std::wstring GetAppDataDir() {
    wchar_t* path = nullptr;
    // Expand %APPDATA%
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : L".";
    std::wstring dir = base + L"\\ShineTVStudio";
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

AppSettings& Settings() { return g_settings; }

// ⚠️ 曾经是 `std::string(w.begin(), w.end())`（**逐字符截断**）：`%APPDATA%` 含中文时路径直接报废。
// 现在读写一律用 `std::filesystem::path`（宽字符 → `_wfopen`），对外给 UTF-8 字符串。
std::filesystem::path SettingsFilePath() { return std::filesystem::path{GetAppDataDir()} / L"settings.json"; }

std::string SettingsPath() { return util::PathToUtf8(SettingsFilePath()); }

void LoadSettings() {
    std::ifstream in = util::OpenInput(SettingsFilePath());
    if (!in) {
        log::Info("No settings file, using defaults");
        return;
    }
    std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (content.empty()) {
        log::Warn("settings.json 为空，使用默认值");
        return;
    }
    // 字段名 ↔ JSON 键由 C++26 静态反射自动映射（util/Reflect.h，见 Doc/RULES-LANG.md §13.6）
    // 宽容语义：键缺失或类型不符 → 保留结构体里的默认值
    const std::size_t hit = util::reflect::FromJsonString(content, g_settings);
    if (hit == 0) {
        log::Warn("settings.json 解析失败或无可识别字段，使用默认值");
        return;
    }
    log::Info("Settings loaded: theme={}（命中 {} 个字段）", g_settings.themeId, hit);
}

void SaveSettings() {
    // firstRun 只服务于首启引导：一旦落盘就置 false（并写回）
    g_settings.firstRun = false;

    const std::string json = util::reflect::ToJsonString(g_settings);
    if (json.empty()) {
        log::Error("settings.json 序列化失败，未写入");
        return;
    }
    std::ofstream out = util::OpenOutput(SettingsFilePath());
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    log::Info("Settings saved（{} 字节，{} 个字段）", json.size(), util::reflect::FieldCount<AppSettings>());
}

} // namespace shine
