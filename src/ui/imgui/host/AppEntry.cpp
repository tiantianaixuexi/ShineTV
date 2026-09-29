#include "ui/imgui/host/AppEntry.h"

#include "core/Async.h"
#include "core/Log.h"
#include "media/Gallery.h"
#include "mcp/MCPServer.h"
#include "novel/NovelCli.h"
#include "ui/imgui/host/AppEnvironment.h"
#include "ui/imgui/host/Host.h"
#include "ui/imgui/pages/Shell.h"
#include "ui/imgui/theme/Theme.h"
#include "ui/imgui/verify/Review.h"

#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW（须在 windows.h 之后）

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace shine::imguiapp {
namespace {

bool Contains(const std::wstring_view text, const wchar_t* needle) {
    return text.find(needle) != std::wstring_view::npos;
}

// 与 Qt 版 AppEntry.cpp:34-70 逐条同源：取证脚本靠这些环境变量把 APPDATA 重定向到
// 各自的沙箱目录，ImGui 版必须认全，否则取证会写到真实用户数据目录。
void ConfigureAppDataSandbox() {
    using shine::app::EnvironmentPath;
    const std::filesystem::path override_dir = EnvironmentPath(L"SHINE_APPDATA_OVERRIDE");
    if (!override_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", override_dir.c_str());
        return;
    }
    // 名字都是 SHINE_*_REVIEW / SHINE_P05_S7 / SHINE_P05_S8：重定向到 <dir>/_appdata。
    static constexpr const wchar_t* kReviewVars[] = {
        L"SHINE_P03_REVIEW", L"SHINE_P04_REVIEW", L"SHINE_P05_S7",     L"SHINE_P05_S8",
        L"SHINE_P05_REVIEW", L"SHINE_P06_REVIEW", L"SHINE_P07_REVIEW", L"SHINE_P08_REVIEW",
        L"SHINE_P09_REVIEW", L"SHINE_P10_REVIEW", L"SHINE_IMGUI_REVIEW",
    };
    for (const wchar_t* name : kReviewVars) {
        if (const std::filesystem::path dir = EnvironmentPath(name); !dir.empty()) {
            SetEnvironmentVariableW(L"APPDATA", (dir / L"_appdata").c_str());
            return;
        }
    }
}

std::wstring WideCommandLine() {
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    std::wstring command_line;
    if (arguments != nullptr) {
        for (int i = 1; i < argument_count; ++i) {
            if (i > 1) {
                command_line += L' ';
            }
            command_line += arguments[i];
        }
        LocalFree(arguments);
    }
    return command_line;
}

std::filesystem::path ExeDir() {
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path EnvPath(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::filesystem::path{} : std::filesystem::path(value);
}

void LoadStartupTheme() {
    std::filesystem::path dir = ExeDir() / "themes";
    if (const std::filesystem::path override_dir = EnvPath("SHINE_THEME_DIR");
        !override_dir.empty()) {
        dir = override_dir;
    }
    if (!theme::LoadThemesFrom(dir)) {
        // 路径转窄串只为日志可读性；日志本身是窄字符流（fmt 在 MinGW 下不支持 wchar_t）。
        shine::log::Error("theme load incomplete from {} — falling back to builtin defaults",
                          dir.string());
    }
    // 先铺默认（深空）再谈持久化：没有 theme.json 时也必须有已应用的样式。
    theme::ApplyCurrentTheme();

    // 自检开关：SHINE_THEME_SET=<ascii id> 直接切主题并打印结果。
    if (const char* forced = std::getenv("SHINE_THEME_SET"); forced != nullptr) {
        theme::ThemeId id = theme::ThemeId::DeepSpace;
        if (theme::ThemeIdFromKey(forced, id)) {
            theme::ApplyTheme(id);
        }
        shine::log::Info("SHINE_THEME_SET={} -> {}", forced,
                         std::string(theme::ThemeDisplayName(theme::CurrentThemeId())));
    } else {
        // 损坏或缺失 → 保持默认主题，不崩（照 Qt 侧 MainWindow 的行为）
        (void)theme::LoadPersistedTheme(theme::DefaultThemeFile());
    }

    if (std::getenv("SHINE_THEME_SELFTEST") != nullptr) {
        std::string report;
        const bool ok = theme::SelfTestRoundTrip(&report);
        shine::log::Info("theme selftest: {}\n{}", ok ? "PASS" : "FAIL", report);
    }
}

} // namespace

int RunApp(int argc, char** argv) {
    // 必须在读任何路径之前：设置、项目索引、主题缓存全走 %APPDATA%。
    ConfigureAppDataSandbox();

    // 日志要早于任何 shine::log:: 调用。Qt 版没显式 Init（默认 logger 无 sink，
    // 日志实际是哑的）；ImGui 版的启动判据全靠这行日志，所以必须自己 Init。
    // Init 幂等（内部判重），--mcp-stdio / --novel-* 分支自己会再调一次。
    shine::log::Init();

    const std::wstring command_line = WideCommandLine();
    const std::wstring_view view{command_line};
    if (Contains(view, L"--mcp-stdio")) {
        _putenv_s("SHINE_LOG_TO_STDERR", "1");
        return shine::mcp::RunStdioServerMain();
    }
    if (Contains(view, L"--novel-")) {
        return shine::novel::RunNovelCli(command_line.c_str());
    }

    Host host;
    Host::Options options;
    if (const std::filesystem::path size = EnvPath("SHINE_IMGUI_WINDOW"); !size.empty()) {
        // 取证时锁死窗口尺寸，跨进程像素比对才有意义。
        if (int width = 0, height = 0; std::sscanf(size.string().c_str(), "%dx%d", &width, &height) == 2 &&
            width > 0 && height > 0) {
            options.width = width;
            options.height = height;
        }
    }

    std::string error;
    if (!host.Initialize(options, error)) {
        shine::log::Error("imgui host init failed: {}", error);
        return 1;
    }

    LoadStartupTheme();

    shine::async::Init();
    shine::gallery::Init();

    pages::Shell shell;

    // SHINE_IMGUI_REVIEW=<dir>：跑一次总回归抓图就退出（phases.md P6.3）。
    // 放在 RunLoop 之前 —— 取证自己泵帧，不需要进消息循环。
    if (const std::filesystem::path review_dir = EnvPath("SHINE_IMGUI_REVIEW");
        !review_dir.empty()) {
        const auto result = shine::imguiverify::RunReview(host, shell, review_dir);
        std::printf("[imgui-review] shots=%d failed=%d dir=%s\n", result.captured, result.failed,
                    review_dir.string().c_str());
        // ⚠️ 退出码必须与 scripts/run_reviews.ps1:66 的约定一致：PASS↔0 / FAIL↔1。
        //    早先这里 FAIL 返 2，脚本判成 "agree=False" —— 失败也报不一致，
        //    绿灯和红灯都失去意义。
        host.Shutdown();
        shine::async::Shutdown();
        shine::log::Shutdown();
        std::_Exit(result.failed == 0 ? 0 : 1);
    }

    host.RunLoop([&shell](float dt) { shell.DrawFrame(dt); });

    shine::gallery::Shutdown();
    // 故意不 join worker：HTTP connect 或静态析构可能让快速关窗永久卡住
    // （与 Qt 版 AppEntry.cpp:148-151 同理由）。
    shine::async::Shutdown();
    shine::log::Shutdown();
    std::_Exit(0);
}

} // namespace shine::imguiapp
