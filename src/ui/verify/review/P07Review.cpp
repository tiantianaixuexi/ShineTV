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
    std::vector<std::string> expected{
        "flow-canvas-normal", "flow-canvas-empty", "flow-node-states", "flow-wiring",
        "flow-200-nodes", "binding-normal", "binding-missing", "batch-running",
        "batch-degraded", "review-findings", "comfy-panel-busy"};
    std::vector<std::string> manifest;
};

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

    std::string report = "P07-S13 shots\n";
    for (const auto& line : state->manifest) report += line + "\n";
    for (const auto& name : state->expected) {
        if (!fs::is_regular_file(state->dir / (name + ".png"))) report += name + " MISSING\n";
    }
    (void)util::WriteFileBytes(state->dir / "shots-manifest.txt", report);
    std::fflush(nullptr);
    std::_Exit(0);
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
