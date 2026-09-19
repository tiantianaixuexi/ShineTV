#include "novel/NovelImageStore.h"

#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelDb.h"
#include "novel/NovelGraph.h"
#include "novel/NovelVisual.h"
#include "util/Encoding.h"
#include "util/Time.h"

#include <fmt/format.h>

#include <cstdlib>
#include <fstream>
#include <random>
#include <utility>

#include <yyjson.h>

namespace shine::novelcore {
namespace {

[[nodiscard]] std::int64_t NowSec() noexcept { return util::NowMillis() / 1000; }

[[nodiscard]] DbError DbErr(std::string_view m) { return DbError{0, std::string{m}}; }

[[nodiscard]] std::string NewJobId() {
    static std::mt19937_64 rng{static_cast<std::uint64_t>(NowSec()) ^ 0x9e3779b97f4a7c15ULL};
    return fmt::format("img_{:x}_{:x}", static_cast<std::uint64_t>(NowSec()), rng());
}

[[nodiscard]] std::string StatusOf(std::string_view s) {
    return std::string{s};
}

// P9.4 简单启发式
void AppendChecklist(std::vector<ImageChecklistItem>& items, const GeneratedImageRow& row,
                     bool fileExists) {
    ImageChecklistItem promptItem;
    promptItem.key = "prompt_nonempty";
    promptItem.ok = !row.prompt.empty();
    promptItem.note = row.prompt.empty() ? "prompt 为空" : fmt::format("{} 字", row.prompt.size());
    items.push_back(promptItem);

    ImageChecklistItem fileItem;
    fileItem.key = "file_on_disk";
    fileItem.ok = fileExists;
    fileItem.note = fileExists ? row.rel_path : "落盘文件缺失";
    items.push_back(fileItem);

    ImageChecklistItem sizeItem;
    sizeItem.key = "size_hint";
    sizeItem.ok = row.width >= 64 && row.height >= 64;
    sizeItem.note = fmt::format("{}x{}", row.width, row.height);
    items.push_back(sizeItem);

    // 立绘类：prompt 含 crowd / extra person 时提示
    ImageChecklistItem castItem;
    castItem.key = "cast_hint";
    const bool multi = row.prompt.find("crowd") != std::string::npos ||
                       row.prompt.find("extra person") != std::string::npos ||
                       row.prompt.find("group of") != std::string::npos;
    castItem.ok = !multi;
    castItem.note = multi ? "prompt 可能含多人，角色立绘建议单主体" : "单主体提示";
    items.push_back(castItem);

    ImageChecklistItem negItem;
    negItem.key = "negative_hint";
    negItem.ok = !row.negative.empty();
    negItem.note = row.negative.empty() ? "未写 negative" : "已含 negative";
    items.push_back(negItem);

    ImageChecklistItem statusItem;
    statusItem.key = "status";
    statusItem.ok = (row.status == "PROPOSED" || row.status == "CANON");
    statusItem.note = row.status;
    items.push_back(statusItem);
}

[[nodiscard]] std::expected<void, DbError> InsertRow(db::sqlite::Database& db,
                                                     const GeneratedImageRow& row) {
    auto st = db.Prepare(
        "INSERT INTO generated_images("
        "job_id,backend,model,prompt,negative,width,height,steps,rel_path,"
        "source_kind,source_id,chapter_id,scene_id,status,error,raw_meta,checklist_json,"
        "created,updated) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19)");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, row.job_id);
    (void)st->BindText(2, row.backend);
    (void)st->BindText(3, row.model);
    (void)st->BindText(4, row.prompt);
    (void)st->BindText(5, row.negative);
    (void)st->BindInt(6, row.width);
    (void)st->BindInt(7, row.height);
    (void)st->BindInt(8, row.steps);
    (void)st->BindText(9, row.rel_path);
    (void)st->BindText(10, row.source_kind);
    (void)st->BindInt(11, row.source_id);
    (void)st->BindInt(12, row.chapter_id);
    (void)st->BindInt(13, row.scene_id);
    (void)st->BindText(14, row.status);
    (void)st->BindText(15, row.error);
    (void)st->BindText(16, row.raw_meta);
    (void)st->BindText(17, row.checklist_json);
    (void)st->BindInt(18, row.created);
    (void)st->BindInt(19, row.updated);
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    }
    return {};
}

