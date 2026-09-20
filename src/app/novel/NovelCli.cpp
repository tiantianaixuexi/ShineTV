#include "app/novel/NovelCli.h"

#include "agent/NovelDirector.h"
#include "agent/NovelStoryboard.h" // S23：V9 叙事分镜
#include "app/novel/NovelPipeline.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelGraph.h"
#include "novel/NovelInit.h"     // S21：初始化链门禁与骨架
#include "agent/NovelVisualStages.h"
#include "novel/NovelChecks.h"
#include "novel/NovelContinuity.h"
#include "novel/NovelGeneration.h"
#include "novel/NovelPromptGen.h"     // S24：V10 提示词产物
#include "openai/OpenAIProvider.h"    // S34：LLM 前置检查（ResolveActiveProfile / ProviderLabel）
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
    // S28：章级 K 校验（`06` §2.3 的 K01–K29）—— 命令行看结果
    const bool wantChecks = Has(args, "--novel-checks");
    // S30：V8 `CONTINUITY`（`12` §2.7 的 C1–C12；**纯机器校验，不调 LLM**）
    const bool wantCont = Has(args, "--novel-continuity");
    // S31：影视化链的**阶段化**实现（`03` §2.2）—— 目前有 V1 `SCENE_BREAKDOWN`
    const bool wantStages = Has(args, "--novel-stages");
    if (!wantGen && !wantRun && !wantInit && !wantSb && !wantPrompt && !wantImages && !wantChecks &&
        !wantCont && !wantStages) {
        log::Error("novel-cli：未知子命令。可用：`--novel-init` / `--novel-stages`(V1) / "
                   "`--novel-storyboard`(V9) / `--novel-prompt`(V10) / `--novel-generate-images`(V11) / "
                   "`--novel-continuity`(V8) / `--novel-checks`(K01–K29) / `--novel-generate` / "
                   "`--novel-run`");
        return 2;
    }

    // ⚠️ S34：**LLM 前置检查** —— 需要 LLM 的子命令（V1–V7 / V9）在**发起调用之前**就说清"没配 Key"。
    // 原先它们会真的去调一次 LLM 才报错：用户看到的现象是"生成不出来"，却要多等一轮才知道原因
    //（真跑实测：`--novel-stages` → `阶段链中断 V1 失败：LLM 调用失败：未配置 OpenAI API 密钥`）。
    // ⚠️ V10 `--novel-prompt` / V11 `--novel-generate-images` / V8 `--novel-continuity` / `--novel-checks`
    // **不调 LLM**，不检查（V11 的缺 SD checkpoint 已有明确中文提示）。
    const auto requireLlm = [](const char* cmd) {
        const openai::LlmProfile prof = openai::ResolveActiveProfile();
        if (prof.apiKey.empty()) {
            log::Error("novel-cli：{} 需要 LLM，但**没有配置 API Key**（当前 provider={}）。"
                       "请在「设置 → LLM」里填 Key（或设对应环境变量）后重跑 —— "
                       "**这不是链路的 bug，是缺外部凭据**。",
                       cmd, std::string{openai::ProviderLabel(prof.provider)});
            return false;
        }
        return true;
    };
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

    if (wantStages) {
        // S31：影视化链的**阶段化**实现（`03` §2.2）—— 目前有 V1 `SCENE_BREAKDOWN`
        //（V8 `CONTINUITY` 在 `--novel-continuity`）。产物落 `work/ch<NNN>/v01_scene_breakdown.json`，
        // 会被 V9（`--novel-storyboard`）当**镜骨架**消费（"镜的切分"提前到 V1）。
        std::int64_t cid = std::atoll(Opt(args, "--novel-stages", "").c_str());
        if (cid <= 0) {
            cid = PickChapter(db);
        }
        if (cid <= 0) {
            log::Error("novel-cli：库里没有可用章节");
            AppendCheckOut(false, "没有可用章节");
            return 2;
        }
        std::atomic<bool> cancel{false};
        auto call = MakeLlmCall(&cancel);
        // S35 `--up-to`：只跑到某阶段（调试省 LLM 调用）。默认 V7 = 全程。
        // ⚠️ 参数校验放在**查 Key 之前**：非法参数不必先有 Key 才知道（否则 `--up-to 乱写`
        // 会被"没配 Key"挡住，看起来像参数没错）。
        agent::VisualStageId upTo = agent::VisualStageId::V7Audio;
        if (const std::string ut = Opt(args, "--up-to", ""); !ut.empty()) {
            const auto parsed = agent::ParseVisualStage(ut);
            if (!parsed) {
                log::Error("novel-cli：`--up-to {}` 不合法（接受 V1…V7 或 1…7）", ut);
                AppendCheckOut(false, "非法 --up-to");
                return 2;
            }
            upTo = *parsed;
        }
        // S34：先查 Key（没配就别白跑一遍）
        if (!requireLlm("--novel-stages")) {
            AppendCheckOut(false, "未配置 LLM API Key");
            return 2;
        }
        log::Info("novel-cli：阶段链目标 V1–{}", agent::VisualStageCode(upTo));
        // S32：**一次跑 V1 → V7**（各自哈希复用会自动跳过已跑过的；链式：上游变了下游必重算）
        const auto sb = agent::RunAllVisualStages(db, call, cid, util::PathToUtf8(projectDir),
                                                  Opt(args, "--hint"), upTo);
        if (!sb) {
            log::Error("novel-cli：阶段链失败 {}", sb.error().message);
            AppendCheckOut(false, sb.error().message);
            return 1;
        }
        log::Info("novel-cli：{}", sb->detail);
        if (!sb->ok) {
            log::Error("novel-cli：阶段链中断 {}", sb->error);
            AppendCheckOut(false, sb->error);
            return 1;
        }
        log::Info("novel-cli：V1–V7 完成：{}", sb->Describe());
        AppendCheckOut(true, fmt::format("V1-V7 {}", sb->Describe()));
        return 0;
    }

    if (wantCont) {
        // S30：V8 `CONTINUITY`（`03` §2.2 的影视化阶段）—— **纯机器校验、不调 LLM**。
        // 判相邻镜的「本镜 end_state → 下一镜 start_state」（`12` §2.7 的 C1–C12），
        // 报告落 `work/ch<NNN>/v08_continuity.json`（`12` §3 的 12-6 就是"此前没有它"）。
        std::int64_t cid = std::atoll(Opt(args, "--novel-continuity", "").c_str());
        if (cid <= 0) {
            cid = PickChapter(db);
        }
        if (cid <= 0) {
            log::Error("novel-cli：库里没有可用章节");
            AppendCheckOut(false, "没有可用章节");
            return 2;
        }
        const novelcore::ContinuityOutcome co =
            novelcore::RunContinuityChecks(db, cid, util::PathToUtf8(projectDir));
        for (const std::string& n : co.notes) {
            log::Warn("  {}", n); // unverified 的原因：**显式列出，不静默**
        }
        for (const novelcore::ContinuityIssue& is : co.issues) {
            log::Warn("{} [{}] {}", is.code, is.severity, is.detail);
        }
        if (!co.ok) {
            log::Error("novel-cli：V8 失败 {}", co.error);
            AppendCheckOut(false, co.error);
            return 1;
        }
        log::Info("novel-cli：V8 连续性：{} · 报告 {}", co.Describe(), co.report_path);
        AppendCheckOut(co.failed == 0, fmt::format("V8 {}", co.Describe()));
        return co.failed == 0 ? 0 : 1;
    }

    if (wantChecks) {
        // S28：章级 K 校验（`06` §2.3 的 K01–K29）—— 命令行可看结果（此前只有提交路径内部跑）。
        // 数据来源按条目自动分级（查库 / 读盘产物 / 调用方传对象）；**没数据的条目如实报 `n/a`**，
        // 不假装通过。K19–K21 现在有**盘上数据源**（V11 落 `generation_checks.json`）。
        std::int64_t cid = std::atoll(Opt(args, "--novel-checks", "").c_str());
        if (cid <= 0) {
            cid = PickChapter(db);
        }
        if (cid <= 0) {
            log::Error("novel-cli：库里没有可用章节");
            AppendCheckOut(false, "没有可用章节");
            return 2;
        }
        novelcore::CheckInputs cin;
        cin.chapter_id = cid;
        cin.project_dir = projectDir;
        const novelcore::ValidationReport rep = novelcore::RunChapterChecks(db, cin);
        const bool showAll = Has(args, "--all");
        int pass = 0;
        int fail = 0;
        int missing = 0;
        int na = 0;
        for (const novelcore::CheckResult& r : rep.checks) {
            switch (r.outcome) {
            case novelcore::CheckOutcome::Pass: ++pass; break;
            case novelcore::CheckOutcome::Fail: ++fail; break;
            case novelcore::CheckOutcome::Missing: ++missing; break;
            case novelcore::CheckOutcome::NotApplicable: ++na; break;
            }
            if (r.outcome == novelcore::CheckOutcome::Fail ||
                r.outcome == novelcore::CheckOutcome::Missing) {
                log::Warn("{} [{}] {}", r.check_id, r.severity, r.detail);
            } else if (showAll) {
                // `--all`：把 `pass` / `n/a` 也打出来（验收时要看"哪几条真的有结论"，
                // 只看失败会漏掉"从 n/a 变成 pass"这类**进步**）
                log::Info("{} [{}] {}", r.check_id, novelcore::CheckOutcomeName(r.outcome), r.detail);
            }
        }
        log::Info("novel-cli：K 校验完成：pass={} fail={} missing={} n/a={}（共 {} 条）", pass, fail,
                  missing, na, rep.checks.size());
        AppendCheckOut(fail == 0 && missing == 0,
                       fmt::format("K checks pass={} fail={} missing={} na={}", pass, fail, missing,
                                   na));
        return (fail == 0 && missing == 0) ? 0 : 1;
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
        // S34：先查 Key（没配就别白跑一遍）
        if (!requireLlm("--novel-storyboard")) {
            AppendCheckOut(false, "未配置 LLM API Key");
            return 2;
        }
        // S35 `--strict`：把 V1 骨架从"强建议"变成"**合同**"（三类偏离任一 > 0 即失败）
        const bool strictSk = Has(args, "--strict");
        if (strictSk) {
            log::Info("novel-cli：**骨架硬校验开启**（V9 必须严格按 V1 骨架出镜）");
        }
        auto sb = agent::GenerateStoryboard(
            db, MakeLlmCall(&cancel),
            {.chapter_id = cid,
             .project_dir = projectDir,
             .extra_hint = Opt(args, "--hint"),
             .strict_skeleton = strictSk});
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
