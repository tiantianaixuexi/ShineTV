#include "ui/verify/checks/P07Checks.h"

#include "ui/pages/imageflow/BatchRenderView.h"
#include "ui/pages/imageflow/BindingView.h"
#include "ui/pages/imageflow/ImageFlowWorkspace.h"
#include "ui/pages/imageflow/ImageReviewView.h"
#include "comfy/ComfyClient.h"
#include "comfy/ComfyQueueModel.h"
#include "comfy/ComfySocket.h"
#include "flow/BatchRender.h"
#include "flow/FlowBinder.h"
#include "flow/FlowValidator.h"
#include "flow/GraphCompiler.h"
#include "flow/GraphHost.h"
#include "flow/ImageReview.h"
#include "flow/WorkflowIO.h"
#include "ui/kit/canvas/FlowCanvas.h"
#include "novel/NovelPromptGen.h"
#include "util/File.h"
#include "util/Random.h"
#include "util/Encoding.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSlider>
#include <QTimer>

#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

namespace shine::app::checks {
namespace {



void Save(const std::filesystem::path& path, const std::string& content, int fails) {
    (void)util::WriteFileBytes(path, content + (fails == 0 ? "\n[P07] overall: PASS\n"
                                                          : "\n[P07] overall: FAIL\n"));
    std::fflush(nullptr);
    std::_Exit(fails == 0 ? 0 : 1);
}

struct Result {
    std::string content;
    int fails = 0;
    void Check(bool ok, const std::string& name, const std::string& detail) {
        content += std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name + " — " + detail + "\n";
        fails += ok ? 0 : 1;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name.c_str());
    }
};

void Pump() { QApplication::processEvents(); }

} // namespace

