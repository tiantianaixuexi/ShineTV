#include "pages/checks/P02Checks.h"
#include "pages/settings/StyleEditorDialog.h"
#include "pages/gallery/Bench.h"
#include "pages/gallery/QssStateProbe.h"
#include "pages/gallery/WidgetGalleryView.h"
#include "pages/shell/MainWindow.h"
#include "widget/theme/Theme.h"
#include "widget/theme/ThemeService.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QFile>
#include <QPixmap>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace shine::app::checks {

std::optional<int> RegisterP02Checks(QApplication& app, MainWindow& window,
                                      std::wstring_view command_line) {
    // 自检设施：SHINE_EXIT_AFTER_SEC=<秒> 到点自动退出（判据：退出码 0）
    if (const char* raw = std::getenv("SHINE_EXIT_AFTER_SEC"); raw != nullptr && *raw != '\0') {
        const double sec = std::atof(raw);
        if (sec > 0.0) {
            QTimer::singleShot(static_cast<int>(sec * 1000), &app, &QApplication::quit);
        }
    }

    // P02-S8 自检：SHINE_STYLEEDITOR_SELFTEST=1 → 改 Token / 另存自定义主题 /
    //   主题菜单数据源 / 重启后仍可选 / 减少动效持久化
    if (const char* raw = std::getenv("SHINE_STYLEEDITOR_SELFTEST"); raw != nullptr && *raw != '\0') {
        bool ok = true;
        shine::app::StyleEditorDialog dlg;
        dlg.SetTokenValue(0, 0xFF123456u);
        const bool saved = dlg.SaveAs("自检主题");
        ok = ok && saved;
        std::printf("[styleeditor] save-as-custom: %s\n", saved ? "PASS" : "FAIL");

        bool listed = false;
        for (std::size_t i = 0; i < shine::theme::CustomThemeCount(); ++i) {
            if (shine::theme::CustomThemeName(i) == "自检主题") {
                listed = true;
            }
        }
        ok = ok && listed;
        std::printf("[styleeditor] in-theme-menu-source: %s\n", listed ? "PASS" : "FAIL");

        // 模拟重启：重扫自定义目录 → 激活 → 取值仍正确（「重启后仍可选」）
        shine::theme::RevertToBuiltin();
        (void)shine::theme::LoadCustomThemes(
            std::filesystem::path{
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdWString()} /
            L"themes");
        const bool reactivated = shine::theme::ActivateCustom("自检主题");
        const bool colorOk = shine::theme::TokenValues(shine::theme::Current())[0] == 0xFF123456u;
        ok = ok && reactivated && colorOk;
        std::printf("[styleeditor] survives-restart: %s (color=%s)\n",
                    reactivated ? "PASS" : "FAIL", colorOk ? "PASS" : "FAIL");

        // 「减少动效」持久化
        shine::theme::ThemeService::SetReduceMotionPersisted(true);
        const QString cfg =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            QStringLiteral("/theme.json");
        QFile f{cfg};
        const bool opened = f.open(QIODevice::ReadOnly);
        const std::string text = opened ? f.readAll().toStdString() : std::string{};
        f.close();
        const bool reduceOk = text.find("\"reduce\": true") != std::string::npos;
        shine::theme::ThemeService::SetReduceMotionPersisted(false);
        ok = ok && reduceOk;
        std::printf("[styleeditor] reduce-motion-persist: %s\n", reduceOk ? "PASS" : "FAIL");

        std::printf("[styleeditor] %s\n", ok ? "PASS" : "FAIL");
        // 报告落文件（GUI 子系统下 stdio 缓冲 + _Exit 不刷缓冲 → 管道取证会丢，
        // 文件是判据的可靠通道）+ 手动刷缓冲
        std::string report = std::string{"[styleeditor] "} + (ok ? "PASS" : "FAIL") + "\n";
        report += std::string{"save-as-custom: "} + (saved ? "PASS" : "FAIL") + "\n";
        report += std::string{"in-theme-menu-source: "} + (listed ? "PASS" : "FAIL") + "\n";
        report += std::string{"survives-restart: "} + (reactivated ? "PASS" : "FAIL") +
                  std::string{" color="} + (colorOk ? "PASS" : "FAIL") + "\n";
        report += std::string{"reduce-motion-persist: "} + (reduceOk ? "PASS" : "FAIL") + "\n";
        (void)shine::util::WriteFileBytes(
            std::filesystem::path{app.applicationDirPath().toStdWString()} /
                L"styleeditor-selftest.txt",
            report);
        std::fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }
    // P02-S7 判据：SHINE_THUMB_BENCH=1 → 2 万张缩略图基准；SHINE_VIEWER_SELFTEST=1 → 锚点缩放自检
    if (const char* raw = std::getenv("SHINE_THUMB_BENCH"); raw != nullptr && *raw != '\0') {
        const std::string report = shine::gallery::ThumbBench();
        std::printf("[thumb-bench] %s\n", report.c_str());
        std::_Exit(report.find("PASS") != std::string::npos ? 0 : 1);
    }
    if (const char* raw = std::getenv("SHINE_VIEWER_SELFTEST"); raw != nullptr && *raw != '\0') {
        const std::string report = shine::gallery::ViewerSelfTest();
        std::printf("[viewer-selftest] %s\n", report.c_str());
        std::_Exit(report.find("PASS") != std::string::npos ? 0 : 1);
    }
    // P02-S6 判据基准：SHINE_TABLE_BENCH=1 → 万行表格滚动流畅性基准
    if (const char* raw = std::getenv("SHINE_TABLE_BENCH"); raw != nullptr && *raw != '\0') {
        const std::string report = shine::gallery::TableBench();
        std::printf("[table-bench] %s\n", report.c_str());
        std::_Exit(report.find("PASS") != std::string::npos ? 0 : 1);
    }
    // P02-S5 诊断：SHINE_QSS_PROBE=1 → QSS 动态属性态强制机制探针
    if (const char* raw = std::getenv("SHINE_QSS_PROBE"); raw != nullptr && *raw != '\0') {
        const std::string report = shine::gallery::QssStateProbe();
        std::printf("[qss-probe]\n%s", report.c_str());
        std::_Exit(0);
    }
    // P02-S5：控件画廊（--widget-gallery；SHINE_GALLERY_SHOTS=<目录> 自动逐页截图后退出）
    if (command_line == L"--widget-gallery") {
        shine::gallery::WidgetGalleryView gallery;
        gallery.resize(1280, 820);
        gallery.show();
        if (const char* dir = std::getenv("SHINE_GALLERY_SHOTS"); dir != nullptr && *dir != '\0') {
            QTimer::singleShot(500, [&gallery, dir] {
                const int n = gallery.GrabAllPages(std::string{dir});
                std::printf("[gallery] shots saved: %d\n", n);
                std::_Exit(n >= 0 ? 0 : 1);
            });
        }
        return app.exec();
    }
    // P02-S9 多模态评审截图包：SHINE_P02_REVIEW=<目录> → UI.md §4 全部 10 张
    if (const char* dir = std::getenv("SHINE_P02_REVIEW"); dir != nullptr && *dir != '\0') {
        shine::gallery::SaveP02Review(std::string{dir});
        std::fflush(nullptr);
        std::_Exit(0);
    }

    // 验收设施：SHINE_SCREENSHOT=<png 路径> 启动 3.5s 后应用内自截图（QWidget::grab）。
    // 刻意不用系统抓屏：系统抓屏要求窗口置前，自动化环境常被前台锁挡住抓错窗口
    //（实测抓到过屏幕上的浏览器；截图验收说明见 docs/40-operations/verification.md）。
    if (const char* raw = std::getenv("SHINE_SCREENSHOT"); raw != nullptr && *raw != '\0') {
        const QString shotPath = QString::fromLocal8Bit(raw);
        QTimer::singleShot(3500, &window, [&window, shotPath] {
            // 抓整个原生窗口（含标题栏）—— 标题的中文字形也要过目；失败退回 widget 截图
            QPixmap pm = window.screen() != nullptr ? window.screen()->grabWindow(window.winId()) : QPixmap{};
            if (pm.isNull()) {
                pm = window.grab();
            }
            const bool ok = pm.save(shotPath);
            std::printf("[self-test] SHINE_SCREENSHOT %s: %s\n", shotPath.toUtf8().constData(),
                        ok ? "saved" : "FAILED");
        });
    }

    // S3 验收设施：SHINE_THEME_TOUR=<输出目录> —— 依次切 4 主题各截一张图后自退
    if (const char* rawTour = std::getenv("SHINE_THEME_TOUR"); rawTour != nullptr && *rawTour != '\0') {
        const std::filesystem::path outDir = shine::util::PathFromUtf8(rawTour);
        for (int i = 0; i < 4; ++i) {
            const shine::theme::ThemeId id = shine::theme::kAllThemes[static_cast<std::size_t>(i)];
            QTimer::singleShot(400 + 900 * i, &app,
                               [id] { shine::theme::ThemeService::Switch(id, false); });
            QTimer::singleShot(400 + 900 * i + 350, &window, [&window, outDir, id] {
                const std::string name{shine::theme::ThemeFileName(id)};
                const std::filesystem::path p = outDir / shine::util::PathFromUtf8(name + ".png");
                (void)window.grab().save(QString::fromStdWString(p.wstring()));
                std::printf("[self-test] theme-tour shot: %s\n", name.c_str());
            });
        }
        QTimer::singleShot(400 + 900 * 4 + 350, &app, [] { std::_Exit(0); });
    }
    return std::nullopt;
}

} // namespace shine::app::checks
