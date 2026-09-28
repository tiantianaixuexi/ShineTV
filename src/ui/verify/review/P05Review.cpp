#include "ui/verify/review/P05Review.h"
#include "ui/verify/review/ReviewProbe.h"

#include "ui/pages/assets/AssetWorkspace.h"
#include "ui/verify/gallery/GalleryWorkspace.h"
#include "core/Async.h"
#include "core/Settings.h"
#include "db/sqlite/SqliteDb.h"
#include "ui/kit/images/Viewer.h"
#include "ui/kit/theme/Theme.h"
#include "ui/kit/theme/ThemeService.h"
#include "media/Gallery.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelImageStore.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Random.h"
#include <QColor>

#include <QApplication>
#include <QPixmap>
#include <QDialog>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace shine::app {
namespace {

namespace fs = std::filesystem;

struct ReviewState {
    fs::path dir;
    fs::path root;
    QWidget* host = nullptr;
    AssetWorkspace* assets = nullptr;
    std::int64_t full_entity = 0;
    std::int64_t missing_entity = 0;
    std::int64_t empty_entity = 0;
    std::vector<std::string> expected{
        "assets-grid", "assets-empty", "assets-card-states",
        "sheet-full", "sheet-missing-layers", "consistency-compare",
        "gallery-grid", "gallery-viewer-zoom", "gallery-context-menu",
        "assets-mixed-theme"};
    std::vector<std::string> manifest;
};

void WaitViewer(QWidget* widget) {
    auto* viewer = dynamic_cast<images::ImageViewer*>(widget);
    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        review::Pump();
        if ((viewer != nullptr && viewer->isVisible() && !viewer->Current().isNull()) ||
            elapsed.elapsed() > 3000) {
            poll.stop();
            loop.quit();
        }
    });
    poll.start(20);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (viewer != nullptr && !viewer->Current().isNull()) {
        QEventLoop settle;
        QTimer::singleShot(180, &settle, &QEventLoop::quit);
        settle.exec(QEventLoop::ExcludeUserInputEvents);
        review::Pump();
    }
}

bool SaveImage(const fs::path& path, const QColor& color, int w = 160, int h = 120) {
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    fs::create_directories(path.parent_path());
    return image.save(QString::fromStdString(shine::util::PathToUtf8(path)), "PNG");
}

void WaitScan(GalleryWorkspace* gallery) {
    QEventLoop loop;
    QTimer poll;
    int elapsed = 0;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if ((!shine::gallery::State().scanning && elapsed > 50) || elapsed > 8000) {
            poll.stop();
            loop.quit();
        }
        elapsed += 20;
    });
    poll.start(20);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (gallery != nullptr) {
        gallery->Sync();
    }
}