void RegisterP07Checks(MainWindow& window) {
    Q_UNUSED(window);
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S1") == nullptr ? "" : std::getenv("SHINE_P07_S1")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::comfy::QueueResult queue;
            r.Check(shine::comfy::ParseQueueJson(
                        R"({"queue_running":[[1,"p1",0,{"create_time":1}]],"queue_pending":[[2,"p2",1,{}]]})", queue) &&
                        queue.running.size() == 1 && queue.pending.size() == 1,
                    "queue", "REST 队列运行/排队行可解析");
            shine::comfy::HistoryResult history;
            r.Check(shine::comfy::ParseHistoryJson(
                        R"({"p1":{"status":{"status_str":"success","completed_at":3},"outputs":{}}})", 10, history) &&
                        history.entries.size() == 1 && !history.entries[0].failed,
                    "history", "历史成功/媒体账可解析");
            shine::comfy::SystemStatsResult stats;
            r.Check(shine::comfy::ParseSystemStatsJson(
                        R"({"devices":[{"name":"GPU","vram_total":1024,"vram_free":512}]})", stats) &&
                        stats.deviceName == "GPU" && stats.FreeGb() > 0,
                    "stats", "VRAM 统计可解析");
            shine::comfy::PromptSubmitResult submit;
            r.Check(shine::comfy::ParsePromptSubmitJson(R"({"prompt_id":"p1","number":1})", submit) &&
                        submit.ok && submit.promptId == "p1",
                    "submit", "prompt_id / number 可解析");
            r.Check(shine::comfy::NormalizeBaseUrl("127.0.0.1:8188/") == "http://127.0.0.1:8188" &&
                        shine::comfy::BuildViewUrl("http://x", "a b.png", "s", "output").find("%20") != std::string::npos,
                    "url", "健康/下载 URL 规整");
            shine::comfy::QueueModel model;
            model.ApplyQueueResult(queue);
            const auto counts = model.CountsSnapshot();
            r.Check(counts.pending == 1 && counts.busy(), "busy", "忙碌 ≠ 卡死：pending/running 状态可见");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S2") == nullptr ? "" : std::getenv("SHINE_P07_S2")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const std::vector<std::string> events{
                R"({"type":"execution_start","data":{"prompt_id":"p"}})",
                R"({"type":"progress","data":{"prompt_id":"p","node":"4","value":2,"max":10}})",
                R"({"type":"progress_state","data":{"prompt_id":"p","nodes":{"4":{"value":3,"max":10}}}})",
                R"({"type":"executing","data":{"prompt_id":"p","node":"4","node_type":"KSampler"}})",
                R"({"type":"executed","data":{"prompt_id":"p","node":"4","output":{"images":[{"filename":"x.png","subfolder":"","type":"output"}]}}})",
                R"({"type":"execution_success","data":{"prompt_id":"p"}})",
                R"({"type":"execution_error","data":{"prompt_id":"p","node_id":"4","node_type":"KSampler","exception_type":"E","exception_message":"bad","traceback":["a","b"],"executed":["1"]}})",
                R"({"type":"execution_interrupted","data":{"prompt_id":"p","node_id":"4"}})",
                R"({"type":"execution_cached","data":{"prompt_id":"p","nodes":["1","2"]}})"};
            bool all = true;
            for (const auto& event : events) {
                shine::comfy::PromptEvent parsed;
                all = all && shine::comfy::ParsePromptEventJson(event, parsed);
            }
            r.Check(all, "ws-events", "9 类 Prompt/进度/执行事件全量映射");
            shine::comfy::StatusEvent status;
            r.Check(shine::comfy::ParseStatusJson(R"({"type":"status","data":{"exec_info":{"queue_remaining":3}}})", status) &&
                        status.execInfoQueueRemaining == 3,
                    "ws-status", "status 队列事件");
            std::string binary(8, '\0');
            binary += "\x89PNG\r\n\x1a\n";
            binary += "x";
            const auto frame = shine::comfy::ParseBinaryPreview(binary);
            r.Check(frame.format == "png" && frame.payload.size() == 9, "ws-binary", "二进制预览帧可识别 PNG");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S3") == nullptr ? "" : std::getenv("SHINE_P07_S3")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::flow::Init();
            shine::flow::ClearGraph();
            shine::comfy::ObjectInfoResult info;
            const std::string object_info = R"({"KSampler":{"input":{"required":{}},"output":[],"name":"KSampler","display_name":"KSampler"}})";
            r.Check(shine::comfy::ParseObjectInfoJson(object_info, info) && info.nodes.size() == 1,
                    "object-info", "/object_info 结构化节点目录");
            shine::flow::RegisterComfyNodes(info.nodes);
            r.Check(shine::flow::RegisteredComfyNodeCount() >= 1 && shine::flow::IsRegistered("KSampler"),
                    "catalog", "节点目录注册/查询");
            r.Check(shine::flow::SpawnNode("KSampler", 10, 10) && shine::flow::NodeCount() == 1,
                    "spawn", "按 object_info 类型建节点");
            shine::flow::Shutdown();
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S4") == nullptr ? "" : std::getenv("SHINE_P07_S4")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::flow::Init();
            shine::flow::ClearGraph();
            shine::comfy::ObjectInfoResult info;
            shine::comfy::ParseObjectInfoJson(
                R"({"KSampler":{"input":{"required":{}},"output":[],"name":"KSampler","display_name":"KSampler"}})", info);
            shine::flow::RegisterComfyNodes(info.nodes);
            const auto report = shine::flow::ImportApiJson(R"({"1":{"class_type":"KSampler","inputs":{"seed":1}}})");
            const auto first = shine::flow::CompileToApiJson();
            const auto second = shine::flow::CompileToApiJson();
            r.Check(report.ok && report.nodes == 1, "import", "API JSON 导入图模型");
            r.Check(first.ok && second.ok && first.apiJson == second.apiJson, "roundtrip", "编译→再编译逐字节稳定");
            r.Check(shine::flow::DetectFormat(R"({"1":{"class_type":"KSampler","inputs":{}}})") == shine::flow::WorkflowFormat::ApiFormat,
                    "format", "按内容识别 API 格式");
            shine::flow::Shutdown();
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S5") == nullptr ? "" : std::getenv("SHINE_P07_S5")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::app::ImageFlowWorkspace workspace;
            workspace.LoadMock();
            Pump();
            r.Check(workspace.Canvas()->NodeCount() == 6 && workspace.Canvas()->LinkCount() == 4,
                    "canvas", "节点/端口/连线只读渲染");
            std::vector<shine::kit::FlowCanvasNode> nodes;
            for (int i = 0; i < 200; ++i) {
                shine::kit::FlowCanvasNode n;
                n.id = "n" + std::to_string(i);
                n.title = "节点" + std::to_string(i);
                n.type = "Test";
                n.x = (i % 20) * 40;
                n.y = (i / 20) * 40;
                nodes.push_back(std::move(n));
            }
            QElapsedTimer timer;
            timer.start();
            workspace.Canvas()->SetGraph(std::move(nodes), {});
            const auto elapsed = timer.elapsed();
            r.Check(workspace.Canvas()->NodeCount() == 200 && elapsed < 2000,
                    "200-nodes", "200 节点建图渲染在 2 秒内完成");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S6") == nullptr ? "" : std::getenv("SHINE_P07_S6")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::kit::FlowCanvas canvas;
            canvas.SetGraph({{"1", "A", "A", 0, 0, 180, 120, {{"out", "IMAGE", false}}, "todo"},
                              {"2", "B", "B", 260, 0, 180, 120, {{"in", "IMAGE", true}}, "todo"}},
                             {{"1", "out", "2", "in", "IMAGE"}});
            const bool wiring = canvas.Links().size() == 1 && canvas.Links()[0].type == "IMAGE";
            canvas.SelectNode("1");
            const auto selected = canvas.SelectedCount();
            canvas.DuplicateSelected();
            const auto duplicated = canvas.NodeCount();
            canvas.SelectNode("1");
            canvas.DeleteSelected();
            r.Check(selected == 1 && duplicated == 3 && canvas.NodeCount() == 2,
                    "interaction", "选择/复制/删除");
            r.Check(wiring, "wiring", "类型同源连线创建与复检");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S7") == nullptr ? "" : std::getenv("SHINE_P07_S7")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::app::ImageFlowWorkspace workspace;
            workspace.LoadMock();
            Pump();
            r.Check(workspace.Canvas()->HasNativeEditor(),
                    "native-controls", "KSampler 原生 QSlider 通过 QGraphicsProxyWidget 嵌入");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S8") == nullptr ? "" : std::getenv("SHINE_P07_S8")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::app::BindingView view;
            view.SetShotContext({7, "prompt", "negative", "frame", 42});
            view.AddDefaultBindings();
            r.Check(view.Probe().contains(QStringLiteral("issues=0")), "binding-normal", "正常镜头绑定无阻塞错误");
            view.SetShotContext({7, "", "", "", 42});
            view.ValidateNow();
            r.Check(view.Probe().contains(QStringLiteral("issues=1")), "binding-missing", "缺 prompt 在提交前报出");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S9") == nullptr ? "" : std::getenv("SHINE_P07_S9")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::flow::GenerationValidationInput input;
            input.object_info_ready = false;
            input.width = 1000;
            input.height = 500;
            input.length = 20;
            input.reference_count = 10;
            const auto result = shine::flow::ValidateForSubmit(input, nullptr);
            r.Check(!result.ok && result.issues.size() >= 3, "k19-k21", "object_info/尺寸/帧数/参考图均阻止提交");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S10") == nullptr ? "" : std::getenv("SHINE_P07_S10")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            shine::flow::BatchRenderQueue queue;
            queue.Add(2, "video", "flow", shine::flow::BatchPriority::ShotVideo);
            queue.Add(1, "asset", "flow", shine::flow::BatchPriority::Asset);
            queue.Add(3, "image", "flow", shine::flow::BatchPriority::SceneImage);
            queue.StartNext();
            const auto jobs = queue.Snapshot();
            const auto running = std::find_if(jobs.begin(), jobs.end(), [](const auto& j) { return j.state == shine::flow::BatchState::Running; });
            r.Check(running != jobs.end() && running->label == "asset", "priority", "Asset(0) < SceneImage(10) < ShotVideo(20)");
            queue.Complete(running->id, 1, "no_controlnet");
            r.Check(queue.DegradationLedger().size() == 1, "degradation", "降级账写入");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S11") == nullptr ? "" : std::getenv("SHINE_P07_S11")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto root = std::filesystem::temp_directory_path() / ("shinetv-p07-review-" + util::RandomHex(6));
            std::filesystem::create_directories(root);
            shine::app::ImageReviewView view;
            view.SetReviewer([](const auto&, shine::flow::ImageReviewError&) {
                shine::flow::ImageReviewReport report;
                report.model = "mock";
                report.findings = {{"face", "脸部", true, "ok", {}}, {"hands", "手部", false, "bad", {}}};
                return std::optional<shine::flow::ImageReviewReport>{std::move(report)};
            });
            view.SetInput({1, "a.png", {}}, (root / "generation_checks.json").string());
            view.Run();
            r.Check(view.Probe().contains(QStringLiteral("has_report=1")) &&
                        std::filesystem::is_regular_file(root / "generation_checks.json"),
                    "image-review", "五项评审报告落 generation_checks");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S12") == nullptr ? "" : std::getenv("SHINE_P07_S12")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            r.Check(shine::novelcore::RunPromptGenSelfCheck(), "prompt-versions", "PV1–PV7 哈希失效/多版本账自检");
            Save(out, r.content, r.fails);
        });
    }
    if (const std::filesystem::path out = util::PathFromUtf8(
            std::getenv("SHINE_P07_S13") == nullptr ? "" : std::getenv("SHINE_P07_S13")); !out.empty()) {
        QTimer::singleShot(300, qApp, [out] {
            Result r;
            const auto manifest = std::filesystem::path{"build/_shots/P07/shots-manifest.txt"};
            const auto text = util::ReadFileBytes(manifest);
            r.Check(text && text->find("flow-canvas-normal") != std::string::npos &&
                        text->find("MISSING") == std::string::npos,
                    "visual-review", "P07 截图包清单完整");
            Save(out, r.content, r.fails);
        });
    }
}

} // namespace shine::app::checks
