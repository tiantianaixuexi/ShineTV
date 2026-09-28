#include "pages/review/P06Review.h"

#include "pages/storyboard/ContinuityView.h"
#include "pages/storyboard/GenShotBridgeView.h"
#include "pages/storyboard/ShotDetailView.h"
#include "pages/storyboard/ShotTableView.h"
#include "pages/storyboard/StoryboardTimeline.h"
#include "pages/storyboard/StoryboardWorkspace.h"
#include "core/Async.h"
#include "db/sqlite/SqliteDb.h"
#include "widget/theme/Theme.h"
#include "widget/theme/ThemeService.h"
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
#include <vector>

namespace shine::app {
namespace {

namespace fs = std::filesystem;

struct ReviewState {
    fs::path dir;
    fs::path root;
    StoryboardWorkspace* workspace = nullptr;
    std::vector<std::string> expected{
        "storyboard-normal", "storyboard-empty", "timeline-placeholder",
        "shottable-multiselect", "shotdetail-performance", "continuity-issues",
        "genshot-export", "storyboard-night-theme"};
    std::vector<std::string> manifest;
};

void Pump() {
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    shine::async::DrainUiQueue();
    QApplication::processEvents();
}

void Grab(ReviewState* state, QWidget* widget, const std::string& name) {
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


template <typename T>
T* FindWidget(QWidget* root) {
    if (root == nullptr) return nullptr;
    for (QWidget* child : root->findChildren<QWidget*>()) {
        if (auto* typed = dynamic_cast<T*>(child)) return typed;
    }
    return dynamic_cast<T*>(root);
}

void WaitFor(StoryboardWorkspace* workspace, std::string_view marker) {
    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    bool done = false;
    const QString expected = QString::fromUtf8(marker.data(), int(marker.size()));
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (workspace->ContinuityProbe().contains(expected) ||
            workspace->GenShotProbe().contains(expected) || elapsed.elapsed() > 10000) {
            done = workspace->ContinuityProbe().contains(expected) ||
                   workspace->GenShotProbe().contains(expected) || elapsed.elapsed() > 10000;
            poll.stop();
            loop.quit();
        }
    });
    poll.start(20);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (!done) qWarning("P06 review wait timeout");
    Pump();
}

bool CreateFixture(ReviewState* state) {
    fs::path root = state->root;
    std::error_code ec;
    fs::create_directories(root / "db", ec);
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
    fs::create_directories(work, ec);
    
    (void)util::WriteFileBytes(
        work / "storyboard.json",
        R"({"shots":[{"scene_ord":1,"ord":1,"transition":"","performance":{},"spatial":{},"camera":{}},{"scene_ord":1,"ord":2,"transition":"cut","performance":{},"spatial":{},"camera":{}},{"scene_ord":1,"ord":3,"transition":"cut","performance":{},"spatial":{},"camera":{}}]})");
    return true;
}

void RunReview(ReviewState* state) {
    if (!CreateFixture(state)) {
        state->manifest.push_back("fixture FAILED");
    } else {
        auto* empty = new StoryboardWorkspace();
        empty->resize(1400, 980);
        empty->show();
        Grab(state, empty, "storyboard-empty");
        empty->deleteLater();

        state->workspace = new StoryboardWorkspace();
        state->workspace->resize(1400, 980);
        state->workspace->show();
        QString error;
        const bool opened = state->workspace->OpenBook(state->root / "db" / "novel.db", state->root, &error);
        
        if (opened) {
            state->workspace->SelectScene(1);
            WaitFor(state->workspace, "checked=1");
            state->workspace->RunGenShotBridge();
            WaitFor(state->workspace, "ran=1");
            Pump();
            Grab(state, state->workspace, "storyboard-normal");
            Grab(state, FindWidget<StoryboardTimeline>(state->workspace), "timeline-placeholder");
            Grab(state, FindWidget<ShotTableView>(state->workspace), "shottable-multiselect");
            Grab(state, FindWidget<ShotDetailView>(state->workspace), "shotdetail-performance");
            Grab(state, FindWidget<ContinuityView>(state->workspace), "continuity-issues");
            Grab(state, FindWidget<GenShotBridgeView>(state->workspace), "genshot-export");
            const auto saved = theme::CurrentThemeId();
            theme::ThemeService::Switch(theme::ThemeId::PolarNight, false);
            Pump();
            Grab(state, state->workspace, "storyboard-night-theme");
            theme::ThemeService::Switch(saved, false);
        } else {
            state->manifest.push_back("workspace-open FAILED " + error.toStdString());
        }
    }
    std::string report = "P06-S9 shots\n";
    for (const auto& line : state->manifest) report += line + "\n";
    for (const auto& name : state->expected) {
        if (!fs::is_regular_file(state->dir / (name + ".png"))) report += name + " MISSING\n";
    }
    (void)util::WriteFileBytes(state->dir / "shots-manifest.txt", report);
    std::fflush(nullptr);
    std::_Exit(0);
}

} // namespace

void SaveP06Review(const std::filesystem::path& dir) {
    auto* state = new ReviewState;
    state->dir = dir;
    state->root = dir / "_fixture";
    std::error_code ec;
    fs::create_directories(state->dir, ec);
    shine::async::Init();
    QTimer::singleShot(0, qApp, [state] { RunReview(state); });
}

} // namespace shine::app
