#include "pages/review/P07Review.h"

#include "pages/imageflow/BindingView.h"
#include "pages/imageflow/BatchRenderView.h"
#include "pages/imageflow/ComfyPanel.h"
#include "pages/imageflow/ImageReviewView.h"
#include "pages/imageflow/ImageFlowWorkspace.h"
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

void Pump() {
    shine::async::DrainUiQueue();
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}

void Grab(State* state, QWidget* widget, const std::string& name) {
    Pump();
    if (widget == nullptr) {
        state->manifest.push_back(name + " 0 FAILED null-widget");
        return;
    }
    widget->repaint();
    const QPixmap pixmap = widget->grab();
    const fs::path path = state->dir / (name + ".png");
    const bool ok = pixmap.save(QString::fromStdString(util::PathToUtf8(path)), "PNG");
    const auto bytes = util::ReadFileBytes(path);
    state->manifest.push_back(name + " " + std::to_string(bytes.value_or(std::string{}).size()) +
                              (ok ? " saved" : " FAILED"));
}

void Run(State* state) {
    auto* empty = new ImageFlowWorkspace();
    empty->resize(1280, 820);
    empty->show();
    Grab(state, empty->Canvas(), "flow-canvas-empty");
    empty->deleteLater();

    state->workspace = new ImageFlowWorkspace();
    state->workspace->resize(1280, 820);
    state->workspace->show();
    state->workspace->LoadMock();
    Pump();
    Grab(state, state->workspace->Canvas(), "flow-canvas-normal");
    Grab(state, state->workspace->Canvas(), "flow-node-states");
    state->workspace->Canvas()->SelectNode("4");
    Grab(state, state->workspace->Canvas(), "flow-wiring");

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
    Grab(state, state->workspace->Canvas(), "flow-200-nodes");

    state->workspace->LoadMock();
    Pump();
    state->workspace->Binding()->SetShotContext({1, "prompt", "negative", "frame", 42});
    state->workspace->Binding()->AddDefaultBindings();
    Grab(state, state->workspace->Binding(), "binding-normal");
    state->workspace->Binding()->SetShotContext({1, "", "", "", 42});
    state->workspace->Binding()->ValidateNow();
    Grab(state, state->workspace->Binding(), "binding-missing");

    state->workspace->Batch()->SetShots({{1, QStringLiteral("S01")}, {2, QStringLiteral("S02")},
                                        {3, QStringLiteral("S03")}});
    state->workspace->Batch()->EnqueueAll();
    state->workspace->Batch()->ShowRunningDemo();
    Grab(state, state->workspace->Batch(), "batch-running");
    state->workspace->Batch()->RunMockBatch();
    Grab(state, state->workspace->Batch(), "batch-degraded");
    Grab(state, state->workspace->Review(), "review-findings");
    state->workspace->Comfy()->SetDemoBusy(true);
    Grab(state, state->workspace->Comfy(), "comfy-panel-busy");

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
    fs::create_directories(dir, ec);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}

} // namespace shine::app
