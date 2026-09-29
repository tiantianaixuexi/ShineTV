#include "ui/verify/review/P06Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "ui/pages/storyboard/ContinuityView.h"
#include "ui/pages/storyboard/GenShotBridgeView.h"
#include "ui/pages/storyboard/ShotDetailView.h"
#include "ui/pages/storyboard/ShotTableView.h"
#include "ui/pages/storyboard/StoryboardTimeline.h"
#include "ui/pages/storyboard/StoryboardWorkspace.h"
#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Random.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPixmap>
#include <QTimer>

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace shine::app {
namespace {

namespace fs = std::filesystem;

struct ReviewState {
    fs::path dir;
    fs::path root;
    StoryboardWorkspace* workspace = nullptr;
    // 与本轮 review::Grab 调用点**一一对应**（8 个 grab，8 个名字）。
    // 7 个在 `if (opened)` 里：开库失败时这 7 张一张都不会产出，现在由
    // Finish() 判成 overall=FAIL，而不是「清单里有、报告写 MISSING、退出码 0」。
    std::vector<std::string> expected{
        "storyboard-normal", "storyboard-empty", "timeline-placeholder",
        "shottable-multiselect", "shotdetail-performance", "continuity-issues",
        "genshot-export", "storyboard-night-theme"};
    std::vector<std::string> manifest;
    // 以前只写进报告字符串、不影响退出码的断言：fixture 成不成功、开库成不成功、
    // 两次等待有没有真等到探针标记（超时会 qWarning 后继续跑）。全部接进
    // FinishOptions::extra，任一条不成立即 overall=FAIL。
    std::vector<std::pair<std::string, bool>> checks;
};

template <typename T>
T* FindWidget(QWidget* root) {
    if (root == nullptr) return nullptr;
    for (QWidget* child : root->findChildren<QWidget*>()) {
        if (auto* typed = dynamic_cast<T*>(child)) return typed;
    }
    return dynamic_cast<T*>(root);
}

// 返回**是否真等到**探针标记。返回 false = 10s 超时（以前这里只 qWarning 一句就
// 继续往下拍，manifest 照样 saved，等于把「没等到」当成「等到了」）。
bool WaitFor(StoryboardWorkspace* workspace, std::string_view marker) {
    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    bool converged = false;
    const QString expected = QString::fromUtf8(marker.data(), int(marker.size()));
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (workspace->ContinuityProbe().contains(expected) ||
            workspace->GenShotProbe().contains(expected)) {
            converged = true;
            poll.stop();
            loop.quit();
        } else if (elapsed.elapsed() > 10000) {
            poll.stop();
            loop.quit();
        }
    });
    poll.start(20);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    review::Pump();
    if (!converged) qWarning("P06 review wait timeout: %s",
                             QString::fromUtf8(marker.data(), int(marker.size())).toUtf8().constData());
    return converged;
}

bool CreateFixture(ReviewState* state) {
    fs::path root = state->root;
    std::error_code ec;
shine::util::EnsureDir(root / "db");
    shine::db::sqlite::Database db;
    if (auto opened = db.Open({.path = root / "db" / "novel.db"}); !opened) return false;
    if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) return false;
    shine::novelcore::NovelGraph graph(db);
    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
    if (!volume) return false;
    auto chapter = graph.UpsertChapter(
        {.volume_id = *volume, .ord = 1, .title = "灯塔下的对峙", .status = "done"});
    if (!chapter) return false;
    auto scene = graph.UpsertScene(
        {.chapter_id = *chapter, .ord = 1, .title = "灯塔平台", .goal = "角色发现追踪者"});
    if (!scene) return false;
    shine::novelcore::NovelVisual visual(db);
    for (int ord = 1; ord <= 3; ++ord) {
        shine::novelcore::ShotRow shot;
        shot.scene_id = *scene;
        shot.ord = ord;
        shot.action = ord == 1 ? "建立空间" : (ord == 2 ? "角色转身" : "镜头推近");
        shot.prompt_text = "灯塔平台上的角色动作，镜头 " + std::to_string(ord);
        shot.negative_text = "watermark, text";
        shot.duration_note = ord == 2 ? "2.0s" : "3.0s";
        shot.start_state_json = R"({"characters":[],"props":[]})";
        shot.end_state_json = ord == 3 ? R"({"characters":[],"props":[]})"
                                       : R"({"characters":[{"entity_id":1}],"props":[]})";
        shot.timeline_json = R"({"duration_s":3.0,"beats":[{"begin_s":0.0,"end_s":3.0}]})";
        if (!visual.UpsertShot(shot)) return false;
    }
    const fs::path work = root / "work" / "ch001";
