#include "pages/review/P10Review.h"

#include "pages/imageflow/ImageFlowWorkspace.h"
#include "pages/pipeline/PipelineWorkspace.h"
#include "pages/videoflow/VideoFlowWorkspace.h"
#include "core/Async.h"
#include "widget/theme/Theme.h"
#include "widget/theme/ThemeService.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QPixmap>
#include <QTimer>
#include <array>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace shine::app {
namespace {
namespace fs = std::filesystem;
struct State { fs::path dir; std::vector<std::string> manifest; };
void Pump() {
    shine::async::DrainUiQueue();
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}
void Grab(State* state, QWidget* widget, const std::string& name) {
    Pump();
    const auto path = state->dir / (name + ".png");
    const bool ok = widget != nullptr && widget->grab().save(QString::fromStdString(util::PathToUtf8(path)), "PNG");
    const auto bytes = util::ReadFileBytes(path);
    state->manifest.push_back(name + " " + std::to_string(bytes.value_or(std::string{}).size()) + (ok ? " saved" : " FAILED"));
}
void Run(State* state) {
    const std::array<shine::theme::ThemeId, 4> themes = shine::theme::kAllThemes;
    for (auto id : themes) {
        const std::string suffix = std::string(shine::theme::ThemeFileName(id));
        shine::theme::ThemeService::Switch(id, false);
        auto* pipeline = new PipelineWorkspace();
        pipeline->resize(1280, 820); pipeline->show(); pipeline->LoadMock(); Pump();
        Grab(state, pipeline, "pipeline-" + suffix);
        auto* image = new ImageFlowWorkspace();
        image->resize(1280, 820); image->show(); image->LoadMock(); Pump();
        Grab(state, image, "imageflow-" + suffix);
        auto* video = new VideoFlowWorkspace();
        video->resize(1280, 820); video->show(); video->LoadMock(); Pump();
        Grab(state, video, "videoflow-" + suffix);
        delete pipeline; delete image; delete video;
    }
    std::string report = "P10-S4/S5 theme matrix\n";
    for (const auto& line : state->manifest) report += line + "\n";
    (void)util::WriteFileBytes(state->dir / "shots-manifest.txt", report);
    std::fflush(nullptr);
    std::_Exit(0);
}
} // namespace
void SaveP10Review(const std::filesystem::path& dir) {
    auto* state = new State; state->dir = dir;
    std::error_code ec; fs::create_directories(dir, ec); shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
