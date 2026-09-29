#include "ui/verify/checks/P03Checks.h"
#include "ui/app/AppEnvironment.h"
#include "ui/pages/shell/MainWindow.h"
#include "project/Project.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QElapsedTimer>
#include <QTimer>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::app::checks {
namespace {

// ── 折叠判据的唯一实现 ────────────────────────────────────────────────
//
// 旧实现在 fold / side 两处各抄了一遍「探针字段是否全归零」，而且
// `if (folded || elapsed->elapsed() > 3000)` 走 TIMEOUT 分支时照样 `_Exit(0)`
// —— 只把 " TIMEOUT" 写进报告，退出码仍然报成功。
// 收敛到一处：**超时 = 没折叠 = FAIL**，退出码必须跟着走。

// 探针里给定的字段是否全部归零（side=0 / inspector=0 / bottom=0 …）。
[[nodiscard]] bool AllCollapsed(const QString& probe, const std::vector<QString>& keys) {
    for (const QString& key : keys) {
        if (!probe.contains(key + QStringLiteral("=0"))) {
            return false;
        }
    }
    return true;
}

// 落报告 → 刷缓冲 → 按判据退出（fold-ms / side-fold-ms / TIMEOUT 三个原有 token 保持不变）。
[[noreturn]] void FinishFold(const std::filesystem::path& out, const char* metric, qint64 ms,
                             bool folded, const QString& probe) {
    const std::string report = std::string{"[P03] "} + metric + "=" + std::to_string(ms) +
                               (folded ? "" : " TIMEOUT") + "\n" + probe.toStdString() +
                               (folded ? "\n[P03] overall: PASS\n" : "\n[P03] overall: FAIL\n");
    (void)shine::util::WriteFileBytes(out, report);
    std::printf("%s", report.c_str());
    std::fflush(nullptr);
    std::_Exit(folded ? 0 : 1);
}

// LayoutProbe() 恒产出的字段集：缺任一字段 = 探针没跑完或写盘被截断。
constexpr std::array<const char*, 14> kLayoutFields{
    "project=", "window=", "hsplitW=",  "sideVisible=", "pending=", "workspace=", "bottomTab=",
    "side=",    "sideNav=", "inspector=", "bottom=",     "hSizes=",  "docTabs=",  "docTab="};

} // namespace

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
            std::printf("[p03-bootstrap] created: %s\n[P03] overall: PASS\n",
                        shine::util::PathToUtf8(ref->rootDir).c_str());
        } else {
            // 建项目失败原来只 printf 就算完，随后应用带着空项目继续跑，
            // 下游截图自检照样退 0（假绿）。失败即刻退非零。
            std::printf("[p03-bootstrap] FAILED: %s\n[P03] overall: FAIL\n",
                        ref.error().message.c_str());
            std::fflush(nullptr);
            std::_Exit(1);
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
                const bool folded = AllCollapsed(probe, {QStringLiteral("inspector"),
                                                         QStringLiteral("bottom")});
                if (folded || elapsed->elapsed() > 3000) {
                    poll->stop();
                    FinishFold(foldOut, "fold-ms", elapsed->elapsed(), folded, probe);
                }
            });
            poll->start();
        });
    }
    // 侧栏折叠（Ctrl+B）判定：与 Ctrl+I / Ctrl+J 同路径，判据 = 中央区拿到侧栏让出的宽度。
    // 单独开一个开关，因为「侧栏收起」和「检查器收起」是两件事，混在一个探针里
    // 会出现「检查器已收、侧栏仍开」被误判成通过。
    if (const std::filesystem::path sideOut = EnvironmentPath(L"SHINE_P03_SIDE"); !sideOut.empty()) {
        QTimer::singleShot(800, &window, [&window, sideOut] {
            window.ToggleSidePanel();
            auto* elapsed = new QElapsedTimer();
            elapsed->start();
            auto* poll = new QTimer(&window);
            poll->setInterval(10);
            QObject::connect(poll, &QTimer::timeout, &window,
                             [&window, sideOut, elapsed, poll] {
                const QString probe = window.LayoutProbe();
                const bool folded = AllCollapsed(probe, {QStringLiteral("side")});
                if (folded || elapsed->elapsed() > 3000) {
                    poll->stop();
                    FinishFold(sideOut, "side-fold-ms", elapsed->elapsed(), folded, probe);
                }
            });
            poll->start();
        });
    }
}

void RegisterP03Probe(MainWindow& window) {
    // P03 验收设施：SHINE_P03_PROBE=<文件> 落布局探针后退出（同沙盒两跑比对 = 重启还原判据）
    //
    // 原来无断言恒退 0。单次运行能判的只有「探针真的落了盘、字段齐全」——
    // 「两跑比对」是**跨运行**判据，本进程无从判断，仍由 harness / 人比对两份文件。
    // 缺字段 = 探针没跑完或写盘被截断，那是真失败，退非零。
    if (const std::filesystem::path probeOut = EnvironmentPath(L"SHINE_P03_PROBE"); !probeOut.empty()) {
        QTimer::singleShot(1200, &window, [&window, probeOut] {
            window.PersistNow();
            const std::string probe = window.LayoutProbe().toStdString();
            std::string report = probe;
            bool ok = shine::util::WriteFileBytes(probeOut, probe);
            for (const char* field : kLayoutFields) {
                if (probe.find(field) == std::string::npos) {
                    report += std::string{"missing-field: "} + field + "\n";
                    ok = false;
                }
            }
            report += ok ? "[P03] overall: PASS\n" : "[P03] overall: FAIL\n";
            std::printf("%s", report.c_str());
            std::fflush(nullptr);
            std::_Exit(ok ? 0 : 1);
        });
    }
}

} // namespace shine::app::checks
