#include "ui/imgui/pages/BookData.h"

#include "db/sqlite/SqliteDb.h"
#include "novel/NovelContinuity.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"

#include <cstdlib>
#include <map>

namespace shine::pages {

namespace {

// timeline_json = {"duration_s":N,…}。只取这一个数，取不到返回 <=0（不猜、不写死 6.0）。
double DurationFromTimeline(const std::string& json) {
    const std::size_t key = json.find("\"duration_s\"");
    if (key == std::string::npos) {
        return 0.0;
    }
    std::size_t i = json.find(':', key);
    if (i == std::string::npos) {
        return 0.0;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) {
        ++i;
    }
    double value = 0.0;
    bool any = false;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
        value = value * 10.0 + static_cast<double>(json[i] - '0');
        ++i;
        any = true;
    }
    if (i < json.size() && json[i] == '.') {
        ++i;
        double scale = 0.1;
        while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
            value += static_cast<double>(json[i] - '0') * scale;
            scale *= 0.1;
            ++i;
            any = true;
        }
    }
    return any ? value : 0.0;
}

} // namespace

// ---- worker 侧：开库 + 查询，产出一份**纯数据**快照（无句柄）----
// wantChapter = 用户选中的章下标（-1 = 取第一章）。镜头 / 阶段产物 / 连续性
// 都只对这一章取 —— 与 Qt 版 StoryboardWorkspace「按章选镜」的口径一致。
//
// ⚠️ 这个函数跑在 worker 线程上（RequestBookReload 派发），**不碰任何 UI 状态**：
//    它只往 out 里填，返回后由 PostToUi 回投。库句柄是局部变量，出函数即关。
void LoadBook(const std::filesystem::path& root, int wantChapter, BookState& out) {
    out.root = root;
    out.bound = true;
    const auto dbPath = root / "db" / "novel.db";
    out.dbPath = util::PathToUtf8(dbPath);

    db::sqlite::Database db;
    if (auto opened = db.Open({.path = dbPath, .readOnly = true, .create = false}); !opened) {
        out.error = "打不开小说库：" + opened.error().message;
        return;
    }
    novelcore::NovelGraph graph(db);
    novelcore::NovelVisual visual(db);

    // —— 卷（设计稿侧栏树的中间层）——
    //
    // ⚠️ 这里**没有**用 NovelGraph —— 因为它没有 ListVolumes。
    //    卷表只有两列（id/title/ord/summary）且就在同一个 db 上，而 UI 层本来就已经
    //    持有一个只读句柄（上面 `db.Open({.readOnly=true})`），一条 SELECT 就够。
    //    不为此去动 shine_core，也不在 UI 侧另发明一套查询封装 —— 只用 db 层
    //    已经在用的 Prepare/Step/ColumnText。
    //
    // 查不到**不算错误**：老工程的 volumes 表可能是空的。那时每章的 volumeTitle 为空，
    // 侧栏按「未归卷」分组如实显示，而不是伪造卷名。
    std::map<RowId, std::string> volumeTitles;
    if (auto st = db.Prepare("SELECT id,title FROM volumes ORDER BY ord")) {
        while (st->Step() == db::sqlite::StepResult::Row) {
            volumeTitles.emplace(static_cast<RowId>(st->ColumnInt(0)), st->ColumnText(1));
        }
    }

    // —— 章（小说页 + 分镜页的当前章）——
    auto chapters = graph.ListChapters(2000);
    if (!chapters) {
        out.error = "读取章节失败：" + chapters.error().message;
        return;
    }
    for (const novelcore::ChapterRow& c : *chapters) {
        BookChapter row;
        row.id = c.id;
        row.volumeId = c.volume_id;
        if (const auto it = volumeTitles.find(c.volume_id); it != volumeTitles.end()) {
            row.volumeTitle = it->second;
        }
        row.ord = c.ord;
        row.title = c.title;
        row.status = c.status;
        row.summary = c.summary;
        row.body = c.body;
        row.words = c.words;
        row.updated = c.updated;
        out.chapters.push_back(std::move(row));
    }

    // 章节 × V1–V7 的产物计数（全书）。真值源是 `ListStageArtifacts` 的返回条数。
    //
    // 逐章 × 逐阶段各查一次：N 章就是 7N 次预编译查询，跑在 worker 上，每次微秒级，
    // 且语义直接来自业务层接口 —— 不另写一条 GROUP BY 的 SQL，避免「界面算的」和
    // 「业务层算的」两套口径（上一轮刚吃过一次：一个空执行体让真通路算出了假结果）。
    out.chapterVStages.resize(out.chapters.size());
    for (std::size_t ci = 0; ci < out.chapters.size(); ++ci) {
        for (int v = 1; v <= 7; ++v) {
            const std::string code = "V" + std::to_string(v);
            if (auto arts = visual.ListStageArtifacts(out.chapters[ci].id, code)) {
                out.chapterVStages[ci][static_cast<std::size_t>(v - 1)] =
                    static_cast<int>(arts->size());
            }
        }
    }

    // —— 资产页：实体 + 主视觉资产 + 形象层进度 ——
    // 同 Qt 版 AssetPageModel::RefreshAssets 的口径；查不到资产 = 该实体还没有。
    if (auto entities = graph.ListEntities({}, {}, 2000)) {
        for (const novelcore::EntityRow& e : *entities) {
            BookAsset row;
            row.entityId = e.id;
            row.kind = e.kind;
            row.name = e.name;
            row.summary = e.summary;
            row.entityStatus = e.status;
            if (auto found = visual.FindAssetByEntity(e.id)) {
                row.hasAsset = true;
                row.assetName = found->name;
                row.canonStatus = found->canon_status;
                row.assetStatus = found->status;
                row.sheetRelPath = found->sheet_rel_path;
                if (auto arts = visual.ListArtifacts(found->id)) {
                    row.layers = static_cast<int>(arts->size());
                    for (const novelcore::VisualArtifactRow& art : *arts) {
                        if (art.status == "DONE") {
                            ++row.layersDone;
                        }
                        if (art.degraded) {
                            row.degraded = true;
                        }
                    }
                }
            }
            out.assets.push_back(std::move(row));
        }
    } else {
        out.error = "读取实体失败：" + entities.error().message;
        return;
    }

    // —— 分镜页：当前章的场 / 镜头 / 阶段产物 / 连续性 ——
    if (out.chapters.empty()) {
        return;  // 没有章 = 没有场与镜头；空态由页面显示
    }
    out.selectedChapter = std::clamp(wantChapter, 0, static_cast<int>(out.chapters.size()) - 1);
    const RowId chapterId = out.chapters[static_cast<std::size_t>(out.selectedChapter)].id;

    std::vector<novelcore::SceneRow> scenes;
    if (auto listed = graph.ListScenes(chapterId)) {
        scenes = std::move(*listed);
    }
    auto sceneOrd = [&scenes](RowId id) {
        for (const novelcore::SceneRow& s : scenes) {
            if (s.id == id) {
                return s.ord;
            }
        }
        return 0;
    };

    // —— 检查器「关联」段的伏笔：场 → scene_foreshadows → foreshadows.title ——
    // ⚠️ ListOpenForeshadows 只给未回收的那一档（PLANNED|PLANTED|DEVELOPING，
    //    见 NovelGraph.h 的注释）：已经 REVEALED / RESOLVED 的伏笔在
    //    foreshadows 表里有行、但这里取不到标题。这是业务层的口径，UI 侧照实
    //    呈现 —— 不把 id 当标题显示，也不绕开它回表再查一次。
    // ⚠️ 伏笔表可能是空的（没跑过 T9 伏笔计划）：那就是空，不造数据。
    std::map<RowId, std::string> foreshadowTitle;
    if (auto opened = graph.ListOpenForeshadows()) {
        for (const novelcore::ForeshadowRow& f : *opened) {
            if (!f.title.empty()) {
                foreshadowTitle[f.id] = f.title;
            }
        }
    }
    for (const novelcore::SceneRow& sc : scenes) {
        BookSceneLink link;
        link.id = sc.id;
        link.ord = sc.ord;
        link.title = sc.title;
        if (auto rows = graph.ListSceneForeshadows(sc.id)) {
            for (const novelcore::SceneForeshadowRow& fr : *rows) {
                // 查不到标题就**跳过这一条**：显示一串 id 不如什么都不显示。
                if (const auto it = foreshadowTitle.find(fr.foreshadowing_id);
                    it != foreshadowTitle.end()) {
                    link.foreshadows.push_back(it->second);
                }
            }
        }
        out.sceneLinks.push_back(std::move(link));
    }

    if (auto listed = visual.ListShotsByChapter(chapterId)) {
        for (const novelcore::ShotRow& s : *listed) {
            BookShot row;
            row.id = s.id;
            row.sceneId = s.scene_id;
            row.sceneOrd = sceneOrd(s.scene_id);
            row.ord = s.ord;
            row.action = s.action;
            row.expression = s.expression;
            row.mood = s.mood;
            row.dialogue = s.dialogue;
            row.narration = s.narration;
            row.durationNote = s.duration_note;
            row.durationSec = DurationFromTimeline(s.timeline_json);
            row.cameraId = s.camera_id;
            row.lightingId = s.lighting_id;
            row.timelineJson = s.timeline_json;
            row.canonStatus = s.canon_status;
            out.shots.push_back(std::move(row));
        }
    }

    // V1–V7：逐阶段查 stage_artifacts，**有产物才算 done**，没有就是 todo。
    for (int v = 1; v <= 7; ++v) {
        const std::string code = "V" + std::to_string(v);
        if (auto arts = visual.ListStageArtifacts(chapterId, code)) {
            out.vStage[static_cast<std::size_t>(v - 1)] = !arts->empty();
        }
    }

    // V8：真实三态结论。⚠️ RunContinuityChecks 会把报告落盘到
    // `work/ch<NNN>/v08_continuity.json`（该函数的既定行为），本调用在 worker 上。
    // 它的 unverified 是「数据不足，不算通过也不算失败」—— 页面照实显示，不粉饰成通过。
    novelcore::ContinuityOutcome outcome =
        novelcore::RunContinuityChecks(db, chapterId, util::PathToUtf8(root));
    out.continuity.ran = true;
    out.continuity.shotsSeen = outcome.shots_seen;
    out.continuity.pairsChecked = outcome.pairs_checked;
    out.continuity.failed = outcome.failed;
    out.continuity.unverified = outcome.unverified;
    out.continuity.issues = std::move(outcome.issues);
    out.continuity.notes = std::move(outcome.notes);
    out.vStage[7] = outcome.error.empty();  // V8 跑出报告才算数；error 非空 = 没跑成
}

} // namespace shine::pages
