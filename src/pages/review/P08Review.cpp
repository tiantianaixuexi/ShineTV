#include "pages/review/P08Review.h"

#include "pages/videoflow/ChainView.h"
#include "pages/videoflow/FinalCutView.h"
#include "pages/videoflow/VideoTaskView.h"
#include "flow/VideoChain.h"
#include "pages/videoflow/VideoFlowWorkspace.h"
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
void Pump() {
    shine::async::DrainUiQueue();
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}
void Grab(State* state, QWidget* widget, const std::string& name) {
    Pump();
    if (widget == nullptr) { state->manifest.push_back(name + " 0 FAILED null-widget"); return; }
    const auto path = state->dir / (name + ".png");
    const bool ok = widget->grab().save(QString::fromStdString(util::PathToUtf8(path)), "PNG");
    const auto bytes = util::ReadFileBytes(path);
    state->manifest.push_back(name + " " + std::to_string(bytes.value_or(std::string{}).size()) + (ok ? " saved" : " FAILED"));
}
void Run(State* state) {
    auto* workspace = new VideoFlowWorkspace();
    workspace->resize(1280, 820);
    workspace->show();
    Grab(state, workspace->Canvas(), "video-flow-empty");
    workspace->LoadMock();
    Pump();
    Grab(state, workspace->Canvas(), "video-flow-normal");
    Grab(state, workspace->Chain(), "chain-connected");
    auto broken = shine::flow::BuildVideoChain({{1, "a", "b", ""}, {2, "x", "c", ""}});
    workspace->Chain()->SetChain(std::move(broken));
    Grab(state, workspace->Chain(), "chain-broken");
    workspace->LoadMock();
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(1);
    Pump();
    workspace->Tasks()->ShowRunning();
    Grab(state, workspace->Tasks(), "video-tasks-running");
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(2);
    Pump();
    Grab(state, workspace->Final(), "final-videos");
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
    fs::create_directories(dir, ec);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
