#include "app/novel/NovelCli.h"

#include "agent/NovelDirector.h"
#include "app/novel/NovelPipeline.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "novel/NovelGraph.h"
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
    if (!wantGen && !wantRun) {
        log::Error("novel-cli：未知子命令（`--novel-generate <chapter_id>` 或 "
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
    log::Info("novel-cli：库 {} · 工程 {}", dbArg, util::PathToUtf8(projectDir));

    std::atomic<bool> cancel{false};

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
        log::Info("novel-cli：开始生成章 #{}（max_revisions={}）", cid, maxRev);
        const ChapterGenOutcome out = GenerateOneChapter(
            db, cid, projectDir, maxRev, &cancel,
            [](const agent::GenerateChapterProgress& p) {
                if (!p.note.empty()) {
                    log::Info("  [{}] {}", agent::PhaseName(p.phase), p.note);
                }
            });
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
