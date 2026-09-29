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
#include <QByteArray>
#include <QCryptographicHash>
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include <array>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::app {
namespace {
namespace fs = std::filesystem;
// 逐字节摘要。只用于「同套主题下的三张页面图互不相同」这一条：
// FinishOptions.fail_on_duplicate 只能全局两两比，而本轮是**同控件跨主题**矩阵
// （每个页面在 5 套主题下各拍一张），两套主题若恰好同色，图片合法地可以一样，
// 所以全局判重必须关掉；能在不误伤的前提下保住的那部分检测就是这一条。
std::string Digest(const fs::path& path) {
    const auto bytes = util::ReadFileBytes(path);
    if (!bytes) {
        return {};
    }
    const QByteArray raw(reinterpret_cast<const char*>(bytes->data()),
                          static_cast<qsizetype>(bytes->size()));
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex())
        .toStdString();
}

struct State {
    fs::path dir;
    // 本轮真正 grab 的图。**清单在 grab 的当场登记**（见 Run 里的 shot lambda），
    // 不另抄一份手写名单 —— 抄第二份就一定会走散，「加了 grab 忘了加清单」和
    // 「清单里有、这轮不拍」正是验收假绿的两种典型形态。
    std::vector<std::string> expected;
    // 每套主题下的三张（子视图名，不含扩展名），按 pipeline / imageflow / videoflow 顺序。
    std::vector<std::vector<std::string>> theme_triples;
    std::vector<std::string> manifest;
    std::vector<std::pair<std::string, bool>> extra;
};

void Finish(State* state) {
    // 主题数写死过一次就再没跟着 kAllThemes 变过：清单退化成空表时，
    // 「一张都没拍」会变成 overall=PASS。这里把「N 套主题 × 3 个页面」钉成判据。
    state->extra.push_back(
        {"theme-matrix-size " + std::to_string(state->theme_triples.size()) + "x3",
         !state->theme_triples.empty() && state->expected.size() == state->theme_triples.size() * 3});
    for (const std::vector<std::string>& triple : state->theme_triples) {
        bool distinct = triple.size() == 3;
        if (distinct) {
            const std::array<std::string, 3> d{Digest(state->dir / (triple[0] + ".png")),
                                              Digest(state->dir / (triple[1] + ".png")),
                                              Digest(state->dir / (triple[2] + ".png"))};
            distinct = !d[0].empty() && d[0] != d[1] && d[0] != d[2] && d[1] != d[2];
        }
        state->extra.push_back(
            {"theme-distinct " + (triple.empty() ? std::string("(none)") : triple.front()), distinct});
    }

    std::vector<std::string> files;
    files.reserve(state->expected.size());
    for (const std::string& stem : state->expected) {
        files.push_back(stem + ".png");
    }
    const review::FinishOptions opt{
        .dir = state->dir,
        // 报告首行就说清判重口径，免得看报告的人把「没报重复」读成「不查重复」。
        .header = "P10-S4/S5 theme matrix — 同控件跨主题：两套主题若恰好同色则合法同图，"
                  "故 fail_on_duplicate=false；同套主题下三张是否互异见 theme-distinct 行",
        .expected = files,
        .manifest = &state->manifest,
        .min_bytes = 100,
        // 见上方 Digest 的注释：跨主题的合法重复会让全局判重误伤，故关闭。
        .fail_on_duplicate = false,
        .extra = state->extra,
    };
    const review::FinishResult result = review::EvaluateShots(opt);
    std::printf("[p10-review]\n%s", result.report.c_str());
    review::WriteAndExit(opt);
}

void Run(State* state) {
    // 主题数跟着 theme::kAllThemes 走（加第 5 套「水墨」后是 5），不再写死 4
    const auto themes = shine::theme::kAllThemes;
    for (auto id : themes) {
        const std::string suffix(shine::theme::ThemeFileName(id));
        shine::theme::ThemeService::Switch(id, false);
        // 名字只写一次：登记进 expected/本套三张，再拿它去抓图。
        // 收尾判据的清单因此直接来自 grab 调用点，不可能对不上。
        std::vector<std::string> triple;
        const auto shot = [&](QWidget* widget, const std::string& name) {
            triple.push_back(name);
            state->expected.push_back(name);
            review::Grab(widget, state->dir, name, state->manifest);
        };
        auto* pipeline = new PipelineWorkspace();
        pipeline->resize(1280, 820); pipeline->show(); pipeline->LoadMock(); review::Pump();
        shot(pipeline, "pipeline-" + suffix);
        auto* image = new ImageFlowWorkspace();
        image->resize(1280, 820); image->show(); image->LoadMock(); review::Pump();
        shot(image, "imageflow-" + suffix);
        auto* video = new VideoFlowWorkspace();
        video->resize(1280, 820); video->show(); video->LoadMock(); review::Pump();
        shot(video, "videoflow-" + suffix);
        state->theme_triples.push_back(std::move(triple));
        delete pipeline; delete image; delete video;
    }
    Finish(state);
}
} // namespace
void SaveP10Review(const std::filesystem::path& dir) {
    auto* state = new State; state->dir = dir;
shine::util::EnsureDir(dir);
    QTimer::singleShot(0, qApp, [state] { Run(state); });
}
} // namespace shine::app