bool CreateFixture(ReviewState* st) {
    fs::path root = st->root;
    std::error_code ec;
shine::util::EnsureDir(root / "db");
    shine::db::sqlite::Database db;
    auto opened = db.Open({.path = root / "db" / "novel.db"});
    if (!opened) {
        return false;
    }
    if (auto schema = shine::novelcore::NovelDb::ApplyCanonicalSchema(db); !schema) {
        return false;
    }
    shine::novelcore::NovelGraph graph(db);
    auto full = graph.UpsertEntity({.kind = "person", .name = "苏黎"});
    auto missing = graph.UpsertEntity({.kind = "person", .name = "顾行舟"});
    auto empty = graph.UpsertEntity({.kind = "person", .name = "未建资产角色"});
    if (!full || !missing || !empty) {
        return false;
    }
    st->full_entity = *full;
    st->missing_entity = *missing;
    st->empty_entity = *empty;
    shine::novelcore::NovelVisual visual(db);

    const auto asset = [&](std::int64_t entity, const std::string& name, const std::string& status) {
        return visual.UpsertAsset({.entity_id = entity, .kind = "character", .name = name,
                                   .status = status});
    };
    auto full_asset = asset(st->full_entity, "苏黎 · 角色设定集", "READY");
    auto missing_asset = asset(st->missing_entity, "顾行舟 · 角色设定集", "PENDING");
    if (!full_asset || !missing_asset) {
        return false;
    }

    const fs::path refs = root / "assets" / "refs";
    const std::array<std::pair<const char*, QColor>, 4> layers{{
        {"front.png", Qt::lightGray}, {"turnaround.png", QColor(210, 220, 230)},
        {"base_body.png", QColor(190, 205, 220)}, {"wardrobe.png", QColor(170, 190, 210)}}};
    std::int64_t parent = 0;
    for (const auto& [file, color] : layers) {
        const fs::path path = refs / file;
        if (!SaveImage(path, color)) {
            return false;
        }
        shine::novelcore::VisualArtifactRow artifact;
        artifact.asset_id = *full_asset;
        artifact.layer = file == std::string("front.png")     ? "front"
                          : file == std::string("turnaround.png") ? "turnaround"
                          : file == std::string("base_body.png")  ? "base_body"
                                                                  : "wardrobe";
        artifact.rel_path = "assets/refs/" + std::string(file);
        artifact.parent_artifact_id = parent;
        artifact.status = "DONE";
        auto id = visual.UpsertArtifact(artifact);
        if (!id) {
            return false;
        }
        parent = *id;
    }

    const std::array<std::pair<const char*, const char*>, 5> state_assets{{
        {"待生成角色", "PENDING"}, {"失败角色", "FAILED"}, {"生成中角色", "GENERATING"},
        {"就绪角色", "SHEET_READY"}, {"降级角色", "READY"}}};
    for (const auto& [name, status] : state_assets) {
        auto entity = graph.UpsertEntity({.kind = "person", .name = name});
        if (!entity || !asset(*entity, name, status)) {
            return false;
        }
    }
    auto degraded = graph.UpsertEntity({.kind = "person", .name = "降级卡片"});
    if (!degraded || !asset(*degraded, "降级卡片", "READY")) {
        return false;
    }
    auto degraded_asset = visual.FindAssetByEntity(*degraded);
    if (!degraded_asset) {
        return false;
    }
    shine::novelcore::VisualArtifactRow degraded_artifact;
    degraded_artifact.asset_id = degraded_asset->id;
    degraded_artifact.layer = "front";
    degraded_artifact.rel_path = "assets/refs/degraded.png";
    degraded_artifact.status = "DONE";
    degraded_artifact.degraded = true;
    if (!SaveImage(root / degraded_artifact.rel_path, QColor(120, 130, 140)) ||
        !visual.UpsertArtifact(degraded_artifact)) {
        return false;
    }

    auto volume = graph.UpsertVolume({.title = "第一卷", .ord = 1});
    if (!volume) {
        return false;
    }
    std::array<std::int64_t, 3> chapters{};
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        auto chapter = graph.UpsertChapter({.volume_id = *volume, .ord = static_cast<int>(i + 1),
                                             .title = "第 " + std::to_string(i + 1) + " 章",
                                             .status = "done"});
        if (!chapter) {
            return false;
        }
        chapters[i] = *chapter;
        shine::novelcore::CharacterStatusRow status;
        status.entity_id = st->full_entity;
        status.chapter_id = *chapter;
        status.mind_state = i == 0 ? "平静" : (i == 1 ? "警觉" : "紧张");
        status.emotion_json = i == 0 ? "{\"fear\":10}" : (i == 1 ? "{\"fear\":55}" : "{\"fear\":85}");
        if (!graph.UpsertCharacterStatus(status)) {
            return false;
        }
    }
    const std::array<const char*, 3> appearances{"短发 · 灰风衣", "短发 · 左颊伤", "湿发 · 黑大衣"};
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        shine::novelcore::VisualStateRow state;
        state.asset_id = *full_asset;
        state.stage_key = i == 0 ? "base" : (i == 1 ? "wound" : "rain");
        state.stage_label = i == 0 ? "初始" : (i == 1 ? "受伤" : "雨夜");
        state.ord = static_cast<int>(i + 1);
        state.from_chapter = chapters[i];
        state.appearance = appearances[i];
        state.materials_colors = i == 2 ? "黑大衣" : "灰风衣";
        if (!visual.UpsertState(state)) {
            return false;
        }
    }

    for (std::size_t i = 0; i < 2; ++i) {
        auto scene = graph.UpsertScene({.chapter_id = chapters[i], .ord = 1, .title = "镜头场景"});
        if (!scene) {
            return false;
        }
        shine::novelcore::ShotRow shot;
        shot.scene_id = *scene;
        shot.ord = static_cast<int>(i + 1);
        shot.character_ids_json = "[" + std::to_string(st->full_entity) + "]";
        shot.action = i == 0 ? "站在灯塔下" : "转身离开";
        shot.expression = i == 0 ? "平静" : "警觉";
        auto id = visual.UpsertShot(shot);
        if (!id) {
            return false;
        }
        shine::novelcore::ImageJobInput input;
        input.prompt = "Su Li storyboard";
        input.negative = "lowres";
        input.shot_id = *id;
        input.width = 96;
        input.height = 72;
        shine::AppSettings saved = shine::Settings();
        shine::Settings().imageBackend = "mock";
        shine::Settings().imageOutputRelDir = "visual/gen";
        auto generated = shine::novelcore::RunImageJob(db, input);
        shine::Settings() = saved;
        if (!generated) {
            return false;
        }
        const QColor shotColor = i == 0 ? QColor(225, 180, 150) : QColor(120, 170, 220);
        if (!SaveImage(root / shine::util::PathFromUtf8(generated->rel_path), shotColor, 192, 144)) {
            return false;
        }
    }

    const fs::path gallery = root / "gallery";
    fs::create_directories(gallery);
    for (int i = 0; i < 8; ++i) {
        if (!SaveImage(gallery / ("图库 " + std::to_string(i) + ".png"),
                       QColor(80 + i * 15, 110, 150))) {
            return false;
        }
    }
    shine::Settings().galleryLocalDir = shine::util::PathToUtf8(gallery);
    return true;
}

