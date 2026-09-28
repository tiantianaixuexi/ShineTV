#include "ui/verify/review/P09Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "ui/pages/pipeline/GanttView.h"
#include "ui/pages/pipeline/LedgerView.h"
#include "ui/pages/pipeline/PipelineWorkspace.h"
#include "ui/pages/pipeline/StopReportView.h"
#include "core/Async.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QPixmap>
#include <QTabWidget>
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
    std::vector<std::string> expected{"pipeline-normal", "pipeline-empty", "gantt-full", "ledger-full", "stop-report"};
    std::vector<std::string> manifest;
};
void Run(State* state) {
    auto* empty = new PipelineWorkspace();
    empty->resize(1280, 820);
    empty->show();
    review::Grab(empty, state->dir, "pipeline-empty", state->manifest);
    auto* workspace = new PipelineWorkspace();
    workspace->resize(1280, 820);
    workspace->show();
    workspace->LoadMock();
    review::Pump();
    review::Grab(workspace, state->dir, "pipeline-normal", state->manifest);
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(0);
    review::Pump();
    review::Grab(workspace->Gantt(), state->dir, "gantt-full", state->manifest);
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(1);
    review::Pump();
    review::Grab(workspace->Ledger(), state->dir, "ledger-full", state->manifest);
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(2);
    review::Pump();
    review::Grab(workspace->StopReport(), state->dir, "stop-report", state->manifest);
    std::string report = "P09-S10 shots\n";
    for (const auto& line : state->manifest) report += line + "\n";
    for (const auto& name : state->expected) if (!fs::is_regular_file(state->dir / (name + ".png"))) report += name + " MISSING\n";
    (void)util::WriteFileBytes(state->dir / "shots-manifest.txt", report);
    std::fflush(nullptr);
    std::_Exit(0);
}
} // namespace
void SaveP09Review(const std::filesystem::path& dir) {
    auto* state = new State;
    state->dir = dir;
    std::error_code ec;
shine::util::EnsureDir(dir);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