[[nodiscard]] std::expected<void, DbError> UpdateStatus(db::sqlite::Database& db, RowId id,
                                                        std::string_view status,
                                                        std::string_view error,
                                                        std::string_view checklist,
                                                        std::string_view rawMeta) {
    auto st = db.Prepare(
        "UPDATE generated_images SET status=?1,error=?2,checklist_json=?3,raw_meta=?4,updated=?5 "
        "WHERE id=?6");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, status);
    (void)st->BindText(2, error);
    (void)st->BindText(3, checklist);
    (void)st->BindText(4, rawMeta);
    (void)st->BindInt(5, NowSec());
    (void)st->BindInt(6, id);
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    }
    return {};
}

[[nodiscard]] GeneratedImageRow RowFromStmt(db::sqlite::Statement& st) {
    GeneratedImageRow r;
    r.id = static_cast<RowId>(st.ColumnInt(0));
    r.job_id = st.ColumnText(1);
    r.backend = st.ColumnText(2);
    r.model = st.ColumnText(3);
    r.prompt = st.ColumnText(4);
    r.negative = st.ColumnText(5);
    r.width = static_cast<int>(st.ColumnInt(6));
    r.height = static_cast<int>(st.ColumnInt(7));
    r.steps = static_cast<int>(st.ColumnInt(8));
    r.rel_path = st.ColumnText(9);
    r.source_kind = st.ColumnText(10);
    r.source_id = static_cast<RowId>(st.ColumnInt(11));
    r.chapter_id = static_cast<RowId>(st.ColumnInt(12));
    r.scene_id = static_cast<RowId>(st.ColumnInt(13));
    r.status = st.ColumnText(14);
    r.error = st.ColumnText(15);
    r.raw_meta = st.ColumnText(16);
    r.checklist_json = st.ColumnText(17);
    r.created = st.ColumnInt(18);
    r.updated = st.ColumnInt(19);
    return r;
}

constexpr std::string_view kSelectCols =
    "id,job_id,backend,model,prompt,negative,width,height,steps,rel_path,"
    "source_kind,source_id,chapter_id,scene_id,status,error,raw_meta,checklist_json,"
    "created,updated";

} // namespace

std::string ChecklistToJson(const std::vector<ImageChecklistItem>& items) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        return "{}";
    }
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "items", arr);
    for (const auto& it : items) {
        yyjson_mut_val* o = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strncpy(doc, o, "key", it.key.data(), it.key.size());
        yyjson_mut_obj_add_bool(doc, o, "ok", it.ok);
        yyjson_mut_obj_add_strncpy(doc, o, "note", it.note.data(), it.note.size());
        yyjson_mut_arr_add_val(arr, o);
    }
    size_t len = 0;
    char* text = yyjson_mut_val_write(root, 0, &len);
    yyjson_mut_doc_free(doc);
    if (!text) {
        return "{}";
    }
    std::string out{text, len};
    std::free(text);
    return out;
}

std::vector<ImageChecklistItem> EvaluateImageChecklist(const GeneratedImageRow& row,
                                                       bool fileExists) {
    std::vector<ImageChecklistItem> items;
    AppendChecklist(items, row, fileExists);
    return items;
}

std::filesystem::path ProjectDirOfDb(const std::filesystem::path& dbPath) {
    return dbPath.parent_path();
}

std::filesystem::path ResolveImageAbsPath(const std::filesystem::path& projectDir,
                                          std::string_view relPath) {
    return projectDir / util::PathFromUtf8(relPath);
}

