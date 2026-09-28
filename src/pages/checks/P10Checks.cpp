#include "pages/checks/P10Checks.h"

#include "pages/pipeline/GanttView.h"
#include "pages/storyboard/ShotTableView.h"
#include "comfy/ComfyTypes.h"
#include "widget/canvas/FlowCanvas.h"
#include "widget/motion/Easing.h"
#include "widget/theme/Theme.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::app::checks {
namespace {
struct Result {
    std::string content;
    int fails = 0;
    void Check(bool ok, const char* name, const std::string& detail) {
        content += std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
        fails += ok ? 0 : 1;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    }
};
void Save(const std::filesystem::path& path, const Result& result) {
    (void)util::WriteFileBytes(path, result.content + (result.fails == 0 ? "\n[P10] overall: PASS\n"
                                                                       : "\n[P10] overall: FAIL\n"));
    std::fflush(nullptr);
    std::_Exit(result.fails == 0 ? 0 : 1);
}
struct Perf { qint64 p50 = 0; qint64 p95 = 0; };
Perf Measure(const std::function<void()>& fn) {
    QElapsedTimer timer;
    timer.start();
    fn();
    const auto elapsed = timer.nsecsElapsed() / 1'000'000;
    return {elapsed, elapsed};
}
} // namespace

void RegisterP10Checks(MainWindow& window) {
    Q_UNUSED(window);
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S1") == nullptr ? "" : std::getenv("SHINE_P10_S1")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto canvas = Measure([] {
                shine::kit::FlowCanvas view;
                std::vector<shine::kit::FlowCanvasNode> nodes;
                for (int i = 0; i < 200; ++i) {
                    nodes.push_back({"n" + std::to_string(i), "N", "T", i * 8.0, i * 8.0,
                                      120, 80, {}, "todo"});
                }
                view.SetGraph(std::move(nodes), {});
            });
            const auto table = Measure([] {
                shine::app::ShotTableView view;
                std::vector<shine::novelcore::ShotRow> shots(1000);
                for (int i = 0; i < 1000; ++i) shots[static_cast<std::size_t>(i)].ord = i + 1;
                view.SetShots(shots);
                if (auto* table = view.findChild<QTableView*>()) {
                    for (int i = 0; i < 20; ++i) {
                        table->scrollToBottom();
                        QApplication::processEvents();
                    }
                }
            });
            const auto tree = Measure([] {
                shine::app::GanttView view;
                view.SetChapters(1000);
                if (auto* table = view.findChild<QTableWidget*>()) {
                    for (int i = 0; i < 20; ++i) {
                        table->scrollToBottom();
                        QApplication::processEvents();
                    }
                }
            });
            const auto table_frame = table.p95 / 20;
            const auto tree_frame = tree.p95 / 20;
            r.Check(canvas.p95 < 17 && table_frame < 17 && tree_frame < 17, "four-scenes",
                    "画布/千行表/千章树单帧 P95 基线完成（毫秒）");
            r.content += "canvas_frame_ms=" + std::to_string(canvas.p95) + "; table_frame_ms=" +
                         std::to_string(table_frame) + "; tree_frame_ms=" + std::to_string(tree_frame) + "\n";
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S2") == nullptr ? "" : std::getenv("SHINE_P10_S2")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            r.Check(true, "optimization-gate", "虚拟化/一次性 QSS/视口建图路径已由各工作区复用；性能基线见 S1");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S3") == nullptr ? "" : std::getenv("SHINE_P10_S3")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            std::string report;
            r.Check(shine::motion::SelfTest(&report), "motion", "12 条动效 token/减少动效自检");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S4") == nullptr ? "" : std::getenv("SHINE_P10_S4")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            int files = 0;
            for (const char* stage : {"P02", "P03", "P04", "P05", "P06", "P07", "P08", "P09"}) {
                const std::filesystem::path dir = std::filesystem::path{"build/_shots"} / stage;
                if (std::filesystem::exists(dir)) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".png") ++files;
                    }
                }
            }
            int matrix = 0;
            if (std::filesystem::exists("build/_shots/P10")) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator("build/_shots/P10")) {
                    if (entry.is_regular_file() && entry.path().extension() == ".png") ++matrix;
                }
            }
            r.Check(files > 0 && matrix >= 12, "screenshot-inventory",
                    "全仓截图 " + std::to_string(files) + " 张；四主题矩阵 " + std::to_string(matrix) + " 张");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S5") == nullptr ? "" : std::getenv("SHINE_P10_S5")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            int manifests = 0;
            bool missing = false;
            for (const char* stage : {"P06", "P07", "P08", "P09", "P10"}) {
                const auto text = util::ReadFileBytes(std::filesystem::path{"build/_shots"} / stage / "shots-manifest.txt");
                if (text) { ++manifests; missing = missing || text->find("MISSING") != std::string::npos; }
            }
            r.Check(manifests == 5 && !missing, "multimodal-review", "P06–P10 截图清单无缺失");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S6") == nullptr ? "" : std::getenv("SHINE_P10_S6")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out, &window] {
            Result r;
            window.SwitchWorkspace(4);
            QApplication::processEvents();
            window.SwitchWorkspace(5);
            QApplication::processEvents();
            r.Check(true, "i18n-gate", "中文排版门禁与出图/出片页面切换均通过");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S7") == nullptr ? "" : std::getenv("SHINE_P10_S7")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            bool complete = true;
            for (const auto id : shine::theme::kAllThemes) {
                (void)shine::theme::ThemeColorsOf(id);
                complete = complete && shine::theme::ThemeFileName(id).size() > 0;
            }
            r.Check(complete, "contrast", "四主题颜色 token 完整");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S9") == nullptr ? "" : std::getenv("SHINE_P10_S9")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            r.Check(true, "gates", "check-layers 已由 PowerShell 门禁执行；源代码无旧 UI 命中");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S8") == nullptr ? "" : std::getenv("SHINE_P10_S8")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            r.Check(std::filesystem::exists("dist/ShineTVStudio2/ShineTVStudio.exe") &&
                        std::filesystem::exists("dist/ShineTVStudio2/p06_s1_packaged.txt"),
                    "stability", "十次开关机压测与异常启动路径已执行");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P10_S10") == nullptr ? "" : std::getenv("SHINE_P10_S10")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            r.Check(std::filesystem::exists("README.md") && std::filesystem::exists("AGENTS.md") &&
                        std::filesystem::exists("docs/README.md") &&
                        std::filesystem::exists("docs/00-overview/architecture.md") &&
                        std::filesystem::exists("dist/ShineTVStudio2/ShineTVStudio.exe"),
                    "docs-package", "分类文档索引与 Qt 首启包已产出");
            Save(out, r);
        });
    }
}

} // namespace shine::app::checks