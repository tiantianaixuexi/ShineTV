#include "ui/verify/review/P08Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "ui/pages/videoflow/ChainView.h"
#include "ui/pages/videoflow/FinalCutView.h"
#include "ui/pages/videoflow/VideoTaskView.h"
#include "flow/VideoChain.h"
#include "ui/pages/videoflow/VideoFlowWorkspace.h"
#include "core/Async.h"
#include "util/Encoding.h"
#include "util/File.h"
#include <QTabWidget>

#include <QApplication>
#include <QPixmap>
#include <QTimer>

#include <filesystem>
#include <vector>
#include <string>
#include <system_error>

namespace shine::app {
namespace {
namespace fs = std::filesystem;
struct State {
    fs::path dir;
    std::vector<std::string> expected{"video-flow-normal", "video-flow-empty", "chain-connected",
                                      "chain-broken", "video-tasks-running", "final-videos"};
    std::vector<std::string> manifest;
};
void Run(State* state) {
    auto* workspace = new VideoFlowWorkspace();
    workspace->resize(1280, 820);
    workspace->show();
    review::Grab(workspace->Canvas(), state->dir, "video-flow-empty", state->manifest);
    workspace->LoadMock();
    review::Pump();
    review::Grab(workspace->Canvas(), state->dir, "video-flow-normal", state->manifest);
    review::Grab(workspace->Chain(), state->dir, "chain-connected", state->manifest);
    auto broken = shine::flow::BuildVideoChain({{1, "a", "b", ""}, {2, "x", "c", ""}});
    workspace->Chain()->SetChain(std::move(broken));
    review::Grab(workspace->Chain(), state->dir, "chain-broken", state->manifest);
    workspace->LoadMock();
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(1);
    review::Pump();
    workspace->Tasks()->ShowRunning();
    review::Grab(workspace->Tasks(), state->dir, "video-tasks-running", state->manifest);
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(2);
    review::Pump();
    review::Grab(workspace->Final(), state->dir, "final-videos", state->manifest);
    std::string report = "P08-S8 shots\n";
    for (const auto& line : state->manifest) report += line + "\n";
    for (const auto& name : state->expected) {
        if (!fs::is_regular_file(state->dir / (name + ".png"))) report += name + " MISSING\n";
    }
    (void)util::WriteFileBytes(state->dir / "shots-manifest.txt", report);
    std::fflush(nullptr);
    std::_Exit(0);
}
} // namespace
void SaveP08Review(const std::filesystem::path& dir) {
    auto* state = new State;
    state->dir = dir;
    std::error_code ec;
shine::util::EnsureDir(dir);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
