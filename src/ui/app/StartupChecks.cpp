#include "ui/app/StartupChecks.h"

#include "ui/app/AppEnvironment.h"
#include "ui/verify/review/P03Review.h"
#include "ui/verify/review/P04Review.h"
#include "ui/verify/review/P05Review.h"
#include "ui/verify/review/P06Review.h"
#include "ui/verify/review/P07Review.h"
#include "ui/verify/review/P08Review.h"
#include "ui/verify/review/P09Review.h"
#include "ui/verify/review/P10Review.h"
#include "ui/pages/settings/FirstRunWizard.h"
#include "core/Settings.h"
#include "ui/kit/motion/Easing.h"
#include "ui/kit/theme/QssBuilder.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "project/Project.h"
#include "project/ProjectIndex.h"
#include "project/ProjectTemplate.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace shine::app {

std::optional<int> RunStartupChecks(QApplication& app) {
    // —— 自检设施（P02-S1）：主题数据层载入 + 反射序列化往返自检 ——
    if (!shine::theme::LoadThemesFrom(std::filesystem::path{QCoreApplication::applicationDirPath().toStdWString()} /
                                      L"themes")) {
        // 缺失/解析失败已逐个记日志；S3 的 ThemeService 负责回退与用户提示
    }
    shine::theme::ThemeService::Initialize(app); // S3：回读持久化选择 + 应用 QSS
    shine::motion::InitializeMotion();           // S4：「减少动效」环境变量开关
    if (const char* rawDump = std::getenv("SHINE_QSS_DUMP"); rawDump != nullptr && *rawDump != '\0') {
        (void)shine::theme::QssBuilder::DumpToFile(shine::theme::Current(),
                                                   shine::util::PathFromUtf8(rawDump));
    }
    if (const char* raw = std::getenv("SHINE_THEME_SELFTEST"); raw != nullptr && *raw != '\0') {
        std::string report;
        const bool ok = shine::theme::SelfTestRoundTrip(&report);
        std::printf("[self-test] theme round-trip: %s\n%s", ok ? "PASS" : "FAIL", report.c_str());
        std::_Exit(ok ? 0 : 1);
    }
    if (const char* raw = std::getenv("SHINE_MOTION_SELFTEST"); raw != nullptr && *raw != '\0') {
        std::string report;
        const bool ok = shine::motion::SelfTest(&report);
        std::printf("[self-test] motion: %s\n%s", ok ? "PASS" : "FAIL", report.c_str());
        std::_Exit(ok ? 0 : 1);
    }
    // P03-S1..S3 自检：SHINE_PROJECT_SELFTEST=1 → 项目模型往返/迁移 + 索引 + 模板骨架
    if (const char* raw = std::getenv("SHINE_PROJECT_SELFTEST"); raw != nullptr && *raw != '\0') {
        std::string r1;
        std::string r2;
        std::string r3;
        const bool ok1 = shine::project::SelfTest(&r1);
        const bool ok2 = shine::project::IndexSelfTest(&r2);
        const bool ok3 = shine::project::TemplateSelfTest(&r3);
        const bool ok = ok1 && ok2 && ok3;
        std::string report = r1 + r2 + r3;
        report += std::string{"[project] overall: "} + (ok ? "PASS" : "FAIL") + "\n";
        std::printf("[self-test] project: %s\n%s", ok ? "PASS" : "FAIL", report.c_str());
        // 报告落文件（GUI 子系统 stdio 缓冲教训：文件才是可靠取证通道）
        (void)shine::util::WriteFileBytes(
            std::filesystem::path{QCoreApplication::applicationDirPath().toStdWString()} /
                L"project-selftest.txt",
            report);
        std::fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }

    // S3 验收设施：SHINE_THEME_SET=<ascii id> 切换主题并持久化（跨进程回读验证用）
    if (const char* rawSet = std::getenv("SHINE_THEME_SET"); rawSet != nullptr && *rawSet != '\0') {
        shine::theme::ThemeId id{};
        if (shine::theme::ThemeIdFromName(rawSet, id)) {
            shine::theme::ThemeService::Switch(id, false);
        }
    }

    // P03-S10 判定：SHINE_P03_FW=<文件> 首启引导一次性（「完成」按钮同一提交路径 → 重启模拟）
    if (const std::filesystem::path fwOut = EnvironmentPath(L"SHINE_P03_FW"); !fwOut.empty()) {
        const bool before = shine::Settings().firstRun;
        shine::app::FirstRunWizard wizard(nullptr, false);
        wizard.Finish();      // 与「完成」按钮同路径：写 Settings + firstRun=false + SaveSettings
        shine::LoadSettings(); // 模拟重启：重读配置文件
        const bool after = shine::Settings().firstRun;
        const bool ok = before && !after;
        std::string report = std::string{"firstRun-before="} + (before ? "true" : "false") + "\n" +
                             "firstRun-after=" + (after ? "true" : "false") + "\n" +
                             (ok ? "PASS 首启引导仅一次（重读后不再出现）\n" : "FAIL\n");
        (void)shine::util::WriteFileBytes(fwOut, report);
        std::printf("[p03-fw]\n%s", report.c_str());
        std::fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }

    // P03-S11 多模态评审截图包：SHINE_P03_REVIEW=<目录> → UI.md §4 全套 12 张（自带 APPDATA 沙盒）
    if (const std::filesystem::path reviewDir = EnvironmentPath(L"SHINE_P03_REVIEW"); !reviewDir.empty()) {
        shine::app::SaveP03Review(shine::util::PathToUtf8(reviewDir));
        return app.exec(); // 截图链走完在包内 _Exit
    }
    // P04-S11 多模态视觉评审：SHINE_P04_REVIEW=<目录> → UI.md §4 全套 14 张 + manifest
    if (const std::filesystem::path reviewDir = EnvironmentPath(L"SHINE_P04_REVIEW"); !reviewDir.empty()) {
        shine::app::SaveP04Review(shine::util::PathToUtf8(reviewDir));
        return app.exec(); // 截图链走完在包内 _Exit
    }
    // P05-S9 多模态视觉评审：资产台 / 设定集 / 一致性 / 图库截图包。
    if (const std::filesystem::path reviewDir = EnvironmentPath(L"SHINE_P05_REVIEW");
        !reviewDir.empty()) {
        shine::app::SaveP05Review(shine::util::PathToUtf8(reviewDir));
        return app.exec();
    }
    // P06-S9 多模态视觉评审：分镜工作区截图包。
    if (const std::filesystem::path reviewDir = EnvironmentPath(L"SHINE_P06_REVIEW");
        !reviewDir.empty()) {
        shine::app::SaveP06Review(reviewDir);
        return app.exec();
    }
    // P07-S13 多模态视觉评审截图包。
    if (const std::filesystem::path reviewDir = EnvironmentPath(L"SHINE_P07_REVIEW");
        !reviewDir.empty()) {
        shine::app::SaveP07Review(reviewDir);
        return app.exec();
    }
    // P08-S8 多模态视觉评审截图包。
    if (const std::filesystem::path reviewDir = EnvironmentPath(L"SHINE_P08_REVIEW");
        !reviewDir.empty()) {
        shine::app::SaveP08Review(reviewDir);
        return app.exec();
    }
    // P09-S10 多模态视觉评审截图包。
    if (const std::filesystem::path reviewDir = EnvironmentPath(L"SHINE_P09_REVIEW");
        !reviewDir.empty()) {
        shine::app::SaveP09Review(reviewDir);
        return app.exec();
    }
    // P10-S4/S5 多主题视觉矩阵。
    if (const std::filesystem::path reviewDir = EnvironmentPath(L"SHINE_P10_REVIEW");
        !reviewDir.empty()) {
        shine::app::SaveP10Review(reviewDir);
        return app.exec();
    }
    return std::nullopt;
}

} // namespace shine::app
