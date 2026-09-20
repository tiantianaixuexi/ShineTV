#include "app/novel/NovelCli.h"

#include "agent/NovelDirector.h"
#include "agent/NovelStoryboard.h" // S23：V9 叙事分镜
#include "app/novel/NovelPipeline.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelGraph.h"
#include "novel/NovelInit.h"     // S21：初始化链门禁与骨架
#include "novel/NovelGeneration.h"
#include "novel/NovelPromptGen.h" // S24：V10 提示词产物
#include "util/Encoding.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace shine::app::novel {
namespace {

// 按空白切 token（`"…"` 包住的一段算一个，便于带空格的路径）
[[nodiscard]] std::vector<std::string> SplitArgs(const wchar_t* cmdline) {
    std::vector<std::string> out;
    const std::string line =
        cmdline == nullptr ? std::string{} : util::PathToUtf8(std::filesystem::path{cmdline});
    std::string cur;
    bool inQuote = false;
    for (const char c : line) {
        if (c == '"') {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && (c == ' ' || c == '\t')) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

[[nodiscard]] bool Has(const std::vector<std::string>& args, std::string_view key) {
    for (const std::string& a : args) {
        if (a == key) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string Opt(const std::vector<std::string>& args, std::string_view key,
                              std::string fallback = {}) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == key) {
            return args[i + 1];
        }
    }
    return fallback;
}

// 逐项结果（与自检同一套开关）：`novel-cli:ok|fail <detail>`
void AppendCheckOut(bool ok, const std::string& detail) {
    const char* path = std::getenv("SHINE_NOVEL_CHECK_OUT");
    if (path == nullptr || *path == '\0') {
        return;
    }
    FILE* f = std::fopen(path, "ab");
    if (f == nullptr) {
        return;
    }
    const std::string line =
        fmt::format("novel-cli:{}{}\n", ok ? "ok" : "fail", detail.empty() ? "" : (" " + detail));
    (void)std::fwrite(line.data(), 1, line.size(), f);
    (void)std::fclose(f);
}

// `0` / 缺省 → 第一张未完成的章（正文为空或状态不是 done）
[[nodiscard]] std::int64_t PickChapter(::shine::db::sqlite::Database& db) {
    novelcore::NovelGraph g(db);
    auto list = g.ListChapters(500);
    if (!list) {
        return 0;
    }
    for (const auto& c : *list) {
        if (c.status != "done") {
            return c.id;
        }
    }
    return list->empty() ? 0 : list->front().id;
}

} // namespace

int RunNovelCli(const wchar_t* cmdline) {
    log::Init();
    // ⚠️ 必须显式加载设置：`LoadSettings()` 原先只在 GUI 路径（`App::Init`）里调，
    // 不走它 → `Settings().mcpNovelDbPath` 是空 → CLI 连库路径都拿不到（实测踩到）。
    LoadSettings();
    const std::vector<std::string> args = SplitArgs(cmdline);

    const bool wantGen = Has(args, "--novel-generate");
    const bool wantRun = Has(args, "--novel-run");
    const bool wantInit = Has(args, "--novel-init");
    const bool wantSb = Has(args, "--novel-storyboard");
    const bool wantPrompt = Has(args, "--novel-prompt");
    // S27：V11 出图（`03` §2.2 的**可选下游动作**）—— 把 V10 的 PromptArtifact 交给出图队列
    const bool wantImages = Has(args, "--novel-generate-images");
    if (!wantGen && !wantRun && !wantInit && !wantSb && !wantPrompt && !wantImages) {
        log::Error("novel-cli：未知子命令（`--novel-init` / `--novel-storyboard <chapter_id>` / "
                   "`--novel-prompt <chapter_id>` / `--novel-generate <chapter_id>` / "
                   "`--novel-run <manual|semi|auto>`）");
        return 2;
    }

    const std::string dbArg = Opt(args, "--db", Settings().mcpNovelDbPath);
    if (dbArg.empty()) {
        log::Error("novel-cli：没有库路径 —— 给 `--db <path>`，或在设置里填 `mcpNovelDbPath`");
        return 2;
    }
    const std::filesystem::path dbPath = util::PathFromUtf8(dbArg);
    const std::string projArg = Opt(args, "--project");
    const std::filesystem::path projectDir =
        projArg.empty() ? dbPath.parent_path() : util::PathFromUtf8(projArg);

    ::shine::db::sqlite::Database db;
    if (auto r = db.Open({.path = dbPath}); !r) {
        log::Error("novel-cli：打开库失败 {}（{}）", r.error().message, dbArg);
        return 2;
    }
    // ⚠️ S26：CLI **自己开库**、不走 `NovelDb` 单例 → 原先**从不做 schema 迁移**，于是新加的列
    // 在旧库上永远补不出来（实测：CLI 跑 V10 报 `no column named version`，同一库用 GUI 打开却是好的）。
    if (auto r = novelcore::NovelDb::EnsureSchemaUpToDate(db); !r) {
        log::Error("novel-cli：schema 升级失败 {}", r.error().message);
        return 2;
    }
    // `--image-backend <mock|openai_images|comfy>`：**只覆盖本次进程**的出图后端（**不写**
    // `settings.json`）。用途：设置里选的是 `comfy`、而 Comfy 出图后端还是 stub 时
    // （`13` §1.2 的 G13），仍能把 V11 的**全链路**（提交 → 账 → 回填 `generation_ref`）跑出来。
    if (const std::string ib = Opt(args, "--image-backend", ""); !ib.empty()) {
        Settings().imageBackend = ib;
        log::Info("novel-cli：本次出图后端覆盖为 {}（不写设置）", ib);
    }
    log::Info("novel-cli：库 {} · 工程 {}", dbArg, util::PathToUtf8(projectDir));

    std::atomic<bool> cancel{false};

    if (wantInit) {
        // S21（`10`）：初始化链 —— 骨架（路径 B/D，不调 LLM）+ 「可开写」门禁报告（`10` §2.3）。
        // 退出码：**0 = 门禁通过（可以开写）**；1 = 门禁未过（逐条列出缺什么）；2 = 参数/环境错。
        if (!Has(args, "--gate-only")) {
            const std::string book = Opt(args, "--book", "未命名小说");
            const int target = std::atoi(Opt(args, "--target-chapters", "100").c_str());
            const novelcore::InitSkeletonResult sk =
                novelcore::RunInitSkeleton(db, projectDir, book, target);
            if (!sk.ok) {
                log::Error("novel-cli：初始化骨架失败 {}", sk.error);
                AppendCheckOut(false, "初始化骨架失败：" + sk.error);
                return 2;
            }
            for (const std::string& c : sk.created) {
                log::Info("novel-cli：已建 {}", c);
            }
        }
        const novelcore::InitReport gate = novelcore::CheckInitGate(db);
        log::Info("novel-cli：{}", gate.Describe());
        for (const novelcore::InitFailure& f : gate.failures) {
            log::Warn("  {} {} —— 修法：{}", f.n_id, f.detail, f.fix_hint);
        }
        AppendCheckOut(gate.passed, gate.Describe());
        return gate.passed ? 0 : 1;
    }

    if (wantImages) {
        // S27：V11 `GENERATION`（`03` §2.2 的**可选下游动作**）—— **不调 LLM、不做评审**，
        // 只有"单镜失败不阻断"的隔离（`09` §2.2）。产出：`generated_images` 任务 +
        // `visual_artifacts`（shot 层，`prompt_artifact_id` 从此不空）+ 回填
        // `prompt_artifacts.generation_ref`（PV5 双向可查）。`--force` = 已出图的镜也重出。
        std::int64_t cid = std::atoll(Opt(args, "--novel-generate-images", "").c_str());
        if (cid <= 0) {
            cid = PickChapter(db);
        }
        if (cid <= 0) {
            log::Error("novel-cli：库里没有可用章节");
            AppendCheckOut(false, "没有可用章节");
            return 2;
        }
        const novelcore::GenerationOutcome gg =
            novelcore::RunChapterGeneration(db, cid, util::PathToUtf8(projectDir),
                                            Has(args, "--force"));
        for (const std::string& w : gg.warnings) {
            log::Warn("  {}", w);
        }
        if (!gg.ok) {
            log::Error("novel-cli：V11 失败 {}", gg.error);
            AppendCheckOut(false, gg.error);
            return 1;
        }
        log::Info("novel-cli：V11 已产出：{} · 校验账 {}", gg.Describe(), gg.checks_path);
        AppendCheckOut(true, fmt::format("V11 {}", gg.Describe()));
        return 0;
    }

    if (wantPrompt) {
        // S24（`11` §2.2 的 V10）：提示词产物 —— 九层组装（`NovelVisual::Assemble`）→
        // `prompt_artifacts` 账（K23 的受检对象 / V11 桥的 `references` 来源）。**不调 LLM**。
        std::int64_t cid = std::atoll(Opt(args, "--novel-prompt", "").c_str());
        if (cid <= 0) {
            cid = PickChapter(db);
        }
        if (cid <= 0) {
            log::Error("novel-cli：库里没有可用章节");
            AppendCheckOut(false, "没有可用章节");
            return 2;
        }
        // S26：把工程目录传下去 —— V10 从 `work/ch<NNN>/storyboard.json` 读空间层（`12` §2.5）
        const novelcore::PromptGenOutcome pg =
            novelcore::GeneratePromptArtifacts(db, cid, util::PathToUtf8(projectDir));
        if (!pg.ok) {
            log::Error("novel-cli：V10 失败 {}", pg.error);
            for (const std::string& w : pg.warnings) {
                log::Warn("  {}", w);
            }
            AppendCheckOut(false, pg.error);
            return 1;
        }
        log::Info("novel-cli：{}", pg.Describe());
        for (const std::string& w : pg.warnings) {
            log::Warn("  {}", w);
        }
        AppendCheckOut(true, pg.Describe());
        return 0;
    }

    if (wantSb) {
        // S23（`11` §2.2 的 V9）：叙事分镜 —— 读该章的 `scenes` → LLM 推演 V1–V8 →
        // `NarrativeShot[]` 落 `shots` 表（K09/K22/K24 的受检对象由此而来）。
        std::int64_t cid = std::atoll(Opt(args, "--novel-storyboard", "").c_str());
        if (cid <= 0) {
            cid = PickChapter(db);
        }
        if (cid <= 0) {
            log::Error("novel-cli：库里没有可用章节");
            AppendCheckOut(false, "没有可用章节");
            return 2;
        }
        auto sb = agent::GenerateStoryboard(
            db, MakeLlmCall(&cancel),
            {.chapter_id = cid, .project_dir = projectDir, .extra_hint = Opt(args, "--hint")});
        if (!sb) {
            log::Error("novel-cli：分镜产出失败 [{}] {}", sb.error().code, sb.error().message);
            AppendCheckOut(false, sb.error().message);
            return 1;
        }
        log::Info("novel-cli：{}", sb->Describe());
        for (const std::string& w : sb->warnings) {
            log::Warn("  {}", w);
        }
        AppendCheckOut(true, sb->Describe());
        return 0;
    }

    if (wantGen) {
        std::int64_t cid = std::atoll(Opt(args, "--novel-generate", "").c_str());
        if (cid <= 0) {
            cid = PickChapter(db);
            log::Info("novel-cli：未指定章号 → 取第一张未完成的章 #{}", cid);
        }
        if (cid <= 0) {
            log::Error("novel-cli：库里没有可用章节（先建章）");
            AppendCheckOut(false, "没有可用章节");
            return 2;
        }
        const int maxRev = std::atoi(Opt(args, "--max-revisions", "2").c_str());
        const bool resume = Has(args, "--resume"); // S19：续跑（默认 = 重写）
        log::Info("novel-cli：开始生成章 #{}（max_revisions={} resume={}）", cid, maxRev, resume);
        const ChapterGenOutcome out = GenerateOneChapter(
            db, cid, projectDir, maxRev, &cancel,
            [](const agent::GenerateChapterProgress& p) {
                if (!p.note.empty()) {
                    log::Info("  [{}] {}", agent::PhaseName(p.phase), p.note);
                }
            },
            resume);
        log::Info("novel-cli：{}", out.Describe());
        AppendCheckOut(out.ok, out.Describe());
        return out.ok ? 0 : 1;
    }

    // `--novel-run`
    const std::string modeStr = Opt(args, "--novel-run", "semi");
    novelcore::RunRequest req;
    req.project_dir = projectDir;
    req.mode = novelcore::RunModeFromString(modeStr);
    req.max_chapters = std::atoi(Opt(args, "--max", "0").c_str());
    req.checkpoint_every = std::atoi(Opt(args, "--checkpoint", "10").c_str());
    req.auto_create_chapters = Has(args, "--auto-create");
    // S16：LLM 可用 / 交叉复核 / 全书预算 —— 与 UI 走**同一份**判定
    FillPreconditions(req, std::atoll(Opt(args, "--max-total-calls", "0").c_str()));
    log::Info("novel-cli：连跑 mode={} max={} checkpoint={} auto_create={} llm_ready={} "
              "cross_review_ok={}",
              modeStr, req.max_chapters, req.checkpoint_every, req.auto_create_chapters,
              req.llm_ready, req.cross_review_ok);

    const novelcore::RunOutcome out =
        RunOnce(db, req, &cancel, [](const novelcore::RunProgress& p) {
            log::Info("  [第 {} 章] {}{}", p.chapter_ord, p.phase,
                      p.note.empty() ? "" : (" · " + p.note));
        });
    if (!out.refuse_reason.empty()) {
        log::Error("novel-cli：被拒启动 —— {}", out.refuse_reason);
        AppendCheckOut(false, "被拒启动：" + out.refuse_reason);
        return 1;
    }
    const std::string summary = fmt::format(
        "连跑结束：完成 {} 章（续跑跳过 {}）{}", out.chapters_done, out.chapters_resumed_skipped,
        out.stop ? fmt::format(" · 停止条件 {} {}", novelcore::StopCodeName(out.stop->code),
                               out.stop->detail)
                 : std::string{});
    log::Info("novel-cli：{}", summary);
    if (!out.stop_report_path.empty()) {
        log::Info("novel-cli：停止报告 {}", util::PathToUtf8(out.stop_report_path));
    }
    AppendCheckOut(true, summary);
    return 0;
}

} // namespace shine::app::novel
