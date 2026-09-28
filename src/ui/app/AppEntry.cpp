#include "ui/app/AppEntry.h"

#include "ui/app/AcceptanceChecks.h"
#include "ui/app/AppEnvironment.h"
#include "ui/app/StartupChecks.h"
#include "ui/pages/shell/MainWindow.h"
#include "core/Async.h"
#include "core/Log.h"
#include "mcp/MCPServer.h"
#include "novel/NovelCli.h"
#include "media/Gallery.h"

#include <QApplication>
#include <QFont>
#include <QTimer>

#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW（须在 windows.h 之后）

#include <cstdlib>
#include <filesystem>
#include <string>

#include <string_view>

namespace shine::app {
namespace {

bool StartsWith(const wchar_t* text, const wchar_t* prefix) {
    return text != nullptr && std::wstring_view(text).starts_with(prefix);
}

void ConfigureAppDataSandbox() {
    // 必须在 QApplication、设置或项目索引读取任何路径之前生效。
    if (const std::filesystem::path override_dir = EnvironmentPath(L"SHINE_APPDATA_OVERRIDE");
        !override_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", override_dir.c_str());
    } else if (const std::filesystem::path review_dir = EnvironmentPath(L"SHINE_P03_REVIEW");
               !review_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (review_dir / L"_appdata").c_str());
    } else if (const std::filesystem::path review_dir = EnvironmentPath(L"SHINE_P04_REVIEW");
               !review_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (review_dir / L"_appdata").c_str());
    } else if (const auto gallery_dir = EnvironmentPath(L"SHINE_P05_S7");
               !gallery_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (gallery_dir / L"_appdata").c_str());
    } else if (const auto reference_dir = EnvironmentPath(L"SHINE_P05_S8");
               !reference_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (reference_dir / L"_appdata").c_str());
    } else if (const auto review_dir = EnvironmentPath(L"SHINE_P05_REVIEW");
               !review_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (review_dir / L"_appdata").c_str());
    } else if (const auto review_dir = EnvironmentPath(L"SHINE_P06_REVIEW");
               !review_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (review_dir / L"_appdata").c_str());
    } else if (const auto review_dir = EnvironmentPath(L"SHINE_P07_REVIEW");
               !review_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (review_dir / L"_appdata").c_str());
    } else if (const auto review_dir = EnvironmentPath(L"SHINE_P08_REVIEW");
               !review_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (review_dir / L"_appdata").c_str());
    } else if (const auto review_dir = EnvironmentPath(L"SHINE_P09_REVIEW");
               !review_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (review_dir / L"_appdata").c_str());
    } else if (const auto review_dir = EnvironmentPath(L"SHINE_P10_REVIEW");
               !review_dir.empty()) {
        SetEnvironmentVariableW(L"APPDATA", (review_dir / L"_appdata").c_str());
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

void ConfigureApplicationFont() {
    QFont font;
    font.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei"),
                      QStringLiteral("Segoe UI")});
    font.setPointSize(10);
    QApplication::setFont(font);
}

} // namespace

int RunApp(int argc, char** argv) {
    ConfigureAppDataSandbox();

    const std::wstring command_line = WideCommandLine();
    if (StartsWith(command_line.c_str(), L"--mcp-stdio") ||
        std::wstring_view(command_line).find(L"--mcp-stdio") != std::wstring_view::npos) {
        _putenv_s("SHINE_LOG_TO_STDERR", "1");
        return shine::mcp::RunStdioServerMain();
    }
    if (std::wstring_view(command_line).find(L"--novel-") != std::wstring_view::npos) {
        return shine::novel::RunNovelCli(command_line.c_str());
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ShineTV Studio"));
    QApplication::setOrganizationName(QStringLiteral("ShineTV"));
    ConfigureApplicationFont();

    if (auto startup_result = RunStartupChecks(app); startup_result.has_value()) {
        return *startup_result;
    }

    shine::async::Init();
    shine::gallery::Init();
    auto* ui_pump = new QTimer(qApp);
    ui_pump->setInterval(15);
    QObject::connect(ui_pump, &QTimer::timeout, [] {
        shine::async::DrainUiQueue();
        shine::gallery::Tick();
    });
    ui_pump->start();

    MainWindow window;
    window.show();

    if (auto acceptance_result = acceptance::RegisterChecks(app, window, command_line);
        acceptance_result.has_value()) {
        return *acceptance_result;
    }

    const int result = app.exec();
    shine::gallery::Shutdown();

    // 故意不 join worker：HTTP connect 或静态析构可能让快速关窗永久卡住。
    shine::async::Shutdown();
    shine::log::Shutdown();
    std::_Exit(result);
}

} // namespace shine::app
