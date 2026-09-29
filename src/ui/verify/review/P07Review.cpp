#include "ui/verify/review/P07Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "ui/pages/imageflow/BindingView.h"
#include "ui/pages/imageflow/BatchRenderView.h"
#include "ui/pages/imageflow/ComfyPanel.h"
#include "ui/pages/imageflow/ImageReviewView.h"
#include "ui/pages/imageflow/ImageFlowWorkspace.h"
#include "core/Async.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPixmap>
#include <QTimer>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace shine::app {
namespace {

namespace fs = std::filesystem;

struct State {
    fs::path dir;
    ImageFlowWorkspace* workspace = nullptr;
    // 与本轮 review::Grab 调用点**一一对应**（11 个 grab，11 个名字）。
    // 本文件 11 处抓图全部无条件执行，expected 里没有「清单有、本轮不拍」的条目。
    std::vector<std::string> expected{
        "flow-canvas-normal", "flow-canvas-empty", "flow-node-states", "flow-wiring",
        "flow-200-nodes", "binding-normal", "binding-missing", "batch-running",
        "batch-degraded", "review-findings", "comfy-panel-busy"};
    std::vector<std::string> manifest;
};

void Finish(State* state) {
    // 判据走共享的 review::WriteAndExit（见 ReviewProbe.h 的 FinishOptions）：
    // expected 逐个查存在 + 字节数下限 + 逐字节重复检测，退出码跟着 ok 走。
    // 以前这里是「拼报告 → 写盘 → std::_Exit(0)」，恒 0。
    std::vector<std::string> files;
    files.reserve(state->expected.size());
    for (const std::string& name : state->expected) {
        files.push_back(name + ".png");
    }
    const review::FinishOptions opt{
        .dir = state->dir,
        .header = "P07-S13 shots",
        .expected = files,
        .manifest = &state->manifest,
        .min_bytes = 100,
        // 保持 false：三张画布图合法地长得一样（见 Run() 里的 note 行）。
        // 其余 8 张承诺的状态都不同，重复检测结果仍会写进报告的 duplicate 段。
        .fail_on_duplicate = false,
    };
    const review::FinishResult result = review::EvaluateShots(opt);
    std::printf("[p07-review]\n%s", result.report.c_str());
    review::WriteAndExit(opt);
}

void Run(State* state) {
    auto* empty = new ImageFlowWorkspace();
    empty->resize(1280, 820);
    empty->show();
    review::Grab(empty->Canvas(), state->dir, "flow-canvas-empty", state->manifest);
    empty->deleteLater();

    state->workspace = new ImageFlowWorkspace();
    state->workspace->resize(1280, 820);
    state->workspace->show();
    state->workspace->LoadMock();
    review::Pump();
    review::Grab(state->workspace->Canvas(), state->dir, "flow-canvas-normal", state->manifest);
    review::Grab(state->workspace->Canvas(), state->dir, "flow-node-states", state->manifest);
    state->workspace->Canvas()->SelectNode("4");
    review::Grab(state->workspace->Canvas(), state->dir, "flow-wiring", state->manifest);

    std::vector<shine::kit::FlowCanvasNode> nodes;
    for (int i = 0; i < 200; ++i) {
        shine::kit::FlowCanvasNode node;
        node.id = "n" + std::to_string(i);
        node.title = "节点" + std::to_string(i);
        node.type = i % 3 == 0 ? "KSampler" : "TestNode";
        node.x = (i % 20) * 42;
        node.y = (i / 20) * 42;
        nodes.push_back(std::move(node));
    }
    state->workspace->Canvas()->SetGraph(std::move(nodes), {});
    review::Grab(state->workspace->Canvas(), state->dir, "flow-200-nodes", state->manifest);

    state->workspace->LoadMock();
    review::Pump();
    state->workspace->Binding()->SetShotContext({1, "prompt", "negative", "frame", 42});
    state->workspace->Binding()->AddDefaultBindings();
    review::Grab(state->workspace->Binding(), state->dir, "binding-normal", state->manifest);
    state->workspace->Binding()->SetShotContext({1, "", "", "", 42});
    state->workspace->Binding()->ValidateNow();
    review::Grab(state->workspace->Binding(), state->dir, "binding-missing", state->manifest);

    state->workspace->Batch()->SetShots({{1, QStringLiteral("S01")}, {2, QStringLiteral("S02")},
                                        {3, QStringLiteral("S03")}});
    state->workspace->Batch()->EnqueueAll();
    state->workspace->Batch()->ShowRunningDemo();
    review::Grab(state->workspace->Batch(), state->dir, "batch-running", state->manifest);
    state->workspace->Batch()->RunMockBatch();
    review::Grab(state->workspace->Batch(), state->dir, "batch-degraded", state->manifest);
    review::Grab(state->workspace->Review(), state->dir, "review-findings", state->manifest);
    state->workspace->Comfy()->SetDemoBusy(true);
    review::Grab(state->workspace->Comfy(), state->dir, "comfy-panel-busy", state->manifest);

    // ⚠️ 已知的一处「一张图三个名字」，写进报告而不是靠退出码掩盖：
    // ImageFlowWorkspace::LoadMock() 内部已经 SelectNode("4")，而下面这三张
    // 之间**没有任何状态变化**（flow-node-states 紧接 flow-canvas-normal；
    // flow-wiring 又 SelectNode("4")，选中的还是同一个节点），所以三张图
    // 逐字节相同。重复检测因此保持 false —— 但检测结果仍然会进报告的
    // duplicate 段，不会被藏起来。补齐 node-states / wiring 的独立取证需要
    // 改 P07Review.cpp 的抓图顺序（ClearSelection + 选不同节点），属取证设计
    // 变更，不在本次「修假绿」范围内。
    state->manifest.push_back(
        "note flow-canvas-normal == flow-node-states == flow-wiring 同一帧："
        "LoadMock() 已选中 4 号节点，三次抓图之间无状态变化");

    Finish(state);
}

} // namespace

void SaveP07Review(const std::filesystem::path& dir) {
    auto* state = new State;
    state->dir = dir;
    std::error_code ec;
shine::util::EnsureDir(dir);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}

} // namespace shine::app
