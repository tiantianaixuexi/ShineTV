#include "ui/verify/checks/P02Checks.h"
#include "ui/pages/settings/StyleEditorDialog.h"
#include "ui/verify/gallery/Bench.h"
#include "ui/verify/gallery/QssStateProbe.h"
#include "ui/verify/gallery/WidgetGalleryView.h"
#include "ui/pages/shell/MainWindow.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
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
#include <vector>

namespace shine::app::checks {
namespace {

// ── 自检判据的共同口径（本文件的唯一实现） ────────────────────────────
//
// 旧写法是 `report.find("PASS") != std::string::npos ? 0 : 1`：**任何位置**
// 出现 PASS 就退 0。ThumbBench / ViewerSelfTest / TableBench 恰好各只产出一条
// 判据 token，所以还没踩雷；但报告一拆成多行判据，「一行 FAIL + 一行 PASS」
// 就会静悄悄退 0（假绿）。改成「必须有 PASS 判据 token 且没有 FAIL token」。

// 报告型判据：只认明确的 PASS/FAIL token。报告**不产** token 时不要走这里
// （见 SHINE_QSS_PROBE：那是纯诊断件，退出码只表示「探针跑完了」）。
[[nodiscard]] bool TokenVerdict(const std::string& report) {
    return report.find("PASS") != std::string::npos &&
           report.find("FAIL") == std::string::npos;
}

// 产物型判据：expected 里每个文件都要落到盘上**且有实际字节**。
// 截图包的判据不是「函数跑完了」（SaveP02Review 返回 void，跑完 ≠ 拍到），
// 而是「该落的图真的落到盘上、且不是几百字节的空图」。
[[nodiscard]] bool ShotsLanded(const std::filesystem::path& dir,
                               const std::vector<std::string>& expected) {
    bool ok = true;
    for (const std::string& name : expected) {
        const auto bytes = shine::util::ReadFileBytes(dir / name);
        if (!bytes || bytes->size() < 100) {
            std::printf("[shots] %s MISSING/EMPTY\n", name.c_str());
            ok = false;
        } else {
            std::printf("[shots] %s %llu saved\n", name.c_str(),
                        static_cast<unsigned long long>(bytes->size()));
        }
    }
    return ok;
}

// 统一收尾：报告落文件 → 刷缓冲 → **退出码跟着判据走**。
// 刷缓冲不可省：GUI 子系统下 _Exit 不跑 atexit，stdio 缓冲里的报告会整段丢失。
[[noreturn]] void ReportAndExit(const std::filesystem::path& reportPath, const std::string& report,
                                bool ok) {
    (void)shine::util::WriteFileBytes(reportPath, report);
    std::printf("%s", report.c_str());
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
}

} // namespace

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
        // 机器可读判据：退出码已经跟着 ok 走了，报告里补一行同口径的 overall。
        report += std::string{"[P02-S8] overall: "} + (ok ? "PASS" : "FAIL") + "\n";
        (void)shine::util::WriteFileBytes(
            std::filesystem::path{app.applicationDirPath().toStdWString()} /
                L"styleeditor-selftest.txt",
            report);
        std::fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }
    // P02-S7 判据：SHINE_THUMB_BENCH=1 → 2 万张缩略图基准；SHINE_VIEWER_SELFTEST=1 → 锚点缩放自检
    //
    // 判据：报告必须「有 PASS token 且无 FAIL token」（见 TokenVerdict）。
    // 这三支只往 stdout 写（无输出路径开关），所以退出前显式刷缓冲 —— 否则 GUI
    // 子系统下 _Exit 会把整段报告连同 overall 一起丢掉。
    if (const char* raw = std::getenv("SHINE_THUMB_BENCH"); raw != nullptr && *raw != '\0') {
        const std::string report = shine::gallery::ThumbBench();
        const bool ok = TokenVerdict(report);
        std::printf("[thumb-bench] %s\n[P02-S7] overall: %s\n", report.c_str(),
                    ok ? "PASS" : "FAIL");
        std::fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }
    if (const char* raw = std::getenv("SHINE_VIEWER_SELFTEST"); raw != nullptr && *raw != '\0') {
        const std::string report = shine::gallery::ViewerSelfTest();
        const bool ok = TokenVerdict(report);
        std::printf("[viewer-selftest] %s\n[P02-S7] overall: %s\n", report.c_str(),
                    ok ? "PASS" : "FAIL");
        std::fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }
    // P02-S6 判据基准：SHINE_TABLE_BENCH=1 → 万行表格滚动流畅性基准
    if (const char* raw = std::getenv("SHINE_TABLE_BENCH"); raw != nullptr && *raw != '\0') {
        const std::string report = shine::gallery::TableBench();
        const bool ok = TokenVerdict(report);
        std::printf("[table-bench] %s\n[P02-S6] overall: %s\n", report.c_str(),
                    ok ? "PASS" : "FAIL");
        std::fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }
    // P02-S5 诊断：SHINE_QSS_PROBE=1 → QSS 动态属性态强制机制探针
    //
    // **这不是验收，刻意不写 overall**：QssStateProbe() 产出的是 A/B/C/D 四组
    // 脸行像素色 + 属性回读，全篇不产 PASS/FAIL 判据 token（判据是「人眼看
    // 颜色对不对」）。TokenVerdict 对它恒为 false（无 PASS），拿它退出就是假绿。
    // 所以退出码只表示「探针跑完了」，overall 显式写 NONE 以免被 harness 误读成通过。
    if (const char* raw = std::getenv("SHINE_QSS_PROBE"); raw != nullptr && *raw != '\0') {
        const std::string report = shine::gallery::QssStateProbe();
        std::printf("[qss-probe]\n%s[qss-probe] overall: NONE (diagnostic-only, 人读判据)\n",
                    report.c_str());
        std::fflush(nullptr);
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
    // P02-S9 多模态评审截图包：SHINE_P02_REVIEW=<目录> → UI.md §4 全部截图
    //
    // 原来写完 manifest 就无条件 `_Exit(0)`：SaveP02Review() 返回 void，图一张没
    // 拍到（目录不存在 / QPixmap::save 静默失败）也报成功。
    // 判据改成「expected 每张图都落到盘上且有实际字节」，清单落 shots-manifest.txt。
    if (const char* dir = std::getenv("SHINE_P02_REVIEW"); dir != nullptr && *dir != '\0') {
        const std::filesystem::path outDir = shine::util::PathFromUtf8(dir);
        shine::gallery::SaveP02Review(std::string{dir});
        std::vector<std::string> expected{
            "gallery-button-states.png", "gallery-container-states.png", "gallery-datatable.png",
            "gallery-thumbgrid.png",     "gallery-imageviewer.png",   "theme-switch-before.png",
            "theme-switch-after.png",   "gallery-motion-reduced.png"};
        for (const shine::theme::ThemeId id : shine::theme::kAllThemes) {
            expected.push_back("gallery-" + std::string{shine::theme::ThemeFileName(id)} +
                               "-full.png");
        }
        const bool ok = ShotsLanded(outDir, expected);
        ReportAndExit(outDir / "shots-manifest.txt",
                      std::string{"[P02-S9] review shots expected="} +
                          std::to_string(expected.size()) + "\n[P02-S9] overall: " +
                          (ok ? "PASS" : "FAIL") + "\n",
                      ok);
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

    // S3 验收设施：SHINE_THEME_TOUR=<输出目录> —— 依次切每个内置主题各截一张图后自退
    //
    // 查过了：**产物是真的落盘的** —— window.grab().save() 走 Qt 图像写出，不经
    // stdio 缓冲，所以上面 styleeditor 那条「缓冲 + _Exit 会丢」的顾虑在这里不成立。
    // 真问题是被 `(void)` 丢掉的 save() 返回值：截图静默失败也照样 `_Exit(0)`，
    // 整个分支一条断言都没有（等于「跑了个寂寞」）。
    // 现在判据 = 「每个内置主题的 <主题>.png 都落到盘上且有实际字节」，清单落 theme-tour.txt。
    //
    // 顺带一个真 bug：kAllThemes 是 **5** 套主题，旧循环硬编码 `i < 4`，第 5 套
    // （PolarNight）从来没被截过 —— 判据若按 kAllThemes 核对会恒 FAIL。
    // 这里改成按 kAllThemes.size() 枚举，节拍与退出时刻同步跟着走。
    if (const char* rawTour = std::getenv("SHINE_THEME_TOUR"); rawTour != nullptr && *rawTour != '\0') {
        const std::filesystem::path outDir = shine::util::PathFromUtf8(rawTour);
        const int themes = static_cast<int>(shine::theme::kAllThemes.size());
        for (int i = 0; i < themes; ++i) {
            const shine::theme::ThemeId id = shine::theme::kAllThemes[static_cast<std::size_t>(i)];
            QTimer::singleShot(400 + 900 * i, &app,
                               [id] { shine::theme::ThemeService::Switch(id, false); });
            QTimer::singleShot(400 + 900 * i + 350, &window, [&window, outDir, id] {
                const std::string name{shine::theme::ThemeFileName(id)};
                const std::filesystem::path p = outDir / shine::util::PathFromUtf8(name + ".png");
                const bool saved = window.grab().save(QString::fromStdWString(p.wstring()));
                std::printf("[self-test] theme-tour shot: %s %s\n", name.c_str(),
                            saved ? "saved" : "FAILED");
            });
        }
        QTimer::singleShot(400 + 900 * themes + 350, &app, [outDir] {
            std::vector<std::string> expected;
            for (const shine::theme::ThemeId id : shine::theme::kAllThemes) {
                expected.push_back(std::string{shine::theme::ThemeFileName(id)} + ".png");
            }
            const bool ok = ShotsLanded(outDir, expected);
            ReportAndExit(outDir / "theme-tour.txt",
                          std::string{"[P02-S3] theme-tour shots expected="} +
                              std::to_string(expected.size()) + "\n[P02-S3] overall: " +
                              (ok ? "PASS" : "FAIL") + "\n",
                          ok);
        });
    }
    return std::nullopt;
}

} // namespace shine::app::checks