shine::util::EnsureDir(work);
    
    (void)util::WriteFileBytes(
        work / "storyboard.json",
        R"({"shots":[{"scene_ord":1,"ord":1,"transition":"","performance":{},"spatial":{},"camera":{}},{"scene_ord":1,"ord":2,"transition":"cut","performance":{},"spatial":{},"camera":{}},{"scene_ord":1,"ord":3,"transition":"cut","performance":{},"spatial":{},"camera":{}}]})");
    return true;
}

void Finish(ReviewState* state) {
    // 判据走共享的 review::WriteAndExit（见 ReviewProbe.h 的 FinishOptions）：
    // expected 逐个查存在 + 字节数下限 + 逐字节重复检测 + extra 断言，退出码跟着
    // ok 走。以前这里是「拼报告 → 写盘 → std::_Exit(0)」，恒 0。
    std::vector<std::string> files;
    files.reserve(state->expected.size());
    for (const std::string& name : state->expected) {
        files.push_back(name + ".png");
    }
    const review::FinishOptions opt{
        .dir = state->dir,
        .header = "P06-S9 shots",
        .expected = files,
        .manifest = &state->manifest,
        .min_bytes = 100,
        // 本轮 8 张图承诺的状态都不同：6 张是六个不同控件，empty / normal 是
        // 未开库与已开库，normal / night-theme 是两套主题（基线已钉死，见下），
        // 不存在「合法地长得一样」的组合。
        .fail_on_duplicate = true,
        .extra = state->checks,
    };
    const review::FinishResult result = review::EvaluateShots(opt);
    std::printf("[p06-review]\n%s", result.report.c_str());
    review::WriteAndExit(opt);
}

void RunReview(ReviewState* state) {
    if (!CreateFixture(state)) {
        state->manifest.push_back("fixture FAILED");
        state->checks.emplace_back("fixture", false);
        Finish(state);
        return;
    }
    // 基线主题**钉死**再开拍：持久化配置里当前主题可能本来就是极夜（下面要拍的
    // night）。不钉死的话 storyboard-normal 与 storyboard-night-theme 逐字节相同 ——
    // 一张图两个名字，重复检测会永远 FAIL，评审也会把两行清单当成两份证据。
    const theme::ThemeId saved = theme::CurrentThemeId();
    theme::ThemeService::Switch(
        saved == theme::ThemeId::PolarNight ? theme::ThemeId::Dusk : saved, false);
    review::Pump();

    auto* empty = new StoryboardWorkspace();
    empty->resize(1400, 980);
    empty->show();
    review::Grab(empty, state->dir, "storyboard-empty", state->manifest);
    empty->deleteLater();

    state->workspace = new StoryboardWorkspace();
    state->workspace->resize(1400, 980);
    state->workspace->show();
    QString error;
    const bool opened = state->workspace->OpenBook(state->root / "db" / "novel.db", state->root, &error);
    state->checks.emplace_back("workspace-open", opened);
    
    if (opened) {
        state->workspace->SelectScene(1);
        state->checks.emplace_back("wait-checked", WaitFor(state->workspace, "checked=1"));
        state->workspace->RunGenShotBridge();
        state->checks.emplace_back("wait-genshot", WaitFor(state->workspace, "ran=1"));
        review::Pump();
        review::Grab(state->workspace, state->dir, "storyboard-normal", state->manifest);
        review::Grab(FindWidget<StoryboardTimeline>(state->workspace), state->dir, "timeline-placeholder", state->manifest);
        review::Grab(FindWidget<ShotTableView>(state->workspace), state->dir, "shottable-multiselect", state->manifest);
        review::Grab(FindWidget<ShotDetailView>(state->workspace), state->dir, "shotdetail-performance", state->manifest);
        review::Grab(FindWidget<ContinuityView>(state->workspace), state->dir, "continuity-issues", state->manifest);
        review::Grab(FindWidget<GenShotBridgeView>(state->workspace), state->dir, "genshot-export", state->manifest);
        theme::ThemeService::Switch(theme::ThemeId::PolarNight, false);
        review::Pump();
        review::Grab(state->workspace, state->dir, "storyboard-night-theme", state->manifest);
        theme::ThemeService::Switch(saved, false);
    } else {
        state->manifest.push_back("workspace-open FAILED " + error.toStdString());
    }
    Finish(state);
}

} // namespace

void SaveP06Review(const std::filesystem::path& dir) {
    auto* state = new ReviewState;
    state->dir = dir;
    state->root = dir / "_fixture";
    std::error_code ec;
shine::util::EnsureDir(state->dir);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { RunReview(state); });
}

} // namespace shine::app