std::expected<GeneratedImageRow, DbError> GetGeneratedImage(db::sqlite::Database& db, RowId id) {
    auto st = db.Prepare(fmt::format("SELECT {} FROM generated_images WHERE id=?1", kSelectCols));
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindInt(1, id);
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    } else if (*s != db::sqlite::StepResult::Row) {
        return std::unexpected(DbErr(fmt::format("generated_images #{} 不存在", id)));
    }
    return RowFromStmt(*st);
}

std::expected<std::vector<GeneratedImageRow>, DbError>
ListGeneratedImages(db::sqlite::Database& db, int limit) {
    if (limit <= 0) {
        limit = 50;
    }
    auto st = db.Prepare(
        fmt::format("SELECT {} FROM generated_images ORDER BY id DESC LIMIT {}", kSelectCols, limit));
    if (!st) {
        return std::unexpected(st.error());
    }
    std::vector<GeneratedImageRow> out;
    while (auto s = st->Step()) {
        if (*s != db::sqlite::StepResult::Row) {
            break;
        }
        out.push_back(RowFromStmt(*st));
    }
    return out;
}

std::expected<void, DbError> SetGeneratedImageStatus(db::sqlite::Database& db, RowId id,
                                                     std::string_view status) {
    if (status != "CANON" && status != "NON_CANON" && status != "DISCARDED" &&
        status != "PROPOSED") {
        return std::unexpected(DbErr("非法状态：仅 CANON/NON_CANON/DISCARDED/PROPOSED"));
    }
    auto cur = GetGeneratedImage(db, id);
    if (!cur) {
        return std::unexpected(cur.error());
    }
    auto st = db.Prepare(
        "UPDATE generated_images SET status=?1,updated=?2 WHERE id=?3");
    if (!st) {
        return std::unexpected(st.error());
    }
    (void)st->BindText(1, status);
    (void)st->BindInt(2, NowSec());
    (void)st->BindInt(3, id);
    if (auto s = st->Step(); !s) {
        return std::unexpected(s.error());
    }
    // 视觉 canon 日志（与 visual_canon_logs 一致）
    if (status == "CANON" || status == "NON_CANON" || status == "DISCARDED") {
        NovelVisual vis(db);
        (void)vis.SetVisualCanon("generated_image", id, status);
    }
    return {};
}

std::expected<int, DbError> ReapStaleImageJobs(db::sqlite::Database& db,
                                               std::int64_t staleSeconds) {
    const std::int64_t now = NowSec();
    const std::int64_t cutoff = now - (staleSeconds > 0 ? staleSeconds : 0);

    // 先数：Database 未暴露 Changes()，用 COUNT 代替
    int staleCount = 0;
    {
        auto st = db.Prepare(
            "SELECT COUNT(*) FROM generated_images "
            "WHERE status IN ('RUNNING','QUEUED') AND updated<?1");
        if (!st) {
            return std::unexpected(st.error());
        }
        (void)st->BindInt(1, cutoff);
        if (auto s = st->Step(); s && *s == db::sqlite::StepResult::Row) {
            staleCount = static_cast<int>(st->ColumnInt(0));
        }
    }
    if (staleCount <= 0) {
        return 0;
    }

    const std::string reason =
        staleSeconds > 0
            ? fmt::format("stale: 超过 {}s 未更新，判定失败（原状态 RUNNING/QUEUED）", staleSeconds)
            : std::string{"stale: 上次进程遗留（启动回收），判定失败"};

    auto up = db.Prepare(
        "UPDATE generated_images SET status='FAILED',error=?1,updated=?2 "
        "WHERE status IN ('RUNNING','QUEUED') AND updated<?3");
    if (!up) {
        return std::unexpected(up.error());
    }
    (void)up->BindText(1, reason);
    (void)up->BindInt(2, now);
    (void)up->BindInt(3, cutoff);
    if (auto s = up->Step(); !s) {
        return std::unexpected(s.error());
    }

    // 审计：audit_logs 缺失时只告警，不影响回收本身
    NovelGraph graph(db);
    if (auto audit = graph.LogAudit("startup_reaper", "reap_stale_image_jobs", "generated_images", 0,
                                    fmt::format("reaped={} cutoff={} now={}", staleCount, cutoff,
                                                now));
        !audit) {
        log::Warn("出图孤儿回收：写 audit_logs 失败：{}", audit.error().message);
    }

    // 必须可见：这代表上一次运行有任务没走完
    log::Warn("出图孤儿回收：{} 条 RUNNING/QUEUED 任务改判 FAILED（cutoff={}）", staleCount, cutoff);
    return staleCount;
}

