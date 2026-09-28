#include "ui/verify/checks/P08Checks.h"

#include "ui/pages/videoflow/ChainView.h"
#include "comfy/ComfyNodeDef.h"
#include "ui/pages/videoflow/FinalCutView.h"
#include "ui/pages/videoflow/VideoFlowWorkspace.h"
#include "ui/pages/videoflow/VideoTaskView.h"
#include "flow/BatchRender.h"
#include "flow/FlowValidator.h"
#include "flow/VideoCatalog.h"
#include "flow/VideoChain.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Random.h"
#include "visual/VideoTaskRunner.h"

#include <QApplication>
#include <QImage>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace shine::app::checks {
namespace {

struct Result {
    std::string content;
    int fails = 0;
    void Check(bool ok, const char* name, const char* detail) {
        content += std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
        fails += ok ? 0 : 1;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    }
};

void Save(const std::filesystem::path& path, const Result& result) {
    (void)util::WriteFileBytes(path, result.content + (result.fails == 0 ? "\n[P08] overall: PASS\n"
                                                                       : "\n[P08] overall: FAIL\n"));
    std::fflush(nullptr);
    std::_Exit(result.fails == 0 ? 0 : 1);
}

} // namespace

void RegisterP08Checks(MainWindow& window) {
    Q_UNUSED(window);
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P08_S1") == nullptr ? "" : std::getenv("SHINE_P08_S1")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            r.Check(shine::flow::IsVideoNodeType("H3Video") && shine::flow::IsVideoNodeType("RIFE") &&
                        !shine::flow::IsVideoNodeType("KSampler"),
                    "video-filter", "视频节点目录筛选");
            shine::comfy::NodeTypeDef h3;
            h3.className = "H3Video";
            h3.displayName = "H3 视频";
            const auto catalog = shine::flow::BuildVideoCatalog({h3});
            r.Check(catalog.size() == 1 && catalog[0].class_name == "H3Video", "catalog", "object_info 定义进入视频目录");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P08_S2") == nullptr ? "" : std::getenv("SHINE_P08_S2")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::app::VideoFlowWorkspace workspace;
            workspace.LoadMock();
            r.Check(workspace.Canvas()->NodeCount() == 4 && workspace.Canvas()->LinkCount() == 3,
                    "reuse-canvas", "视频工作区复用 P07 FlowCanvas");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P08_S3") == nullptr ? "" : std::getenv("SHINE_P08_S3")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::flow::GenerationValidationInput input;
            input.object_info_ready = true;
            input.width = 1001;
            input.height = 513;
            input.length = 20;
            input.reference_count = 10;
            const auto result = shine::flow::ValidateForSubmit(input, nullptr);
            r.Check(!result.ok && result.issues.size() >= 3, "video-validation", "32 对齐/n%17/参考图上限阻止提交");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P08_S4") == nullptr ? "" : std::getenv("SHINE_P08_S4")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto chain = shine::flow::BuildVideoChain({{1, "a", "b", ""}, {2, "b", "c", ""},
                                                               {3, "x", "d", ""}});
            r.Check(chain.links.size() == 2 && chain.links[0].connected && !chain.links[1].connected,
                    "chain", "首尾帧连贯/断链与 chain_ignored 明示");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P08_S5") == nullptr ? "" : std::getenv("SHINE_P08_S5")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            r.Check(shine::video::RunVideoQueueSelfCheck() == 0, "video-runner", "VideoTaskRunner 优先级/多产物/降级自检");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P08_S6") == nullptr ? "" : std::getenv("SHINE_P08_S6")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto root = std::filesystem::temp_directory_path() / ("shinetv-p08-frame-" + util::RandomHex(5));
            std::filesystem::create_directories(root);
            const auto image_path = root / "first.png";
            QImage image(32, 32, QImage::Format_RGB32);
            image.fill(Qt::red);
            r.Check(image.save(QString::fromStdString(image_path.string())), "shell-qimage", "首帧 PNG 可由 QImage 读取");
            shine::app::VideoTaskView view;
            view.SetShots({{1, QStringLiteral("S01")}});
            view.SetFirstFrame(1, image_path);
            view.EnqueueAll();
            r.Check(view.Probe().contains(QStringLiteral("frames=1")), "task-row", "任务行保留首帧缩略图来源");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P08_S7") == nullptr ? "" : std::getenv("SHINE_P08_S7")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::app::FinalCutView view;
            view.SetVideos({{1, QStringLiteral("S01.mp4")}, {2, QStringLiteral("S02.mp4")}});
            const auto root = std::filesystem::temp_directory_path() / ("shinetv-p08-final-" + util::RandomHex(5));
            r.Check(view.ExportScene(root), "final-export", "成片导出 output/videos/scene_playlist.txt");
            Save(out, r);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P08_S8") == nullptr ? "" : std::getenv("SHINE_P08_S8")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto manifest = std::filesystem::path{"build/_shots/P08/shots-manifest.txt"};
            const auto text = util::ReadFileBytes(manifest);
            r.Check(text && text->find("MISSING") == std::string::npos &&
                        text->find("video-flow-normal") != std::string::npos,
                    "visual-review", "P08 截图包清单完整");
            Save(out, r);
        });
    }
}

} // namespace shine::app::checks
