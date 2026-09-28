#include "pages/review/P09Review.h"

#include "pages/pipeline/GanttView.h"
#include "pages/pipeline/LedgerView.h"
#include "pages/pipeline/PipelineWorkspace.h"
#include "pages/pipeline/StopReportView.h"
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
    auto* empty = new PipelineWorkspace();
    empty->resize(1280, 820);
    empty->show();
    Grab(state, empty, "pipeline-empty");
    auto* workspace = new PipelineWorkspace();
    workspace->resize(1280, 820);
    workspace->show();
    workspace->LoadMock();
    Pump();
    Grab(state, workspace, "pipeline-normal");
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(0);
    Pump();
    Grab(state, workspace->Gantt(), "gantt-full");
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(1);
    Pump();
    Grab(state, workspace->Ledger(), "ledger-full");
    if (auto* tabs = workspace->findChild<QTabWidget*>()) tabs->setCurrentIndex(2);
    Pump();
    Grab(state, workspace->StopReport(), "stop-report");
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
    fs::create_directories(dir, ec);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