std::expected<GeneratedImageRow, ImageGenError> RunImageJob(db::sqlite::Database& db,
                                                            const ImageJobInput& in) {
    const std::int64_t now = NowSec();
    GeneratedImageRow job;
    job.job_id = NewJobId();
    job.backend = Settings().imageBackend;
    job.model = in.model.empty() ? Settings().imageModel : in.model;
    job.width = in.width > 0 ? in.width : Settings().imageWidth;
    job.height = in.height > 0 ? in.height : Settings().imageHeight;
    job.steps = in.steps > 0 ? in.steps : Settings().imageSteps;
    job.chapter_id = in.chapter_id;
    job.scene_id = in.scene_id;
    job.created = now;
    job.updated = now;
    job.status = "RUNNING";

    // 来源
    if (in.shot_id > 0) {
        job.source_kind = "shot";
        job.source_id = in.shot_id;
    } else if (in.asset_id > 0) {
        job.source_kind = "asset";
        job.source_id = in.asset_id;
    } else if (in.entity_id > 0) {
        job.source_kind = "entity";
        job.source_id = in.entity_id;
    } else {
        job.source_kind = "manual";
    }

    // prompt：给定优先，否则 Assemble
    job.prompt = in.prompt;
    job.negative = in.negative;
    NovelVisual vis(db);
    if (job.prompt.empty()) {
        AssemblePromptInput asmIn;
        asmIn.chapter_id = in.chapter_id;
        asmIn.scene_id = in.scene_id;
        if (in.asset_id > 0) {
            asmIn.asset_id = in.asset_id;
        }
        if (in.entity_id > 0) {
            asmIn.character_id = in.entity_id;
        }
        if (in.shot_id > 0) {
            asmIn.shot_id = in.shot_id;
        }
        auto assembled = vis.Assemble(asmIn);
        if (!assembled) {
            job.status = "FAILED";
            job.error = assembled.error().message.empty()
                            ? "Assemble 失败且未提供 prompt"
                            : assembled.error().message;
            if (job.prompt.empty() && job.error.find("Assemble") != std::string::npos) {
                job.error = "未提供 prompt，且从设定组装失败：" + assembled.error().message;
            }
            (void)InsertRow(db, job);
            return std::unexpected(
                ImageGenError{.code = "assemble", .message = job.error});
        }
        job.prompt = assembled->final_prompt;
        if (job.negative.empty()) {
            job.negative = assembled->negative_prompt;
        }
        if (job.source_kind == "manual" && assembled->resolved_asset_id > 0) {
            job.source_kind = "asset";
            job.source_id = assembled->resolved_asset_id;
        }
    }
    if (job.prompt.empty()) {
        job.status = "FAILED";
        job.error = "prompt 为空";
        (void)InsertRow(db, job);
        return std::unexpected(ImageGenError{.code = "io", .message = job.error});
    }

    // 相对路径 visual/gen/<job_id>.png
    const auto relDir = ImageRelDir();
    job.rel_path = util::PathToUtf8(relDir / (job.job_id + ".png"));

    // 先落 QUEUED/RUNNING 行
    if (auto ins = InsertRow(db, job); !ins) {
        return std::unexpected(
            ImageGenError{.code = "io", .message = ins.error().message});
    }
    RowId rowId = 0;
    {
        auto sel = db.Prepare(
            "SELECT id FROM generated_images WHERE job_id=?1 ORDER BY id DESC LIMIT 1");
        if (sel) {
            (void)sel->BindText(1, job.job_id);
            if (auto s = sel->Step(); s && *s == db::sqlite::StepResult::Row) {
                rowId = static_cast<RowId>(sel->ColumnInt(0));
            }
        }
    }
    job.id = rowId;

    // 生成：需要工程目录 = db 路径父目录
    std::filesystem::path projectDir;
    {
        // Database 不暴露 path；用 Settings novelRoot + project 或调用方写入 rel
        // 这里：若 NovelDb 单例打开且同库，用其 path；否则用 temp
        if (NovelDb::Instance().isOpen() &&
            NovelDb::Instance().raw().isOpen() &&
            &NovelDb::Instance().raw() == &db) {
            projectDir = ProjectDirOfDb(NovelDb::Instance().path());
        } else {
            projectDir = std::filesystem::temp_directory_path() / "shine_novel_imggen";
        }
    }
    const auto absPath = ResolveImageAbsPath(projectDir, job.rel_path);

    auto backend = MakeImageBackend();
    ImageGenRequest req;
    req.prompt = job.prompt;
    req.negative = job.negative;
    req.width = job.width;
    req.height = job.height;
    req.steps = job.steps;
    req.model = job.model;
    req.outputPath = absPath;

    auto gen = backend->Generate(req);
    job.backend = std::string{backend->name()};
    if (!gen) {
        job.status = "FAILED";
        job.error = gen.error().message;
        job.raw_meta = fmt::format("{{\"code\":\"{}\",\"http\":{}}}", gen.error().code,
                                   gen.error().http_status);
        const bool exists = std::filesystem::exists(absPath);
        job.checklist_json = ChecklistToJson(EvaluateImageChecklist(job, exists));
        if (rowId > 0) {
            (void)UpdateStatus(db, rowId, "FAILED", job.error, job.checklist_json, job.raw_meta);
        } else {
            (void)InsertRow(db, job);
        }
        return std::unexpected(gen.error());
    }

    job.status = "PROPOSED";
    job.error.clear();
    job.raw_meta = gen->raw_meta;
    if (!gen->path.empty()) {
        // 若 backend 写到了别的绝对路径，尽量记录相对工程的路径
        std::error_code ec;
        const auto rel = std::filesystem::relative(gen->path, projectDir, ec);
        if (!ec && !rel.empty()) {
            job.rel_path = util::PathToUtf8(rel);
        } else {
            job.rel_path = util::PathToUtf8(gen->path);
        }
    }
    const bool exists = std::filesystem::exists(absPath) || std::filesystem::exists(gen->path);
    job.checklist_json = ChecklistToJson(EvaluateImageChecklist(job, exists));
    if (rowId > 0) {
        (void)UpdateStatus(db, rowId, "PROPOSED", "", job.checklist_json, job.raw_meta);
    } else {
        (void)InsertRow(db, job);
    }
    log::Info("出图完成 PROPOSED：{} -> {}", job.job_id, job.rel_path);
    return job;
}

