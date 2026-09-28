#include "ui/verify/review/P10Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "ui/pages/imageflow/ImageFlowWorkspace.h"
#include "ui/pages/pipeline/PipelineWorkspace.h"
#include "ui/pages/videoflow/VideoFlowWorkspace.h"
#include "core/Async.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
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
void Run(State* state) {
    const std::array<shine::theme::ThemeId, 4> themes = shine::theme::kAllThemes;
    for (auto id : themes) {
        const std::string suffix = std::string(shine::theme::ThemeFileName(id));
        shine::theme::ThemeService::Switch(id, false);
        auto* pipeline = new PipelineWorkspace();
        pipeline->resize(1280, 820); pipeline->show(); pipeline->LoadMock(); review::Pump();
        review::Grab(pipeline, state->dir, "pipeline-" + suffix, state->manifest);
        auto* image = new ImageFlowWorkspace();
        image->resize(1280, 820); image->show(); image->LoadMock(); review::Pump();
        review::Grab(image, state->dir, "imageflow-" + suffix, state->manifest);
        auto* video = new VideoFlowWorkspace();
        video->resize(1280, 820); video->show(); video->LoadMock(); review::Pump();
        review::Grab(video, state->dir, "videoflow-" + suffix, state->manifest);
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
shine::util::EnsureDir(dir);
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
