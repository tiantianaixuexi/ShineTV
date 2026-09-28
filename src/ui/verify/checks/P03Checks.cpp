#include "ui/verify/checks/P03Checks.h"
#include "ui/app/AppEnvironment.h"
#include "ui/pages/shell/MainWindow.h"
#include "project/Project.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QElapsedTimer>
#include <QTimer>

#include <cstdio>
#include <filesystem>
#include <string>

namespace shine::app::checks {

void RegisterP03WindowChecks(MainWindow& window) {
    // P03 验收设施：SHINE_P03_BOOTSTRAP=<项目根> 建中文项目（影视化模板）+ 固定非默认布局
    if (const std::filesystem::path rootDir = EnvironmentPath(L"SHINE_P03_BOOTSTRAP"); !rootDir.empty()) {
        shine::project::ProjectSpec spec;
        spec.name = "灯语回声";
        spec.rootDir = rootDir;
        spec.templateId = "film";
        spec.premise = "一盏灯把迷路的人带回家";
        if (auto ref = window.Service().Create(spec); ref) {
            window.EnterProject(*ref);
            window.SetTestLayout();
            std::printf("[p03-bootstrap] created: %s\n",
                        shine::util::PathToUtf8(ref->rootDir).c_str());
        } else {
            std::printf("[p03-bootstrap] FAILED: %s\n", ref.error().message.c_str());
        }
    }
    // P03-S6 判定：SHINE_P03_FOLD=<文件> 折叠动画耗时（Ctrl+I / Ctrl+J 同路径，判据档 = motion.base 200ms）
    if (const std::filesystem::path foldOut = EnvironmentPath(L"SHINE_P03_FOLD"); !foldOut.empty()) {
        QTimer::singleShot(800, &window, [&window, foldOut] {
            window.ToggleInspector();
            window.ToggleBottomDock();
            auto* elapsed = new QElapsedTimer();
            elapsed->start();
            auto* poll = new QTimer(&window);
            poll->setInterval(10);
            QObject::connect(poll, &QTimer::timeout, &window,
                             [&window, foldOut, elapsed, poll] {
                const QString probe = window.LayoutProbe();
                const bool folded = probe.contains(QStringLiteral("inspector=0")) &&
                                    probe.contains(QStringLiteral("bottom=0"));
                if (folded || elapsed->elapsed() > 3000) {
                    poll->stop();
                    const std::string report =
                        "fold-ms=" + std::to_string(elapsed->elapsed()) +
                        (folded ? "" : " TIMEOUT") + "\n" + probe.toStdString();
                    (void)shine::util::WriteFileBytes(foldOut, report);
                    std::fflush(nullptr);
                    std::_Exit(0);
                }
            });
            poll->start();
        });
    }
}

void RegisterP03Probe(MainWindow& window) {
    // P03 验收设施：SHINE_P03_PROBE=<文件> 落布局探针后退出（同沙盒两跑比对 = 重启还原判据）
    if (const std::filesystem::path probeOut = EnvironmentPath(L"SHINE_P03_PROBE"); !probeOut.empty()) {
        QTimer::singleShot(1200, &window, [&window, probeOut] {
            window.PersistNow();
            (void)shine::util::WriteFileBytes(probeOut, window.LayoutProbe().toStdString());
            std::fflush(nullptr);
            std::_Exit(0);
        });
    }
}

} // namespace shine::app::checks