bool RunImageQueueSelfCheck() {
    db::sqlite::Database mem;
    if (auto r = mem.Open({.memory = true}); !r) {
        log::Error("P9 队列自检：内存库打开失败");
        return false;
    }
    if (auto r = ::shine::novelcore::NovelDb::ApplyCanonicalSchema(mem); !r) {
        log::Error("P9 队列自检：建表失败 {}", r.error().message);
        return false;
    }

    const AppSettings saved = Settings();
    Settings().imageBackend = "mock";
    Settings().imageOutputRelDir = "visual/gen";

    ImageJobInput in;
    in.prompt = "young man, black hair, dark forest, portrait";
    in.negative = "lowres, blurry";
    in.chapter_id = 3;
    in.width = 128;
    in.height = 128;
    auto job = RunImageJob(mem, in);
    Settings() = saved;
    if (!job) {
        log::Error("P9 队列自检：RunImageJob 失败 {}", job.error().message);
        return false;
    }
    if (job->status != "PROPOSED") {
        log::Error("P9 队列自检：期望 PROPOSED，实际 {}", job->status);
        return false;
    }
    if (job->id <= 0 || job->rel_path.empty() || job->job_id.empty()) {
        log::Error("P9 队列自检：行字段不完整 id={} rel={}", job->id, job->rel_path);
        return false;
    }
    auto list = ListGeneratedImages(mem, 10);
    if (!list || list->empty() || (*list)[0].id != job->id) {
        log::Error("P9 队列自检：List 失败");
        return false;
    }
    if (auto st = SetGeneratedImageStatus(mem, job->id, "CANON"); !st) {
        log::Error("P9 队列自检：标 CANON 失败 {}", st.error().message);
        return false;
    }
    auto after = GetGeneratedImage(mem, job->id);
    if (!after || after->status != "CANON") {
        log::Error("P9 队列自检：CANON 未写回");
        return false;
    }

    // no_key 失败路径
    {
        const AppSettings s2 = Settings();
        Settings().imageBackend = "openai_images";
        Settings().imageApiKey.clear();
        Settings().openaiApiKey.clear();
        const bool envHasKey = [] {
            const char* e = std::getenv("OPENAI_API_KEY");
            return e != nullptr && *e != '\0';
        }();
        ImageJobInput bad;
        bad.prompt = "fail path";
        auto failJob = RunImageJob(mem, bad);
        Settings() = s2;
        if (!envHasKey) {
            if (failJob || failJob.error().code != "no_key") {
                log::Error("P9 队列自检：失败路径应 no_key");
                return false;
            }
            auto failedList = ListGeneratedImages(mem, 5);
            bool sawFail = false;
            if (failedList) {
                for (const auto& r : *failedList) {
                    if (r.status == "FAILED") {
                        sawFail = true;
                    }
                }
            }
            if (!sawFail) {
                log::Error("P9 队列自检：失败任务未写 FAILED 行");
                return false;
            }
        }
    }

    // checklist 非空
    if (job->checklist_json.find("prompt_nonempty") == std::string::npos) {
        log::Error("P9 队列自检：checklist 缺 prompt_nonempty");
        return false;
    }

    // 孤儿态回收：stale 行（updated=0）应改判 FAILED；updated 在未来的行必须保留
    {
        if (auto r = mem.Exec("INSERT INTO generated_images(job_id,status,created,updated) "
                              "VALUES('stale_job_1','RUNNING',0,0)");
            !r) {
            log::Error("P9 队列自检：插入 stale 行失败 {}", r.error().message);
            return false;
        }
        if (auto r = mem.Exec("INSERT INTO generated_images(job_id,status,created,updated) "
                              "VALUES('fresh_job_1','RUNNING',9999999999,9999999999)");
            !r) {
            log::Error("P9 队列自检：插入 fresh 行失败 {}", r.error().message);
            return false;
        }
        auto reaped = ReapStaleImageJobs(mem, 0);
        if (!reaped) {
            log::Error("P9 队列自检：孤儿回收失败 {}", reaped.error().message);
            return false;
        }
        if (*reaped != 1) {
            log::Error("P9 队列自检：孤儿回收应改判 1 条，实际 {}", *reaped);
            return false;
        }
        auto rows = ListGeneratedImages(mem, 20);
        bool staleFailed = false;
        bool freshKept = false;
        if (rows) {
            for (const auto& r : *rows) {
                if (r.job_id == "stale_job_1" && r.status == "FAILED") {
                    staleFailed = true;
                }
                if (r.job_id == "fresh_job_1" && r.status == "RUNNING") {
                    freshKept = true;
                }
            }
        }
        if (!staleFailed || !freshKept) {
            log::Error("P9 队列自检：孤儿回收结果不符 staleFailed={} freshKept={}", staleFailed,
                       freshKept);
            return false;
        }
    }

    log::Info("P9 队列自检通过（PROPOSED→CANON / FAILED 行 / checklist / 孤儿回收）");
    return true;
}

} // namespace shine::novelcore