void Finish(ReviewState* st) {
    bool ok = true;
    for (const std::string& name : st->expected) {
        const fs::path file = st->dir / (name + ".png");
        const auto bytes = shine::util::ReadFileBytes(file);
        if (!bytes || bytes->size() < 100) {
            ok = false;
        }
    }
    std::string report = "P05-S9 visual review\n";
    for (const std::string& line : st->manifest) {
        report += line + "\n";
    }
    report += ok ? "overall=PASS\n" : "overall=FAIL\n";
    (void)shine::util::WriteFileBytes(st->dir / "shots-manifest.txt", report);
    std::printf("[p05-review]\n%s", report.c_str());
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
}

void RunReview(ReviewState* st) {
    if (!CreateFixture(st)) {
        st->manifest.push_back("fixture FAILED");
        Finish(st);
        return;
    }
    shine::async::Init();
    shine::gallery::Init();
    auto* host = new QWidget;
    st->host = host;
    auto* layout = new QVBoxLayout(host);
    st->assets = new AssetWorkspace(host);
    layout->addWidget(st->assets);
    host->resize(1500, 900);
    host->show();
    QString error;
    if (!st->assets->OpenBook(st->root / "db" / "novel.db", st->root, &error)) {
        st->manifest.push_back("open FAILED " + error.toStdString());
        Finish(st);
        return;
    }

    st->assets->SelectEntity(0);
    st->assets->ShowDetailPage(0);
    review::Grab(st->assets, st->dir, "assets-grid", st->manifest);
    st->assets->SelectEntity(st->empty_entity);
    review::Grab(st->assets, st->dir, "assets-empty", st->manifest);
    st->assets->SelectEntity(0);
    review::Grab(st->assets, st->dir, "assets-card-states", st->manifest);
    st->assets->SelectEntity(st->full_entity);
    review::Grab(st->assets, st->dir, "sheet-full", st->manifest);
    st->assets->SelectEntity(st->missing_entity);
    review::Grab(st->assets, st->dir, "sheet-missing-layers", st->manifest);
    st->assets->SelectEntity(st->full_entity);
    st->assets->ShowDetailPage(1);
    review::Grab(st->assets, st->dir, "consistency-compare", st->manifest);

    st->assets->ShowDetailPage(3);
    st->assets->GlobalGallery()->SetLocalDirectory(st->root / "gallery");
    WaitScan(st->assets->GlobalGallery());
    review::Grab(st->assets, st->dir, "gallery-grid", st->manifest);
    if (!st->assets->SelectFirstGlobalGallery()) {
        st->manifest.push_back("gallery-viewer-zoom 0 FAILED no-selection");
    } else {
        st->assets->GlobalGallery()->OpenViewerForCurrent();
        if (auto* viewer = st->assets->GlobalGallery()->ViewerWidget()) {
            WaitViewer(viewer);
            review::Grab(viewer, st->dir, "gallery-viewer-zoom", st->manifest);
        } else {
            st->manifest.push_back("gallery-viewer-zoom 0 FAILED no-viewer");
        }
    }
    QMenu menu(st->assets);
    menu.addAction(QStringLiteral("放大看"));
    menu.addAction(QStringLiteral("复制路径"));
    menu.addAction(QStringLiteral("在资源管理器中显示"));
    menu.addSeparator();
    menu.addAction(QStringLiteral("设为工作流输入"));
    menu.addMenu(QStringLiteral("被谁引用"));
    menu.show();
    review::Pump();
    review::Grab(&menu, st->dir, "gallery-context-menu", st->manifest);
    menu.close();

    shine::theme::ThemeService::Switch(shine::theme::ThemeId::Dusk, false);
    st->assets->ShowDetailPage(0);
    review::Grab(st->assets, st->dir, "assets-mixed-theme", st->manifest);
    Finish(st);
}

} // namespace

void SaveP05Review(const std::string& dirUtf8) {
    auto* state = new ReviewState;
    state->dir = shine::util::PathFromUtf8(dirUtf8);
    state->root = state->dir / "_fixture";
    std::error_code ec;
    fs::create_directories(state->dir, ec);
    fs::remove_all(state->root, ec);
shine::util::EnsureDir(state->root);
    QTimer::singleShot(0, qApp, [state] { RunReview(state); });
}

} // namespace shine::app
